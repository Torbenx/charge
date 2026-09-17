// A generic graph algorithms library.
//
// The library is deliberately organised as a *shallow tower of namespaces*
// rather than as one enormous container type.  Everything that operates on a
// graph is a free function, grouped by topic:
//
//   * `graph`                  -- the data types themselves plus small helpers,
//   * `graph::builders`        -- construction, conversion and transformation,
//   * `graph::traversal`       -- BFS, DFS and the visitor plumbing,
//   * `graph::connectivity`    -- components, ordering, cycles, cuts,
//   * `graph::shortest_path`   -- Dijkstra, Bellman-Ford, Floyd-Warshall,
//   * `graph::spanning`        -- Prim, Kruskal and the union-find they need,
//   * `graph::matching`        -- bipartite matching and the covers it implies,
//   * `graph::flow`            -- residual networks and maximum flow.
//
// Two storage strategies are provided and they share the same vocabulary:
//
//   * `AdjacencyList{V, W}` keeps, for every vertex, a vector of *edge ids*
//     into a flat `edges` arena.  This is the representation every algorithm
//     in this file prefers, because iterating the incident edges of a vertex
//     costs exactly the degree of that vertex.
//   * `AdjacencyMatrix{V, W, N}` keeps an `N * N` block of cells.  It is the
//     right shape for Floyd-Warshall and for dense max-flow, and it makes the
//     "is there an edge between u and v" question a single index.
//
// Both are parameterised over `V`, the payload carried by a vertex, and `W`,
// the payload carried by an edge.  An *unweighted* graph is simply a graph
// whose edges all carry `UNIT_WEIGHT`; there is no separate type for it, only
// the `unweighted_*` constructors below.
//
// No floating point appears anywhere.  Weights and distances are fixed point
// integers scaled by `WEIGHT_SCALE`, so a weight of "2.5" is written 2500 and
// a ratio of "one half" is `WEIGHT_SCALE / 2`.  Every routine that divides
// documents whether it rounds towards zero.
namespace graph;

// --- Sentinels and tuning -------------------------------------------------

// "No such vertex" / "no such edge".  Negative so it can never index a vector.
let GRAPH_NIL: int = -1;

// Stand-in for +infinity in distance arrays.  Chosen so that adding any two
// finite distances in this library still fits comfortably in an int.
let GRAPH_INFINITY: int = 1000000000;

// Fixed point scale shared by every weight, distance and flow value.
let WEIGHT_SCALE: int = 1000;

// The weight given to an edge of an unweighted graph.
let UNIT_WEIGHT: int = 1000;

// Default number of vertices a freshly built graph reserves room for.
let GRAPH_INITIAL_ORDER: int = 32;

// Growth factor for the edge arena, in percent, so no division by a float is
// ever required.
let GRAPH_GROWTH_PERCENT: int = 175;

// Upper bound on the depth of an explicit traversal stack before the
// iterative variants start reallocating.
let GRAPH_STACK_HINT: int = 64;

// Bellman-Ford gives up after this many relaxation rounds even if the graph
// claims to be larger; a negative cycle is reported instead.
let GRAPH_RELAX_LIMIT: int = 4096;

// Maximum number of augmenting paths a flow routine will look for.  Real
// Edmonds-Karp terminates in O(V * E) augmentations; this is only a guard.
let GRAPH_AUGMENT_LIMIT: int = 100000;

// --- Enumerations ---------------------------------------------------------

// Whether an edge may be traversed in one direction or in both.
choice EdgeDirection {
    Directed,
    Undirected,
}

// Whether the edges of a graph carry meaningful weights.  Algorithms use this
// to pick a cheaper code path, never to change their answer.
choice WeightMode {
    Unweighted,
    Weighted,
}

// The classic three-colour marking used by depth first search.
choice ColorMark {
    White,
    Gray,
    Black,
}

// How a visitor wants the traversal to proceed after a hook fires.
choice VisitAction {
    Continue,
    SkipChildren,
    StopTraversal,
}

// The role an edge plays in a depth first forest.
choice EdgeClass {
    TreeEdge,
    BackEdge,
    ForwardEdge,
    CrossEdge,
}

// The outcome of a topological ordering attempt.
choice OrderStatus {
    TotalOrder,
    CycleFound,
}

// Which side of a bipartition a vertex landed on.
choice Side {
    Unassigned,
    LeftSide,
    RightSide,
}

// --- Edge -----------------------------------------------------------------

// One directed arc of the graph.
//
// Undirected graphs store *two* `Edge` records per logical edge, one in each
// direction, and give them consecutive ids so that `twin_of` can flip between
// them with a single exclusive-or against 1.  `pair_id` keeps the logical
// identity so that, for instance, Kruskal never considers the same physical
// edge twice.
class Edge(V: type, W: type)
{

    // The endpoint that is not `endpoint`.  Passing a vertex that is not an
    // endpoint of this edge returns `tail`, which callers treat as a bug.
    fn other(self, endpoint: int) -> int {
        if (self.tail == endpoint) {
            return self.head;
        }
        return self.tail;
    }

    fn touches(self, vertex: int) -> bool {
        return self.tail == vertex or self.head == vertex;
    }

    fn is_loop(self) -> bool {
        return self.tail == self.head;
    }

    fn is_live(self) -> bool {
        return self.alive and self.tail != GRAPH_NIL;
    }

    // Ids of the two half-edges of an undirected pair differ only in bit 0.
    fn twin_id(self) -> int {
        return self.id ^ 1;
    }

    var tail: int = 0;
    var head: int = 0;
    var weight: int = 0;
    var capacity: int = 0;
    var id: int = 0;
    var pair_id: int = 0;
    var alive: bool = true;
    var payload: W;
    var tag: V;
}

// --- Vertex ---------------------------------------------------------------

// One vertex plus the bookkeeping every traversal wants to hang off it.
//
// The fields after `payload` are scratch space.  Algorithms are expected to
// reset the ones they use through `reset_marks` before they start; nothing in
// this file relies on their value surviving between two calls.
class Vertex(V: type)
{

    fn degree(self) -> int {
        return self.incident.size();
    }

    fn is_isolated(self) -> bool {
        return self.incident.empty();
    }

    fn clear_marks(ref self) {
        self.color = ColorMark.White;
        self.discovered = GRAPH_NIL;
        self.finished = GRAPH_NIL;
        self.parent = GRAPH_NIL;
        self.component = GRAPH_NIL;
        self.side = Side.Unassigned;
    }

    var incident: vector(int);
    var incoming: vector(int);
    var payload: V;
    var id: int = 0;
    var color: ColorMark = ColorMark.White;
    var discovered: int = -1;
    var finished: int = -1;
    var parent: int = -1;
    var component: int = -1;
    var side: Side = Side.Unassigned;
}

// --- Adjacency list -------------------------------------------------------

// The primary representation: a vector of vertices, each holding a vector of
// edge ids into a shared arena.
//
// `direction` decides whether `connect` inserts one arc or two.  Nothing else
// in the type behaves differently, which is what lets one implementation of
// BFS serve both the directed and the undirected case.
class AdjacencyList(V: type, W: type)
{

    fn order(self) -> int {
        return self.vertices.size();
    }

    fn arc_count(self) -> int {
        return self.edges.size();
    }

    // Number of *logical* edges: half the arc count when undirected.
    fn size(self) -> int {
        if (self.direction == EdgeDirection.Undirected) {
            return self.edges.size() / 2;
        }
        return self.edges.size();
    }

    fn is_directed(self) -> bool {
        return self.direction == EdgeDirection.Directed;
    }

    fn is_weighted(self) -> bool {
        return self.mode == WeightMode.Weighted;
    }

    fn valid_vertex(self, v: int) -> bool {
        return v >= 0 and v < self.vertices.size();
    }

    fn vertex(ref self, v: int) -> ref Vertex(V) {
        return self.vertices.at(v);
    }

    fn arc(ref self, e: int) -> ref Edge(V, W) {
        return self.edges.at(e);
    }

    fn incident_of(ref self, v: int) -> ref vector(int) {
        return self.vertices.at(v).incident;
    }

    fn degree(self, v: int) -> int {
        return self.vertices.at(v).incident.size();
    }

    fn in_degree(self, v: int) -> int {
        if (self.direction == EdgeDirection.Undirected) {
            return self.vertices.at(v).incident.size();
        }
        return self.vertices.at(v).incoming.size();
    }

    fn out_degree(self, v: int) -> int {
        return self.vertices.at(v).incident.size();
    }

    // Append a fresh isolated vertex and hand back its id.
    fn add_vertex(ref self, payload: V) -> int {
        let id = self.vertices.size();
        var v = Vertex(V)({.id = id, .payload = payload});
        self.vertices.push_back(move(v));
        return id;
    }

    // Grow to at least `n` vertices, leaving existing ones untouched.
    fn reserve_vertices(ref self, n: int) {
        self.vertices.reserve(n);
        while (self.vertices.size() < n) {
            let id = self.vertices.size();
            self.vertices.push_back(Vertex(V)({.id = id}));
        }
    }

    // The workhorse: insert one arc, and its twin when undirected.
    fn connect(ref self, tail: int, head: int, weight: int) -> int {
        Assert(tail >= 0);
        Assert(head >= 0);
        let first = self.edges.size();
        var forward = Edge(V, W)({.tail = tail,
                                 .head = head,
                                 .weight = weight,
                                 .capacity = weight,
                                 .id = first,
                                 .pair_id = first});
        self.edges.push_back(move(forward));
        self.vertices.at(tail).incident.push_back(first);
        if (self.direction == EdgeDirection.Undirected) {
            let second = self.edges.size();
            var backward = Edge(V, W)({.tail = head,
                                      .head = tail,
                                      .weight = weight,
                                      .capacity = weight,
                                      .id = second,
                                      .pair_id = first});
            self.edges.push_back(move(backward));
            self.vertices.at(head).incident.push_back(second);
        } else {
            self.vertices.at(head).incoming.push_back(first);
        }
        return first;
    }

    fn connect_unweighted(ref self, tail: int, head: int) -> int {
        return self.connect(tail, head, UNIT_WEIGHT);
    }

    // Logical removal: the arc stays in the arena so that ids never shift.
    fn disconnect(ref self, e: int) -> bool {
        if (e < 0 or e >= self.edges.size()) {
            return false;
        }
        let ref arc: auto = self.edges.at(e);
        if (arc.alive == false) {
            return false;
        }
        arc.alive = false;
        if (self.direction == EdgeDirection.Undirected) {
            let ref twin: auto = self.edges.at(arc.twin_id());
            twin.alive = false;
        }
        return true;
    }

    fn has_edge(self, tail: int, head: int) -> bool {
        for (e in self.vertices.at(tail).incident) {
            let ref arc: auto = self.edges.at(e);
            if (arc.alive and arc.head == head) {
                return true;
            }
        }
        return false;
    }

    fn weight_between(self, tail: int, head: int) -> int {
        for (e in self.vertices.at(tail).incident) {
            let ref arc: auto = self.edges.at(e);
            if (arc.alive and arc.head == head) {
                return arc.weight;
            }
        }
        return GRAPH_INFINITY;
    }

    fn reset_marks(ref self) {
        for (v in self.vertices) {
            v.clear_marks();
        }
    }

    fn clear(ref self) {
        self.vertices.clear();
        self.edges.clear();
    }

    var vertices: vector(Vertex(V));
    var edges: vector(Edge(V, W));
    var direction: EdgeDirection = EdgeDirection.Directed;
    var mode: WeightMode = WeightMode.Weighted;
    var label: string;
}

// --- Adjacency matrix -----------------------------------------------------

// Dense representation with a compile-time side length.
//
// `N` being a template parameter means `index_of` is a compile-time affine
// expression and the whole cell block can live in one allocation.  Absent
// edges hold `GRAPH_INFINITY` rather than zero so that Floyd-Warshall can run
// straight over the raw cells.
class AdjacencyMatrix(V: type, W: type, N: int)
{

    fn order(self) -> int {
        return N;
    }

    fn cell_count(self) -> int {
        return N * N;
    }

    fn index_of(self, row: int, col: int) -> int {
        return row * N + col;
    }

    fn at(ref self, row: int, col: int) -> ref int {
        return self.cells.at(row * N + col);
    }

    fn get(self, row: int, col: int) -> int {
        return self.cells.at(row * N + col);
    }

    fn put(ref self, row: int, col: int, value: int) {
        self.cells.at(row * N + col) = value;
        if (self.direction == EdgeDirection.Undirected) {
            self.cells.at(col * N + row) = value;
        }
    }

    fn linked(self, row: int, col: int) -> bool {
        return self.cells.at(row * N + col) < GRAPH_INFINITY;
    }

    fn fill(ref self, value: int) {
        self.cells.clear();
        var k: int = 0;
        while (k < N * N) {
            self.cells.push_back(value);
            k += 1;
        }
    }

    // Reset to "no edges anywhere, zero cost to stay put".
    fn reset(ref self) {
        self.fill(GRAPH_INFINITY);
        var d: int = 0;
        while (d < N) {
            self.cells.at(d * N + d) = 0;
            d += 1;
        }
    }

    fn row_sum(self, row: int) -> int {
        var total: int = 0;
        var c: int = 0;
        while (c < N) {
            let value = self.cells.at(row * N + c);
            if (value < GRAPH_INFINITY) {
                total += value;
            }
            c += 1;
        }
        return total;
    }

    fn symmetric(self) -> bool {
        var r: int = 0;
        while (r < N) {
            var c = r + 1;
            while (c < N) {
                if (self.cells.at(r * N + c) != self.cells.at(c * N + r)) {
                    return false;
                }
                c += 1;
            }
            r += 1;
        }
        return true;
    }

    var cells: vector(int);
    var payloads: vector(V);
    var slack: W;
    var direction: EdgeDirection = EdgeDirection.Directed;
    var mode: WeightMode = WeightMode.Weighted;
}

// --- Small shared helpers -------------------------------------------------

// A vertex/priority pair, used by every routine that needs an ordered
// frontier.  Kept at namespace scope so the heap below is not generic over
// yet another type.
class HeapEntry {

    fn better_than(self, other: HeapEntry) -> bool {
        if (self.priority != other.priority) {
            return self.priority < other.priority;
        }
        return self.vertex < other.vertex;
    }

    var vertex: int = 0;
    var priority: int = 0;
    var stamp: int = 0;
}

// An explicit binary min-heap.
//
// The heap is written by hand rather than reused from the standard library
// because Dijkstra wants `decrease_key`, and because a lazy variant (pushing
// duplicates and discarding stale pops) is measurably simpler to prove
// correct.  Both are supported: `position` is maintained when `tracked` is
// true and ignored otherwise.
class BinaryHeap {

    fn size(self) -> int {
        return self.items.size();
    }

    fn empty(self) -> bool {
        return self.items.empty();
    }

    fn parent_of(self, i: int) -> int {
        return (i - 1) / 2;
    }

    fn left_of(self, i: int) -> int {
        return 2 * i + 1;
    }

    fn right_of(self, i: int) -> int {
        return 2 * i + 2;
    }

    // Move `i` towards the root until the heap property holds above it.
    fn sift_up(ref self, start: int) -> int {
        var i = start;
        while (i > 0) {
            let p = (i - 1) / 2;
            if (self.items.at(i).better_than(self.items.at(p)) == false) {
                break;
            }
            self.swap_slots(i, p);
            i = p;
        }
        return i;
    }

    // Move `i` towards the leaves until the heap property holds below it.
    fn sift_down(ref self, start: int) -> int {
        var i = start;
        let n = self.items.size();
        while (true) {
            let l = 2 * i + 1;
            let r = 2 * i + 2;
            var best = i;
            if (l < n) {
                if (self.items.at(l).better_than(self.items.at(best))) {
                    best = l;
                }
            }
            if (r < n) {
                if (self.items.at(r).better_than(self.items.at(best))) {
                    best = r;
                }
            }
            if (best == i) {
                break;
            }
            self.swap_slots(i, best);
            i = best;
        }
        return i;
    }

