// Generic B-tree implementing an ordered map from K to V.
//
// The tree is parameterised three ways:
//
//   * `K`     -- the key type, ordered by `compare_keys`,
//   * `V`     -- the mapped value type,
//   * `ORDER` -- the branching factor, i.e. the maximum number of children a
//                single node may have.  A node therefore stores at most
//                `ORDER - 1` keys and, unless it is the root, at least
//                `ORDER / 2 - 1` keys.
//
// Every node lives in a flat arena (`nodes`) and every link is an integer
// index into that arena; `BTREE_NIL` (-1) stands for "no such node".  Keeping
// the links as indices means a whole tree can be copied, serialised or
// relocated without touching a single pointer.
//
// The structural invariants, restated by `check_validity` with `assert` and
// `prove`, are:
//
//   1. Keys inside a node are strictly increasing.
//   2. A node with `n` keys has either `0` or `n + 1` children.
//   3. Every non-root node holds between `min_keys()` and `max_keys()` keys.
//   4. All leaves sit at exactly the same depth.
//   5. The subtree hanging off child `i` contains only keys that are greater
//      than key `i - 1` and smaller than key `i`.
//
// Insertion splits full nodes on the way down (proactive splitting), so a
// split never has to propagate back towards the root.  Deletion mirrors that
// by refilling any child that is about to become minimal *before* descending
// into it, either by borrowing a separator from a sibling or by merging with
// one.
namespace b_tree;

// --- Enumerations ---------------------------------------------------------

// What happened while making room for a new key.
choice SplitOutcome {
    NoSplit,
    SplitPerformed,
    RootGrew,
}

// How a child was rescued from dropping below the minimum occupancy.
choice RebalanceAction {
    BorrowedFromLeft,
    BorrowedFromRight,
    MergedWithLeft,
    MergedWithRight,
    NoActionNeeded,
}

// Result classification for a search inside a single node.
choice SlotKind {
    ExactMatch,
    DescendLeft,
    DescendRight,
    MissingSlot,
}

// Direction of a traversal or of a range scan.
choice ScanOrder {
    Ascending,
    Descending,
}

// --- Tuning constants -----------------------------------------------------

// Sentinel index meaning "no node".  Negative so it can never index the arena.
let BTREE_NIL: int = -1;

// Branching factor used when a tree is created without an explicit order.
let BTREE_DEFAULT_ORDER: int = 6;

// Upper bound on the height of any tree we are willing to build.  Only used
// to size traversal stacks up front so they never reallocate.
let BTREE_MAX_HEIGHT: int = 48;

// Arena capacity used when a tree is created without a size hint.
let BTREE_INITIAL_CAPACITY: int = 32;

// Growth factor for the arena, expressed as a percentage so that no floating
// point is required anywhere in the container.
let BTREE_GROWTH_PERCENT: int = 200;

// Target fill level, in percent, used by `bulk_load` when it decides how many
// keys to pack into each freshly built leaf.  Leaving a little slack means the
// first few insertions after a bulk load do not immediately split anything.
let BTREE_BULK_FILL_PERCENT: int = 75;

// Number of key/value samples the statistics pass is allowed to retain.
let BTREE_SAMPLE_LIMIT: int = 8;

// --- Node -----------------------------------------------------------------

// A single B-tree node.
//
// `keys` and `values` are parallel arrays of length `count`; `children` is
// either empty (a leaf) or of length `count + 1` (an internal node).  The node
// is generic over the branching factor as well as the key and value types, so
// `max_keys()` and `min_keys()` are compile-time constants rather than fields.
class BTreeNode(K: type, V: type, ORDER: int)
{

    // --- Capacity ---------------------------------------------------------

    fn max_children(self) -> int {
        return ORDER;
    }

    fn max_keys(self) -> int {
        return ORDER - 1;
    }

    // Minimum occupancy for every node except the root.  Written as a
    // division so that both even and odd branching factors behave.
    fn min_keys(self) -> int {
        return ORDER / 2 - 1;
    }

    fn min_children(self) -> int {
        return ORDER / 2;
    }

    fn is_full(self) -> bool {
        return self.count >= ORDER - 1;
    }

    fn is_minimal(self) -> bool {
        return self.count <= ORDER / 2 - 1;
    }

    // True when a key can be taken away without violating rule 3.
    fn has_spare_key(self) -> bool {
        return self.count > ORDER / 2 - 1;
    }

    fn is_leaf(self) -> bool {
        return self.leaf;
    }

    fn key_count(self) -> int {
        return self.count;
    }

    fn child_count(self) -> int {
        if (self.leaf) {
            return 0;
        }
        return self.count + 1;
    }

    fn occupancy_percent(self) -> int {
        if (ORDER <= 1) {
            return 0;
        }
        return self.count * 100 / (ORDER - 1);
    }

    // --- Slot access ------------------------------------------------------

    fn key_at(ref self, index: int) -> ref K {
        return self.keys[index];
    }

    fn value_at(ref self, index: int) -> ref V {
        return self.values[index];
    }

    fn child_at(self, index: int) -> int {
        if (self.leaf) {
            return BTREE_NIL;
        }
        if (index < 0 or index > self.count) {
            return BTREE_NIL;
        }
        return self.children[index];
    }

    // Materialises slot `index` as a `pair{K, V}`; the pair is the unit every
    // bulk operation in this file trades in.
    fn entry_at(self, index: int) -> pair(K, V) {
        return pair(K, V)({.first = self.keys[index], .second = self.values[index]});
    }

    // Copies the whole node out as a vector of pairs.  Used by the bulk
    // loader, by `in_order` and by the statistics pass.
    fn entries(self) -> vector(pair(K, V)) {
        var out = vector(pair(K, V))();
        out.reserve(self.count);
        var i: int = 0;
        while (i < self.count) {
            out.push_back(pair(K, V)({.first = self.keys[i], .second = self.values[i]}));
            i += 1;
        }
        return out;
    }

    // The node's separator keys paired with the child that sits to their left.
    fn separators(self) -> vector(pair(K, int)) {
        var out = vector(pair(K, int))();
        if (self.leaf) {
            return out;
        }
        var i: int = 0;
        while (i < self.count) {
            out.push_back(pair(K, int)({.first = self.keys[i], .second = self.children[i]}));
            i += 1;
        }
        return out;
    }

    // --- Search inside one node -------------------------------------------

