// Generic red-black tree providing an ordered map from K to V.
//
// The tree is stored in a flat node arena (`nodes`) and every link is an
// integer index into that arena.  The sentinel index `RB_NIL` (-1) plays the
// role of the traditional NIL leaf, which keeps the structure free of raw
// pointers and makes the whole container trivially relocatable.
//
// Invariants maintained by every mutating operation:
//   1. Every node is either red or black.
//   2. The root is black.
//   3. A red node never has a red child.
//   4. Every path from a node down to a NIL leaf contains the same number of
//      black nodes (the "black height" of that node).
//   5. NIL leaves count as black.
//
// `check_validity` restates those invariants with `assert` and `prove` so the
// verifier can reason about them directly.
namespace red_black_tree;

// --- Colours and helper enums -------------------------------------------

choice Colour {
    Red,
    Black,
}

choice ChildSide {
    LeftChild,
    RightChild,
    NoChild,
}

// Result of a lookup: the slot index plus whether the key was actually there.
choice FindOutcome {
    FoundExact,
    FoundPredecessor,
    FoundSuccessor,
    NotFound,
}

// The sentinel used for "no such node".  Written as a negative literal so the
// arena can never accidentally address it.
let RB_NIL: int = -1;

// A red-black tree of n nodes has height at most 2 * log2(n + 1); this bound
// is only used to size traversal stacks up front.
let RB_MAX_HEIGHT: int = 96;

// Arena capacity used when a tree is created without a size hint.
let RB_INITIAL_CAPACITY: int = 32;

// Growth factor, expressed as a percentage so no floating point is needed.
let RB_GROWTH_PERCENT: int = 200;

// --- Node ----------------------------------------------------------------

class RbNode(K: type, V: type)
{

    fn has_left(self) -> bool {
        return self.left != RB_NIL;
    }

    fn has_right(self) -> bool {
        return self.right != RB_NIL;
    }

    fn is_leaf(self) -> bool {
        return self.left == RB_NIL and self.right == RB_NIL;
    }

    fn child_count(self) -> int {
        var n: int = 0;
        if (self.left != RB_NIL) {
            n += 1;
        }
        if (self.right != RB_NIL) {
            n += 1;
        }
        return n;
    }

    fn reset_links(ref self) {
        self.left = RB_NIL;
        self.right = RB_NIL;
        self.parent = RB_NIL;
    }

    var key: K;
    var value: V;
    var left: int = -1;
    var right: int = -1;
    var parent: int = -1;
    var colour: Colour = Colour.Red;
    var live: bool = true;
}

// --- Tree ----------------------------------------------------------------

class RedBlackTree(K: type, V: type)
{

    // --- Size and shape queries ------------------------------------------

    fn size(self) -> int {
        return self.count;
    }

    fn empty(self) -> bool {
        return self.count == 0;
    }

    fn arena_size(self) -> int {
        return self.nodes.size();
    }

    fn is_nil(self, i: int) -> bool {
        return i == RB_NIL;
    }

    fn alive(self, i: int) -> bool {
        if (i == RB_NIL) {
            return false;
        }
        return self.nodes[i].live;
    }

    // --- Colour helpers ---------------------------------------------------
    //
    // NIL leaves are black by rule 5, so every colour query has to funnel
    // through these helpers rather than touching `.colour` directly.

    fn colour_of(self, i: int) -> Colour {
        if (i == RB_NIL) {
            return Colour.Black;
        }
        return self.nodes[i].colour;
    }

    fn is_red(self, i: int) -> bool {
        if (i == RB_NIL) {
            return false;
        }
        return self.nodes[i].colour == Colour.Red;
    }

    fn is_black(self, i: int) -> bool {
        if (i == RB_NIL) {
            return true;
        }
        return self.nodes[i].colour == Colour.Black;
    }

    fn set_colour(ref self, i: int, c: Colour) {
        if (i != RB_NIL) {
            self.nodes[i].colour = c;
        }
    }

    fn paint_red(ref self, i: int) {
        self.set_colour(i, Colour.Red);
    }

    fn paint_black(ref self, i: int) {
        self.set_colour(i, Colour.Black);
    }

    fn swap_colours(ref self, a: int, b: int) {
        if (a != RB_NIL and b != RB_NIL) {
            swap(self.nodes[a].colour, self.nodes[b].colour);
        }
    }

    // --- Link helpers -----------------------------------------------------