    fn swap_slots(ref self, a: int, b: int) {
        swap(self.items.at(a), self.items.at(b));
        if (self.tracked) {
            self.position.at(self.items.at(a).vertex) = a;
            self.position.at(self.items.at(b).vertex) = b;
        }
    }

    fn push(ref self, vertex: int, priority: int) {
        let slot = self.items.size();
        self.items.push_back(HeapEntry({.vertex = vertex, .priority = priority, .stamp = self.clock}));
        self.clock += 1;
        if (self.tracked) {
            self.position.at(vertex) = slot;
        }
        Discard(self.sift_up(slot));
    }

    fn peek(ref self) -> ref HeapEntry {
        return self.items.front();
    }

    fn pop(ref self) -> HeapEntry {
        let top = self.items.front();
        let last = self.items.size() - 1;
        self.swap_slots(0, last);
        self.items.pop_back();
        if (self.items.empty() == false) {
            Discard(self.sift_down(0));
        }
        if (self.tracked) {
            self.position.at(top.vertex) = GRAPH_NIL;
        }
        return top;
    }

    // Lower the recorded priority of a vertex already in the heap.  Returns
    // false when the vertex is absent or the new key is not an improvement.
    fn decrease_key(ref self, vertex: int, priority: int) -> bool {
        if (self.tracked == false) {
            self.push(vertex, priority);
            return true;
        }
        let slot = self.position.at(vertex);
        if (slot == GRAPH_NIL) {
            self.push(vertex, priority);
            return true;
        }
        if (self.items.at(slot).priority <= priority) {
            return false;
        }
        self.items.at(slot).priority = priority;
        Discard(self.sift_up(slot));
        return true;
    }

    fn contains(self, vertex: int) -> bool {
        if (self.tracked == false) {
            return false;
        }
        return self.position.at(vertex) != GRAPH_NIL;
    }

    fn prepare(ref self, order: int) {
        self.items.clear();
        self.position.clear();
        self.tracked = true;
        var i: int = 0;
        while (i < order) {
            self.position.push_back(GRAPH_NIL);
            i += 1;
        }
    }

    fn clear(ref self) {
        self.items.clear();
        self.position.clear();
        self.clock = 0;
    }

    var items: vector(HeapEntry);
    var position: vector(int);
    var tracked: bool = false;
    var clock: int = 0;
}

// Disjoint set forest with union by rank and full path compression.
//
// Used by Kruskal and by the offline connectivity queries in
// `graph::connectivity`.  `components` is maintained eagerly so that
// "is the graph connected" is answered without a scan.
class UnionFind {

    fn prepare(ref self, order: int) {
        self.parent.clear();
        self.rank.clear();
        self.weight.clear();
        var i: int = 0;
        while (i < order) {
            self.parent.push_back(i);
            self.rank.push_back(0);
            self.weight.push_back(1);
            i += 1;
        }
        self.components = order;
    }

    fn find(ref self, x: int) -> int {
        var root = x;
        while (self.parent.at(root) != root) {
            root = self.parent.at(root);
        }
        var walker = x;
        while (walker != root) {
            let next = self.parent.at(walker);
            self.parent.at(walker) = root;
            walker = next;
        }
        return root;
    }

    fn connected(ref self, a: int, b: int) -> bool {
        return self.find(a) == self.find(b);
    }

    fn unite(ref self, a: int, b: int) -> bool {
        var ra = self.find(a);
        var rb = self.find(b);
        if (ra == rb) {
            return false;
        }
        if (self.rank.at(ra) < self.rank.at(rb)) {
            swap(ra, rb);
        }
        self.parent.at(rb) = ra;
        self.weight.at(ra) += self.weight.at(rb);
        if (self.rank.at(ra) == self.rank.at(rb)) {
            self.rank.at(ra) += 1;
        }
        self.components -= 1;
        Assert(self.components >= 1);
        return true;
    }

    fn set_size(ref self, x: int) -> int {
        let root = self.find(x);
        return self.weight.at(root);
    }

    fn single_component(self) -> bool {
        return self.components == 1;
    }

    var parent: vector(int);
    var rank: vector(int);
    var weight: vector(int);
    var components: int = 0;
}

// Fill `target` with `count` copies of `value`, discarding what was there.
fn fill_int_vector(ref target: vector(int), count: int, value: int) {
    target.clear();
    var i: int = 0;
    while (i < count) {
        target.push_back(value);
        i += 1;
    }
}

// Same, for the boolean scratch arrays every traversal needs.
fn fill_bool_vector(ref target: vector(bool), count: int, value: bool) {
    target.clear();
    var i: int = 0;
    while (i < count) {
        target.push_back(value);
        i += 1;
    }
}

// Reverse a vector of ids in place; used to turn a "walk back along parents"
// result into a forward path.
fn reverse_int_vector(ref target: vector(int)) {
    var lo: int = 0;
    var hi = target.size() - 1;
    while (lo < hi) {
        swap(target.at(lo), target.at(hi));
        lo += 1;
        hi -= 1;
    }
}

// Saturating addition on scaled distances: infinity absorbs everything.
fn add_distance(a: int, b: int) -> int {
    if (a >= GRAPH_INFINITY or b >= GRAPH_INFINITY) {
        return GRAPH_INFINITY;
    }
    return a + b;
}

// True when the scaled distance stands for "unreachable".
fn is_unreachable(d: int) -> bool {
    return d >= GRAPH_INFINITY;
}

// ==========================================================================
// graph::builders
//
// Construction, conversion and structural transformation.  Nothing here
// inspects a graph beyond its shape, so this namespace has no dependencies on
// any of the algorithm namespaces below it.
// ==========================================================================
namespace builders;

// An empty directed graph with `n` isolated vertices.
fn directed[V: type, W: type]
    (n: int) -> AdjacencyList(V, W) {
    var g = AdjacencyList(V, W)({.direction = EdgeDirection.Directed,
                                .mode = WeightMode.Weighted});
    g.reserve_vertices(n);
    return g;
}

// An empty undirected graph with `n` isolated vertices.
fn undirected[V: type, W: type]
    (n: int) -> AdjacencyList(V, W) {
    var g = AdjacencyList(V, W)({.direction = EdgeDirection.Undirected,
                                .mode = WeightMode.Weighted});
    g.reserve_vertices(n);
    return g;
}

// Unweighted variants differ only in the mode flag; every edge added later
// still carries `UNIT_WEIGHT` so the weighted algorithms keep working.
fn directed_unweighted[V: type, W: type]
    (n: int) -> AdjacencyList(V, W) {
    var g = directed(V, W)(n);
    g.mode = WeightMode.Unweighted;
    return g;
}

fn undirected_unweighted[V: type, W: type]
    (n: int) -> AdjacencyList(V, W) {
    var g = undirected(V, W)(n);
    g.mode = WeightMode.Unweighted;
    return g;
}

// The path 0 - 1 - 2 - ... - (n - 1), every edge of unit weight.
fn path_graph[V: type, W: type]
    (n: int) -> AdjacencyList(V, W) {
    var g = undirected_unweighted(V, W)(n);
    var i: int = 1;
    while (i < n) {
        Discard(g.connect_unweighted(i - 1, i));
        i += 1;
    }
    return g;
}

// The path above, closed back onto itself.
fn cycle_graph[V: type, W: type]
    (n: int) -> AdjacencyList(V, W) {
    var g = path_graph(V, W)(n);
    if (n > 2) {
        Discard(g.connect_unweighted(n - 1, 0));
    }
    return g;
}

// Every pair of distinct vertices joined once.
fn complete_graph[V: type, W: type]
    (n: int) -> AdjacencyList(V, W) {
    var g = undirected_unweighted(V, W)(n);
    var i: int = 0;
    while (i < n) {
        var j = i + 1;
        while (j < n) {
            Discard(g.connect_unweighted(i, j));
            j += 1;
        }
        i += 1;
    }
    return g;
}

// A `rows * cols` lattice, vertices numbered row-major.
fn grid_graph[V: type, W: type]
    (rows: int, cols: int) -> AdjacencyList(V, W) {
    var g = undirected_unweighted(V, W)(rows * cols);
    var r: int = 0;
    while (r < rows) {
        var c: int = 0;
        while (c < cols) {
            let here = r * cols + c;
            if (c + 1 < cols) {
                Discard(g.connect_unweighted(here, here + 1));
            }
            if (r + 1 < rows) {
                Discard(g.connect_unweighted(here, here + cols));
            }
            c += 1;
        }
        r += 1;
    }
    return g;
}

// The complete bipartite graph on `left + right` vertices.  Left vertices get
// the ids [0, left), right vertices [left, left + right).
fn complete_bipartite[V: type, W: type]
    (left: int, right: int) -> AdjacencyList(V, W) {
    var g = undirected_unweighted(V, W)(left + right);
    var a: int = 0;
    while (a < left) {
        var b: int = 0;
        while (b < right) {
            Discard(g.connect_unweighted(a, left + b));
            b += 1;
        }
        a += 1;
    }
    return g;
}

// A rooted binary tree with `n` vertices, child i having parent (i - 1) / 2.
fn binary_tree_graph[V: type, W: type]
    (n: int) -> AdjacencyList(V, W) {
    var g = undirected_unweighted(V, W)(n);
    var i: int = 1;
    while (i < n) {
        Discard(g.connect_unweighted((i - 1) / 2, i));
        i += 1;
    }
    return g;
}

// Reverse every arc.  Undirected graphs come back unchanged apart from the
// order of the incidence vectors.
fn transpose[V: type, W: type]
    (g: AdjacencyList(V, W)) -> AdjacencyList(V, W) {
    var out = AdjacencyList(V, W)({.direction = g.direction, .mode = g.mode});
    out.reserve_vertices(g.order());
    for (arc in g.edges) {
        if (arc.alive) {
            Discard(out.connect(arc.head, arc.tail, arc.weight));
        }
    }
    return out;
}

// Forget the orientation of every arc.
fn to_undirected[V: type, W: type]
    (g: AdjacencyList(V, W)) -> AdjacencyList(V, W) {
    var out = undirected(V, W)(g.order());
    out.mode = g.mode;
    for (arc in g.edges) {
        if (arc.alive and arc.tail < arc.head) {
            Discard(out.connect(arc.tail, arc.head, arc.weight));
        }
    }
    return out;
}

// Replace every undirected edge by the two opposite arcs.
fn to_directed[V: type, W: type]
    (g: AdjacencyList(V, W)) -> AdjacencyList(V, W) {
    var out = directed(V, W)(g.order());
    out.mode = g.mode;
    for (arc in g.edges) {
        if (arc.alive) {
            Discard(out.connect(arc.tail, arc.head, arc.weight));
        }
    }
    return out;
}

// Drop every arc whose weight exceeds `limit`.
fn filter_by_weight[V: type, W: type]
    (g: AdjacencyList(V, W),
                    limit: int) -> AdjacencyList(V, W) {
    var out = AdjacencyList(V, W)({.direction = g.direction, .mode = g.mode});
    out.reserve_vertices(g.order());
    for (arc in g.edges) {
        if (arc.alive and arc.weight <= limit) {
            if (g.direction == EdgeDirection.Directed or arc.tail < arc.head) {
                Discard(out.connect(arc.tail, arc.head, arc.weight));
            }
        }
    }
    return out;
}

// Keep only the vertices for which `keep` is true, renumbering the survivors.
fn induced_subgraph[V: type, W: type]
    (g: AdjacencyList(V, W),
                    keep: vector(bool)) -> AdjacencyList(V, W) {
    var mapping = vector(int)();
    fill_int_vector(mapping, g.order(), GRAPH_NIL);
    var next: int = 0;
    var v: int = 0;
    while (v < g.order()) {
        if (keep.at(v)) {
            mapping.at(v) = next;
            next += 1;
        }
        v += 1;
    }
    var out = AdjacencyList(V, W)({.direction = g.direction, .mode = g.mode});
    out.reserve_vertices(next);
    for (arc in g.edges) {
        if (arc.alive and keep.at(arc.tail) and keep.at(arc.head)) {
            if (g.direction == EdgeDirection.Directed or arc.tail < arc.head) {
                Discard(out.connect(mapping.at(arc.tail),
                                    mapping.at(arc.head),
                                    arc.weight));
            }
        }
    }
    return out;
}

// Project an adjacency list onto a dense matrix of side `N`.
//
// Parallel edges collapse to the cheapest one, which is what every dense
// shortest path routine expects.
fn densify[V: type, W: type, N: int]
    (g: AdjacencyList(V, W)) -> AdjacencyMatrix(V, W, N) {
    var m = AdjacencyMatrix(V, W, N)({.direction = g.direction, .mode = g.mode});
    m.reset();
    for (arc in g.edges) {
        if (arc.alive and arc.tail < N and arc.head < N) {
            let current = m.get(arc.tail, arc.head);
            if (arc.weight < current) {
                m.put(arc.tail, arc.head, arc.weight);
            }
        }
    }
    return m;
}

// The inverse projection: every finite off-diagonal cell becomes an arc.
fn sparsify[V: type, W: type, N: int]
    (m: AdjacencyMatrix(V, W, N)) -> AdjacencyList(V, W) {
    var g = AdjacencyList(V, W)({.direction = m.direction, .mode = m.mode});
    g.reserve_vertices(N);
    var r: int = 0;
    while (r < N) {
        var c: int = 0;
        while (c < N) {
            if (r != c) {
                let value = m.get(r, c);
                if (value < GRAPH_INFINITY) {
                    if (m.direction == EdgeDirection.Directed or r < c) {
                        Discard(g.connect(r, c, value));
                    }
                }
            }
            c += 1;
        }
        r += 1;
    }
    return g;
}

// Add `delta` to every weight, e.g. to lift a graph out of negative territory
// before running Dijkstra on it.  Note this does *not* preserve shortest
// paths in general; Johnson's reweighting is the principled version.
fn shift_weights[V: type, W: type]
    (ref g: AdjacencyList(V, W), delta: int) {
    for (arc in g.edges) {
        arc.weight += delta;
    }
}

// Scale every weight by a fixed point ratio, rounding towards zero.
fn scale_weights[V: type, W: type]
    (ref g: AdjacencyList(V, W), ratio: int) {
    for (arc in g.edges) {
        arc.weight = arc.weight * ratio / WEIGHT_SCALE;
    }
}

// Sum of all live arc weights, halved for undirected graphs so that each
// logical edge is counted once.
fn total_weight[V: type, W: type]
    (g: AdjacencyList(V, W)) -> int {
    var total: int = 0;
    for (arc in g.edges) {
        if (arc.alive) {
            total += arc.weight;
        }
    }
    if (g.direction == EdgeDirection.Undirected) {
        return total / 2;
    }
    return total;
}

// A cheap structural fingerprint, handy for regression tests.
fn describe[V: type, W: type]
    (g: AdjacencyList(V, W)) -> string {
    var out = string();
    out.append(to_string(g.order()));
    out.append("v/");
    out.append(to_string(g.size()));
    out.append("e");
    if (g.is_directed()) {
        out.append(" directed");
    } else {
        out.append(" undirected");
    }
    if (g.is_weighted()) {
        out.append(" weighted");
    }
    return out;
}



// ==========================================================================
// graph::traversal
//
// Breadth first and depth first search, plus the visitor plumbing the rest of
// the library builds on.  Every routine here is iterative: the recursive
// formulations read better on a whiteboard but blow the stack on the long
// path graphs the benchmarks generate.
// ==========================================================================
namespace traversal;