    // Index of the first key that is not smaller than `key`.  This is the
    // classic branch point: on an exact hit it is the matching slot, otherwise
    // it is the index of the child that must be descended into.
    fn lower_bound(self, key: K) -> int {
        var lo: int = 0;
        var hi: int = self.count;
        while (lo < hi) {
            let mid = lo + (hi - lo) / 2;
            if (compare_keys(self.keys[mid], *key) < 0) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo;
    }

    // Index of the first key that is strictly greater than `key`.
    fn upper_bound(self, key: K) -> int {
        var lo: int = 0;
        var hi: int = self.count;
        while (lo < hi) {
            let mid = lo + (hi - lo) / 2;
            if (compare_keys(self.keys[mid], *key) <= 0) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo;
    }

    // A linear scan, kept because it is what the validity checker uses: the
    // binary search above is only correct if rule 1 holds, and the checker
    // must not assume the invariant it is trying to prove.
    fn linear_find(self, key: K) -> int {
        var i: int = 0;
        while (i < self.count) {
            if (compare_keys(self.keys[i], *key) == 0) {
                return i;
            }
            i += 1;
        }
        return -1;
    }

    fn contains_key(self, key: K) -> bool {
        let slot = self.lower_bound(key);
        if (slot >= self.count) {
            return false;
        }
        return compare_keys(self.keys[slot], *key) == 0;
    }

    // Classifies a search without performing it, so callers can branch on an
    // enum rather than on a pair of integers.
    fn classify(self, key: K) -> SlotKind {
        if (self.count == 0) {
            return SlotKind.MissingSlot;
        }
        let slot = self.lower_bound(key);
        if (slot < self.count) {
            if (compare_keys(self.keys[slot], *key) == 0) {
                return SlotKind.ExactMatch;
            } else {
                return SlotKind.DescendLeft;
            }
        } else {
            return SlotKind.DescendRight;
        }
    }

    // Search returning both the slot and whether it was a hit.  The `pair`
    // return type is what every caller in `BTree` destructures.
    fn probe(self, key: K) -> pair(int, bool) {
        let slot = self.lower_bound(key);
        if (slot < self.count) {
            if (compare_keys(self.keys[slot], *key) == 0) {
                return pair(int, bool)({.first = slot, .second = true});
            }
        }
        return pair(int, bool)({.first = slot, .second = false});
    }

    // --- Array shuffling --------------------------------------------------
    //
    // Every mutation below moves elements by hand instead of calling
    // `vector::insert` / `vector::erase`, because the key, value and child
    // arrays have to stay in step with each other and with `count`.

    // Opens a hole at `at` by shifting keys and values one slot to the right.
    fn shift_keys_right(ref self, at: int) {
        self.keys.push_back(self.keys[self.count - 1]);
        self.values.push_back(self.values[self.count - 1]);
        var i = self.count - 1;
        while (i > at) {
            self.keys[i] = self.keys[i - 1];
            self.values[i] = self.values[i - 1];
            i -= 1;
        }
    }

    // Closes the hole at `at` by shifting keys and values one slot left.
    fn shift_keys_left(ref self, at: int) {
        var i = at;
        while (i + 1 < self.count) {
            self.keys[i] = self.keys[i + 1];
            self.values[i] = self.values[i + 1];
            i += 1;
        }
        self.keys.pop_back();
        self.values.pop_back();
    }

    fn shift_children_right(ref self, at: int) {
        if (self.leaf) {
            return;
        }
        self.children.push_back(self.children[self.count]);
        var i = self.count;
        while (i > at) {
            self.children[i] = self.children[i - 1];
            i -= 1;
        }
    }

    fn shift_children_left(ref self, at: int) {
        if (self.leaf) {
            return;
        }
        var i = at;
        while (i + 1 <= self.count) {
            self.children[i] = self.children[i + 1];
            i += 1;
        }
        self.children.pop_back();
    }

    // --- Primitive edits --------------------------------------------------

    fn insert_entry_at(ref self, at: int, key: K, value: V) {
        if (self.count == 0) {
            self.keys.push_back(key);
            self.values.push_back(value);
            self.count = 1;
            return;
        }
        if (at >= self.count) {
            self.keys.push_back(key);
            self.values.push_back(value);
            self.count += 1;
            return;
        }
        self.shift_keys_right(at);
        self.keys[at] = key;
        self.values[at] = value;
        self.count += 1;
    }

    fn remove_entry_at(ref self, at: int) -> pair(K, V) {
        let taken = pair(K, V)({.first = self.keys[at], .second = self.values[at]});
        self.shift_keys_left(at);
        self.count -= 1;
        return taken;
    }

    fn insert_child_at(ref self, at: int, child: int) {
        if (self.leaf) {
            return;
        }
        if (at > self.count) {
            self.children.push_back(child);
            return;
        }
        self.shift_children_right(at);
        self.children[at] = child;
    }

    fn remove_child_at(ref self, at: int) -> int {
        if (self.leaf) {
            return BTREE_NIL;
        }
        let taken = self.children[at];
        self.shift_children_left(at);
        return taken;
    }

    fn push_entry(ref self, key: K, value: V) {
        self.keys.push_back(key);
        self.values.push_back(value);
        self.count += 1;
    }

    fn push_pair(ref self, entry: pair(K, V)) {
        self.keys.push_back(entry.first);
        self.values.push_back(entry.second);
        self.count += 1;
    }

    fn pop_entry(ref self) -> pair(K, V) {
        let taken = pair(K, V)({.first = self.keys[self.count - 1], .second = self.values[self.count - 1]});
        self.keys.pop_back();
        self.values.pop_back();
        self.count -= 1;
        return taken;
    }

    fn reset_slots(ref self) {
        self.keys.clear();
        self.values.clear();
        self.children.clear();
        self.count = 0;
        self.leaf = true;
        self.parent = BTREE_NIL;
        self.depth = 0;
    }

    var keys: vector(K);
    var values: vector(V);
    var children: vector(int);
    var count: int = 0;
    var leaf: bool = true;
    var parent: int = -1;
    var depth: int = 0;
    var live: bool = true;
}

// --- Statistics -----------------------------------------------------------

// Everything the shape pass measures.  It is generic over K and V purely so
// that it can carry a handful of sampled entries alongside the numbers, which
// makes the struct useful for debugging as well as for testing.
class BTreeStats(K: type, V: type)
{

    fn average_occupancy(self) -> int {
        if (self.node_count == 0) {
            return 0;
        }
        return self.key_count * 100 / self.node_count;
    }

    fn leaf_fraction_percent(self) -> int {
        if (self.node_count == 0) {
            return 0;
        }
        return self.leaf_count * 100 / self.node_count;
    }

    fn balanced(self) -> bool {
        return self.min_leaf_depth == self.max_leaf_depth;
    }

    fn record_sample(ref self, entry: pair(K, V)) {
        if (self.samples.size() < BTREE_SAMPLE_LIMIT) {
            self.samples.push_back(entry);
        }
    }

    // One `(depth, node count)` pair per level, in increasing depth order.
    fn level_widths(self) -> vector(pair(int, int)) {
        var out = vector(pair(int, int))();
        for (row in self.per_depth) {
            out.push_back(pair(int, int)({.first = row.first, .second = row.second}));
        }
        return out;
    }

    fn describe(self) -> string {
        var text = string();
        text.append("height=");
        text.append(to_string(self.height));
        text.append(" nodes=");
        text.append(to_string(self.node_count));
        text.append(" keys=");
        text.append(to_string(self.key_count));
        text.append(" fill=");
        text.append(to_string(self.average_occupancy()));
        return text;
    }

    var height: int = 0;
    var node_count: int = 0;
    var leaf_count: int = 0;
    var key_count: int = 0;
    var min_occupancy: int = 0;
    var max_occupancy: int = 0;
    var min_leaf_depth: int = 0;
    var max_leaf_depth: int = 0;
    var splits_seen: int = 0;
    var merges_seen: int = 0;
    var per_depth: vector(pair(int, int));
    var occupancy_histogram: vector(pair(int, int));
    var samples: vector(pair(K, V));
}

// --- Cursor ---------------------------------------------------------------

// A position inside a tree: the node, the slot within that node, and the full
// root-to-node path as `(node, child index)` pairs.  Predecessor and successor
// are both implemented on top of this.
class BTreeCursor(K: type, V: type, ORDER: int)
{

    fn valid(self) -> bool {
        return self.node != BTREE_NIL and self.slot >= 0;
    }

    fn depth(self) -> int {
        return self.path.size();
    }

    fn push_step(ref self, node: int, child: int) {
        self.path.push_back(pair(int, int)({.first = node, .second = child}));
    }

    fn pop_step(ref self) -> pair(int, int) {
        if (self.path.size() == 0) {
            return pair(int, int)({.first = BTREE_NIL, .second = -1});
        }
        let top = self.path.back();
        self.path.pop_back();
        return pair(int, int)({.first = top.first, .second = top.second});
    }

    fn parent_step(self) -> pair(int, int) {
        if (self.path.size() == 0) {
            return pair(int, int)({.first = BTREE_NIL, .second = -1});
        }
        return self.path[self.path.size() - 1];
    }

    fn invalidate(ref self) {
        self.node = BTREE_NIL;
        self.slot = -1;
        self.path.clear();
    }

    var node: int = -1;
    var slot: int = -1;
    var order: ScanOrder = ScanOrder.Ascending;
    var path: vector(pair(int, int));
    var pending: vector(pair(K, V));
}

// --- Tree -----------------------------------------------------------------

class BTree(K: type, V: type, ORDER: int)
{

    // --- Size and shape queries -------------------------------------------

    fn size(self) -> int {
        return self.count;
    }

    fn empty(self) -> bool {
        return self.count == 0;
    }

    fn order(self) -> int {
        return ORDER;
    }

    fn max_keys(self) -> int {
        return ORDER - 1;
    }

    fn min_keys(self) -> int {
        return ORDER / 2 - 1;
    }

    fn arena_size(self) -> int {
        return self.nodes.size();
    }

    fn is_nil(self, i: int) -> bool {
        return i == BTREE_NIL;
    }

    fn alive(self, i: int) -> bool {
        if (i == BTREE_NIL) {
            return false;
        }
        return self.nodes[i].live;
    }

    fn node_at(ref self, i: int) -> ref BTreeNode(K, V, ORDER) {
        return self.nodes[i];
    }

    fn root_node(ref self) -> ref BTreeNode(K, V, ORDER) {
        return self.nodes[self.root];
    }

    // --- Arena management --------------------------------------------------

    fn reserve(ref self, capacity: int) {
        self.nodes.reserve(capacity);
    }

    fn grow_if_needed(ref self) {
        if (self.nodes.size() >= self.nodes.capacity()) {
            let target = self.nodes.capacity() * BTREE_GROWTH_PERCENT / 100;
            if (target <= self.nodes.size()) {
                self.nodes.reserve(self.nodes.size() + BTREE_INITIAL_CAPACITY);
            } else {
                self.nodes.reserve(target);
            }
        }
    }

    // Hands back a fresh node, recycling a released slot when one exists.
    fn allocate_node(ref self, leaf: bool, depth: int) -> int {
        if (self.free_list != BTREE_NIL) {
            let recycled = self.free_list;
            self.free_list = self.nodes[recycled].parent;
            self.nodes[recycled].reset_slots();
            self.nodes[recycled].leaf = leaf;
            self.nodes[recycled].depth = depth;
            self.nodes[recycled].live = true;
            return recycled;
        }
        self.grow_if_needed();
        var fresh = BTreeNode(K, V, ORDER)({.leaf = leaf, .depth = depth});
        self.nodes.push_back(fresh);
        return self.nodes.size() - 1;
    }

    // Returns a node to the free list.  The free list threads itself through
    // the `parent` field, which is unused while a node is dead.
    fn release_node(ref self, i: int) {
        if (i == BTREE_NIL) {
            return;
        }
        self.nodes[i].reset_slots();
        self.nodes[i].live = false;
        self.nodes[i].parent = self.free_list;
        self.free_list = i;
    }

    fn set_parent(ref self, child: int, parent: int) {
        if (child != BTREE_NIL) {
            self.nodes[child].parent = parent;
        }
    }

    // Re-stamps `depth` on a whole subtree.  Called after a merge or a root
    // change, both of which move entire subtrees one level up or down.
    fn restamp_depths(ref self, start: int, depth: int) {
        if (start == BTREE_NIL) {
            return;
        }
        var stack = vector(pair(int, int))();
        stack.reserve(BTREE_MAX_HEIGHT);
        stack.push_back(pair(int, int)({.first = start, .second = depth}));
        while (stack.size() > 0) {
            let top = stack.back();
            stack.pop_back();
            self.nodes[top.first].depth = top.second;
            if (not self.nodes[top.first].leaf) {
                var c: int = 0;
                while (c <= self.nodes[top.first].count) {
                    let child = self.nodes[top.first].children[c];
                    if (child != BTREE_NIL) {
                        stack.push_back(pair(int, int)({.first = child, .second = top.second + 1}));
                    }
                    c += 1;
                }
            }
        }
        Destroy(stack);
    }

    // --- Search ------------------------------------------------------------

    // Search within a single node, delegated so callers can stay index-free.
    fn find_in_node(self, node: int, key: K) -> pair(int, bool) {
        if (node == BTREE_NIL) {
            return pair(int, bool)({.first = -1, .second = false});
        }
        return self.nodes[node].probe(key);
    }

    // Walks down the tree and returns `(node, slot)` for an exact hit, or
    // `(BTREE_NIL, -1)` when the key is absent.
    fn locate(self, key: K) -> pair(int, int) {
        var current = self.root;
        while (current != BTREE_NIL) {
            let hit = self.nodes[current].probe(key);
            if (hit.second) {
                return pair(int, int)({.first = current, .second = hit.first});
            }
            if (self.nodes[current].leaf) {
                return pair(int, int)({.first = BTREE_NIL, .second = -1});
            }
            current = self.nodes[current].children[hit.first];
        }
        return pair(int, int)({.first = BTREE_NIL, .second = -1});
    }

    // Same walk as `locate`, but recording every step so the caller can climb
    // back up afterwards.  This is what feeds `BTreeCursor`.
    fn locate_with_path(self, key: K) -> BTreeCursor(K, V, ORDER) {
        var cursor = BTreeCursor(K, V, ORDER)();
        var current = self.root;
        while (current != BTREE_NIL) {
            let hit = self.nodes[current].probe(key);
            if (hit.second) {
                cursor.node = current;
                cursor.slot = hit.first;
                return cursor;
            }
            if (self.nodes[current].leaf) {
                cursor.node = BTREE_NIL;
                cursor.slot = -1;
                return cursor;
            }
            cursor.push_step(current, hit.first);
            current = self.nodes[current].children[hit.first];
        }
        cursor.invalidate();
        return cursor;
    }

    fn contains(self, key: K) -> bool {
        let found = self.locate(key);
        return found.first != BTREE_NIL;
    }

    fn find(ref self, key: K) -> ref V {
        let found = self.locate(key);
        if (found.first == BTREE_NIL) {
            return self.missing;
        }
        return self.nodes[found.first].values[found.second];
    }

    // Depth at which `key` lives, or -1 when it is absent.  Cheap proxy for
    // "how many comparisons did that lookup cost".
    fn depth_of(self, key: K) -> int {
        var current = self.root;
        var depth: int = 0;
        while (current != BTREE_NIL) {
            let hit = self.nodes[current].probe(key);
            if (hit.second) {
                return depth;
            }
            if (self.nodes[current].leaf) {
                return -1;
            }
            current = self.nodes[current].children[hit.first];
            depth += 1;
        }
        return -1;
    }

    // --- Minimum and maximum ------------------------------------------------

    fn minimum_in(self, start: int) -> pair(int, int) {
        var current = start;
        while (current != BTREE_NIL) {
            if (self.nodes[current].leaf) {
                if (self.nodes[current].count == 0) {
                    return pair(int, int)({.first = BTREE_NIL, .second = -1});
                }
                return pair(int, int)({.first = current, .second = 0});
            }
            current = self.nodes[current].children[0];
        }
        return pair(int, int)({.first = BTREE_NIL, .second = -1});
    }

    fn maximum_in(self, start: int) -> pair(int, int) {
        var current = start;
        while (current != BTREE_NIL) {
            if (self.nodes[current].leaf) {
                if (self.nodes[current].count == 0) {
                    return pair(int, int)({.first = BTREE_NIL, .second = -1});
                }
                return pair(int, int)({.first = current, .second = self.nodes[current].count - 1});
            }
            current = self.nodes[current].children[self.nodes[current].count];
        }
        return pair(int, int)({.first = BTREE_NIL, .second = -1});
    }

    fn minimum(self) -> pair(int, int) {
        return self.minimum_in(self.root);
    }

    fn maximum(self) -> pair(int, int) {
        return self.maximum_in(self.root);
    }

    fn min_key(ref self) -> ref K {
        let at = self.minimum();
        if (at.first == BTREE_NIL) {
            return self.missing_key;
        }
        return self.nodes[at.first].keys[at.second];
    }

    fn max_key(ref self) -> ref K {
        let at = self.maximum();
        if (at.first == BTREE_NIL) {
            return self.missing_key;
        }
        return self.nodes[at.first].keys[at.second];
    }

    fn min_entry(self) -> pair(K, V) {
        let at = self.minimum();
        if (at.first == BTREE_NIL) {
            return pair(K, V)({.first = self.missing_key, .second = self.missing});
        }
        return self.nodes[at.first].entry_at(at.second);
    }

    fn max_entry(self) -> pair(K, V) {
        let at = self.maximum();
        if (at.first == BTREE_NIL) {
            return pair(K, V)({.first = self.missing_key, .second = self.missing});
        }
        return self.nodes[at.first].entry_at(at.second);
    }

    // --- Insertion ----------------------------------------------------------
    //
    // Proactive splitting: on the way down, any full node is split before it
    // is entered, which guarantees the node a new key lands in always has room
    // and removes the need to propagate a split upwards.

    // Splits `.nodes[parent].children[index]`, which must be full.  The median
    // key moves up into `parent` at slot `index`, and the upper half of the
    // child becomes a brand new sibling at `index + 1`.
    fn split_child(ref self, parent: int, index: int) -> int {
        let full = self.nodes[parent].children[index];
        let median = (ORDER - 1) / 2;
        let sibling = self.allocate_node(self.nodes[full].leaf, self.nodes[full].depth);

        // Move everything above the median into the new sibling.
        var i = median + 1;
        while (i < self.nodes[full].count) {
            self.nodes[sibling].push_entry(self.nodes[full].keys[i], self.nodes[full].values[i]);
            i += 1;
        }

        // Internal nodes carry one more child than they carry keys.
        if (not self.nodes[full].leaf) {
            var c = median + 1;
            while (c <= self.nodes[full].count) {
                let moved = self.nodes[full].children[c];
                self.nodes[sibling].children.push_back(moved);
                self.set_parent(moved, sibling);
                c += 1;
            }
            var trim = self.nodes[full].count;
            while (trim > median) {
                self.nodes[full].children.pop_back();
                trim -= 1;
            }
        }

        // The median itself is promoted, so the left half keeps `median` keys.
        let promoted_key = self.nodes[full].keys[median];
        let promoted_value = self.nodes[full].values[median];
        var drop = self.nodes[full].count;
        while (drop > median) {
            self.nodes[full].keys.pop_back();
            self.nodes[full].values.pop_back();
            drop -= 1;
        }
        self.nodes[full].count = median;

        self.nodes[parent].insert_entry_at(index, promoted_key, promoted_value);
        self.nodes[parent].insert_child_at(index + 1, sibling);
        self.set_parent(sibling, parent);
        self.set_parent(full, parent);
        self.splits += 1;
        return sibling;
    }

    // Grows the tree by one level.  Only ever called when the root is full.
    fn grow_root(ref self) -> int {
        let old_root = self.root;
        let fresh = self.allocate_node(false, 0);
        self.nodes[fresh].children.push_back(old_root);
        self.set_parent(old_root, fresh);
        self.root = fresh;
        self.restamp_depths(fresh, 0);
        let sibling = self.split_child(fresh, 0);
        Discard(sibling);
        self.height_hint += 1;
        return fresh;
    }

    // Inserts into a subtree whose root is known not to be full.
    fn insert_non_full(ref self, start: int, key: K, value: V) -> bool {
        var current = start;
        while (true) {
            let hit = self.nodes[current].probe(key);
            if (hit.second) {
                // Existing key: overwrite the mapped value in place.
                self.nodes[current].values[hit.first] = value;
                return false;
            }
            if (self.nodes[current].leaf) {
                self.nodes[current].insert_entry_at(hit.first, key, value);
                self.count += 1;
                return true;
            } else {
                var slot = hit.first;
                let child = self.nodes[current].children[slot];
                if (self.nodes[child].is_full()) {
                    let sibling = self.split_child(current, slot);
                    Discard(sibling);
                    // The promoted separator may itself be the key we want.
                    if (compare_keys(self.nodes[current].keys[slot], key) == 0) {
                        self.nodes[current].values[slot] = value;
                        return false;
                    }
                    if (compare_keys(key, self.nodes[current].keys[slot]) > 0) {
                        slot += 1;
                    }
                }
                current = self.nodes[current].children[slot];
            }
        }
    }

    fn insert(ref self, key: K, value: V) -> bool {
        if (self.root == BTREE_NIL) {
            self.root = self.allocate_node(true, 0);
            self.nodes[self.root].push_entry(key, value);
            self.count += 1;
            self.height_hint = 1;
            return true;
        }
        if (self.nodes[self.root].is_full()) {
            let grown = self.grow_root();
            Discard(grown);
        }
        return self.insert_non_full(self.root, key, value);
    }

    fn insert_pair(ref self, entry: pair(K, V)) -> bool {
        return self.insert(entry.first, entry.second);
    }

    // Inserts every element of a `vector{pair{K, V}}` and reports how many of
    // them were genuinely new keys.
    fn insert_all(ref self, entries: vector(pair(K, V))) -> int {
        var added: int = 0;
        for (entry in entries) {
            if (self.insert(entry.first, entry.second)) {
                added += 1;
            }
        }
        return added;
    }

    // Insert-or-append semantics for a multimap-flavoured tree whose values
    // are themselves vectors.
    fn append_value(ref self, key: K, value: V) -> bool {
        let found = self.locate(key);
        if (found.first == BTREE_NIL) {
            return self.insert(key, value);
        }
        self.nodes[found.first].values[found.second] = value;
        return false;
    }

    // --- Deletion -----------------------------------------------------------
    //
    // Deletion is the mirror image of insertion: on the way down, any child
    // that is already minimal is refilled *before* it is entered, so the node
    // a key is finally removed from always has a key to spare.

    // Moves the parent separator at `index - 1` down into child `index` and
    // pulls the left sibling's last key up to replace it.
    fn borrow_from_left(ref self, parent: int, index: int) {
        let child = self.nodes[parent].children[index];
        let left = self.nodes[parent].children[index - 1];

        // Separator comes down to the front of the child.
        self.nodes[child].insert_entry_at(0, self.nodes[parent].keys[index - 1],
                                      self.nodes[parent].values[index - 1]);

        // The left sibling's last key goes up into the separator slot.
        let lifted = self.nodes[left].pop_entry();
        self.nodes[parent].keys[index - 1] = lifted.first;
        self.nodes[parent].values[index - 1] = lifted.second;

        // Internal nodes also hand over the corresponding child pointer.
        if (not self.nodes[child].leaf) {
            let moved = self.nodes[left].children[self.nodes[left].count + 1];
            self.nodes[left].children.pop_back();
            self.nodes[child].insert_child_at(0, moved);
            self.set_parent(moved, child);
            self.restamp_depths(moved, self.nodes[child].depth + 1);
        }
        self.borrows += 1;
    }

    // The symmetric operation on the right-hand sibling.
    fn borrow_from_right(ref self, parent: int, index: int) {
        let child = self.nodes[parent].children[index];
        let right = self.nodes[parent].children[index + 1];

        // Separator comes down to the back of the child.
        self.nodes[child].push_entry(self.nodes[parent].keys[index], self.nodes[parent].values[index]);

        // The right sibling's first key goes up into the separator slot.
        let lifted = self.nodes[right].remove_entry_at(0);
        self.nodes[parent].keys[index] = lifted.first;
        self.nodes[parent].values[index] = lifted.second;

        if (not self.nodes[child].leaf) {
            let moved = self.nodes[right].remove_child_at(0);
            self.nodes[child].children.push_back(moved);
            self.set_parent(moved, child);
            self.restamp_depths(moved, self.nodes[child].depth + 1);
        }
        self.borrows += 1;
    }

    // Folds child `index + 1` into child `index`, pulling the separator at
    // `index` down between them.  The right-hand child is released.
    fn merge_children(ref self, parent: int, index: int) -> int {
        let left = self.nodes[parent].children[index];
        let right = self.nodes[parent].children[index + 1];

        // The separator becomes the first of the merged keys.
        let separator = self.nodes[parent].remove_entry_at(index);
        self.nodes[left].push_entry(separator.first, separator.second);

        // Then everything the right sibling was holding.
        var i: int = 0;
        while (i < self.nodes[right].count) {
            self.nodes[left].push_entry(self.nodes[right].keys[i], self.nodes[right].values[i]);
            i += 1;
        }

        if (not self.nodes[right].leaf) {
            var c: int = 0;
            while (c <= self.nodes[right].count) {
                let moved = self.nodes[right].children[c];
                if (moved != BTREE_NIL) {
                    self.nodes[left].children.push_back(moved);
                    self.set_parent(moved, left);
                    self.restamp_depths(moved, self.nodes[left].depth + 1);
                }
                c += 1;
            }
        }

        // Drop the now-empty right child from the parent's child array.
        let dropped = self.nodes[parent].remove_child_at(index + 1);
        Discard(dropped);
        self.release_node(right);
        self.merges += 1;

        // Merging away the last separator of the root shrinks the tree.
        if (parent == self.root and self.nodes[parent].count == 0) {
            self.release_node(self.root);
            self.root = left;
            self.set_parent(left, BTREE_NIL);
            self.restamp_depths(left, 0);
            if (self.height_hint > 0) {
                self.height_hint -= 1;
            }
        }
        return left;
    }

    // Guarantees child `index` has more than the minimum number of keys, by
    // borrowing from whichever sibling can spare one and merging otherwise.
    // Returns both the action taken and the index the child now lives at.
    fn fill_child(ref self, parent: int, index: int) -> pair(RebalanceAction, int) {
        let child = self.nodes[parent].children[index];
        if (self.nodes[child].has_spare_key()) {
            return pair(RebalanceAction, int)({.first = RebalanceAction.NoActionNeeded,
                                              .second = index});
        }

        if (index > 0) {
            let left = self.nodes[parent].children[index - 1];
            if (self.nodes[left].has_spare_key()) {
                self.borrow_from_left(parent, index);
                return pair(RebalanceAction, int)({.first = RebalanceAction.BorrowedFromLeft,
                                                  .second = index});
            } else {
                if (index < self.nodes[parent].count) {
                    let right = self.nodes[parent].children[index + 1];
                    if (self.nodes[right].has_spare_key()) {
                        self.borrow_from_right(parent, index);
                        return pair(RebalanceAction, int)({
                            .first = RebalanceAction.BorrowedFromRight, .second = index});
                    } else {
                        // Neither sibling can spare a key, so merge with the
                        // left one; the child slides one slot to the left.
                        let merged = self.merge_children(parent, index - 1);
                        Discard(merged);
                        return pair(RebalanceAction, int)({
                            .first = RebalanceAction.MergedWithLeft, .second = index - 1});
                    }
                } else {
                    let merged = self.merge_children(parent, index - 1);
                    Discard(merged);
                    return pair(RebalanceAction, int)({
                        .first = RebalanceAction.MergedWithLeft, .second = index - 1});
                }
            }
        } else {
            // Leftmost child: the right sibling is the only candidate.
            if (index < self.nodes[parent].count) {
                let right = self.nodes[parent].children[index + 1];
                if (self.nodes[right].has_spare_key()) {
                    self.borrow_from_right(parent, index);
                    return pair(RebalanceAction, int)({
                        .first = RebalanceAction.BorrowedFromRight, .second = index});
                } else {
                    let merged = self.merge_children(parent, index);
                    Discard(merged);
                    return pair(RebalanceAction, int)({
                        .first = RebalanceAction.MergedWithRight, .second = index});
                }
            } else {
                return pair(RebalanceAction, int)({.first = RebalanceAction.NoActionNeeded,
                                                  .second = index});
            }
        }
    }

    // Removes slot `slot` of a leaf.  The caller has already ensured the leaf
    // is either the root or holds more than the minimum number of keys.
    fn remove_from_leaf(ref self, node: int, slot: int) -> pair(K, V) {
        let taken = self.nodes[node].remove_entry_at(slot);
        self.count -= 1;
        return taken;
    }

    // Removes slot `slot` of an internal node by replacing it with either its
    // in-order predecessor or its in-order successor, whichever comes from the
    // side that can afford to lose a key, then deleting that key recursively.
    fn remove_from_branch(ref self, node: int, slot: int) -> bool {
        let left = self.nodes[node].children[slot];
        let right = self.nodes[node].children[slot + 1];

        if (self.nodes[left].has_spare_key()) {
            let at = self.maximum_in(left);
            let replacement = self.nodes[at.first].entry_at(at.second);
            self.nodes[node].keys[slot] = replacement.first;
            self.nodes[node].values[slot] = replacement.second;
            return self.erase_from(left, replacement.first);
        } else {
            if (self.nodes[right].has_spare_key()) {
                let at = self.minimum_in(right);
                let replacement = self.nodes[at.first].entry_at(at.second);
                self.nodes[node].keys[slot] = replacement.first;
                self.nodes[node].values[slot] = replacement.second;
                return self.erase_from(right, replacement.first);
            } else {
                // Both children are minimal: fold them together and delete the
                // key from the merged node instead.
                let doomed_key = self.nodes[node].keys[slot];
                let merged = self.merge_children(node, slot);
                return self.erase_from(merged, doomed_key);
            }
        }
    }

    // Deletes `key` from the subtree rooted at `start`, refilling children on
    // the way down so no node ever underflows behind us.
    fn erase_from(ref self, start: int, key: K) -> bool {
        var current = start;
        while (true) {
            if (current == BTREE_NIL) {
                return false;
            }
            let hit = self.nodes[current].probe(key);
            if (hit.second) {
                if (self.nodes[current].leaf) {
                    let taken = self.remove_from_leaf(current, hit.first);
                    Discard(taken);
                    return true;
                } else {
                    return self.remove_from_branch(current, hit.first);
                }
            }
            if (self.nodes[current].leaf) {
                return false;
            }
            var slot = hit.first;
            let child = self.nodes[current].children[slot];
            if (self.nodes[child].is_minimal()) {
                let action = self.fill_child(current, slot);
                slot = action.second;
                if (action.first == RebalanceAction.MergedWithLeft) {
                    // The merge may have collapsed the root beneath us.
                    if (self.nodes[current].count == 0 and current == self.root) {
                        current = self.root;
                        slot = 0;
                    }
                }
                if (slot > self.nodes[current].count) {
                    slot = self.nodes[current].count;
                }
            }
            current = self.nodes[current].children[slot];
        }
    }

    fn erase(ref self, key: K) -> bool {
        if (self.root == BTREE_NIL) {
            return false;
        }
        let removed = self.erase_from(self.root, key);
        if (self.root != BTREE_NIL) {
            if (self.nodes[self.root].count == 0 and self.nodes[self.root].leaf) {
                self.release_node(self.root);
                self.root = BTREE_NIL;
                self.height_hint = 0;
            }
        }
        return removed;
    }

    fn erase_min(ref self) -> pair(K, V) {
        let at = self.minimum();
        if (at.first == BTREE_NIL) {
            return pair(K, V)({.first = self.missing_key, .second = self.missing});
        }
        let entry = self.nodes[at.first].entry_at(at.second);
        Discard(self.erase(entry.first));
        return entry;
    }

    fn erase_max(ref self) -> pair(K, V) {
        let at = self.maximum();
        if (at.first == BTREE_NIL) {
            return pair(K, V)({.first = self.missing_key, .second = self.missing});
        }
        let entry = self.nodes[at.first].entry_at(at.second);
        Discard(self.erase(entry.first));
        return entry;
    }

    // --- Predecessor and successor ------------------------------------------
    //
    // Both walk down to the key first, recording the path, and then either
    // dive into the neighbouring subtree or climb back up until the step they
    // came from was not the extreme child.

    fn predecessor(self, key: K) -> pair(K, V) {
        var cursor = self.locate_with_path(key);
        if (not cursor.valid()) {
            // The key is absent: fall back to the largest key below it.
            return self.largest_below(key);
        }
        if (not self.nodes[cursor.node].leaf) {
            let at = self.maximum_in(self.nodes[cursor.node].children[cursor.slot]);
            if (at.first != BTREE_NIL) {
                return self.nodes[at.first].entry_at(at.second);
            }
        }
        if (cursor.slot > 0) {
            return self.nodes[cursor.node].entry_at(cursor.slot - 1);
        }
        // Climb: the predecessor is the separator left of the child we
        // descended through, at the first level where that index was non-zero.
        while (cursor.depth() > 0) {
            let step = cursor.pop_step();
            if (step.second > 0) {
                return self.nodes[step.first].entry_at(step.second - 1);
            }
        }
        return pair(K, V)({.first = self.missing_key, .second = self.missing});
    }

    fn successor(self, key: K) -> pair(K, V) {
        var cursor = self.locate_with_path(key);
        if (not cursor.valid()) {
            return self.smallest_above(key);
        }
        if (not self.nodes[cursor.node].leaf) {
            let at = self.minimum_in(self.nodes[cursor.node].children[cursor.slot + 1]);
            if (at.first != BTREE_NIL) {
                return self.nodes[at.first].entry_at(at.second);
            }
        }
        if (cursor.slot + 1 < self.nodes[cursor.node].count) {
            return self.nodes[cursor.node].entry_at(cursor.slot + 1);
        }
        while (cursor.depth() > 0) {
            let step = cursor.pop_step();
            if (step.second < self.nodes[step.first].count) {
                return self.nodes[step.first].entry_at(step.second);
            }
        }
        return pair(K, V)({.first = self.missing_key, .second = self.missing});
    }

    // Largest key strictly smaller than `key`, for keys that are not present.
    fn largest_below(self, key: K) -> pair(K, V) {
        var current = self.root;
        var best = pair(K, V)({.first = self.missing_key, .second = self.missing});
        var found: bool = false;
        while (current != BTREE_NIL) {
            let slot = self.nodes[current].lower_bound(key);
            if (slot > 0) {
                best = self.nodes[current].entry_at(slot - 1);
                found = true;
            }
            if (self.nodes[current].leaf) {
                current = BTREE_NIL;
            } else {
                current = self.nodes[current].children[slot];
            }
        }
        if (not found) {
            return pair(K, V)({.first = self.missing_key, .second = self.missing});
        }
        return best;
    }

    // Smallest key strictly greater than `key`.
    fn smallest_above(self, key: K) -> pair(K, V) {
        var current = self.root;
        var best = pair(K, V)({.first = self.missing_key, .second = self.missing});
        var found: bool = false;
        while (current != BTREE_NIL) {
            let slot = self.nodes[current].upper_bound(key);
            if (slot < self.nodes[current].count) {
                best = self.nodes[current].entry_at(slot);
                found = true;
            }
            if (self.nodes[current].leaf) {
                current = BTREE_NIL;
            } else {
                current = self.nodes[current].children[slot];
            }
        }
        if (not found) {
            return pair(K, V)({.first = self.missing_key, .second = self.missing});
        }
        return best;
    }

    // --- Traversal ------------------------------------------------------------
    //
    // The in-order walk keeps an explicit `(node, child index)` stack instead
    // of recursing, which bounds the stack depth by BTREE_MAX_HEIGHT and makes
    // the ordering of the interleaved key/child visits obvious.

    fn in_order(self, ref out: vector(pair(K, V))) {
        if (self.root == BTREE_NIL) {
            return;
        }
        var stack = vector(pair(int, int))();
        stack.reserve(BTREE_MAX_HEIGHT);
        stack.push_back(pair(int, int)({.first = self.root, .second = 0}));
        while (stack.size() > 0) {
            let top = stack.back();
            stack.pop_back();
            let node = top.first;
            let step = top.second;
            if (self.nodes[node].leaf) {
                var i: int = 0;
                while (i < self.nodes[node].count) {
                    out.push_back(self.nodes[node].entry_at(i));
                    i += 1;
                }
            } else {
                if (step <= self.nodes[node].count) {
                    // Come back to this node once the child has been walked.
                    stack.push_back(pair(int, int)({.first = node, .second = step + 1}));
                    if (step > 0) {
                        // Emit the separator that precedes the upcoming child.
                        out.push_back(self.nodes[node].entry_at(step - 1));
                    }
                    let child = self.nodes[node].children[step];
                    if (child != BTREE_NIL) {
                        stack.push_back(pair(int, int)({.first = child, .second = 0}));
                    }
                }
            }
        }
        Destroy(stack);
    }

    fn keys_in_order(self, ref out: vector(K)) {
        var entries = vector(pair(K, V))();
        self.in_order(entries);
        for (entry in entries) {
            out.push_back(entry.first);
        }
        Destroy(entries);
    }

    fn values_in_order(self, ref out: vector(V)) {
        var entries = vector(pair(K, V))();
        self.in_order(entries);
        for (entry in entries) {
            out.push_back(entry.second);
        }
        Destroy(entries);
    }

    fn to_pairs(self) -> vector(pair(K, V)) {
        var out = vector(pair(K, V))();
        out.reserve(self.count);
        self.in_order(out);
        return out;
    }

    // Level order, returned as one `vector{int}` of node indices per level.
    fn level_order(self) -> vector(vector(int)) {
        var levels = vector(vector(int))();
        if (self.root == BTREE_NIL) {
            return levels;
        }
        var frontier = vector(int)();
        frontier.push_back(self.root);
        while (frontier.size() > 0) {
            var next = vector(int)();
            var row = vector(int)();
            for (node in frontier) {
                row.push_back(node);
                if (not self.nodes[node].leaf) {
                    var c: int = 0;
                    while (c <= self.nodes[node].count) {
                        let child = self.nodes[node].children[c];
                        if (child != BTREE_NIL) {
                            next.push_back(child);
                        }
                        c += 1;
                    }
                }
            }
            levels.push_back(row);
            frontier = next;
        }
        return levels;
    }

    // Every leaf, left to right.
    fn leaves(self) -> vector(int) {
        var out = vector(int)();
        if (self.root == BTREE_NIL) {
            return out;
        }
        var stack = vector(int)();
        stack.reserve(BTREE_MAX_HEIGHT);
        stack.push_back(self.root);
        while (stack.size() > 0) {
            let node = stack.back();
            stack.pop_back();
            if (self.nodes[node].leaf) {
                out.push_back(node);
            } else {
                var c = self.nodes[node].count;
                while (c >= 0) {
                    let child = self.nodes[node].children[c];
                    if (child != BTREE_NIL) {
                        stack.push_back(child);
                    }
                    c -= 1;
                }
            }
        }
        Destroy(stack);
        return out;
    }

    // --- Range queries --------------------------------------------------------

    // Collects every entry whose key lies in the half-open range
    // `[low, high)`.  The walk prunes any subtree that cannot intersect the
    // range, which is the whole point of doing it on the tree rather than on
    // the flattened `to_pairs()` output.
    fn range_query(self, low: K, high: K,
                   ref out: vector(pair(K, V))) -> int {
        if (self.root == BTREE_NIL) {
            return 0;
        }
        var emitted: int = 0;
        var stack = vector(int)();
        stack.reserve(BTREE_MAX_HEIGHT);
        stack.push_back(self.root);
        while (stack.size() > 0) {
            let node = stack.back();
            stack.pop_back();
            let first = self.nodes[node].lower_bound(low);
            var i = first;
            while (i < self.nodes[node].count) {
                if (compare_keys(self.nodes[node].keys[i], *high) >= 0) {
                    i = self.nodes[node].count;
                } else {
                    out.push_back(self.nodes[node].entry_at(i));
                    emitted += 1;
                    i += 1;
                }
            }
            if (not self.nodes[node].leaf) {
                let last = self.nodes[node].upper_bound(high);
                var c = first;
                while (c <= last) {
                    if (c <= self.nodes[node].count) {
                        let child = self.nodes[node].children[c];
                        if (child != BTREE_NIL) {
                            stack.push_back(child);
                        }
                    }
                    c += 1;
                }
            }
        }
        Destroy(stack);
        return emitted;
    }

    // How many keys fall in `[low, high)`, without materialising them.
    fn count_range(self, low: K, high: K) -> int {
        var scratch = vector(pair(K, V))();
        let total = self.range_query(low, high, scratch);
        Destroy(scratch);
        return total;
    }

    // Range query that buckets its results by the leaf they came from, giving
    // one `vector{pair{K, V}}` per leaf index.
    fn range_by_leaf(self, low: K,
                     high: K) -> map(int, vector(pair(K, V))) {
        var buckets = map(int, vector(pair(K, V)))();
        let leaf_list = self.leaves();
        for (leaf in leaf_list) {
            var row = vector(pair(K, V))();
            var i: int = 0;
            while (i < self.nodes[leaf].count) {
                let entry = self.nodes[leaf].entry_at(i);
                if (compare_keys(entry.first, *low) >= 0 and
                   compare_keys(entry.first, *high) < 0) {
                    row.push_back(entry);
                }
                i += 1;
            }
            if (row.size() > 0) {
                Discard(buckets.insert(leaf, row));
            }
            Destroy(row);
        }
        return buckets;
    }

    // Every key in the range, grouped under the separator key of the subtree
    // it was found in.  Deliberately built as a
    // `map{K, vector{pair{K, vector{V}}}}` to exercise nested value types.
    fn range_grouped(self, low: K,
                     high: K) -> map(K, vector(pair(K, vector(V)))) {
        var grouped = map(K, vector(pair(K, vector(V))))();
        var hits = vector(pair(K, V))();
        let total = self.range_query(low, high, hits);
        if (total == 0) {
            Destroy(hits);
            return grouped;
        }
        var current_group = vector(pair(K, vector(V)))();
        var group_key = hits[0].first;
        var index: int = 0;
        while (index < hits.size()) {
            let entry = hits[index];
            var bag = vector(V)();
            bag.push_back(entry.second);
            current_group.push_back(pair(K, vector(V))({.first = entry.first, .second = bag}));
            if (current_group.size() >= ORDER - 1) {
                Discard(grouped.insert(group_key, current_group));
                current_group.clear();
                if (index + 1 < hits.size()) {
                    group_key = hits[index + 1].first;
                }
            }
            Destroy(bag);
            index += 1;
        }
        if (current_group.size() > 0) {
            Discard(grouped.insert(group_key, current_group));
        }
        Destroy(current_group);
        Destroy(hits);
        return grouped;
    }

    // Classic lower/upper bound over the whole tree rather than one node.
    fn lower_bound(self, key: K) -> pair(K, V) {
        let found = self.locate(key);
        if (found.first != BTREE_NIL) {
            return self.nodes[found.first].entry_at(found.second);
        }
        return self.smallest_above(key);
    }

    fn upper_bound(self, key: K) -> pair(K, V) {
        return self.smallest_above(key);
    }

    // --- Bulk loading ---------------------------------------------------------
    //
    // Building bottom-up from an already sorted input is much cheaper than
    // repeated insertion: each level is packed to `BTREE_BULK_FILL_PERCENT` of
    // capacity and then the separators are lifted into a freshly built parent
    // level, repeating until a single node remains.

    fn bulk_fill_width(self) -> int {
        let width = (ORDER - 1) * BTREE_BULK_FILL_PERCENT / 100;
        if (width < 1) {
            return 1;
        }
        return width;
    }

    // Packs a sorted run of entries into a row of leaves, returning the leaf
    // indices together with the separator that must sit above each gap.
    fn build_leaf_level(ref self, sorted: vector(pair(K, V)),
                        ref out_separators: vector(pair(K, V))) -> vector(int) {
        var level = vector(int)();
        let width = self.bulk_fill_width();
        var index: int = 0;
        while (index < sorted.size()) {
            let leaf = self.allocate_node(true, 0);
            var packed: int = 0;
            while (packed < width and index < sorted.size()) {
                self.nodes[leaf].push_pair(sorted[index]);
                self.count += 1;
                packed += 1;
                index += 1;
            }
            level.push_back(leaf);
            // The next entry becomes the separator between this leaf and the
            // one that follows it, unless the input is exhausted.
            if (index < sorted.size()) {
                out_separators.push_back(sorted[index]);
                index += 1;
            }
        }
        return level;
    }

    // Lifts one finished level into the level above it.
    fn build_branch_level(ref self, children: vector(int),
                          separators: vector(pair(K, V)),
                          ref out_separators: vector(pair(K, V))) -> vector(int) {
        var level = vector(int)();
        let width = self.bulk_fill_width();
        var index: int = 0;
        var sep_index: int = 0;
        while (index < children.size()) {
            let branch = self.allocate_node(false, 0);
            self.nodes[branch].children.push_back(children[index]);
            self.set_parent(children[index], branch);
            index += 1;
            var packed: int = 0;
            while (packed < width and index < children.size()) {
                if (sep_index < separators.size()) {
                    self.nodes[branch].push_pair(separators[sep_index]);
                    sep_index += 1;
                }
                self.nodes[branch].children.push_back(children[index]);
                self.set_parent(children[index], branch);
                packed += 1;
                index += 1;
            }
            level.push_back(branch);
            // One separator is consumed by the gap between two branch nodes.
            if (index < children.size() and sep_index < separators.size()) {
                out_separators.push_back(separators[sep_index]);
                sep_index += 1;
            }
        }
        return level;
    }

    // Replaces the whole tree with one built from `sorted`, which must already
    // be in increasing key order.
    fn bulk_load(ref self, sorted: vector(pair(K, V))) -> bool {
        self.clear();
        if (sorted.size() == 0) {
            return true;
        }
        var separators = vector(pair(K, V))();
        var level = self.build_leaf_level(sorted, separators);
        var depth: int = 1;
        while (level.size() > 1) {
            var next_separators = vector(pair(K, V))();
            let parents = self.build_branch_level(level, separators, next_separators);
            separators = next_separators;
            level = parents;
            depth += 1;
            if (depth > BTREE_MAX_HEIGHT) {
                // Defensive stop; a correctly shrinking level list can never
                // reach this, but bailing out beats looping forever.
                panic("bulk load exceeded the maximum supported height");
            }
            Destroy(next_separators);
        }
        self.root = level[0];
        self.set_parent(self.root, BTREE_NIL);
        self.restamp_depths(self.root, 0);
        self.height_hint = depth;
        Destroy(separators);
        Destroy(level);
        return true;
    }

    // Convenience wrapper: sort-free rebuild from the tree's own contents,
    // used after a long run of deletions has left the tree sparse.
    fn compact(ref self) -> bool {
        var entries = self.to_pairs();
        let ok = self.bulk_load(entries);
        Destroy(entries);
        return ok;
    }

    // --- Height and occupancy statistics -------------------------------------

    fn height(self) -> int {
        if (self.root == BTREE_NIL) {
            return 0;
        }
        var current = self.root;
        var levels: int = 0;
        while (current != BTREE_NIL) {
            levels += 1;
            if (self.nodes[current].leaf) {
                current = BTREE_NIL;
            } else {
                current = self.nodes[current].children[0];
            }
        }
        return levels;
    }

    // Depth of every leaf, which rule 4 says must all be equal.
    fn leaf_depths(self) -> vector(pair(int, int)) {
        var out = vector(pair(int, int))();
        if (self.root == BTREE_NIL) {
            return out;
        }
        var stack = vector(pair(int, int))();
        stack.reserve(BTREE_MAX_HEIGHT);
        stack.push_back(pair(int, int)({.first = self.root, .second = 0}));
        while (stack.size() > 0) {
            let top = stack.back();
            stack.pop_back();
            if (self.nodes[top.first].leaf) {
                out.push_back(pair(int, int)({.first = top.first, .second = top.second}));
            } else {
                var c: int = 0;
                while (c <= self.nodes[top.first].count) {
                    let child = self.nodes[top.first].children[c];
                    if (child != BTREE_NIL) {
                        stack.push_back(pair(int, int)({.first = child, .second = top.second + 1}));
                    }
                    c += 1;
                }
            }
        }
        Destroy(stack);
        return out;
    }

    // One `(occupancy, node count)` bucket per distinct key count seen.
    fn occupancy_histogram(self) -> vector(pair(int, int)) {
        var buckets = vector(pair(int, int))();
        var slot: int = 0;
        while (slot < ORDER) {
            buckets.push_back(pair(int, int)({.first = slot, .second = 0}));
            slot += 1;
        }
        let levels = self.level_order();
        for (row in levels) {
            for (node in row) {
                let filled = self.nodes[node].count;
                if (filled >= 0 and filled < buckets.size()) {
                    buckets[filled].second += 1;
                }
            }
        }
        return buckets;
    }

    // Single pass collecting everything `BTreeStats` reports.
    fn statistics(self) -> BTreeStats(K, V) {
        var stats = BTreeStats(K, V)();
        stats.splits_seen = self.splits;
        stats.merges_seen = self.merges;
        if (self.root == BTREE_NIL) {
            return stats;
        }

        let levels = self.level_order();
        stats.height = levels.size();
        var depth: int = 0;
        var lowest = ORDER;
        var highest: int = 0;
        while (depth < levels.size()) {
            let row = levels[depth];
            var row_keys: int = 0;
            for (node in row) {
                stats.node_count += 1;
                row_keys += self.nodes[node].count;
                stats.key_count += self.nodes[node].count;
                if (self.nodes[node].leaf) {
                    stats.leaf_count += 1;
                }
                if (self.nodes[node].count < lowest) {
                    lowest = self.nodes[node].count;
                }
                if (self.nodes[node].count > highest) {
                    highest = self.nodes[node].count;
                }
            }
            stats.per_depth.push_back(pair(int, int)({.first = depth, .second = row.size()}));
            depth += 1;
        }
        stats.min_occupancy = lowest;
        stats.max_occupancy = highest;

        let depths = self.leaf_depths();
        var shallowest = BTREE_MAX_HEIGHT;
        var deepest: int = 0;
        for (row in depths) {
            if (row.second < shallowest) {
                shallowest = row.second;
            }
            if (row.second > deepest) {
                deepest = row.second;
            }
        }
        stats.min_leaf_depth = shallowest;
        stats.max_leaf_depth = deepest;
        stats.occupancy_histogram = self.occupancy_histogram();

        // A handful of entries for eyeballing in a debugger.
        var entries = vector(pair(K, V))();
        self.in_order(entries);
        var taken: int = 0;
        while (taken < entries.size() and taken < BTREE_SAMPLE_LIMIT) {
            stats.record_sample(entries[taken]);
            taken += 1;
        }
        Destroy(entries);
        return stats;
    }

    // --- Validity -------------------------------------------------------------
    //
    // Every B-tree rule from the file header is restated here as an explicit
    // proposition so the verifier can reason about it directly.

    fn check_node_ordering(self, node: int) -> bool {
        var i: int = 1;
        while (i < self.nodes[node].count) {
            // Rule 1: keys inside a node strictly increase.
            Assert(compare_keys(self.nodes[node].keys[i - 1], self.nodes[node].keys[i]) < 0);
            Prove(compare_keys(self.nodes[node].keys[i - 1], self.nodes[node].keys[i]) < 0);
            i += 1;
        }
        return true;
    }

    fn check_node_arity(self, node: int) -> bool {
        // Rule 2: a node has either no children at all or exactly one more
        // child than it has keys.
        if (self.nodes[node].leaf) {
            Assert(self.nodes[node].children.size() == 0);
            Prove(self.nodes[node].child_count() == 0);
        } else {
            Assert(self.nodes[node].children.size() == self.nodes[node].count + 1);
            Prove(self.nodes[node].child_count() == self.nodes[node].key_count() + 1);
        }
        return true;
    }

    fn check_occupancy(self, node: int) -> bool {
        // Rule 3: occupancy bounds, relaxed for the root.
        Assert(self.nodes[node].count <= ORDER - 1);
        Prove(self.nodes[node].count <= self.nodes[node].max_keys());
        if (node == self.root) {
            Assert(self.nodes[node].count >= 1 or self.count == 0);
        } else {
            Assert(self.nodes[node].count >= ORDER / 2 - 1);
            Prove(self.nodes[node].count >= self.nodes[node].min_keys());
        }
        return true;
    }

    fn check_separator_bounds(self, node: int) -> bool {
        if (self.nodes[node].leaf) {
            return true;
        }
        var slot: int = 0;
        while (slot <= self.nodes[node].count) {
            let child = self.nodes[node].children[slot];
            Assert(child != BTREE_NIL);
            Assert(self.nodes[child].parent == node);
            Assert(self.nodes[child].depth == self.nodes[node].depth + 1);

            // Rule 5: the subtree below child `slot` sits strictly between the
            // separators on either side of it.
            if (slot > 0) {
                let lower = self.nodes[node].keys[slot - 1];
                let least = self.minimum_in(child);
                if (least.first != BTREE_NIL) {
                    Assert(compare_keys(lower, self.nodes[least.first].keys[least.second]) < 0);
                    Prove(compare_keys(lower, self.nodes[least.first].keys[least.second]) < 0);
                }
            }
            if (slot < self.nodes[node].count) {
                let upper = self.nodes[node].keys[slot];
                let greatest = self.maximum_in(child);
                if (greatest.first != BTREE_NIL) {
                    Assert(compare_keys(self.nodes[greatest.first].keys[greatest.second],
                                        upper) < 0);
                    Prove(compare_keys(self.nodes[greatest.first].keys[greatest.second],
                                       upper) < 0);
                }
            }
            slot += 1;
        }
        return true;
    }

    fn check_validity(self) -> bool {
        Assert(self.count >= 0);
        Assert(self.count <= self.nodes.size() * (ORDER - 1));
        Assert(ORDER >= 3);

        if (self.root == BTREE_NIL) {
            // An empty tree trivially satisfies every rule.
            Assert(self.count == 0);
            Prove(self.count == 0);
            return true;
        }

        Assert(self.nodes[self.root].live);
        Assert(self.nodes[self.root].parent == BTREE_NIL);
        Assert(self.nodes[self.root].depth == 0);
        Prove(self.nodes[self.root].parent == BTREE_NIL);

        let levels = self.level_order();
        var depth: int = 0;
        while (depth < levels.size()) {
            let row = levels[depth];
            for (node in row) {
                Assert(self.nodes[node].live);
                Assert(self.nodes[node].depth == depth);
                Discard(self.check_node_ordering(node));
                Discard(self.check_node_arity(node));
                Discard(self.check_occupancy(node));
                Discard(self.check_separator_bounds(node));
            }
            depth += 1;
        }

        // Rule 4: every leaf sits at the same depth.
        let depths = self.leaf_depths();
        Assert(depths.size() > 0);
        var index: int = 1;
        while (index < depths.size()) {
            Assert(depths[index].second == depths[0].second);
            Prove(depths[index].second == depths[0].second);
            index += 1;
        }
        Prove(depths.size() > 0);

        // The in-order walk must be strictly increasing and must visit every
        // live key exactly once.
        var entries = vector(pair(K, V))();
        self.in_order(entries);
        Assert(entries.size() == self.count);
        Prove(entries.size() == self.count);
        var walk: int = 1;
        while (walk < entries.size()) {
            Assert(compare_keys(entries[walk - 1].first, entries[walk].first) < 0);
            Prove(compare_keys(entries[walk - 1].first, entries[walk].first) < 0);
            walk += 1;
        }
        Destroy(entries);

        // Height bound: a tree of `count` keys and branching factor ORDER can
        // be no taller than log base (ORDER / 2) of the key count.
        let measured = self.height();
        Assert(measured > 0);
        Assert(measured <= BTREE_MAX_HEIGHT);
        Prove(measured <= BTREE_MAX_HEIGHT);
        return true;
    }

    // --- Wholesale operations -------------------------------------------------

    fn clear(ref self) {
        for (node in self.nodes) {
            node.reset_slots();
            node.live = false;
        }
        self.nodes.clear();
        self.root = BTREE_NIL;
        self.free_list = BTREE_NIL;
        self.count = 0;
        self.height_hint = 0;
        self.splits = 0;
        self.merges = 0;
        self.borrows = 0;
    }

    fn copy_into(self, ref target: BTree(K, V, ORDER)) -> int {
        var entries = self.to_pairs();
        let added = target.insert_all(entries);
        Destroy(entries);
        return added;
    }

    var nodes: vector(BTreeNode(K, V, ORDER));
    var overflow: map(K, vector(pair(K, V)));
    var pending: vector(pair(K, vector(V)));
    var root: int = -1;
    var free_list: int = -1;
    var count: int = 0;
    var height_hint: int = 0;
    var splits: int = 0;
    var merges: int = 0;
    var borrows: int = 0;
    var missing: V;
    var missing_key: K;
}

// --- Free functions ---------------------------------------------------------

// Three-way comparison used by every ordered operation above.  Free rather
// than a method so it can be specialised per key type without touching the
// container.
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

// True when `entries` is sorted strictly increasing, which is the precondition
// `bulk_load` relies on.
fn is_sorted_by_key[K: type, V: type]
    (entries: vector(pair(K, V))) -> bool {
    var i: int = 1;
    while (i < entries.size()) {
        if (compare_keys(entries[i - 1].first, entries[i].first) >= 0) {
            return false;
        }
        i += 1;
    }
    return true;
}

// Merge of two sorted `vector{pair{K, V}}` runs, dropping duplicates in favour
// of the left-hand side.  Used to prepare input for `bulk_load`.
fn merge_sorted[K: type, V: type]
    (left: vector(pair(K, V)),
                right: vector(pair(K, V))) -> vector(pair(K, V)) {
    var out = vector(pair(K, V))();
    out.reserve(left.size() + right.size());
    var i: int = 0;
    var j: int = 0;
    while (i < left.size() and j < right.size()) {
        let order = compare_keys(left[i].first, right[j].first);
        if (order < 0) {
            out.push_back(left[i]);
            i += 1;
        } else {
            if (order > 0) {
                out.push_back(right[j]);
                j += 1;
            } else {
                out.push_back(left[i]);
                i += 1;
                j += 1;
            }
        }
    }
    while (i < left.size()) {
        out.push_back(left[i]);
        i += 1;
    }
    while (j < right.size()) {
        out.push_back(right[j]);
        j += 1;
    }
    return out;
}

// Copies every entry of `source` into `target`, preserving order.
fn merge_into[K: type, V: type, ORDER: int]
    (ref target: BTree(K, V, ORDER),
              source: BTree(K, V, ORDER)) -> int {
    var entries = source.to_pairs();
    let added = target.insert_all(entries);
    Destroy(entries);
    return added;
}

// Builds a fresh tree holding the union of two trees.  The result is bulk
// loaded from the merged run rather than inserted key by key.
fn union_of[K: type, V: type, ORDER: int]
    (left: BTree(K, V, ORDER),
            right: BTree(K, V, ORDER)) -> BTree(K, V, ORDER) {
    var out = BTree(K, V, ORDER)();
    var a = left.to_pairs();
    var b = right.to_pairs();
    var merged = merge_sorted(K, V)(a, b);
    Discard(out.bulk_load(merged));
    Destroy(merged);
    Destroy(a);
    Destroy(b);
    return out;
}

// Keys present in both trees, paired with the left tree's value.
fn intersection_of[K: type, V: type, ORDER: int]
    (left: BTree(K, V, ORDER),
                   right: BTree(K, V, ORDER)) -> vector(pair(K, V)) {
    var out = vector(pair(K, V))();
    var entries = left.to_pairs();
    for (entry in entries) {
        if (right.contains(entry.first)) {
            out.push_back(entry);
        }
    }
    Destroy(entries);
    return out;
}

// Removes every key in the half-open range `[low, high)`, returning the number
// of entries that were actually erased.
fn erase_range[K: type, V: type, ORDER: int]
    (ref tree: BTree(K, V, ORDER), low: K, high: K) -> int {
    var doomed = vector(pair(K, V))();
    let found = tree.range_query(low, high, doomed);
    var removed: int = 0;
    if (found == 0) {
        Destroy(doomed);
        return 0;
    }
    for (entry in doomed) {
        if (tree.erase(entry.first)) {
            removed += 1;
        }
    }
    Destroy(doomed);
    return removed;
}

// Groups the whole tree into buckets of at most `width` consecutive entries,
// keyed by the first key of each bucket.  The deeply nested return type is
// what makes this useful as a stress case for the generic machinery.
fn chunk_by_width[K: type, V: type, ORDER: int]
    (tree: BTree(K, V, ORDER),
                  width: int) -> map(K, vector(pair(K, vector(V)))) {
    var chunks = map(K, vector(pair(K, vector(V))))();
    var entries = tree.to_pairs();
    if (entries.size() == 0) {
        Destroy(entries);
        return chunks;
    }
    var bucket = vector(pair(K, vector(V)))();
    var bucket_key = entries[0].first;
    var index: int = 0;
    while (index < entries.size()) {
        var values = vector(V)();
        values.push_back(entries[index].second);
        bucket.push_back(pair(K, vector(V))({.first = entries[index].first, .second = values}));
        Destroy(values);
        if (bucket.size() >= width) {
            Discard(chunks.insert(bucket_key, bucket));
            bucket.clear();
            if (index + 1 < entries.size()) {
                bucket_key = entries[index + 1].first;
            }
        }
        index += 1;
    }
    if (bucket.size() > 0) {
        Discard(chunks.insert(bucket_key, bucket));
    }
    Destroy(bucket);
    Destroy(entries);
    return chunks;
}

// Flattens the nested grouping above back into a flat run of pairs.
fn flatten_groups[K: type, V: type]
    (groups: vector(pair(K, vector(V)))) -> vector(pair(K, V)) {
    var out = vector(pair(K, V))();
    for (group in groups) {
        for (value in group.second) {
            out.push_back(pair(K, V)({.first = group.first, .second = value}));
        }
    }
    return out;
}

// Structural equality: same size and the same in-order key sequence.
fn tree_equal[K: type, V: type, ORDER: int]
    (left: BTree(K, V, ORDER),
              right: BTree(K, V, ORDER)) -> bool {
    if (left.size() != right.size()) {
        return false;
    }
    var a = left.to_pairs();
    var b = right.to_pairs();
    var index: int = 0;
    var same: bool = true;
    while (index < a.size()) {
        if (compare_keys(a[index].first, b[index].first) != 0) {
            same = false;
            index = a.size();
        } else {
            index += 1;
        }
    }
    Destroy(a);
    Destroy(b);
    return same;
}

// Rebuilds `tree` at a different branching factor by streaming its contents
// through a sorted run.  The two branching factors are independent template
// parameters, so this is the one place both instantiations meet.
fn rebalance_to_order[K: type, V: type, FROM: int, TO: int]
    (source: BTree(K, V, FROM),
                      ref target: BTree(K, V, TO)) -> bool {
    var entries = source.to_pairs();
    if (not is_sorted_by_key(K, V)(entries)) {
        Destroy(entries);
        return false;
    }
    let ok = target.bulk_load(entries);
    Destroy(entries);
    return ok;
}

// Summarises a tree's shape as a single line of text.
fn describe_shape[K: type, V: type, ORDER: int]
    (tree: BTree(K, V, ORDER)) -> string {
    var stats = tree.statistics();
    var text = stats.describe();
    text.append(" order=");
    text.append(to_string(ORDER));
    text.append(" balanced=");
    if (stats.balanced()) {
        text.append("yes");
    } else {
        text.append("no");
    }
    return text;
}

// --- Demo -------------------------------------------------------------------

// Exercises the container end to end at two different branching factors, which
// is also the cheapest way to force both instantiations to be checked.
fn demo_b_tree() {
    var tree = BTree(int, string, 6)();
    tree.reserve(BTREE_INITIAL_CAPACITY);

    // Insert a pseudo-random permutation of [0, 64).
    for (i in range(0, 64)) {
        Discard(tree.insert((i * 37) % 64, to_string(i)));
    }
    println(describe_shape(int, string, 6)(tree));
    if (not tree.check_validity()) {
        panic("B-tree invariants violated after bulk insert");
    }

    // Delete every third key and re-check.
    var index: int = 0;
    while (index < 64) {
        if (index % 3 == 0) {
            Discard(tree.erase(index));
        }
        index += 1;
    }
    if (not tree.check_validity()) {
        panic("B-tree invariants violated after bulk erase");
    }

    // Range query over the middle half of the key space.
    var window = vector(pair(int, string))();
    let hits = tree.range_query(16, 48, window);
    println(to_string(hits));
    for (entry in window) {
        println(entry.second);
    }
    Destroy(window);

    // Grouped range query, exercising the nested map/vector/pair return type.
    var grouped = tree.range_grouped(0, 64);
    println(to_string(grouped.size()));
    Destroy(grouped);

    // Bulk load a wider tree from the narrow one's sorted contents.
    var wide = BTree(int, string, 16)();
    if (not rebalance_to_order(int, string, 6, 16)(tree, wide)) {
        panic("rebalance produced an unsorted run");
    }
    if (not wide.check_validity()) {
        panic("B-tree invariants violated after bulk load");
    }
    println(describe_shape(int, string, 16)(wide));

    // Predecessor and successor around a key that is definitely present.
    let before = tree.predecessor(32);
    let after = tree.successor(32);
    println(to_string(before.first));
    println(to_string(after.first));

    // Statistics pass.
    var stats = tree.statistics();
    println(to_string(stats.height));
    println(to_string(stats.average_occupancy()));
    println(to_string(stats.leaf_fraction_percent()));
    for (bucket in stats.occupancy_histogram) {
        println(to_string(bucket.second));
    }
    Destroy(stats);

    Destroy(wide);
    Destroy(tree);
}