    fn left_of(self, i: int) -> int {
        if (i == RB_NIL) {
            return RB_NIL;
        }
        return self.nodes[i].left;
    }

    fn right_of(self, i: int) -> int {
        if (i == RB_NIL) {
            return RB_NIL;
        }
        return self.nodes[i].right;
    }

    fn parent_of(self, i: int) -> int {
        if (i == RB_NIL) {
            return RB_NIL;
        }
        return self.nodes[i].parent;
    }

    fn grandparent_of(self, i: int) -> int {
        return self.parent_of(self.parent_of(i));
    }

    // Which side of its parent a node hangs off, or NoChild for the root.
    fn side_of(self, i: int) -> ChildSide {
        let p = self.parent_of(i);
        if (p == RB_NIL) {
            return ChildSide.NoChild;
        } else {
            if (self.nodes[p].left == i) {
                return ChildSide.LeftChild;
            } else {
                return ChildSide.RightChild;
            }
        }
    }

    fn sibling_of(self, i: int) -> int {
        let p = self.parent_of(i);
        if (p == RB_NIL) {
            return RB_NIL;
        } else {
            if (self.nodes[p].left == i) {
                return self.nodes[p].right;
            } else {
                return self.nodes[p].left;
            }
        }
    }

    fn uncle_of(self, i: int) -> int {
        return self.sibling_of(self.parent_of(i));
    }

    fn is_left_child(self, i: int) -> bool {
        return self.side_of(i) == ChildSide.LeftChild;
    }

    fn is_right_child(self, i: int) -> bool {
        return self.side_of(i) == ChildSide.RightChild;
    }

    // Re-point whatever used to reference `old` at `fresh`.
    fn attach(ref self, parent: int, child: int, side: ChildSide) {
        if (parent == RB_NIL) {
            self.root = child;
        } else {
            if (side == ChildSide.LeftChild) {
                self.nodes[parent].left = child;
            } else {
                self.nodes[parent].right = child;
            }
        }
        if (child != RB_NIL) {
            self.nodes[child].parent = parent;
        }
    }

    // --- Rotations --------------------------------------------------------
    //
    //        x                y
    //       / \              / \
    //      a   y    ==>     x   c
    //         / \          / \
    //        b   c        a   b

    fn rotate_left(ref self, x: int) {
        if (x == RB_NIL) {
            return;
        }
        let y = self.nodes[x].right;
        if (y == RB_NIL) {
            // Nothing to pivot on; a well-formed caller never gets here.
            return;
        }
        self.nodes[x].right = self.nodes[y].left;
        if (self.nodes[y].left != RB_NIL) {
            self.nodes[self.nodes[y].left].parent = x;
        }
        self.nodes[y].parent = self.nodes[x].parent;
        if (self.nodes[x].parent == RB_NIL) {
            self.root = y;
        } else {
            if (x == self.nodes[self.nodes[x].parent].left) {
                self.nodes[self.nodes[x].parent].left = y;
            } else {
                self.nodes[self.nodes[x].parent].right = y;
            }
        }
        self.nodes[y].left = x;
        self.nodes[x].parent = y;
        self.rotations += 1;
    }

    //        y                x
    //       / \              / \
    //      x   c    ==>     a   y
    //     / \                  / \
    //    a   b                b   c

    fn rotate_right(ref self, y: int) {
        if (y == RB_NIL) {
            return;
        }
        let x = self.nodes[y].left;
        if (x == RB_NIL) {
            return;
        }
        self.nodes[y].left = self.nodes[x].right;
        if (self.nodes[x].right != RB_NIL) {
            self.nodes[self.nodes[x].right].parent = y;
        }
        self.nodes[x].parent = self.nodes[y].parent;
        if (self.nodes[y].parent == RB_NIL) {
            self.root = x;
        } else {
            if (y == self.nodes[self.nodes[y].parent].right) {
                self.nodes[self.nodes[y].parent].right = x;
            } else {
                self.nodes[self.nodes[y].parent].left = x;
            }
        }
        self.nodes[x].right = y;
        self.nodes[y].parent = x;
        self.rotations += 1;
    }

    // Rotate `i` in whichever direction brings its subtree back into balance.
    fn rotate_towards(ref self, i: int, side: ChildSide) {
        if (side == ChildSide.LeftChild) {
            self.rotate_right(i);
        } else {
            if (side == ChildSide.RightChild) {
                self.rotate_left(i);
            } else {
                Discard(i);
            }
        }
    }