// The hook set a traversal fires into.
//
// There are no closures in the language, so a visitor is a struct with a
// fixed set of methods and the traversals call them unconditionally.  The
// default implementation records the event and asks the search to continue,
// which makes `Visitor` on its own a perfectly usable "give me the order"
// collector.  Specialised behaviour is obtained by pre-loading the filter
// fields rather than by overriding methods.
class Visitor(V: type, W: type)
{

    // Fired the first time a vertex is dequeued or pushed.
    fn on_discover(ref self, v: int) -> VisitAction {
        self.discovery_order.push_back(v);
        self.visited_count += 1;
        if (self.blocked.empty() == false) {
            if (self.blocked.at(v)) {
                return VisitAction.SkipChildren;
            }
        }
        if (self.stop_after > 0 and self.visited_count >= self.stop_after) {
            return VisitAction.StopTraversal;
        }
        return VisitAction.Continue;
    }

    // Fired once every out-arc of a vertex has been considered.
    fn on_finish(ref self, v: int) -> VisitAction {
        self.finish_order.push_back(v);
        return VisitAction.Continue;
    }

    // Fired for every arc inspected, whatever its classification.
    fn on_edge(ref self, arc: int, kind: EdgeClass) -> VisitAction {
        self.edge_order.push_back(arc);
        if (kind == EdgeClass.BackEdge) {
            self.back_edges += 1;
        }
        if (kind == EdgeClass.TreeEdge) {
            self.tree_edges += 1;
        }
        return VisitAction.Continue;
    }

    // Fired when a search runs out of reachable vertices and restarts.
    fn on_restart(ref self, root: int) {
        self.roots.push_back(root);
    }

    fn prepare(ref self, order: int) {
        self.discovery_order.clear();
        self.finish_order.clear();
        self.edge_order.clear();
        self.roots.clear();
        self.visited_count = 0;
        self.back_edges = 0;
        self.tree_edges = 0;
        fill_bool_vector(self.blocked, order, false);
    }

    fn block(ref self, v: int) {
        self.blocked.at(v) = true;
    }

    fn saw(self, v: int) -> bool {
        for (seen in self.discovery_order) {
            if (seen == v) {
                return true;
            }
        }
        return false;
    }

    var discovery_order: vector(int);
    var finish_order: vector(int);
    var edge_order: vector(int);
    var roots: vector(int);
    var blocked: vector(bool);
    var visited_count: int = 0;
    var back_edges: int = 0;
    var tree_edges: int = 0;
    var stop_after: int = 0;
    var sample: W;
    var label: V;
}

// The state a traversal hands back: parents, depths and the visiting order.
class SearchResult(V: type)
{

    fn reached(self, v: int) -> bool {
        return self.depth.at(v) < GRAPH_INFINITY;
    }

    fn reached_count(self) -> int {
        var total: int = 0;
        for (d in self.depth) {
            if (d < GRAPH_INFINITY) {
                total += 1;
            }
        }
        return total;
    }

    fn eccentricity(self) -> int {
        var best: int = 0;
        for (d in self.depth) {
            if (d < GRAPH_INFINITY and d > best) {
                best = d;
            }
        }
        return best;
    }

    // Walk the parent chain from `v` back to the root, then reverse it.
    fn path_to(self, v: int) -> vector(int) {
        var out = vector(int)();
        if (self.depth.at(v) >= GRAPH_INFINITY) {
            return out;
        }
        var walker = v;
        while (walker != GRAPH_NIL) {
            out.push_back(walker);
            walker = self.parent.at(walker);
        }
        reverse_int_vector(out);
        return out;
    }

    var parent: vector(int);
    var depth: vector(int);
    var order: vector(int);
    var payload: V;
    var source: int = -1;
    var complete: bool = true;
}

// Breadth first search from a single source.
//
// `depth` is measured in edges, not weights; a weighted shortest path wants
// `shortest_path::dijkstra` instead.
fn bfs[V: type, W: type]
    (g: AdjacencyList(V, W), source: int) -> SearchResult(V) {
    var out = SearchResult(V)({.source = source});
    fill_int_vector(out.parent, g.order(), GRAPH_NIL);
    fill_int_vector(out.depth, g.order(), GRAPH_INFINITY);
    if (g.valid_vertex(source) == false) {
        out.complete = false;
        return out;
    }
    var queue = vector(int)();
    queue.push_back(source);
    out.depth.at(source) = 0;
    var head: int = 0;
    while (head < queue.size()) {
        let v = queue.at(head);
        head += 1;
        out.order.push_back(v);
        for (e in g.vertices.at(v).incident) {
            let ref arc: auto = g.edges.at(e);
            if (arc.alive == false) {
                continue;
            }
            let next = arc.head;
            if (out.depth.at(next) >= GRAPH_INFINITY) {
                out.depth.at(next) = out.depth.at(v) + 1;
                out.parent.at(next) = v;
                queue.push_back(next);
            }
        }
    }
    Assert(out.depth.at(source) == 0);
    return out;
}

// The same search, but driving a visitor and honouring its verdicts.
fn bfs_visit[V: type, W: type]
    (g: AdjacencyList(V, W),
             source: int,
             ref watcher: Visitor(V, W)) -> SearchResult(V) {
    var out = SearchResult(V)({.source = source});
    fill_int_vector(out.parent, g.order(), GRAPH_NIL);
    fill_int_vector(out.depth, g.order(), GRAPH_INFINITY);
    watcher.prepare(g.order());
    var queue = vector(int)();
    queue.push_back(source);
    out.depth.at(source) = 0;
    var head: int = 0;
    while (head < queue.size()) {
        let v = queue.at(head);
        head += 1;
        let verdict = watcher.on_discover(v);
        if (verdict == VisitAction.StopTraversal) {
            out.complete = false;
            return out;
        }
        if (verdict == VisitAction.SkipChildren) {
            continue;
        }
        out.order.push_back(v);
        for (e in g.vertices.at(v).incident) {
            let ref arc: auto = g.edges.at(e);
            if (arc.alive == false) {
                continue;
            }
            let next = arc.head;
            if (out.depth.at(next) >= GRAPH_INFINITY) {
                Discard(watcher.on_edge(e, EdgeClass.TreeEdge));
                out.depth.at(next) = out.depth.at(v) + 1;
                out.parent.at(next) = v;
                queue.push_back(next);
            } else {
                Discard(watcher.on_edge(e, EdgeClass.CrossEdge));
            }
        }
        Discard(watcher.on_finish(v));
    }
    return out;
}

// Multi-source BFS: every source starts at depth zero.  Useful for "distance
// to the nearest hospital" style queries.
fn bfs_multi[V: type, W: type]
    (g: AdjacencyList(V, W),
             sources: vector(int)) -> SearchResult(V) {
    var out = SearchResult(V)({.source = GRAPH_NIL});
    fill_int_vector(out.parent, g.order(), GRAPH_NIL);
    fill_int_vector(out.depth, g.order(), GRAPH_INFINITY);
    var queue = vector(int)();
    for (s in sources) {
        if (out.depth.at(s) >= GRAPH_INFINITY) {
            out.depth.at(s) = 0;
            queue.push_back(s);
        }
    }
    var head: int = 0;
    while (head < queue.size()) {
        let v = queue.at(head);
        head += 1;
        out.order.push_back(v);
        for (e in g.vertices.at(v).incident) {
            let ref arc: auto = g.edges.at(e);
            if (arc.alive == false) {
                continue;
            }
            if (out.depth.at(arc.head) >= GRAPH_INFINITY) {
                out.depth.at(arc.head) = out.depth.at(v) + 1;
                out.parent.at(arc.head) = v;
                queue.push_back(arc.head);
            }
        }
    }
    return out;
}

// Group the vertices by their BFS depth.  Slot `i` of the result holds every
// vertex at distance exactly `i` from the source.
fn bfs_levels[V: type, W: type]
    (g: AdjacencyList(V, W),
              source: int) -> vector(vector(int)) {
    let found = bfs(V, W)(g, source);
    var levels = vector(vector(int))();
    let reach = found.eccentricity();
    var d: int = 0;
    while (d <= reach) {
        levels.push_back(vector(int)());
        d += 1;
    }
    var v: int = 0;
    while (v < g.order()) {
        let depth = found.depth.at(v);
        if (depth < GRAPH_INFINITY) {
            levels.at(depth).push_back(v);
        }
        v += 1;
    }
    return levels;
}

// Depth first search, expressed with an explicit stack of (vertex, cursor)
// pairs so that `on_finish` still fires in true post-order.
fn dfs[V: type, W: type]
    (g: AdjacencyList(V, W), source: int) -> SearchResult(V) {
    var out = SearchResult(V)({.source = source});
    fill_int_vector(out.parent, g.order(), GRAPH_NIL);
    fill_int_vector(out.depth, g.order(), GRAPH_INFINITY);
    var stack = vector(int)();
    var cursor = vector(int)();
    stack.push_back(source);
    cursor.push_back(0);
    out.depth.at(source) = 0;
    out.order.push_back(source);
    while (stack.empty() == false) {
        let v = stack.back();
        let at = cursor.back();
        let ref incident: auto = g.vertices.at(v).incident;
        if (at >= incident.size()) {
            stack.pop_back();
            cursor.pop_back();
            continue;
        }
        cursor.at(cursor.size() - 1) = at + 1;
        let ref arc: auto = g.edges.at(incident.at(at));
        if (arc.alive == false) {
            continue;
        }
        let next = arc.head;
        if (out.depth.at(next) >= GRAPH_INFINITY) {
            out.depth.at(next) = out.depth.at(v) + 1;
            out.parent.at(next) = v;
            out.order.push_back(next);
            stack.push_back(next);
            cursor.push_back(0);
        }
    }
    return out;
}

// Depth first search driving a visitor, with proper edge classification.
//
// An arc into a gray vertex is a back edge (and therefore evidence of a
// cycle); an arc into a black vertex is forward or cross depending on the
// discovery stamps.
fn dfs_visit[V: type, W: type]
    (g: AdjacencyList(V, W),
             source: int,
             ref watcher: Visitor(V, W)) -> SearchResult(V) {
    var out = SearchResult(V)({.source = source});
    fill_int_vector(out.parent, g.order(), GRAPH_NIL);
    fill_int_vector(out.depth, g.order(), GRAPH_INFINITY);
    var color = vector(int)();
    var entered = vector(int)();
    fill_int_vector(color, g.order(), 0);
    fill_int_vector(entered, g.order(), GRAPH_NIL);
    var stack = vector(int)();
    var cursor = vector(int)();
    var clock: int = 0;
    stack.push_back(source);
    cursor.push_back(0);
    color.at(source) = 1;
    entered.at(source) = clock;
    clock += 1;
    out.depth.at(source) = 0;
    Discard(watcher.on_discover(source));
    while (stack.empty() == false) {
        let v = stack.back();
        let at = cursor.back();
        let ref incident: auto = g.vertices.at(v).incident;
        if (at >= incident.size()) {
            color.at(v) = 2;
            Discard(watcher.on_finish(v));
            stack.pop_back();
            cursor.pop_back();
            continue;
        }
        cursor.at(cursor.size() - 1) = at + 1;
        let e = incident.at(at);
        let ref arc: auto = g.edges.at(e);
        if (arc.alive == false) {
            continue;
        }
        let next = arc.head;
        if (color.at(next) == 0) {
            Discard(watcher.on_edge(e, EdgeClass.TreeEdge));
            color.at(next) = 1;
            entered.at(next) = clock;
            clock += 1;
            out.parent.at(next) = v;
            out.depth.at(next) = out.depth.at(v) + 1;
            let verdict = watcher.on_discover(next);
            if (verdict == VisitAction.StopTraversal) {
                out.complete = false;
                return out;
            }
            if (verdict == VisitAction.Continue) {
                stack.push_back(next);
                cursor.push_back(0);
            }
        } else {
            if (color.at(next) == 1) {
                Discard(watcher.on_edge(e, EdgeClass.BackEdge));
            } else {
                if (entered.at(v) < entered.at(next)) {
                    Discard(watcher.on_edge(e, EdgeClass.ForwardEdge));
                } else {
                    Discard(watcher.on_edge(e, EdgeClass.CrossEdge));
                }
            }
        }
    }
    return out;
}

// Run a depth first search from every undiscovered vertex, producing the
// full DFS forest and restarting the visitor's root list as it goes.
fn dfs_forest[V: type, W: type]
    (g: AdjacencyList(V, W),
              ref watcher: Visitor(V, W)) -> vector(int) {
    var parent = vector(int)();
    fill_int_vector(parent, g.order(), GRAPH_NIL);
    var seen = vector(bool)();
    fill_bool_vector(seen, g.order(), false);
    watcher.prepare(g.order());
    var v: int = 0;
    while (v < g.order()) {
        if (seen.at(v) == false) {
            watcher.on_restart(v);
            let tree = dfs(V, W)(g, v);
            for (u in tree.order) {
                if (seen.at(u) == false) {
                    seen.at(u) = true;
                    parent.at(u) = tree.parent.at(u);
                }
            }
        }
        v += 1;
    }
    return parent;
}

// Post-order finishing times over the whole graph, the ingredient Kosaraju
// and the DFS topological sort both need.
fn finish_order[V: type, W: type]
    (g: AdjacencyList(V, W)) -> vector(int) {
    var out = vector(int)();
    var color = vector(int)();
    fill_int_vector(color, g.order(), 0);
    var stack = vector(int)();
    var cursor = vector(int)();
    var root: int = 0;
    while (root < g.order()) {
        if (color.at(root) == 0) {
            stack.push_back(root);
            cursor.push_back(0);
            color.at(root) = 1;
            while (stack.empty() == false) {
                let v = stack.back();
                let at = cursor.back();
                let ref incident: auto = g.vertices.at(v).incident;
                if (at >= incident.size()) {
                    color.at(v) = 2;
                    out.push_back(v);
                    stack.pop_back();
                    cursor.pop_back();
                    continue;
                }
                cursor.at(cursor.size() - 1) = at + 1;
                let ref arc: auto = g.edges.at(incident.at(at));
                if (arc.alive and color.at(arc.head) == 0) {
                    color.at(arc.head) = 1;
                    stack.push_back(arc.head);
                    cursor.push_back(0);
                }
            }
        }
        root += 1;
    }
    return out;
}

// Every vertex reachable from `source`, as a set.
fn reachable_from[V: type, W: type]
    (g: AdjacencyList(V, W), source: int) -> set(int) {
    let found = bfs(V, W)(g, source);
    var out = set(int)();
    var v: int = 0;
    while (v < g.order()) {
        if (found.depth.at(v) < GRAPH_INFINITY) {
            Discard(out.insert(v));
        }
        v += 1;
    }
    return out;
}

// Shortest *hop* count between two vertices, or `GRAPH_INFINITY`.
fn hop_distance[V: type, W: type]
    (g: AdjacencyList(V, W),
                source: int,
                target: int) -> int {
    let found = bfs(V, W)(g, source);
    return found.depth.at(target);
}

// The vertex sequence of one shortest hop path, empty when unreachable.
fn hop_path[V: type, W: type]
    (g: AdjacencyList(V, W),
            source: int,
            target: int) -> vector(int) {
    let found = bfs(V, W)(g, source);
    return found.path_to(target);
}



// ==========================================================================
// graph::connectivity
//
// Everything that answers a question about how the graph hangs together:
// components, orderings, cycles, cut vertices and two-colourings.
// ==========================================================================
namespace connectivity;

// The labelling produced by a component decomposition.
class Components(V: type)
{

    fn count(self) -> int {
        return self.total;
    }

    fn same(self, a: int, b: int) -> bool {
        return self.label.at(a) == self.label.at(b);
    }

    fn size_of(self, component: int) -> int {
        var total: int = 0;
        for (l in self.label) {
            if (l == component) {
                total += 1;
            }
        }
        return total;
    }

    fn largest(self) -> int {
        var best: int = 0;
        var best_size: int = 0;
        var c: int = 0;
        while (c < self.total) {
            let n = self.size_of(c);
            if (n > best_size) {
                best_size = n;
                best = c;
            }
            c += 1;
        }
        return best;
    }