    // --- Arena management -------------------------------------------------

    fn reserve(ref self, capacity: int) {
        if (capacity > self.nodes.size()) {
            self.nodes.reserve(capacity);
        }
    }

    fn allocate(ref self, key: K, value: V) -> int {
        if (self.free_list != RB_NIL) {
            // Recycle a slot released by a previous erase.
            let slot = self.free_list;
            self.free_list = self.nodes[slot].right;
            self.nodes[slot].key = key;
            self.nodes[slot].value = value;
            self.nodes[slot].reset_links();
            self.nodes[slot].colour = Colour.Red;
            self.nodes[slot].live = true;
            return slot;
        }
        var fresh = RbNode(K, V)({.key = key, .value = value});
        fresh.colour = Colour.Red;
        self.nodes.push_back(fresh);
        return self.nodes.size() - 1;
    }

    fn release(ref self, i: int) {
        if (i == RB_NIL) {
            return;
        }
        self.nodes[i].live = false;
        self.nodes[i].left = RB_NIL;
        self.nodes[i].parent = RB_NIL;
        self.nodes[i].right = self.free_list;
        self.free_list = i;
    }

    // --- Search -----------------------------------------------------------

    fn find_index(self, key: K) -> int {
        var current = self.root;
        while (current != RB_NIL) {
            let order = compare_keys(self.nodes[current].key, *key);
            if (order == 0) {
                return current;
            } else {
                if (order > 0) {
                    current = self.nodes[current].left;
                } else {
                    current = self.nodes[current].right;
                }
            }
        }
        return RB_NIL;
    }

    fn contains(self, key: K) -> bool {
        return self.find_index(key) != RB_NIL;
    }

    fn at(ref self, key: K) -> ref V {
        let i = self.find_index(key);
        if (i == RB_NIL) {
            return self.missing;
        }
        return self.nodes[i].value;
    }

    // Smallest node whose key is >= `key`, or RB_NIL when none exists.
    fn lower_bound(self, key: K) -> int {
        var current = self.root;
        var best = RB_NIL;
        while (current != RB_NIL) {
            let order = compare_keys(self.nodes[current].key, *key);
            if (order < 0) {
                current = self.nodes[current].right;
            } else {
                best = current;
                if (order == 0) {
                    // Exact hit: nothing to the left can be smaller and still
                    // satisfy the bound.
                    return current;
                } else {
                    current = self.nodes[current].left;
                }
            }
        }
        return best;
    }

    // Smallest node whose key is strictly greater than `key`.
    fn upper_bound(self, key: K) -> int {
        var current = self.root;
        var best = RB_NIL;
        while (current != RB_NIL) {
            let order = compare_keys(self.nodes[current].key, *key);
            if (order > 0) {
                best = current;
                current = self.nodes[current].left;
            } else {
                current = self.nodes[current].right;
            }
        }
        return best;
    }

    // --- Extremes and stepping -------------------------------------------

    fn subtree_minimum(self, start: int) -> int {
        var current = start;
        if (current == RB_NIL) {
            return RB_NIL;
        }
        while (self.nodes[current].left != RB_NIL) {
            current = self.nodes[current].left;
        }
        return current;
    }

    fn subtree_maximum(self, start: int) -> int {
        var current = start;
        if (current == RB_NIL) {
            return RB_NIL;
        }
        while (self.nodes[current].right != RB_NIL) {
            current = self.nodes[current].right;
        }
        return current;
    }

    fn minimum(self) -> int {
        return self.subtree_minimum(self.root);
    }

    fn maximum(self) -> int {
        return self.subtree_maximum(self.root);
    }

    fn successor(self, i: int) -> int {
        if (i == RB_NIL) {
            return RB_NIL;
        }
        if (self.nodes[i].right != RB_NIL) {
            return self.subtree_minimum(self.nodes[i].right);
        }
        var current = i;
        var up = self.nodes[i].parent;
        while (up != RB_NIL and current == self.nodes[up].right) {
            current = up;
            up = self.nodes[up].parent;
        }
        return up;
    }

    fn predecessor(self, i: int) -> int {
        if (i == RB_NIL) {
            return RB_NIL;
        }
        if (self.nodes[i].left != RB_NIL) {
            return self.subtree_maximum(self.nodes[i].left);
        }
        var current = i;
        var up = self.nodes[i].parent;
        while (up != RB_NIL and current == self.nodes[up].left) {
            current = up;
            up = self.nodes[up].parent;
        }
        return up;
    }

    // --- Insertion --------------------------------------------------------
    //
    // Ordinary binary-search-tree descent, then `insert_fixup` restores the
    // colour invariants that the freshly painted red node may have broken.

    fn insert(ref self, key: K, value: V) -> bool {
        if (self.nodes.size() == 0) {
            self.reserve(RB_INITIAL_CAPACITY);
        }

        var parent = RB_NIL;
        var current = self.root;
        var side = ChildSide.NoChild;

        while (current != RB_NIL) {
            parent = current;
            let order = compare_keys(self.nodes[current].key, key);
            if (order == 0) {
                // Key already present: overwrite in place, shape unchanged.
                self.nodes[current].value = value;
                return false;
            } else {
                if (order > 0) {
                    side = ChildSide.LeftChild;
                    current = self.nodes[current].left;
                } else {
                    side = ChildSide.RightChild;
                    current = self.nodes[current].right;
                }
            }
        }

        let fresh = self.allocate(key, value);
        self.nodes[fresh].parent = parent;
        if (parent == RB_NIL) {
            self.root = fresh;
        } else {
            if (side == ChildSide.LeftChild) {
                self.nodes[parent].left = fresh;
            } else {
                self.nodes[parent].right = fresh;
            }
        }
        self.count += 1;
        self.insert_fixup(fresh);
        self.paint_black(self.root);
        return true;
    }

    // Restore rules 2 and 3 after a red node was spliced in at `start`.
    //
    // The case analysis is deliberately written as a nested tree rather than
    // a flat chain: outer level picks the parent's side, the next level
    // distinguishes a red uncle from a black one, and the innermost level
    // separates the "inner" triangle case from the "outer" line case.
    fn insert_fixup(ref self, start: int) {
        var z = start;
        var guard: int = 0;

        while (z != RB_NIL and guard < RB_MAX_HEIGHT) {
            guard += 1;
            let parent = self.nodes[z].parent;

            if (parent == RB_NIL) {
                // z is the root; rule 2 is restored by the caller.
                self.paint_black(z);
                break;
            } else {
                if (self.is_black(parent)) {
                    // No red-red violation left to repair.
                    break;
                } else {
                    let grand = self.nodes[parent].parent;
                    if (grand == RB_NIL) {
                        // A red root with a red child: blacken the root.
                        self.paint_black(parent);
                        break;
                    } else {
                        if (parent == self.nodes[grand].left) {
                            // --- Parent is the LEFT child of grandparent ---
                            let uncle = self.nodes[grand].right;
                            if (uncle != RB_NIL and self.is_red(uncle)) {
                                // Case 1: recolour and push the problem up.
                                self.paint_black(parent);
                                self.paint_black(uncle);
                                self.paint_red(grand);
                                self.recolours += 3;
                                z = grand;
                            } else {
                                // Uncle is black (possibly NIL).
                                if (z == self.nodes[parent].right) {
                                    // Case 2: inner triangle.  Rotate left at
                                    // the parent to turn it into case 3, then
                                    // fall straight through to that case.
                                    z = parent;
                                    self.rotate_left(z);
                                    let np = self.nodes[z].parent;
                                    if (np == RB_NIL) {
                                        self.paint_black(z);
                                        break;
                                    } else {
                                        let ng = self.nodes[np].parent;
                                        if (ng == RB_NIL) {
                                            self.paint_black(np);
                                            break;
                                        } else {
                                            // Case 3 after the pre-rotation.
                                            self.paint_black(np);
                                            self.paint_red(ng);
                                            self.recolours += 2;
                                            self.rotate_right(ng);
                                        }
                                    }
                                } else {
                                    // Case 3: outer line, single rotation.
                                    self.paint_black(parent);
                                    self.paint_red(grand);
                                    self.recolours += 2;
                                    self.rotate_right(grand);
                                }
                            }
                        } else {
                            // --- Parent is the RIGHT child (mirror image) --
                            let uncle = self.nodes[grand].left;
                            if (uncle != RB_NIL and self.is_red(uncle)) {
                                // Case 1 mirrored.
                                self.paint_black(parent);
                                self.paint_black(uncle);
                                self.paint_red(grand);
                                self.recolours += 3;
                                z = grand;
                            } else {
                                if (z == self.nodes[parent].left) {
                                    // Case 2 mirrored: inner triangle.
                                    z = parent;
                                    self.rotate_right(z);
                                    let np = self.nodes[z].parent;
                                    if (np == RB_NIL) {
                                        self.paint_black(z);
                                        break;
                                    } else {
                                        let ng = self.nodes[np].parent;
                                        if (ng == RB_NIL) {
                                            self.paint_black(np);
                                            break;
                                        } else {
                                            // Case 3 mirrored.
                                            self.paint_black(np);
                                            self.paint_red(ng);
                                            self.recolours += 2;
                                            self.rotate_left(ng);
                                        }
                                    }
                                } else {
                                    // Case 3 mirrored: outer line.
                                    self.paint_black(parent);
                                    self.paint_red(grand);
                                    self.recolours += 2;
                                    self.rotate_left(grand);
                                }
                            }
                        }
                    }
                }
            }
        }

        self.paint_black(self.root);
    }