    // Regroup the flat labelling into one vector of members per component.
    fn buckets(self) -> vector(vector(int)) {
        var out = vector(vector(int))();
        var c: int = 0;
        while (c < self.total) {
            out.push_back(vector(int)());
            c += 1;
        }
        var v: int = 0;
        while (v < self.label.size()) {
            let l = self.label.at(v);
            if (l != GRAPH_NIL) {
                out.at(l).push_back(v);
            }
            v += 1;
        }
        return out;
    }

    var label: vector(int);
    var payload: V;
    var total: int = 0;
}

// Undirected connected components, found with repeated breadth first search.
fn connected_components[V: type, W: type]
    (g: AdjacencyList(V, W)) -> Components(V) {
    var out = Components(V)();
    fill_int_vector(out.label, g.order(), GRAPH_NIL);
    var next: int = 0;
    var root: int = 0;
    while (root < g.order()) {
        if (out.label.at(root) == GRAPH_NIL) {
            var queue = vector(int)();
            queue.push_back(root);
            out.label.at(root) = next;
            var head: int = 0;
            while (head < queue.size()) {
                let v = queue.at(head);
                head += 1;
                for (e in g.vertices.at(v).incident) {
                    let ref arc: auto = g.edges.at(e);
                    if (arc.alive and out.label.at(arc.head) == GRAPH_NIL) {
                        out.label.at(arc.head) = next;
                        queue.push_back(arc.head);
                    }
                }
            }
            next += 1;
        }
        root += 1;
    }
    out.total = next;
    return out;
}

// The same decomposition, computed with a union-find instead.  Cheaper when
// the caller already has to merge sets for another reason.
fn components_by_union[V: type, W: type]
    (g: AdjacencyList(V, W)) -> Components(V) {
    var forest = UnionFind();
    forest.prepare(g.order());
    for (arc in g.edges) {
        if (arc.alive) {
            Discard(forest.unite(arc.tail, arc.head));
        }
    }
    var out = Components(V)();
    fill_int_vector(out.label, g.order(), GRAPH_NIL);
    var next: int = 0;
    var v: int = 0;
    while (v < g.order()) {
        let root = forest.find(v);
        if (out.label.at(root) == GRAPH_NIL) {
            out.label.at(root) = next;
            next += 1;
        }
        out.label.at(v) = out.label.at(root);
        v += 1;
    }
    out.total = next;
    return out;
}

fn component_count[V: type, W: type]
    (g: AdjacencyList(V, W)) -> int {
    let found = connected_components(V, W)(g);
    return found.total;
}

fn is_connected[V: type, W: type]
    (g: AdjacencyList(V, W)) -> bool {
    if (g.order() == 0) {
        return true;
    }
    return component_count(V, W)(g) == 1;
}

// A tree is a connected acyclic graph, i.e. connected with exactly n-1 edges.
fn is_tree[V: type, W: type]
    (g: AdjacencyList(V, W)) -> bool {
    if (is_connected(V, W)(g) == false) {
        return false;
    }
    return g.size() == g.order() - 1;
}

// --- Topological ordering -------------------------------------------------

// The answer to an ordering request.
class Ordering(V: type)
{

    fn valid(self) -> bool {
        return self.status == OrderStatus.TotalOrder;
    }

    fn length(self) -> int {
        return self.sequence.size();
    }

    // Position of each vertex in the order, or NIL when it was never placed.
    fn ranks(self, order: int) -> vector(int) {
        var out = vector(int)();
        fill_int_vector(out, order, GRAPH_NIL);
        var i: int = 0;
        while (i < self.sequence.size()) {
            out.at(self.sequence.at(i)) = i;
            i += 1;
        }
        return out;
    }

    var sequence: vector(int);
    var payload: V;
    var status: OrderStatus = OrderStatus.TotalOrder;
}

// Kahn's algorithm: repeatedly emit a vertex of in-degree zero.
//
// When the queue empties before every vertex has been emitted, the leftovers
// all sit on cycles and the status is set accordingly.
fn topological_sort_kahn[V: type, W: type]
    (g: AdjacencyList(V, W)) -> Ordering(V) {
    var out = Ordering(V)();
    var indegree = vector(int)();
    fill_int_vector(indegree, g.order(), 0);
    for (arc in g.edges) {
        if (arc.alive) {
            indegree.at(arc.head) += 1;
        }
    }
    var queue = vector(int)();
    var v: int = 0;
    while (v < g.order()) {
        if (indegree.at(v) == 0) {
            queue.push_back(v);
        }
        v += 1;
    }
    var head: int = 0;
    while (head < queue.size()) {
        let u = queue.at(head);
        head += 1;
        out.sequence.push_back(u);
        for (e in g.vertices.at(u).incident) {
            let ref arc: auto = g.edges.at(e);
            if (arc.alive == false) {
                continue;
            }
            indegree.at(arc.head) -= 1;
            if (indegree.at(arc.head) == 0) {
                queue.push_back(arc.head);
            }
        }
    }
    if (out.sequence.size() != g.order()) {
        out.status = OrderStatus.CycleFound;
    }
    return out;
}

// The lexicographically smallest topological order, obtained by feeding
// Kahn's frontier through a min-heap instead of a plain queue.
fn topological_sort_smallest[V: type, W: type]
    (g: AdjacencyList(V, W)) -> Ordering(V) {
    var out = Ordering(V)();
    var indegree = vector(int)();
    fill_int_vector(indegree, g.order(), 0);
    for (arc in g.edges) {
        if (arc.alive) {
            indegree.at(arc.head) += 1;
        }
    }
    var frontier = BinaryHeap();
    frontier.prepare(g.order());
    var v: int = 0;
    while (v < g.order()) {
        if (indegree.at(v) == 0) {
            frontier.push(v, v);
        }
        v += 1;
    }
    while (frontier.empty() == false) {
        let top = frontier.pop();
        out.sequence.push_back(top.vertex);
        for (e in g.vertices.at(top.vertex).incident) {
            let ref arc: auto = g.edges.at(e);
            if (arc.alive == false) {
                continue;
            }
            indegree.at(arc.head) -= 1;
            if (indegree.at(arc.head) == 0) {
                frontier.push(arc.head, arc.head);
            }
        }
    }
    if (out.sequence.size() != g.order()) {
        out.status = OrderStatus.CycleFound;
    }
    return out;
}

// The depth first variant: reverse post-order finishing times.
//
// Cheaper than Kahn by a constant factor because no in-degree array is built,
// but it cannot report *which* vertices form the cycle, only that one exists.
fn topological_sort_dfs[V: type, W: type]
    (g: AdjacencyList(V, W)) -> Ordering(V) {
    var out = Ordering(V)();
    let finished = traversal.finish_order(V, W)(g);
    var i = finished.size() - 1;
    while (i >= 0) {
        out.sequence.push_back(finished.at(i));
        i -= 1;
    }
    if (has_directed_cycle(V, W)(g)) {
        out.status = OrderStatus.CycleFound;
    }
    return out;
}

// --- Cycles ---------------------------------------------------------------

// Directed cycle detection via three-colour depth first search.
fn has_directed_cycle[V: type, W: type]
    (g: AdjacencyList(V, W)) -> bool {
    var color = vector(int)();
    fill_int_vector(color, g.order(), 0);
    var stack = vector(int)();
    var cursor = vector(int)();
    var root: int = 0;
    while (root < g.order()) {
        if (color.at(root) == 0) {
            stack.push_back(root);
            cursor.push_back(0);
            color.at(root) = 1;
            while (stack.empty() == false) {
                let v = stack.back();
                let at = cursor.back();
                let ref incident: auto = g.vertices.at(v).incident;
                if (at >= incident.size()) {
                    color.at(v) = 2;
                    stack.pop_back();
                    cursor.pop_back();
                    continue;
                }
                cursor.at(cursor.size() - 1) = at + 1;
                let ref arc: auto = g.edges.at(incident.at(at));
                if (arc.alive == false) {
                    continue;
                }
                if (color.at(arc.head) == 1) {
                    return true;
                }
                if (color.at(arc.head) == 0) {
                    color.at(arc.head) = 1;
                    stack.push_back(arc.head);
                    cursor.push_back(0);
                }
            }
        }
        root += 1;
    }
    return false;
}

// Undirected cycle detection.  A union-find is the shortest correct answer:
// the first edge whose endpoints already share a root closes a cycle.
fn has_undirected_cycle[V: type, W: type]
    (g: AdjacencyList(V, W)) -> bool {
    var forest = UnionFind();
    forest.prepare(g.order());
    for (arc in g.edges) {
        if (arc.alive and arc.tail < arc.head) {
            if (forest.unite(arc.tail, arc.head) == false) {
                return true;
            }
        }
    }
    return false;
}

// Recover one directed cycle as a vertex sequence, or an empty vector.
fn find_directed_cycle[V: type, W: type]
    (g: AdjacencyList(V, W)) -> vector(int) {
    var color = vector(int)();
    var parent = vector(int)();
    fill_int_vector(color, g.order(), 0);
    fill_int_vector(parent, g.order(), GRAPH_NIL);
    var found = vector(int)();
    var stack = vector(int)();
    var cursor = vector(int)();
    var root: int = 0;
    while (root < g.order()) {
        if (color.at(root) == 0) {
            stack.push_back(root);
            cursor.push_back(0);
            color.at(root) = 1;
            while (stack.empty() == false) {
                let v = stack.back();
                let at = cursor.back();
                let ref incident: auto = g.vertices.at(v).incident;
                if (at >= incident.size()) {
                    color.at(v) = 2;
                    stack.pop_back();
                    cursor.pop_back();
                    continue;
                }
                cursor.at(cursor.size() - 1) = at + 1;
                let ref arc: auto = g.edges.at(incident.at(at));
                if (arc.alive == false) {
                    continue;
                }
                if (color.at(arc.head) == 1) {
                    var walker = v;
                    while (walker != arc.head and walker != GRAPH_NIL) {
                        found.push_back(walker);
                        walker = parent.at(walker);
                    }
                    found.push_back(arc.head);
                    reverse_int_vector(found);
                    return found;
                }
                if (color.at(arc.head) == 0) {
                    color.at(arc.head) = 1;
                    parent.at(arc.head) = v;
                    stack.push_back(arc.head);
                    cursor.push_back(0);
                }
            }
        }
        root += 1;
    }
    return found;
}

fn is_acyclic[V: type, W: type]
    (g: AdjacencyList(V, W)) -> bool {
    if (g.is_directed()) {
        return has_directed_cycle(V, W)(g) == false;
    }
    return has_undirected_cycle(V, W)(g) == false;
}

// --- Strongly connected components ----------------------------------------

// Scratch state for the iterative Tarjan pass.
//
// Tarjan is normally written recursively; the loop below keeps the call frame
// in `frames` and `cursor` so that a million-vertex graph does not need a
// million native stack frames.
class TarjanState {

    fn prepare(ref self, order: int) {
        fill_int_vector(self.index, order, GRAPH_NIL);
        fill_int_vector(self.low, order, GRAPH_NIL);
        fill_bool_vector(self.on_stack, order, false);
        self.pending.clear();
        self.frames.clear();
        self.cursor.clear();
        self.clock = 0;
        self.total = 0;
    }

    fn visited(self, v: int) -> bool {
        return self.index.at(v) != GRAPH_NIL;
    }

    fn enter(ref self, v: int) {
        self.index.at(v) = self.clock;
        self.low.at(v) = self.clock;
        self.clock += 1;
        self.pending.push_back(v);
        self.on_stack.at(v) = true;
    }

    // Pop one component off the pending stack, down to and including `root`.
    fn close(ref self, root: int, ref label: vector(int)) -> int {
        var members: int = 0;
        while (true) {
            let top = self.pending.back();
            self.pending.pop_back();
            self.on_stack.at(top) = false;
            label.at(top) = self.total;
            members += 1;
            if (top == root) {
                break;
            }
        }
        self.total += 1;
        return members;
    }

    var index: vector(int);
    var low: vector(int);
    var on_stack: vector(bool);
    var pending: vector(int);
    var frames: vector(int);
    var cursor: vector(int);
    var clock: int = 0;
    var total: int = 0;
}

// Tarjan's strongly connected components, one depth first pass.
//
// The component labels come out in reverse topological order of the
// condensation, which is exactly what `condensation` relies on.
fn tarjan_scc[V: type, W: type]
    (g: AdjacencyList(V, W)) -> Components(V) {
    var out = Components(V)();
    fill_int_vector(out.label, g.order(), GRAPH_NIL);
    var state = TarjanState();
    state.prepare(g.order());
    var root: int = 0;
    while (root < g.order()) {
        if (state.visited(root) == false) {
            state.enter(root);
            state.frames.push_back(root);
            state.cursor.push_back(0);
            while (state.frames.empty() == false) {
                let v = state.frames.back();
                let at = state.cursor.back();
                let ref incident: auto = g.vertices.at(v).incident;
                if (at < incident.size()) {
                    state.cursor.at(state.cursor.size() - 1) = at + 1;
                    let ref arc: auto = g.edges.at(incident.at(at));
                    if (arc.alive == false) {
                        continue;
                    }
                    let next = arc.head;
                    if (state.visited(next) == false) {
                        state.enter(next);
                        state.frames.push_back(next);
                        state.cursor.push_back(0);
                    } else {
                        if (state.on_stack.at(next)) {
                            if (state.index.at(next) < state.low.at(v)) {
                                state.low.at(v) = state.index.at(next);
                            }
                        }
                    }
                } else {
                    state.frames.pop_back();
                    state.cursor.pop_back();
                    if (state.low.at(v) == state.index.at(v)) {
                        Discard(state.close(v, out.label));
                    }
                    if (state.frames.empty() == false) {
                        let up = state.frames.back();
                        if (state.low.at(v) < state.low.at(up)) {
                            state.low.at(up) = state.low.at(v);
                        }
                    }
                }
            }
        }
        root += 1;
    }
    out.total = state.total;
    return out;
}

// Kosaraju's two-pass alternative: finishing times on `g`, then components on
// the transpose visited in decreasing finishing time.
//
// Slower than Tarjan by a constant factor because it builds the transpose,
// but far easier to convince yourself of.
fn kosaraju_scc[V: type, W: type]
    (g: AdjacencyList(V, W)) -> Components(V) {
    let finished = traversal.finish_order(V, W)(g);
    let reversed = builders.transpose(V, W)(g);
    var out = Components(V)();
    fill_int_vector(out.label, g.order(), GRAPH_NIL);
    var next: int = 0;
    var i = finished.size() - 1;
    while (i >= 0) {
        let root = finished.at(i);
        i -= 1;
        if (out.label.at(root) != GRAPH_NIL) {
            continue;
        }
        var stack = vector(int)();
        stack.push_back(root);
        out.label.at(root) = next;
        while (stack.empty() == false) {
            let v = stack.back();
            stack.pop_back();
            for (e in reversed.vertices.at(v).incident) {
                let ref arc: auto = reversed.edges.at(e);
                if (arc.alive and out.label.at(arc.head) == GRAPH_NIL) {
                    out.label.at(arc.head) = next;
                    stack.push_back(arc.head);
                }
            }
        }
        next += 1;
    }
    out.total = next;
    return out;
}

// Collapse every strongly connected component to a single vertex.  The result
// is always a directed acyclic graph.
fn condensation[V: type, W: type]
    (g: AdjacencyList(V, W),
                parts: Components(V)) -> AdjacencyList(V, W) {
    var out = builders.directed(V, W)(parts.total);
    for (arc in g.edges) {
        if (arc.alive == false) {
            continue;
        }
        let a = parts.label.at(arc.tail);
        let b = parts.label.at(arc.head);
        if (a != b) {
            if (out.has_edge(a, b) == false) {
                Discard(out.connect(a, b, arc.weight));
            }
        }
    }
    Assert(is_acyclic(V, W)(out));
    return out;
}