    // --- Deletion ---------------------------------------------------------

    // Replace the subtree rooted at `target` with the subtree rooted at
    // `replacement`.  The replacement's own children are left untouched.
    fn transplant(ref self, target: int, replacement: int) {
        let parent = self.parent_of(target);
        if (parent == RB_NIL) {
            self.root = replacement;
        } else {
            if (target == self.nodes[parent].left) {
                self.nodes[parent].left = replacement;
            } else {
                self.nodes[parent].right = replacement;
            }
        }
        if (replacement != RB_NIL) {
            self.nodes[replacement].parent = parent;
        }
    }

    fn erase(ref self, key: K) -> bool {
        let victim = self.find_index(key);
        if (victim == RB_NIL) {
            return false;
        }
        self.erase_index(victim);
        return true;
    }

    fn erase_index(ref self, victim: int) {
        if (victim == RB_NIL) {
            return;
        }

        // `removed` is the node that physically leaves its position, which
        // for a two-child victim is the in-order successor rather than the
        // victim itself.
        var removed = victim;
        var removed_colour = self.nodes[victim].colour;
        var fixup_at = RB_NIL;
        var fixup_parent = RB_NIL;

        if (self.nodes[victim].left == RB_NIL) {
            // At most a right child: splice it up directly.
            fixup_at = self.nodes[victim].right;
            fixup_parent = self.nodes[victim].parent;
            self.transplant(victim, self.nodes[victim].right);
        } else {
            if (self.nodes[victim].right == RB_NIL) {
                // At most a left child.
                fixup_at = self.nodes[victim].left;
                fixup_parent = self.nodes[victim].parent;
                self.transplant(victim, self.nodes[victim].left);
            } else {
                // Two children: hoist the in-order successor into place.
                removed = self.subtree_minimum(self.nodes[victim].right);
                removed_colour = self.nodes[removed].colour;
                fixup_at = self.nodes[removed].right;
                if (self.nodes[removed].parent == victim) {
                    // Successor is the victim's direct right child.
                    fixup_parent = removed;
                    if (fixup_at != RB_NIL) {
                        self.nodes[fixup_at].parent = removed;
                    }
                } else {
                    fixup_parent = self.nodes[removed].parent;
                    self.transplant(removed, self.nodes[removed].right);
                    self.nodes[removed].right = self.nodes[victim].right;
                    self.nodes[self.nodes[removed].right].parent = removed;
                }
                self.transplant(victim, removed);
                self.nodes[removed].left = self.nodes[victim].left;
                self.nodes[self.nodes[removed].left].parent = removed;
                self.nodes[removed].colour = self.nodes[victim].colour;
            }
        }

        self.release(victim);
        self.count -= 1;

        if (removed_colour == Colour.Black) {
            // Removing a black node shortens one path by one black link, so
            // rule 4 has to be repaired.
            self.delete_fixup(fixup_at, fixup_parent);
        }
        self.paint_black(self.root);
    }

    // Rebalance after a black node was removed from below `parent`.
    //
    // `x` carries an extra conceptual "black token"; the loop pushes that
    // token up the tree until it can be absorbed.  `x` may legitimately be
    // RB_NIL, which is why the parent is threaded through explicitly.
    fn delete_fixup(ref self, start: int, start_parent: int) {
        var x = start;
        var parent = start_parent;
        var guard: int = 0;

        while (x != self.root and guard < RB_MAX_HEIGHT) {
            guard += 1;
            if (self.is_red(x)) {
                // Absorb the extra black into a red node.
                self.paint_black(x);
                break;
            } else {
                if (parent == RB_NIL) {
                    // Reached the root through the parent chain.
                    break;
                } else {
                    if (x == self.nodes[parent].left) {
                        // --- x is the LEFT child ---------------------------
                        var w = self.nodes[parent].right;
                        if (self.is_red(w)) {
                            // Case 1: red sibling.  Rotate it out of the way
                            // so the remaining cases see a black sibling.
                            self.paint_black(w);
                            self.paint_red(parent);
                            self.recolours += 2;
                            self.rotate_left(parent);
                            w = self.nodes[parent].right;
                        }
                        if (w == RB_NIL) {
                            // Degenerate shape: push the token upwards.
                            x = parent;
                            parent = self.nodes[x].parent;
                        } else {
                            if (self.is_black(self.nodes[w].left) and self.is_black(self.nodes[w].right)) {
                                // Case 2: both of the sibling's children are
                                // black.  Recolour and move the token up.
                                self.paint_red(w);
                                self.recolours += 1;
                                x = parent;
                                parent = self.nodes[x].parent;
                            } else {
                                if (self.is_black(self.nodes[w].right)) {
                                    // Case 3: the sibling's far child is
                                    // black but its near child is red.
                                    // Rotate right to convert into case 4.
                                    if (self.nodes[w].left != RB_NIL) {
                                        self.paint_black(self.nodes[w].left);
                                        self.recolours += 1;
                                    }
                                    self.paint_red(w);
                                    self.rotate_right(w);
                                    w = self.nodes[parent].right;
                                }
                                if (w == RB_NIL) {
                                    x = parent;
                                    parent = self.nodes[x].parent;
                                } else {
                                    // Case 4: the sibling's far child is red.
                                    // One rotation finishes the repair.
                                    self.set_colour(w, self.colour_of(parent));
                                    self.paint_black(parent);
                                    if (self.nodes[w].right != RB_NIL) {
                                        self.paint_black(self.nodes[w].right);
                                        self.recolours += 1;
                                    }
                                    self.rotate_left(parent);
                                    x = self.root;
                                    parent = RB_NIL;
                                }
                            }
                        }
                    } else {
                        // --- x is the RIGHT child (mirror image) -----------
                        var w = self.nodes[parent].left;
                        if (self.is_red(w)) {
                            // Case 1 mirrored.
                            self.paint_black(w);
                            self.paint_red(parent);
                            self.recolours += 2;
                            self.rotate_right(parent);
                            w = self.nodes[parent].left;
                        }
                        if (w == RB_NIL) {
                            x = parent;
                            parent = self.nodes[x].parent;
                        } else {
                            if (self.is_black(self.nodes[w].left) and self.is_black(self.nodes[w].right)) {
                                // Case 2 mirrored.
                                self.paint_red(w);
                                self.recolours += 1;
                                x = parent;
                                parent = self.nodes[x].parent;
                            } else {
                                if (self.is_black(self.nodes[w].left)) {
                                    // Case 3 mirrored.
                                    if (self.nodes[w].right != RB_NIL) {
                                        self.paint_black(self.nodes[w].right);
                                        self.recolours += 1;
                                    }
                                    self.paint_red(w);
                                    self.rotate_left(w);
                                    w = self.nodes[parent].left;
                                }
                                if (w == RB_NIL) {
                                    x = parent;
                                    parent = self.nodes[x].parent;
                                } else {
                                    // Case 4 mirrored.
                                    self.set_colour(w, self.colour_of(parent));
                                    self.paint_black(parent);
                                    if (self.nodes[w].left != RB_NIL) {
                                        self.paint_black(self.nodes[w].left);
                                        self.recolours += 1;
                                    }
                                    self.rotate_right(parent);
                                    x = self.root;
                                    parent = RB_NIL;
                                }
                            }
                        }
                    }
                }
            }
        }

        self.paint_black(x);
    }

    // --- Traversals -------------------------------------------------------
    //
    // Both traversals are iterative: the arena representation makes an
    // explicit index stack cheaper than recursion, and it keeps the worst
    // case stack depth bounded by RB_MAX_HEIGHT.