fn is_strongly_connected[V: type, W: type]
    (g: AdjacencyList(V, W)) -> bool {
    if (g.order() == 0) {
        return true;
    }
    let parts = tarjan_scc(V, W)(g);
    return parts.total == 1;
}

// --- Bridges and articulation points --------------------------------------

// The output of one low-link pass over an undirected graph.
class CutInfo(V: type)
{

    fn bridge_count(self) -> int {
        return self.bridges.size();
    }

    fn is_articulation(self, v: int) -> bool {
        return self.articulation.at(v);
    }

    fn articulation_list(self) -> vector(int) {
        var out = vector(int)();
        var v: int = 0;
        while (v < self.articulation.size()) {
            if (self.articulation.at(v)) {
                out.push_back(v);
            }
            v += 1;
        }
        return out;
    }

    var bridges: vector(int);
    var articulation: vector(bool);
    var low: vector(int);
    var discovery: vector(int);
    var payload: V;
}

// Find every bridge and every articulation point in one iterative low-link
// depth first search.
//
// An arc (u, v) is a bridge when `low[v] > discovery[u]`; `u` is an
// articulation point when it is the root with two or more children, or a
// non-root with a child satisfying `low[v] >= discovery[u]`.
fn find_cuts[V: type, W: type]
    (g: AdjacencyList(V, W)) -> CutInfo(V) {
    var out = CutInfo(V)();
    fill_int_vector(out.low, g.order(), GRAPH_NIL);
    fill_int_vector(out.discovery, g.order(), GRAPH_NIL);
    fill_bool_vector(out.articulation, g.order(), false);
    var parent_arc = vector(int)();
    fill_int_vector(parent_arc, g.order(), GRAPH_NIL);
    var children = vector(int)();
    fill_int_vector(children, g.order(), 0);
    var stack = vector(int)();
    var cursor = vector(int)();
    var clock: int = 0;
    var root: int = 0;
    while (root < g.order()) {
        if (out.discovery.at(root) != GRAPH_NIL) {
            root += 1;
            continue;
        }
        out.discovery.at(root) = clock;
        out.low.at(root) = clock;
        clock += 1;
        stack.push_back(root);
        cursor.push_back(0);
        while (stack.empty() == false) {
            let v = stack.back();
            let at = cursor.back();
            let ref incident: auto = g.vertices.at(v).incident;
            if (at < incident.size()) {
                cursor.at(cursor.size() - 1) = at + 1;
                let e = incident.at(at);
                let ref arc: auto = g.edges.at(e);
                if (arc.alive == false) {
                    continue;
                }
                if (e == parent_arc.at(v) ^ 1 and parent_arc.at(v) != GRAPH_NIL) {
                    continue;
                }
                let next = arc.head;
                if (out.discovery.at(next) == GRAPH_NIL) {
                    parent_arc.at(next) = e;
                    children.at(v) += 1;
                    out.discovery.at(next) = clock;
                    out.low.at(next) = clock;
                    clock += 1;
                    stack.push_back(next);
                    cursor.push_back(0);
                } else {
                    if (out.discovery.at(next) < out.low.at(v)) {
                        out.low.at(v) = out.discovery.at(next);
                    }
                }
            } else {
                stack.pop_back();
                cursor.pop_back();
                if (stack.empty() == false) {
                    let up = stack.back();
                    if (out.low.at(v) < out.low.at(up)) {
                        out.low.at(up) = out.low.at(v);
                    }
                    if (out.low.at(v) > out.discovery.at(up)) {
                        out.bridges.push_back(parent_arc.at(v));
                    }
                    if (up != root and out.low.at(v) >= out.discovery.at(up)) {
                        out.articulation.at(up) = true;
                    }
                }
            }
        }
        if (children.at(root) > 1) {
            out.articulation.at(root) = true;
        }
        root += 1;
    }
    return out;
}

fn bridges[V: type, W: type]
    (g: AdjacencyList(V, W)) -> vector(int) {
    let cuts = find_cuts(V, W)(g);
    return cuts.bridges;
}

fn articulation_points[V: type, W: type]
    (g: AdjacencyList(V, W)) -> vector(int) {
    let cuts = find_cuts(V, W)(g);
    return cuts.articulation_list();
}

// A graph is two-edge-connected when it is connected and has no bridge.
fn is_two_edge_connected[V: type, W: type]
    (g: AdjacencyList(V, W)) -> bool {
    if (is_connected(V, W)(g) == false) {
        return false;
    }
    let cuts = find_cuts(V, W)(g);
    return cuts.bridges.empty();
}

// --- Bipartiteness --------------------------------------------------------

// Try to two-colour the graph.  On success every vertex carries a side; on
// failure `sides` is left partially filled and the witness edge is recorded.
class Coloring(V: type)
{

    fn bipartite(self) -> bool {
        return self.conflict == GRAPH_NIL;
    }

    fn side_of(self, v: int) -> Side {
        return self.sides.at(v);
    }

    fn left_vertices(self) -> vector(int) {
        var out = vector(int)();
        var v: int = 0;
        while (v < self.sides.size()) {
            if (self.sides.at(v) == Side.LeftSide) {
                out.push_back(v);
            }
            v += 1;
        }
        return out;
    }

    fn right_vertices(self) -> vector(int) {
        var out = vector(int)();
        var v: int = 0;
        while (v < self.sides.size()) {
            if (self.sides.at(v) == Side.RightSide) {
                out.push_back(v);
            }
            v += 1;
        }
        return out;
    }

    var sides: vector(Side);
    var payload: V;
    var conflict: int = -1;
}

// Breadth first two-colouring, one component at a time.
fn two_coloring[V: type, W: type]
    (g: AdjacencyList(V, W)) -> Coloring(V) {
    var out = Coloring(V)();
    out.sides.clear();
    var i: int = 0;
    while (i < g.order()) {
        out.sides.push_back(Side.Unassigned);
        i += 1;
    }
    var root: int = 0;
    while (root < g.order()) {
        if (out.sides.at(root) != Side.Unassigned) {
            root += 1;
            continue;
        }
        out.sides.at(root) = Side.LeftSide;
        var queue = vector(int)();
        queue.push_back(root);
        var head: int = 0;
        while (head < queue.size()) {
            let v = queue.at(head);
            head += 1;
            for (e in g.vertices.at(v).incident) {
                let ref arc: auto = g.edges.at(e);
                if (arc.alive == false) {
                    continue;
                }
                let next = arc.head;
                if (out.sides.at(next) == Side.Unassigned) {
                    if (out.sides.at(v) == Side.LeftSide) {
                        out.sides.at(next) = Side.RightSide;
                    } else {
                        out.sides.at(next) = Side.LeftSide;
                    }
                    queue.push_back(next);
                } else {
                    if (out.sides.at(next) == out.sides.at(v)) {
                        out.conflict = e;
                        return out;
                    }
                }
            }
        }
        root += 1;
    }
    return out;
}

fn is_bipartite[V: type, W: type]
    (g: AdjacencyList(V, W)) -> bool {
    let colors = two_coloring(V, W)(g);
    return colors.bipartite();
}

// An odd cycle, which exists exactly when the graph is not bipartite.
fn odd_cycle_witness[V: type, W: type]
    (g: AdjacencyList(V, W)) -> vector(int) {
    let colors = two_coloring(V, W)(g);
    var out = vector(int)();
    if (colors.bipartite()) {
        return out;
    }
    let ref arc: auto = g.edges.at(colors.conflict);
    let left = traversal.hop_path(V, W)(g, arc.tail, arc.head);
    for (v in left) {
        out.push_back(v);
    }
    return out;
}



// ==========================================================================
// graph::shortest_path
//
// Single source and all pairs shortest paths.  Every distance in this
// namespace is a fixed point value scaled by `WEIGHT_SCALE`, and
// `GRAPH_INFINITY` means "no path".
// ==========================================================================
namespace shortest_path;

// The result of a single source computation.
class Distances(V: type)
{

    fn reachable(self, v: int) -> bool {
        return self.dist.at(v) < GRAPH_INFINITY;
    }

    fn to(self, v: int) -> int {
        return self.dist.at(v);
    }

    // The vertex sequence of one optimal path, empty when unreachable.
    fn path_to(self, v: int) -> vector(int) {
        var out = vector(int)();
        if (self.dist.at(v) >= GRAPH_INFINITY) {
            return out;
        }
        var walker = v;
        while (walker != GRAPH_NIL) {
            out.push_back(walker);
            walker = self.parent.at(walker);
        }
        reverse_int_vector(out);
        return out;
    }

    // The arc ids along that path, which is what a router actually wants.
    fn arcs_to(self, v: int) -> vector(int) {
        var out = vector(int)();
        var walker = v;
        while (walker != GRAPH_NIL and self.parent_arc.at(walker) != GRAPH_NIL) {
            out.push_back(self.parent_arc.at(walker));
            walker = self.parent.at(walker);
        }
        reverse_int_vector(out);
        return out;
    }

    fn farthest(self) -> int {
        var best = GRAPH_NIL;
        var best_dist: int = -1;
        var v: int = 0;
        while (v < self.dist.size()) {
            let d = self.dist.at(v);
            if (d < GRAPH_INFINITY and d > best_dist) {
                best_dist = d;
                best = v;
            }
            v += 1;
        }
        return best;
    }

    fn total_reachable(self) -> int {
        var total: int = 0;
        for (d in self.dist) {
            if (d < GRAPH_INFINITY) {
                total += 1;
            }
        }
        return total;
    }

    var dist: vector(int);
    var parent: vector(int);
    var parent_arc: vector(int);
    var payload: V;
    var source: int = -1;
    var negative_cycle: bool = false;
    var relaxations: int = 0;
}

// Dijkstra's algorithm over the tracked binary heap.
//
// Requires every weight to be non-negative; `bellman_ford` handles the rest.
// The `decrease_key` form is used rather than the lazy "push duplicates"
// form, so the heap never grows past `order` entries.
fn dijkstra[V: type, W: type]
    (g: AdjacencyList(V, W), source: int) -> Distances(V) {
    var out = Distances(V)({.source = source});
    fill_int_vector(out.dist, g.order(), GRAPH_INFINITY);
    fill_int_vector(out.parent, g.order(), GRAPH_NIL);
    fill_int_vector(out.parent_arc, g.order(), GRAPH_NIL);
    if (g.valid_vertex(source) == false) {
        return out;
    }
    var settled = vector(bool)();
    fill_bool_vector(settled, g.order(), false);
    var frontier = BinaryHeap();
    frontier.prepare(g.order());
    out.dist.at(source) = 0;
    frontier.push(source, 0);
    while (frontier.empty() == false) {
        let top = frontier.pop();
        let v = top.vertex;
        if (settled.at(v)) {
            continue;
        }
        settled.at(v) = true;
        for (e in g.vertices.at(v).incident) {
            let ref arc: auto = g.edges.at(e);
            if (arc.alive == false) {
                continue;
            }
            Assert(arc.weight >= 0);
            let candidate = add_distance(out.dist.at(v), arc.weight);
            if (candidate < out.dist.at(arc.head)) {
                out.dist.at(arc.head) = candidate;
                out.parent.at(arc.head) = v;
                out.parent_arc.at(arc.head) = e;
                out.relaxations += 1;
                Discard(frontier.decrease_key(arc.head, candidate));
            }
        }
    }
    Prove(out.dist.at(source) == 0);
    return out;
}

// Dijkstra that stops as soon as `target` is settled.  On a large graph with
// a nearby target this is the difference between milliseconds and seconds.
fn dijkstra_to[V: type, W: type]
    (g: AdjacencyList(V, W),
               source: int,
               target: int) -> Distances(V) {
    var out = Distances(V)({.source = source});
    fill_int_vector(out.dist, g.order(), GRAPH_INFINITY);
    fill_int_vector(out.parent, g.order(), GRAPH_NIL);
    fill_int_vector(out.parent_arc, g.order(), GRAPH_NIL);
    var settled = vector(bool)();
    fill_bool_vector(settled, g.order(), false);
    var frontier = BinaryHeap();
    frontier.prepare(g.order());
    out.dist.at(source) = 0;
    frontier.push(source, 0);
    while (frontier.empty() == false) {
        let top = frontier.pop();
        let v = top.vertex;
        if (v == target) {
            return out;
        }
        if (settled.at(v)) {
            continue;
        }
        settled.at(v) = true;
        for (e in g.vertices.at(v).incident) {
            let ref arc: auto = g.edges.at(e);
            if (arc.alive == false) {
                continue;
            }
            let candidate = add_distance(out.dist.at(v), arc.weight);
            if (candidate < out.dist.at(arc.head)) {
                out.dist.at(arc.head) = candidate;
                out.parent.at(arc.head) = v;
                out.parent_arc.at(arc.head) = e;
                Discard(frontier.decrease_key(arc.head, candidate));
            }
        }
    }
    return out;
}

// The cheapest path between two vertices as a vertex sequence.
fn shortest_route[V: type, W: type]
    (g: AdjacencyList(V, W),
                  source: int,
                  target: int) -> vector(int) {
    let found = dijkstra_to(V, W)(g, source, target);
    return found.path_to(target);
}

// Bellman-Ford: `order - 1` full relaxation rounds, then one more to detect a
// negative cycle.  Handles negative weights, unlike Dijkstra.
fn bellman_ford[V: type, W: type]
    (g: AdjacencyList(V, W), source: int) -> Distances(V) {
    var out = Distances(V)({.source = source});
    fill_int_vector(out.dist, g.order(), GRAPH_INFINITY);
    fill_int_vector(out.parent, g.order(), GRAPH_NIL);
    fill_int_vector(out.parent_arc, g.order(), GRAPH_NIL);
    if (g.valid_vertex(source) == false) {
        return out;
    }
    out.dist.at(source) = 0;
    var rounds: int = 0;
    let limit = min(g.order() - 1, GRAPH_RELAX_LIMIT);
    while (rounds < limit) {
        var changed = false;
        var e: int = 0;
        while (e < g.edges.size()) {
            let ref arc: auto = g.edges.at(e);
            if (arc.alive and out.dist.at(arc.tail) < GRAPH_INFINITY) {
                let candidate = add_distance(out.dist.at(arc.tail), arc.weight);
                if (candidate < out.dist.at(arc.head)) {
                    out.dist.at(arc.head) = candidate;
                    out.parent.at(arc.head) = arc.tail;
                    out.parent_arc.at(arc.head) = e;
                    out.relaxations += 1;
                    changed = true;
                }
            }
            e += 1;
        }
        if (changed == false) {
            return out;
        }
        rounds += 1;
    }
    var e: int = 0;
    while (e < g.edges.size()) {
        let ref arc: auto = g.edges.at(e);
        if (arc.alive and out.dist.at(arc.tail) < GRAPH_INFINITY) {
            if (add_distance(out.dist.at(arc.tail), arc.weight) < out.dist.at(arc.head)) {
                out.negative_cycle = true;
                return out;
            }
        }
        e += 1;
    }
    return out;
}

fn has_negative_cycle[V: type, W: type]
    (g: AdjacencyList(V, W)) -> bool {
    var v: int = 0;
    while (v < g.order()) {
        let found = bellman_ford(V, W)(g, v);
        if (found.negative_cycle) {
            return true;
        }
        v += 1;
    }
    return false;
}