    fn in_order(self, ref out: vector(int)) {
        var stack = vector(int)();
        stack.reserve(RB_MAX_HEIGHT);
        var current = self.root;
        while (current != RB_NIL or stack.size() > 0) {
            if (current != RB_NIL) {
                stack.push_back(current);
                current = self.nodes[current].left;
            } else {
                let top = stack.back();
                stack.pop_back();
                out.push_back(top);
                current = self.nodes[top].right;
            }
        }
        Destroy(stack);
    }

    fn pre_order(self, ref out: vector(int)) {
        if (self.root == RB_NIL) {
            return;
        }
        var stack = vector(int)();
        stack.reserve(RB_MAX_HEIGHT);
        stack.push_back(self.root);
        while (stack.size() > 0) {
            let top = stack.back();
            stack.pop_back();
            out.push_back(top);
            // Right first so the left child is popped next.
            if (self.nodes[top].right != RB_NIL) {
                stack.push_back(self.nodes[top].right);
            }
            if (self.nodes[top].left != RB_NIL) {
                stack.push_back(self.nodes[top].left);
            }
        }
        Destroy(stack);
    }

    fn keys_in_order(self, ref out: vector(K)) {
        var order = vector(int)();
        self.in_order(order);
        for (i in order) {
            out.push_back(self.nodes[i].key);
        }
        Destroy(order);
    }

    // --- Height and shape metrics ----------------------------------------

    fn height(self) -> int {
        return self.subtree_height(self.root);
    }

    fn subtree_height(self, start: int) -> int {
        if (start == RB_NIL) {
            return 0;
        }
        var stack = vector(int)();
        var depths = vector(int)();
        stack.push_back(start);
        depths.push_back(1);
        var best: int = 0;
        while (stack.size() > 0) {
            let node = stack.back();
            let depth = depths.back();
            stack.pop_back();
            depths.pop_back();
            if (depth > best) {
                best = depth;
            }
            if (self.nodes[node].left != RB_NIL) {
                stack.push_back(self.nodes[node].left);
                depths.push_back(depth + 1);
            }
            if (self.nodes[node].right != RB_NIL) {
                stack.push_back(self.nodes[node].right);
                depths.push_back(depth + 1);
            }
        }
        Destroy(stack);
        Destroy(depths);
        return best;
    }

    // Number of black nodes on the leftmost spine, which by rule 4 equals the
    // black height of every other root-to-leaf path.
    fn black_height(self) -> int {
        var current = self.root;
        var blacks: int = 0;
        while (current != RB_NIL) {
            if (self.is_black(current)) {
                blacks += 1;
            }
            current = self.nodes[current].left;
        }
        // The NIL leaf itself counts as black.
        return blacks + 1;
    }

    // Recomputes the black height of a subtree, returning -1 when the paths
    // below `start` disagree.  Used only by the validity checker.
    fn measured_black_height(self, start: int) -> int {
        if (start == RB_NIL) {
            return 1;
        }
        let left = self.measured_black_height(self.nodes[start].left);
        if (left < 0) {
            return -1;
        } else {
            let right = self.measured_black_height(self.nodes[start].right);
            if (right < 0) {
                return -1;
            } else {
                if (left != right) {
                    return -1;
                } else {
                    if (self.is_black(start)) {
                        return left + 1;
                    } else {
                        return left;
                    }
                }
            }
        }
    }

    fn min_key(ref self) -> ref K {
        let i = self.minimum();
        if (i == RB_NIL) {
            return self.missing_key;
        }
        return self.nodes[i].key;
    }

    fn max_key(ref self) -> ref K {
        let i = self.maximum();
        if (i == RB_NIL) {
            return self.missing_key;
        }
        return self.nodes[i].key;
    }

    // --- Validity checker -------------------------------------------------
    //
    // Every red-black rule is restated here so the verifier has the
    // invariants available as explicit propositions.