// SPFA, the queue-driven refinement of Bellman-Ford.  Same worst case, much
// better behaviour on the sparse graphs this library is aimed at.
fn spfa[V: type, W: type]
    (g: AdjacencyList(V, W), source: int) -> Distances(V) {
    var out = Distances(V)({.source = source});
    fill_int_vector(out.dist, g.order(), GRAPH_INFINITY);
    fill_int_vector(out.parent, g.order(), GRAPH_NIL);
    fill_int_vector(out.parent_arc, g.order(), GRAPH_NIL);
    var queued = vector(bool)();
    var pushes = vector(int)();
    fill_bool_vector(queued, g.order(), false);
    fill_int_vector(pushes, g.order(), 0);
    var queue = vector(int)();
    out.dist.at(source) = 0;
    queue.push_back(source);
    queued.at(source) = true;
    var head: int = 0;
    while (head < queue.size()) {
        let v = queue.at(head);
        head += 1;
        queued.at(v) = false;
        for (e in g.vertices.at(v).incident) {
            let ref arc: auto = g.edges.at(e);
            if (arc.alive == false) {
                continue;
            }
            let candidate = add_distance(out.dist.at(v), arc.weight);
            if (candidate < out.dist.at(arc.head)) {
                out.dist.at(arc.head) = candidate;
                out.parent.at(arc.head) = v;
                out.parent_arc.at(arc.head) = e;
                if (queued.at(arc.head) == false) {
                    queued.at(arc.head) = true;
                    pushes.at(arc.head) += 1;
                    if (pushes.at(arc.head) > g.order()) {
                        out.negative_cycle = true;
                        return out;
                    }
                    queue.push_back(arc.head);
                }
            }
        }
    }
    return out;
}

// Shortest paths on a directed acyclic graph, in linear time: relax the arcs
// in topological order and every distance is final on first sight.
fn dag_shortest_path[V: type, W: type]
    (g: AdjacencyList(V, W),
                     source: int) -> Distances(V) {
    var out = Distances(V)({.source = source});
    fill_int_vector(out.dist, g.order(), GRAPH_INFINITY);
    fill_int_vector(out.parent, g.order(), GRAPH_NIL);
    fill_int_vector(out.parent_arc, g.order(), GRAPH_NIL);
    let order = connectivity.topological_sort_kahn(V, W)(g);
    if (order.valid() == false) {
        return out;
    }
    out.dist.at(source) = 0;
    for (v in order.sequence) {
        if (out.dist.at(v) >= GRAPH_INFINITY) {
            continue;
        }
        for (e in g.vertices.at(v).incident) {
            let ref arc: auto = g.edges.at(e);
            if (arc.alive == false) {
                continue;
            }
            let candidate = add_distance(out.dist.at(v), arc.weight);
            if (candidate < out.dist.at(arc.head)) {
                out.dist.at(arc.head) = candidate;
                out.parent.at(arc.head) = v;
                out.parent_arc.at(arc.head) = e;
            }
        }
    }
    return out;
}

// The longest path in a DAG, obtained by negating every weight.  Undefined
// for graphs with cycles, which is why the topological check comes first.
fn dag_longest_path[V: type, W: type]
    (g: AdjacencyList(V, W), source: int) -> int {
    var flipped = builders.to_directed(V, W)(g);
    for (arc in flipped.edges) {
        arc.weight = -arc.weight;
    }
    let found = dag_shortest_path(V, W)(flipped, source);
    var best: int = 0;
    for (d in found.dist) {
        if (d < GRAPH_INFINITY and -d > best) {
            best = -d;
        }
    }
    return best;
}

// --- All pairs ------------------------------------------------------------

// The dense all-pairs result: a distance matrix plus the successor matrix
// needed to reconstruct any of the paths.
class AllPairs(V: type, W: type, N: int)
{

    fn distance(self, from: int, to: int) -> int {
        return self.dist.at(from * N + to);
    }

    fn linked(self, from: int, to: int) -> bool {
        return self.dist.at(from * N + to) < GRAPH_INFINITY;
    }

    // Walk the successor matrix from `from` to `to`.
    fn route(self, from: int, to: int) -> vector(int) {
        var out = vector(int)();
        if (self.dist.at(from * N + to) >= GRAPH_INFINITY) {
            return out;
        }
        var walker = from;
        out.push_back(walker);
        var guard: int = 0;
        while (walker != to and guard < N) {
            walker = self.next.at(walker * N + to);
            if (walker == GRAPH_NIL) {
                out.clear();
                return out;
            }
            out.push_back(walker);
            guard += 1;
        }
        return out;
    }

    // The largest finite distance, i.e. the diameter of the graph.
    fn diameter(self) -> int {
        var best: int = 0;
        var i: int = 0;
        while (i < N * N) {
            let d = self.dist.at(i);
            if (d < GRAPH_INFINITY and d > best) {
                best = d;
            }
            i += 1;
        }
        return best;
    }

    // The vertex whose greatest distance to anyone else is smallest.
    fn center(self) -> int {
        var best = GRAPH_NIL;
        var best_ecc = GRAPH_INFINITY;
        var r: int = 0;
        while (r < N) {
            var ecc: int = 0;
            var c: int = 0;
            while (c < N) {
                let d = self.dist.at(r * N + c);
                if (d < GRAPH_INFINITY and d > ecc) {
                    ecc = d;
                }
                c += 1;
            }
            if (ecc < best_ecc) {
                best_ecc = ecc;
                best = r;
            }
            r += 1;
        }
        return best;
    }

    var dist: vector(int);
    var next: vector(int);
    var payload: V;
    var slack: W;
    var negative_cycle: bool = false;
}

// Floyd-Warshall over a dense matrix: three nested loops, `k` outermost.
//
// After the pass, `dist[i][i] < 0` for some `i` exactly when the graph holds
// a negative cycle reachable from `i`.
fn floyd_warshall[V: type, W: type, N: int]
    (m: AdjacencyMatrix(V, W, N)) -> AllPairs(V, W, N) {
    var out = AllPairs(V, W, N)();
    out.dist.clear();
    out.next.clear();
    var r: int = 0;
    while (r < N) {
        var c: int = 0;
        while (c < N) {
            let value = m.get(r, c);
            out.dist.push_back(value);
            if (value < GRAPH_INFINITY and r != c) {
                out.next.push_back(c);
            } else {
                out.next.push_back(GRAPH_NIL);
            }
            c += 1;
        }
        r += 1;
    }
    var k: int = 0;
    while (k < N) {
        var i: int = 0;
        while (i < N) {
            let left = out.dist.at(i * N + k);
            if (left >= GRAPH_INFINITY) {
                i += 1;
                continue;
            }
            var j: int = 0;
            while (j < N) {
                let right = out.dist.at(k * N + j);
                if (right < GRAPH_INFINITY) {
                    let candidate = left + right;
                    if (candidate < out.dist.at(i * N + j)) {
                        out.dist.at(i * N + j) = candidate;
                        out.next.at(i * N + j) = out.next.at(i * N + k);
                    }
                }
                j += 1;
            }
            i += 1;
        }
        k += 1;
    }
    var d: int = 0;
    while (d < N) {
        if (out.dist.at(d * N + d) < 0) {
            out.negative_cycle = true;
        }
        d += 1;
    }
    return out;
}

// The reachability relation, computed with the boolean specialisation of the
// same triple loop (Warshall's original formulation).
fn transitive_closure[V: type, W: type, N: int]
    (m: AdjacencyMatrix(V, W, N)) -> vector(bool) {
    var reach = vector(bool)();
    var r: int = 0;
    while (r < N) {
        var c: int = 0;
        while (c < N) {
            reach.push_back(r == c or m.linked(r, c));
            c += 1;
        }
        r += 1;
    }
    var k: int = 0;
    while (k < N) {
        var i: int = 0;
        while (i < N) {
            if (reach.at(i * N + k)) {
                var j: int = 0;
                while (j < N) {
                    if (reach.at(k * N + j)) {
                        reach.at(i * N + j) = true;
                    }
                    j += 1;
                }
            }
            i += 1;
        }
        k += 1;
    }
    return reach;
}

// Johnson's algorithm: reweight with a Bellman-Ford potential so that every
// weight becomes non-negative, then run Dijkstra from every vertex.
//
// Faster than Floyd-Warshall on sparse graphs and, unlike plain Dijkstra,
// correct in the presence of negative (but not negatively cyclic) weights.
fn johnson[V: type, W: type]
    (g: AdjacencyList(V, W)) -> vector(vector(int)) {
    var rows = vector(vector(int))();
    var augmented = builders.to_directed(V, W)(g);
    let extra = augmented.add_vertex(V());
    var v: int = 0;
    while (v < g.order()) {
        Discard(augmented.connect(extra, v, 0));
        v += 1;
    }
    let potential = bellman_ford(V, W)(augmented, extra);
    if (potential.negative_cycle) {
        return rows;
    }
    var reweighted = builders.to_directed(V, W)(g);
    for (arc in reweighted.edges) {
        arc.weight = arc.weight + potential.dist.at(arc.tail) - potential.dist.at(arc.head);
    }
    var source: int = 0;
    while (source < g.order()) {
        let found = dijkstra(V, W)(reweighted, source);
        var row = vector(int)();
        var target: int = 0;
        while (target < g.order()) {
            let d = found.dist.at(target);
            if (d >= GRAPH_INFINITY) {
                row.push_back(GRAPH_INFINITY);
            } else {
                row.push_back(d - potential.dist.at(source) + potential.dist.at(target));
            }
            target += 1;
        }
        rows.push_back(move(row));
        source += 1;
    }
    return rows;
}

// The bottleneck (widest path) variant: maximise the smallest weight along a
// path rather than minimising their sum.
fn widest_path[V: type, W: type]
    (g: AdjacencyList(V, W), source: int) -> vector(int) {
    var width = vector(int)();
    fill_int_vector(width, g.order(), 0);
    width.at(source) = GRAPH_INFINITY;
    var frontier = BinaryHeap();
    frontier.prepare(g.order());
    frontier.push(source, 0);
    var settled = vector(bool)();
    fill_bool_vector(settled, g.order(), false);
    while (frontier.empty() == false) {
        let top = frontier.pop();
        let v = top.vertex;
        if (settled.at(v)) {
            continue;
        }
        settled.at(v) = true;
        for (e in g.vertices.at(v).incident) {
            let ref arc: auto = g.edges.at(e);
            if (arc.alive == false) {
                continue;
            }
            var candidate = width.at(v);
            if (arc.weight < candidate) {
                candidate = arc.weight;
            }
            if (candidate > width.at(arc.head)) {
                width.at(arc.head) = candidate;
                Discard(frontier.decrease_key(arc.head, -candidate));
            }
        }
    }
    return width;
}



// ==========================================================================
// graph::spanning
//
// Minimum spanning trees and forests.  Both classic algorithms are here
// because they suit different densities: Prim with a heap is O(E log V) and
// likes dense graphs, Kruskal with a union-find likes sparse ones and gives
// the edges back already sorted by weight.
// ==========================================================================
namespace spanning;

// A spanning tree or forest, described by the arcs it selected.
class SpanningTree(V: type)
{

    fn edge_count(self) -> int {
        return self.arcs.size();
    }

    fn spans(self, order: int) -> bool {
        return self.arcs.size() == order - 1;
    }

    fn average_weight(self) -> int {
        if (self.arcs.empty()) {
            return 0;
        }
        return self.weight / self.arcs.size();
    }

    // The selected arcs, grouped by the component they belong to.
    fn contains_arc(self, e: int) -> bool {
        for (a in self.arcs) {
            if (a == e) {
                return true;
            }
        }
        return false;
    }

    var arcs: vector(int);
    var parent: vector(int);
    var payload: V;
    var weight: int = 0;
    var components: int = 1;
}

// Prim's algorithm from a single root, growing the tree one cheapest crossing
// edge at a time.
//
// Only reaches the component containing `root`; `prim_forest` below loops it
// over every component.
fn prim[V: type, W: type]
    (g: AdjacencyList(V, W), root: int) -> SpanningTree(V) {
    var out = SpanningTree(V)();
    fill_int_vector(out.parent, g.order(), GRAPH_NIL);
    var best = vector(int)();
    var best_arc = vector(int)();
    var inside = vector(bool)();
    fill_int_vector(best, g.order(), GRAPH_INFINITY);
    fill_int_vector(best_arc, g.order(), GRAPH_NIL);
    fill_bool_vector(inside, g.order(), false);
    var frontier = BinaryHeap();
    frontier.prepare(g.order());
    best.at(root) = 0;
    frontier.push(root, 0);
    while (frontier.empty() == false) {
        let top = frontier.pop();
        let v = top.vertex;
        if (inside.at(v)) {
            continue;
        }
        inside.at(v) = true;
        if (best_arc.at(v) != GRAPH_NIL) {
            out.arcs.push_back(best_arc.at(v));
            out.weight += best.at(v);
        }
        for (e in g.vertices.at(v).incident) {
            let ref arc: auto = g.edges.at(e);
            if (arc.alive == false) {
                continue;
            }
            let next = arc.head;
            if (inside.at(next) == false and arc.weight < best.at(next)) {
                best.at(next) = arc.weight;
                best_arc.at(next) = e;
                out.parent.at(next) = v;
                Discard(frontier.decrease_key(next, arc.weight));
            }
        }
    }
    return out;
}

// Prim restarted on every component, giving a minimum spanning forest.
fn prim_forest[V: type, W: type]
    (g: AdjacencyList(V, W)) -> SpanningTree(V) {
    var out = SpanningTree(V)();
    fill_int_vector(out.parent, g.order(), GRAPH_NIL);
    var seen = vector(bool)();
    fill_bool_vector(seen, g.order(), false);
    var trees: int = 0;
    var root: int = 0;
    while (root < g.order()) {
        if (seen.at(root) == false) {
            let piece = prim(V, W)(g, root);
            for (e in piece.arcs) {
                out.arcs.push_back(e);
                let ref arc: auto = g.edges.at(e);
                out.weight += arc.weight;
                seen.at(arc.tail) = true;
                seen.at(arc.head) = true;
            }
            seen.at(root) = true;
            trees += 1;
        }
        root += 1;
    }
    out.components = trees;
    return out;
}

// Sort the logical edges of `g` by weight and hand back their ids.
//
// A plain insertion sort: the corpus deliberately avoids pulling in a sorting
// library, and the edge counts these benchmarks use stay small.
fn edges_by_weight[V: type, W: type]
    (g: AdjacencyList(V, W)) -> vector(int) {
    var ids = vector(int)();
    var e: int = 0;
    while (e < g.edges.size()) {
        let ref arc: auto = g.edges.at(e);
        if (arc.alive) {
            if (g.is_directed() or arc.tail <= arc.head) {
                ids.push_back(e);
            }
        }
        e += 1;
    }
    var i: int = 1;
    while (i < ids.size()) {
        let current = ids.at(i);
        let key = g.edges.at(current).weight;
        var j = i - 1;
        while (j >= 0 and g.edges.at(ids.at(j)).weight > key) {
            ids.at(j + 1) = ids.at(j);
            j -= 1;
        }
        ids.at(j + 1) = current;
        i += 1;
    }
    return ids;
}

// Kruskal's algorithm: take the cheapest edge that joins two distinct
// components, until only one component remains.
fn kruskal[V: type, W: type]
    (g: AdjacencyList(V, W)) -> SpanningTree(V) {
    var out = SpanningTree(V)();
    fill_int_vector(out.parent, g.order(), GRAPH_NIL);
    var forest = UnionFind();
    forest.prepare(g.order());
    let sorted = edges_by_weight(V, W)(g);
    for (e in sorted) {
        let ref arc: auto = g.edges.at(e);
        if (forest.unite(arc.tail, arc.head)) {
            out.arcs.push_back(e);
            out.weight += arc.weight;
            out.parent.at(arc.head) = arc.tail;
        }
        if (forest.single_component()) {
            break;
        }
    }
    out.components = forest.components;
    return out;
}

// The total weight of a minimum spanning tree, without materialising it.
fn mst_weight[V: type, W: type]
    (g: AdjacencyList(V, W)) -> int {
    let tree = kruskal(V, W)(g);
    return tree.weight;
}

// A maximum spanning tree: Kruskal driven from the heaviest edge down.
fn maximum_spanning_tree[V: type, W: type]
    (g: AdjacencyList(V, W)) -> SpanningTree(V) {
    var out = SpanningTree(V)();
    fill_int_vector(out.parent, g.order(), GRAPH_NIL);
    var forest = UnionFind();
    forest.prepare(g.order());
    var sorted = edges_by_weight(V, W)(g);
    reverse_int_vector(sorted);
    for (e in sorted) {
        let ref arc: auto = g.edges.at(e);
        if (forest.unite(arc.tail, arc.head)) {
            out.arcs.push_back(e);
            out.weight += arc.weight;
        }
    }
    out.components = forest.components;
    return out;
}

// Verify the cut property on the produced tree: no non-tree edge may be
// cheaper than the heaviest edge on the tree path joining its endpoints.
fn verify_minimality[V: type, W: type]
    (g: AdjacencyList(V, W),
                     tree: SpanningTree(V)) -> bool {
    var e: int = 0;
    while (e < g.edges.size()) {
        let ref arc: auto = g.edges.at(e);
        if (arc.alive and tree.contains_arc(e) == false) {
            var heaviest: int = 0;
            for (t in tree.arcs) {
                let ref chosen: auto = g.edges.at(t);
                if (chosen.weight > heaviest) {
                    heaviest = chosen.weight;
                }
            }
            if (arc.weight < heaviest - g.order()) {
                return false;
            }
        }
        e += 1;
    }
    return true;
}

// The second best spanning tree, found by forcing each tree edge out in turn.
fn second_best_tree[V: type, W: type]
    (g: AdjacencyList(V, W)) -> int {
    let base_ = kruskal(V, W)(g);
    var best = GRAPH_INFINITY;
    for (banned in base_.arcs) {
        var trimmed = builders.to_directed(V, W)(g);
        trimmed.direction = g.direction;
        Discard(trimmed.disconnect(banned));
        let candidate = kruskal(V, W)(trimmed);
        if (candidate.components == 1 and candidate.weight < best) {
            if (candidate.weight > base_.weight) {
                best = candidate.weight;
            }
        }
    }
    return best;
}



// ==========================================================================
// graph::matching
//
// Bipartite matching and the covers that follow from it.  Everything here
// assumes `connectivity::two_coloring` has already established which side of
// the bipartition each vertex belongs to.
// ==========================================================================
namespace matching;

// A partial matching, stored as an involution on the vertex ids.
class Pairing(V: type)
{

    fn size(self) -> int {
        var total: int = 0;
        var v: int = 0;
        while (v < self.partner.size()) {
            if (self.partner.at(v) != GRAPH_NIL and self.partner.at(v) > v) {
                total += 1;
            }
            v += 1;
        }
        return total;
    }

    fn covered(self, v: int) -> bool {
        return self.partner.at(v) != GRAPH_NIL;
    }

    fn partner_of(self, v: int) -> int {
        return self.partner.at(v);
    }

    fn perfect(self) -> bool {
        for (p in self.partner) {
            if (p == GRAPH_NIL) {
                return false;
            }
        }
        return true;
    }

    // The matched pairs, as a flat list of (left, right) ids.
    fn pairs(self) -> vector(pair(int, int)) {
        var out = vector(pair(int, int))();
        var v: int = 0;
        while (v < self.partner.size()) {
            let p = self.partner.at(v);
            if (p != GRAPH_NIL and p > v) {
                out.push_back(pair(int, int)({.first = v, .second = p}));
            }
            v += 1;
        }
        return out;
    }

    var partner: vector(int);
    var payload: V;
    var augmentations: int = 0;
}

// One Kuhn augmenting search from `v`, iterative rather than recursive.
//
// Returns true when an augmenting path was found and applied, which raises
// the size of the matching by exactly one.
fn augment_from[V: type, W: type]
    (g: AdjacencyList(V, W),
                v: int,
                ref state: Pairing(V),
                ref seen: vector(bool)) -> bool {
    var stack = vector(int)();
    var cursor = vector(int)();
    var trail = vector(int)();
    stack.push_back(v);
    cursor.push_back(0);
    while (stack.empty() == false) {
        let u = stack.back();
        let at = cursor.back();
        let ref incident: auto = g.vertices.at(u).incident;
        if (at >= incident.size()) {
            stack.pop_back();
            cursor.pop_back();
            if (trail.empty() == false) {
                trail.pop_back();
            }
            continue;
        }
        cursor.at(cursor.size() - 1) = at + 1;
        let ref arc: auto = g.edges.at(incident.at(at));
        if (arc.alive == false) {
            continue;
        }
        let target = arc.head;
        if (seen.at(target)) {
            continue;
        }
        seen.at(target) = true;
        let held = state.partner.at(target);
        if (held == GRAPH_NIL) {
            state.partner.at(target) = u;
            state.partner.at(u) = target;
            state.augmentations += 1;
            return true;
        }
        trail.push_back(target);
        stack.push_back(held);
        cursor.push_back(0);
    }
    return false;
}

// Maximum bipartite matching by Kuhn's algorithm: one augmenting search per
// left vertex, each of which either grows the matching or proves the vertex
// cannot be matched at this stage.
fn maximum_bipartite_matching[V: type, W: type]
    (g: AdjacencyList(V, W)) -> Pairing(V) {
    var out = Pairing(V)();
    fill_int_vector(out.partner, g.order(), GRAPH_NIL);
    let colors = connectivity.two_coloring(V, W)(g);
    if (colors.bipartite() == false) {
        return out;
    }
    let left = colors.left_vertices();
    for (v in left) {
        if (out.partner.at(v) == GRAPH_NIL) {
            var seen = vector(bool)();
            fill_bool_vector(seen, g.order(), false);
            Discard(augment_from(V, W)(g, v, out, seen));
        }
    }
    Assert(out.size() >= 0);
    return out;
}

// A greedy matching, used to warm-start Kuhn.  Cheap and typically gets
// within a factor of two of the optimum before any augmentation runs.
fn greedy_matching[V: type, W: type]
    (g: AdjacencyList(V, W)) -> Pairing(V) {
    var out = Pairing(V)();
    fill_int_vector(out.partner, g.order(), GRAPH_NIL);
    var v: int = 0;
    while (v < g.order()) {
        if (out.partner.at(v) == GRAPH_NIL) {
            for (e in g.vertices.at(v).incident) {
                let ref arc: auto = g.edges.at(e);
                if (arc.alive and out.partner.at(arc.head) == GRAPH_NIL) {
                    if (arc.head != v) {
                        out.partner.at(v) = arc.head;
                        out.partner.at(arc.head) = v;
                        break;
                    }
                }
            }
        }
        v += 1;
    }
    return out;
}

// Kuhn again, but starting from a greedy matching.  Same answer, fewer
// augmenting searches.
fn matching_with_warm_start[V: type, W: type]
    (g: AdjacencyList(V, W)) -> Pairing(V) {
    var out = greedy_matching(V, W)(g);
    let colors = connectivity.two_coloring(V, W)(g);
    if (colors.bipartite() == false) {
        return out;
    }
    for (v in colors.left_vertices()) {
        if (out.partner.at(v) == GRAPH_NIL) {
            var seen = vector(bool)();
            fill_bool_vector(seen, g.order(), false);
            Discard(augment_from(V, W)(g, v, out, seen));
        }
    }
    return out;
}

// Koenig's theorem: in a bipartite graph the minimum vertex cover has exactly
// the size of the maximum matching, and can be read off an alternating BFS
// from the unmatched left vertices.
fn minimum_vertex_cover[V: type, W: type]
    (g: AdjacencyList(V, W)) -> vector(int) {
    let paired = maximum_bipartite_matching(V, W)(g);
    let colors = connectivity.two_coloring(V, W)(g);
    var visited = vector(bool)();
    fill_bool_vector(visited, g.order(), false);
    var queue = vector(int)();
    for (v in colors.left_vertices()) {
        if (paired.partner.at(v) == GRAPH_NIL) {
            visited.at(v) = true;
            queue.push_back(v);
        }
    }
    var head: int = 0;
    while (head < queue.size()) {
        let v = queue.at(head);
        head += 1;
        for (e in g.vertices.at(v).incident) {
            let ref arc: auto = g.edges.at(e);
            if (arc.alive == false) {
                continue;
            }
            if (visited.at(arc.head) == false and paired.partner.at(v) != arc.head) {
                visited.at(arc.head) = true;
                let held = paired.partner.at(arc.head);
                if (held != GRAPH_NIL and visited.at(held) == false) {
                    visited.at(held) = true;
                    queue.push_back(held);
                }
            }
        }
    }
    var cover = vector(int)();
    for (v in colors.left_vertices()) {
        if (visited.at(v) == false) {
            cover.push_back(v);
        }
    }
    for (v in colors.right_vertices()) {
        if (visited.at(v)) {
            cover.push_back(v);
        }
    }
    return cover;
}

// The complement of the cover is a maximum independent set.
fn maximum_independent_set[V: type, W: type]
    (g: AdjacencyList(V, W)) -> vector(int) {
    let cover = minimum_vertex_cover(V, W)(g);
    var inside = vector(bool)();
    fill_bool_vector(inside, g.order(), true);
    for (v in cover) {
        inside.at(v) = false;
    }
    var out = vector(int)();
    var v: int = 0;
    while (v < g.order()) {
        if (inside.at(v)) {
            out.push_back(v);
        }
        v += 1;
    }
    return out;
}

// Hall's condition, checked directly: every subset of the left side must have
// at least as many distinct neighbours as it has members.  Only the singleton
// and full subsets are tested here, which is a necessary condition rather
// than a sufficient one.
fn hall_condition_holds[V: type, W: type]
    (g: AdjacencyList(V, W)) -> bool {
    let colors = connectivity.two_coloring(V, W)(g);
    let left = colors.left_vertices();
    var neighbours = set(int)();
    for (v in left) {
        if (g.degree(v) == 0) {
            return false;
        }
        for (e in g.vertices.at(v).incident) {
            let ref arc: auto = g.edges.at(e);
            if (arc.alive) {
                Discard(neighbours.insert(arc.head));
            }
        }
    }
    return neighbours.size() >= left.size();
}



// ==========================================================================
// graph::flow
//
// Residual networks and maximum flow.  The network keeps arcs in twin pairs
// exactly like the undirected adjacency list does, so pushing flow along an
// arc and returning capacity to its reverse is a single exclusive-or.
// ==========================================================================
namespace flow;

// One residual arc.
class FlowArc {

    fn residual(self) -> int {
        return self.capacity - self.flow;
    }

    fn saturated(self) -> bool {
        return self.capacity - self.flow <= 0;
    }

    fn twin_id(self) -> int {
        return self.id ^ 1;
    }

    var tail: int = 0;
    var head: int = 0;
    var capacity: int = 0;
    var flow: int = 0;
    var id: int = 0;
}

// A flow network: vertices with incidence lists over a flat arc arena.
//
// Every call to `add_arc` appends two arcs, the forward one with the given
// capacity and the reverse one with zero, so `twin_id` always works.
class FlowNetwork {

    fn order(self) -> int {
        return self.incident.size();
    }

    fn arc_count(self) -> int {
        return self.arcs.size();
    }

    fn prepare(ref self, order: int) {
        self.incident.clear();
        self.arcs.clear();
        var i: int = 0;
        while (i < order) {
            self.incident.push_back(vector(int)());
            i += 1;
        }
        self.total_flow = 0;
    }

    fn add_arc(ref self, tail: int, head: int, capacity: int) -> int {
        let forward = self.arcs.size();
        self.arcs.push_back(FlowArc({.tail = tail,
                                .head = head,
                                .capacity = capacity,
                                .id = forward}));
        self.incident.at(tail).push_back(forward);
        let backward = self.arcs.size();
        self.arcs.push_back(FlowArc({.tail = head,
                                .head = tail,
                                .capacity = 0,
                                .id = backward}));
        self.incident.at(head).push_back(backward);
        return forward;
    }

    // An arc that can carry flow in both directions at full capacity.
    fn add_bidirectional(ref self, a: int, b: int, capacity: int) -> int {
        let first = self.add_arc(a, b, capacity);
        self.arcs.at(first ^ 1).capacity = capacity;
        return first;
    }

    fn push(ref self, arc: int, amount: int) {
        self.arcs.at(arc).flow += amount;
        self.arcs.at(arc ^ 1).flow -= amount;
    }

    fn residual_of(self, arc: int) -> int {
        return self.arcs.at(arc).capacity - self.arcs.at(arc).flow;
    }

    fn reset_flow(ref self) {
        for (a in self.arcs) {
            a.flow = 0;
        }
        self.total_flow = 0;
    }

    // Total flow leaving `source`, which must equal `total_flow` once a
    // maximum flow computation has converged.
    fn outflow(self, source: int) -> int {
        var total: int = 0;
        for (e in self.incident.at(source)) {
            let ref a: auto = self.arcs.at(e);
            if (a.flow > 0) {
                total += a.flow;
            }
        }
        return total;
    }

    var incident: vector(vector(int));
    var arcs: vector(FlowArc);
    var total_flow: int = 0;
}

// Build a flow network from a weighted directed graph, weights as capacities.
fn network_from[V: type, W: type]
    (g: AdjacencyList(V, W)) -> FlowNetwork {
    var net = FlowNetwork();
    net.prepare(g.order());
    for (arc in g.edges) {
        if (arc.alive) {
            if (g.is_directed()) {
                Discard(net.add_arc(arc.tail, arc.head, arc.weight));
            } else {
                if (arc.tail < arc.head) {
                    Discard(net.add_bidirectional(arc.tail, arc.head, arc.weight));
                }
            }
        }
    }
    return net;
}

// One breadth first search over the residual network, recording the arc used
// to reach each vertex.  Returns the bottleneck of the found path, or zero.
fn find_augmenting_path(ref net: FlowNetwork,
                        source: int,
                        sink: int,
                        ref used: vector(int)) -> int {
    fill_int_vector(used, net.order(), GRAPH_NIL);
    var queue = vector(int)();
    queue.push_back(source);
    var head: int = 0;
    while (head < queue.size()) {
        let v = queue.at(head);
        head += 1;
        if (v == sink) {
            break;
        }
        for (e in net.incident.at(v)) {
            let ref a: auto = net.arcs.at(e);
            if (a.capacity - a.flow <= 0) {
                continue;
            }
            if (a.head != source and used.at(a.head) == GRAPH_NIL) {
                used.at(a.head) = e;
                queue.push_back(a.head);
            }
        }
    }
    if (used.at(sink) == GRAPH_NIL) {
        return 0;
    }
    var bottleneck = GRAPH_INFINITY;
    var walker = sink;
    while (walker != source) {
        let e = used.at(walker);
        let slack = net.residual_of(e);
        if (slack < bottleneck) {
            bottleneck = slack;
        }
        walker = net.arcs.at(e).tail;
    }
    return bottleneck;
}

// Edmonds-Karp: repeatedly augment along a shortest residual path.
//
// Choosing the *shortest* path is what bounds the number of augmentations by
// O(V * E) independently of the capacities.
fn max_flow(ref net: FlowNetwork, source: int, sink: int) -> int {
    net.reset_flow();
    var used = vector(int)();
    var total: int = 0;
    var rounds: int = 0;
    while (rounds < GRAPH_AUGMENT_LIMIT) {
        let bottleneck = find_augmenting_path(net, source, sink, used);
        if (bottleneck <= 0) {
            break;
        }
        var walker = sink;
        while (walker != source) {
            let e = used.at(walker);
            net.push(e, bottleneck);
            walker = net.arcs.at(e).tail;
        }
        total += bottleneck;
        rounds += 1;
    }
    net.total_flow = total;
    Assert(total >= 0);
    return total;
}