    fn check_validity(self) -> bool {
        Assert(self.count >= 0);
        Assert(self.count <= self.nodes.size());

        if (self.root == RB_NIL) {
            // An empty tree trivially satisfies every rule.
            Assert(self.count == 0);
            Prove(self.count == 0);
            return true;
        }

        // Rule 2: the root is black.
        Assert(self.is_black(self.root));
        Prove(self.colour_of(self.root) == Colour.Black);

        // Rule 5: the sentinel leaf is black.
        Assert(self.is_black(RB_NIL));
        Prove(self.colour_of(RB_NIL) == Colour.Black);

        var order = vector(int)();
        self.in_order(order);

        // The in-order walk must visit exactly the live nodes.
        Assert(order.size() == self.count);
        Prove(order.size() == self.count);

        var index: int = 0;
        while (index < order.size()) {
            let node = order[index];
            Assert(self.nodes[node].live);

            // Rule 1: the colour is one of exactly two values.
            Assert(self.colour_of(node) == Colour.Red or self.colour_of(node) == Colour.Black);

            // Rule 3: a red node has no red child.
            if (self.is_red(node)) {
                Assert(self.is_black(self.nodes[node].left));
                Assert(self.is_black(self.nodes[node].right));
                Prove(self.is_black(self.left_of(node)) and self.is_black(self.right_of(node)));
            } else {
                // Black nodes constrain nothing about their children's colour,
                // but the parent links must still be consistent.
                if (self.nodes[node].left != RB_NIL) {
                    Assert(self.parent_of(self.nodes[node].left) == node);
                }
                if (self.nodes[node].right != RB_NIL) {
                    Assert(self.parent_of(self.nodes[node].right) == node);
                }
            }

            // Search-tree ordering: the walk is strictly increasing.
            if (index > 0) {
                let previous = order[index - 1];
                Assert(compare_keys(self.nodes[previous].key, self.nodes[node].key) < 0);
                Prove(compare_keys(self.nodes[previous].key, self.nodes[node].key) < 0);
            }

            index += 1;
        }
        Destroy(order);

        // Rule 4: every root-to-leaf path carries the same black count.
        let measured = self.measured_black_height(self.root);
        Assert(measured > 0);
        Assert(measured == self.black_height());
        Prove(measured == self.black_height());

        // The classic height bound follows from rules 3 and 4.
        Assert(self.height() <= 2 * measured);
        Prove(self.height() <= 2 * self.black_height());

        return true;
    }

    // --- Bulk operations --------------------------------------------------

    fn clear(ref self) {
        for (node in self.nodes) {
            node.live = false;
            node.reset_links();
        }
        self.nodes.clear();
        self.root = RB_NIL;
        self.free_list = RB_NIL;
        self.count = 0;
        self.rotations = 0;
        self.recolours = 0;
    }

    var nodes: vector(RbNode(K, V));
    var root: int = -1;
    var free_list: int = -1;
    var count: int = 0;
    var rotations: int = 0;
    var recolours: int = 0;
    var missing: V;
    var missing_key: K;
}

// --- Free functions -------------------------------------------------------

// Three-way comparison used by every ordered operation above.
fn compare_keys[T: type]
    (a: T, b: T) -> int {
    if (a < b) {
        return -1;
    } else {
        if (b < a) {
            return 1;
        } else {
            return 0;
        }
    }
}

// Copies every key/value pair of `source` into `target`, preserving order.
fn merge_into[K: type, V: type]
    (ref target: RedBlackTree(K, V), source: RedBlackTree(K, V)) {
    var order = vector(int)();
    source.in_order(order);
    for (i in order) {
        target.insert(source.nodes[i].key, source.nodes[i].value);
    }
    Destroy(order);
}

// Removes every key in the half-open range [low, high).
fn erase_range[K: type, V: type]
    (ref tree: RedBlackTree(K, V), low: K, high: K) -> int {
    var removed: int = 0;
    while (true) {
        let node = tree.lower_bound(low);
        if (node == RB_NIL) {
            break;
        } else {
            if (compare_keys(tree.nodes[node].key, *high) >= 0) {
                break;
            } else {
                tree.erase_index(node);
                removed += 1;
            }
        }
    }
    return removed;
}

fn describe_tree() {
    var tree = RedBlackTree(int, string)();
    tree.reserve(RB_INITIAL_CAPACITY);
    for (i in range(0, 32)) {
        tree.insert((i * 7) % 32, to_string(i));
    }
    println(to_string(tree.size()));
    println(to_string(tree.height()));
    println(to_string(tree.black_height()));
    if (not tree.check_validity()) {
        panic("red-black invariants violated after bulk insert");
    }
    var index: int = 0;
    while (index < 32) {
        if (index % 2 == 0) {
            Discard(tree.erase(index));
        }
        index += 1;
    }
    if (not tree.check_validity()) {
        panic("red-black invariants violated after bulk erase");
    }
    Destroy(tree);
}