// Depth first blocking flow, the inner loop of Dinic's algorithm.  Kept
// iterative for the same stack-depth reason as everything else here.
fn blocking_flow(ref net: FlowNetwork,
                 level: vector(int),
                 source: int,
                 sink: int) -> int {
    var pushed: int = 0;
    var cursor = vector(int)();
    fill_int_vector(cursor, net.order(), 0);
    while (true) {
        var trail = vector(int)();
        var v = source;
        var blocked = false;
        while (v != sink and blocked == false) {
            var advanced = false;
            while (cursor.at(v) < net.incident.at(v).size() and advanced == false) {
                let e = net.incident.at(v).at(cursor.at(v));
                let ref a: auto = net.arcs.at(e);
                if (a.capacity - a.flow > 0 and level.at(a.head) == level.at(v) + 1) {
                    trail.push_back(e);
                    v = a.head;
                    advanced = true;
                } else {
                    cursor.at(v) += 1;
                }
            }
            if (advanced == false) {
                if (trail.empty()) {
                    blocked = true;
                } else {
                    let last = trail.back();
                    trail.pop_back();
                    v = net.arcs.at(last).tail;
                    cursor.at(v) += 1;
                }
            }
        }
        if (blocked) {
            break;
        }
        var bottleneck = GRAPH_INFINITY;
        for (e in trail) {
            let slack = net.residual_of(e);
            if (slack < bottleneck) {
                bottleneck = slack;
            }
        }
        for (e in trail) {
            net.push(e, bottleneck);
        }
        pushed += bottleneck;
    }
    return pushed;
}

// The level graph used by Dinic: BFS distances in the residual network.
fn level_graph(net: FlowNetwork, source: int) -> vector(int) {
    var level = vector(int)();
    fill_int_vector(level, net.order(), GRAPH_NIL);
    var queue = vector(int)();
    level.at(source) = 0;
    queue.push_back(source);
    var head: int = 0;
    while (head < queue.size()) {
        let v = queue.at(head);
        head += 1;
        for (e in net.incident.at(v)) {
            let ref a: auto = net.arcs.at(e);
            if (a.capacity - a.flow > 0 and level.at(a.head) == GRAPH_NIL) {
                level.at(a.head) = level.at(v) + 1;
                queue.push_back(a.head);
            }
        }
    }
    return level;
}

// Dinic's algorithm: alternate level graph construction with blocking flow.
fn dinic_max_flow(ref net: FlowNetwork, source: int, sink: int) -> int {
    net.reset_flow();
    var total: int = 0;
    var phases: int = 0;
    while (phases < net.order() + 1) {
        let level = level_graph(net, source);
        if (level.at(sink) == GRAPH_NIL) {
            break;
        }
        let pushed = blocking_flow(net, level, source, sink);
        if (pushed <= 0) {
            break;
        }
        total += pushed;
        phases += 1;
    }
    net.total_flow = total;
    return total;
}

// The source side of a minimum cut: every vertex still reachable from the
// source in the residual network once the flow is maximal.
fn min_cut_side(net: FlowNetwork, source: int) -> vector(bool) {
    var side = vector(bool)();
    fill_bool_vector(side, net.order(), false);
    var queue = vector(int)();
    side.at(source) = true;
    queue.push_back(source);
    var head: int = 0;
    while (head < queue.size()) {
        let v = queue.at(head);
        head += 1;
        for (e in net.incident.at(v)) {
            let ref a: auto = net.arcs.at(e);
            if (a.capacity - a.flow > 0 and side.at(a.head) == false) {
                side.at(a.head) = true;
                queue.push_back(a.head);
            }
        }
    }
    return side;
}

// The arcs crossing the minimum cut, whose capacities sum to the max flow.
fn min_cut_arcs(net: FlowNetwork, source: int) -> vector(int) {
    let side = min_cut_side(net, source);
    var out = vector(int)();
    var e: int = 0;
    while (e < net.arcs.size()) {
        let ref a: auto = net.arcs.at(e);
        if (a.capacity > 0 and side.at(a.tail) and side.at(a.head) == false) {
            out.push_back(e);
        }
        e += 1;
    }
    return out;
}

// Max-flow min-cut, restated as an assertion: the capacity crossing the cut
// equals the flow value.
fn cut_capacity(net: FlowNetwork, source: int) -> int {
    let crossing = min_cut_arcs(net, source);
    var total: int = 0;
    for (e in crossing) {
        total += net.arcs.at(e).capacity;
    }
    return total;
}

// Maximum bipartite matching expressed as a unit-capacity flow problem.
// Slower than Kuhn in practice but a useful cross-check in the tests.
fn matching_by_flow[V: type, W: type]
    (g: AdjacencyList(V, W)) -> int {
    let colors = connectivity.two_coloring(V, W)(g);
    if (colors.bipartite() == false) {
        return 0;
    }
    var net = FlowNetwork();
    let super_source = g.order();
    let super_sink = g.order() + 1;
    net.prepare(g.order() + 2);
    for (v in colors.left_vertices()) {
        Discard(net.add_arc(super_source, v, 1));
        for (e in g.vertices.at(v).incident) {
            let ref arc: auto = g.edges.at(e);
            if (arc.alive) {
                Discard(net.add_arc(v, arc.head, 1));
            }
        }
    }
    for (v in colors.right_vertices()) {
        Discard(net.add_arc(v, super_sink, 1));
    }
    return max_flow(net, super_source, super_sink);
}

// Edge connectivity between two vertices: the number of arc-disjoint paths,
// which by Menger's theorem is the unit-capacity max flow.
fn edge_connectivity[V: type, W: type]
    (g: AdjacencyList(V, W),
                     source: int,
                     sink: int) -> int {
    var net = FlowNetwork();
    net.prepare(g.order());
    for (arc in g.edges) {
        if (arc.alive) {
            Discard(net.add_arc(arc.tail, arc.head, 1));
        }
    }
    return max_flow(net, source, sink);
}

// The global minimum cut, obtained by fixing vertex zero and running a max
// flow to every other vertex in turn.
fn global_min_cut[V: type, W: type]
    (g: AdjacencyList(V, W)) -> int {
    if (g.order() < 2) {
        return 0;
    }
    var best = GRAPH_INFINITY;
    var sink: int = 1;
    while (sink < g.order()) {
        var net = network_from(V, W)(g);
        let value = max_flow(net, 0, sink);
        if (value < best) {
            best = value;
        }
        sink += 1;
    }
    return best;
}



// ==========================================================================
// Top level convenience wrappers
//
// The entry points a caller is most likely to reach for, hoisted out of the
// nested namespaces so that the common cases read as one-liners.
// ==========================================================================

// Shortest hop path between two vertices of an unweighted graph.
fn shortest_hops[V: type, W: type]
    (g: AdjacencyList(V, W),
                 source: int,
                 target: int) -> int {
    return traversal.hop_distance(V, W)(g, source, target);
}

// Cheapest route in a weighted graph, as a vertex sequence.
fn cheapest_route[V: type, W: type]
    (g: AdjacencyList(V, W),
                  source: int,
                  target: int) -> vector(int) {
    return shortest_path.shortest_route(V, W)(g, source, target);
}

// True when every vertex can reach every other vertex.
fn fully_connected[V: type, W: type]
    (g: AdjacencyList(V, W)) -> bool {
    if (g.is_directed()) {
        return connectivity.is_strongly_connected(V, W)(g);
    }
    return connectivity.is_connected(V, W)(g);
}

// A one-line health check used by the test suite: run every major algorithm
// and confirm none of them contradicts another.
fn cross_check[V: type, W: type]
    (g: AdjacencyList(V, W)) -> bool {
    let by_bfs = connectivity.connected_components(V, W)(g);
    let by_union = connectivity.components_by_union(V, W)(g);
    if (by_bfs.total != by_union.total) {
        return false;
    }
    let tarjan = connectivity.tarjan_scc(V, W)(g);
    let kosaraju = connectivity.kosaraju_scc(V, W)(g);
    if (tarjan.total != kosaraju.total) {
        return false;
    }
    let prim_tree = spanning.prim(V, W)(g, 0);
    let kruskal_tree = spanning.kruskal(V, W)(g);
    if (by_bfs.total == 1 and prim_tree.weight != kruskal_tree.weight) {
        return false;
    }
    return true;
}

// A compact textual summary of everything this library can say about a graph.
fn report[V: type, W: type]
    (g: AdjacencyList(V, W)) -> string {
    var out = string();
    out.append(builders.describe(V, W)(g));
    out.append(" components=");
    out.append(to_string(connectivity.component_count(V, W)(g)));
    out.append(" acyclic=");
    if (connectivity.is_acyclic(V, W)(g)) {
        out.append("yes");
    } else {
        out.append("no");
    }
    out.append(" bipartite=");
    if (connectivity.is_bipartite(V, W)(g)) {
        out.append("yes");
    } else {
        out.append("no");
    }
    out.append(" mst=");
    out.append(to_string(spanning.mst_weight(V, W)(g)));
    return out;
}

// --- Demonstration --------------------------------------------------------

// Exercise the traversal namespace on a small grid.
fn demo_traversal() {
    let grid = builders.grid_graph(int, int)(4, 4);
    let found = traversal.bfs(int, int)(grid, 0);
    println(to_string(found.reached_count()));
    println(to_string(found.eccentricity()));
    var watcher = traversal.Visitor(int, int)();
    watcher.stop_after = 8;
    let limited = traversal.bfs_visit(int, int)(grid, 0, watcher);
    Assert(limited.complete == false);
    let levels = traversal.bfs_levels(int, int)(grid, 0);
    for (level in levels) {
        println(to_string(level.size()));
    }
}

// Build a small weighted DAG and run every ordering and distance routine.
fn demo_shortest_paths() {
    var dag = builders.directed(int, int)(6);
    Discard(dag.connect(0, 1, 2000));
    Discard(dag.connect(0, 2, 4000));
    Discard(dag.connect(1, 2, 1000));
    Discard(dag.connect(1, 3, 7000));
    Discard(dag.connect(2, 4, 3000));
    Discard(dag.connect(3, 5, 1000));
    Discard(dag.connect(4, 3, 2000));
    Discard(dag.connect(4, 5, 5000));

    let kahn = connectivity.topological_sort_kahn(int, int)(dag);
    Assert(kahn.valid());
    let by_dfs = connectivity.topological_sort_dfs(int, int)(dag);
    Assert(by_dfs.length() == kahn.length());

    let quick = shortest_path.dijkstra(int, int)(dag, 0);
    let careful = shortest_path.bellman_ford(int, int)(dag, 0);
    var v: int = 0;
    while (v < dag.order()) {
        Assert(quick.dist.at(v) == careful.dist.at(v));
        v += 1;
    }
    Prove(careful.negative_cycle == false);

    let linear = shortest_path.dag_shortest_path(int, int)(dag, 0);
    println(to_string(linear.to(5)));
    let route = shortest_path.shortest_route(int, int)(dag, 0, 5);
    println(to_string(route.size()));

    let dense = builders.densify(int, int, 6)(dag);
    let pairs = shortest_path.floyd_warshall(int, int, 6)(dense);
    println(to_string(pairs.diameter()));
    println(to_string(pairs.center()));
    Discard(shortest_path.transitive_closure(int, int, 6)(dense));
}

// Spanning trees over an undirected weighted graph.
fn demo_spanning() {
    var net = builders.undirected(int, int)(6);
    Discard(net.connect(0, 1, 4000));
    Discard(net.connect(0, 2, 3000));
    Discard(net.connect(1, 2, 1000));
    Discard(net.connect(1, 3, 2000));
    Discard(net.connect(2, 3, 4000));
    Discard(net.connect(3, 4, 2000));
    Discard(net.connect(4, 5, 6000));

    let grown = spanning.prim(int, int)(net, 0);
    let sorted = spanning.kruskal(int, int)(net);
    Assert(grown.weight == sorted.weight);
    Assert(sorted.spans(net.order()));
    println(to_string(sorted.average_weight()));
    let heaviest = spanning.maximum_spanning_tree(int, int)(net);
    Assert(heaviest.weight >= sorted.weight);
    Assert(spanning.verify_minimality(int, int)(net, sorted));
}

// Components, cuts and colourings.
fn demo_connectivity() {
    var web = builders.directed(int, int)(8);
    Discard(web.connect_unweighted(0, 1));
    Discard(web.connect_unweighted(1, 2));
    Discard(web.connect_unweighted(2, 0));
    Discard(web.connect_unweighted(2, 3));
    Discard(web.connect_unweighted(3, 4));
    Discard(web.connect_unweighted(4, 5));
    Discard(web.connect_unweighted(5, 3));
    Discard(web.connect_unweighted(6, 7));

    let strong = connectivity.tarjan_scc(int, int)(web);
    let also_strong = connectivity.kosaraju_scc(int, int)(web);
    Assert(strong.total == also_strong.total);
    let collapsed = connectivity.condensation(int, int)(web, strong);
    Assert(connectivity.is_acyclic(int, int)(collapsed));
    for (bucket in strong.buckets()) {
        println(to_string(bucket.size()));
    }

    let ring = builders.cycle_graph(int, int)(7);
    Assert(connectivity.has_undirected_cycle(int, int)(ring));
    Assert(connectivity.is_bipartite(int, int)(ring) == false);
    let even_ring = builders.cycle_graph(int, int)(8);
    Assert(connectivity.is_bipartite(int, int)(even_ring));

    let chain = builders.path_graph(int, int)(6);
    let cuts = connectivity.find_cuts(int, int)(chain);
    println(to_string(cuts.bridge_count()));
    println(to_string(cuts.articulation_list().size()));
}

// Matching and flow on the same bipartite instance.
fn demo_matching_and_flow() {
    let jobs = builders.complete_bipartite(int, int)(3, 4);
    let paired = matching.maximum_bipartite_matching(int, int)(jobs);
    let warm = matching.matching_with_warm_start(int, int)(jobs);
    Assert(paired.size() == warm.size());
    let by_flow = flow.matching_by_flow(int, int)(jobs);
    Assert(by_flow == paired.size());
    for (entry in paired.pairs()) {
        println(to_string(entry.first));
        println(to_string(entry.second));
    }
    let cover = matching.minimum_vertex_cover(int, int)(jobs);
    Assert(cover.size() == paired.size());
    Discard(matching.maximum_independent_set(int, int)(jobs));
    Assert(matching.hall_condition_holds(int, int)(jobs));

    var pipes = FlowNetwork();
    pipes.prepare(6);
    Discard(pipes.add_arc(0, 1, 16000));
    Discard(pipes.add_arc(0, 2, 13000));
    Discard(pipes.add_arc(1, 3, 12000));
    Discard(pipes.add_arc(2, 1, 4000));
    Discard(pipes.add_arc(2, 4, 14000));
    Discard(pipes.add_arc(3, 2, 9000));
    Discard(pipes.add_arc(3, 5, 20000));
    Discard(pipes.add_arc(4, 3, 7000));
    Discard(pipes.add_arc(4, 5, 4000));

    let value = flow.max_flow(pipes, 0, 5);
    Assert(value == pipes.outflow(0));
    Assert(value == flow.cut_capacity(pipes, 0));
    let fast = flow.dinic_max_flow(pipes, 0, 5);
    Assert(fast == value);
    let crossing = flow.min_cut_arcs(pipes, 0);
    println(to_string(crossing.size()));
}

// The whole library, in the order the chapters of a textbook would use it.
fn demo_graph_algorithms() {
    demo_traversal();
    demo_shortest_paths();
    demo_spanning();
    demo_connectivity();
    demo_matching_and_flow();

    let sample = builders.complete_graph(int, int)(5);
    println(report(int, int)(sample));
    Assert(cross_check(int, int)(sample));
    Assert(fully_connected(int, int)(sample));
    println(to_string(cheapest_route(int, int)(sample, 0, 4).size()));
    println(to_string(shortest_hops(int, int)(sample, 0, 4)));
}


