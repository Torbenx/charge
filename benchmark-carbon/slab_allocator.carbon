// Slab and free-list memory allocator.
//
// The allocator is organised in three layers:
//
//   * an Arena owns a run of pages and hands out raw, page-aligned extents,
//   * a Slab carves one extent into equally sized blocks tracked by a bitmap,
//   * a FreeList recycles individual blocks and coalesces adjacent neighbours.
//
// Every hot path is written against explicit reference categories so that the
// borrow checker can see exactly which layer owns which byte range at any
// point in time.
namespace memory;

// --- Tuning constants ----------------------------------------------------

let PAGE_SIZE: int = 4096;
let PAGE_SHIFT: int = 12;
let PAGE_MASK: int = 4095;              // 0xFFF
let WORD_BITS: int = 64;
let WORD_SHIFT: int = 6;
let WORD_MASK: int = 63;                // 0x3F
let ALL_ONES: int = -1;                 // every bit set

let MIN_ALIGNMENT: int = 8;
let MAX_ALIGNMENT: int = 4096;
let MIN_BLOCK_SIZE: int = 16;
let MAX_SMALL_SIZE: int = 2048;

let SLAB_TARGET_BLOCKS: int = 64;
let SLAB_MIN_BLOCKS: int = 4;
let SLAB_MAX_BLOCKS: int = 1024;

let ARENA_INITIAL_PAGES: int = 64;
let ARENA_GROWTH_NUMERATOR: int = 3;
let ARENA_GROWTH_DENOMINATOR: int = 2;

// Fixed-point scale for every ratio in this file. A value of 1000 means
// "1.000", a value of 875 means "0.875". No floating point is used anywhere.
let PERMILLE: int = 1000;

// A slab is evicted once its occupancy drops below this permille value and
// the owning cache already holds more than EVICT_KEEP_SLABS partial slabs.
let EVICT_THRESHOLD_PERMILLE: int = 125;
let EVICT_KEEP_SLABS: int = 2;

// Poison patterns written over freed payloads when debug fill is enabled.
let POISON_FREED: int = 221;            // 0xDD
let POISON_FRESH: int = 205;            // 0xCD
let GUARD_MAGIC: int = 3735928559;      // 0xDEADBEEF

let INVALID_INDEX: int = -1;

// --- Enumerations --------------------------------------------------------

choice BlockState {
    Free,
    Allocated,
    Split,
    Quarantined,
}

choice SlabState {
    Fresh,
    Partial,
    Full,
    Retired,
}

choice ClassKind {
    Tiny,
    Small,
    Medium,
    Large,
}

choice ValidationCode {
    Ok,
    BitmapCountMismatch,
    OverlappingBlocks,
    UnalignedBase,
    DanglingFreeNode,
    GuardCorrupted,
    OccupancyOutOfRange,
    ClassSizeMismatch,
}

choice RefillOutcome {
    Reused,
    GrewArena,
    Failed,
}

// --- Alignment helpers ---------------------------------------------------

// Round `value` up to the next multiple of `alignment`. The alignment is
// assumed to be a power of two; callers go through `normalize_alignment`.
fn align_up(value: int, alignment: int) -> int {
    Assert(alignment > 0);
    let mask = alignment - 1;
    return (value + mask) & ^mask;
}

fn align_down(value: int, alignment: int) -> int {
    Assert(alignment > 0);
    let mask = alignment - 1;
    return value & ^mask;
}

fn is_aligned(value: int, alignment: int) -> bool {
    let mask = alignment - 1;
    return (value & mask) == 0;
}

fn alignment_mask(alignment: int) => alignment - 1;

fn is_power_of_two(value: int) -> bool {
    if (value <= 0) {
        return false;
    }
    return (value & (value - 1)) == 0;
}

// Smallest power of two that is greater than or equal to `value`.
fn next_power_of_two(value: int) -> int {
    var result: int = 1;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

fn normalize_alignment(requested: int) -> int {
    if (requested < MIN_ALIGNMENT) {
        return MIN_ALIGNMENT;
    }
    if (requested > MAX_ALIGNMENT) {
        return MAX_ALIGNMENT;
    }
    if (is_power_of_two(requested)) {
        return requested;
    }
    return next_power_of_two(requested);
}

fn page_of(offset: int) => offset >> PAGE_SHIFT;

fn page_base(offset: int) -> int {
    return offset & ^PAGE_MASK;
}

fn pages_for(bytes: int) -> int {
    return (bytes + PAGE_MASK) >> PAGE_SHIFT;
}

// Fixed-point ratio helper: returns `part / whole` scaled by PERMILLE.
fn permille_of(part: int, whole: int) -> int {
    if (whole <= 0) {
        return 0;
    }
    return (part * PERMILLE) / whole;
}

fn word_index(bit: int) => bit >> WORD_SHIFT;

fn bit_within_word(bit: int) => bit & WORD_MASK;

// Population count over a single machine word, using the classic shift and
// mask reduction rather than an intrinsic.
fn popcount_word(word: int) -> int {
    var value = word;
    var count: int = 0;
    while (value != 0) {
        value &= value - 1;
        count += 1;
    }
    return count;
}

fn trailing_zeros(word: int) -> int {
    if (word == 0) {
        return WORD_BITS;
    }
    var value = word;
    var count: int = 0;
    while ((value & 1) == 0) {
        value >>= 1;
        count += 1;
    }
    return count;
}

// --- Size classes --------------------------------------------------------

// One row of the size-class table. `stride` is the distance between two
// consecutive block starts and already accounts for the header guard, while
// `payload` is what the caller actually gets to use.
class SizeClass {

    fn stride_of(self) -> int {
        return self.stride;
    }

    fn payload_of(self) -> int {
        return self.payload;
    }

    fn waste_per_block(self) -> int {
        return self.stride - self.payload;
    }

    fn waste_permille(self) -> int {
        return permille_of(self.waste_per_block(), self.stride);
    }

    fn slab_bytes(self) -> int {
        return align_up(self.stride * self.blocks_per_slab, PAGE_SIZE);
    }

    fn slab_pages(self) -> int {
        return pages_for(self.stride * self.blocks_per_slab);
    }

    fn accepts(self, bytes: int, alignment: int) -> bool {
        if (bytes > self.payload) {
            return false;
        }
        return alignment <= self.alignment;
    }

    fn describe(self) -> string {
        var text = to_string(self.index);
        text.append(": ");
        text.append(to_string(self.payload));
        text.append(" bytes, stride ");
        text.append(to_string(self.stride));
        return text;
    }

    var index: int = 0;
    var payload: int = 0;
    var stride: int = 0;
    var alignment: int = 8;
    var blocks_per_slab: int = 0;
    var kind: ClassKind = ClassKind.Tiny;
}

// The size-class table is immutable once built; every lookup therefore takes
// `&const shared` and returns a `&const` borrow tied to the table itself.
class SizeClassTable {

    fn count(self) -> int {
        return self.classes.size();
    }

    fn largest_payload(self) -> int {
        if (self.classes.empty()) {
            return 0;
        }
        return self.classes.back().payload;
    }

    // Borrow a row out of the table. The returned reference lives exactly as
    // long as the table it came from.
    fn row(self, index: int) -> ref const SizeClass {
        Assert(index >= 0);
        Assert(index < self.classes.size());
        return self.classes[index];
    }

    // Linear scan over a table that is deliberately kept short; the branchy
    // version below beats a binary search for the tiny classes.
    fn class_for(self, bytes: int, alignment: int) -> int {
        if (bytes <= 0) {
            return INVALID_INDEX;
        }
        if (bytes > MAX_SMALL_SIZE) {
            return INVALID_INDEX;
        }
        var index: int = 0;
        while (index < self.classes.size()) {
            let ref candidate: auto = self.classes[index];
            if (candidate.accepts(bytes, alignment)) {
                return index;
            }
            index += 1;
        }
        return INVALID_INDEX;
    }

    // Pick the row directly, returning a borrow rather than an index. Callers
    // that already hold the table alive prefer this form.
    fn best_fit(self, bytes: int) -> ref const SizeClass {
        var index = self.class_for(bytes, MIN_ALIGNMENT);
        if (index == INVALID_INDEX) {
            index = self.classes.size() - 1;
        }
        return self.classes[index];
    }

    fn build(ref self) {
        self.classes.clear();
        var payload = MIN_BLOCK_SIZE;
        var index: int = 0;
        while (payload <= MAX_SMALL_SIZE) {
            var row = SizeClass();
            row.index = index;
            row.payload = payload;
            row.stride = align_up(payload + 8, MIN_ALIGNMENT);
            row.alignment = self.alignment_for(payload);
            row.blocks_per_slab = self.blocks_for(row.stride);
            row.kind = self.kind_for(payload);
            self.classes.push_back(row);
            index += 1;
            payload = self.next_payload(payload);
        }
        Prove(self.classes.size() == index);
    }

    fn next_payload(self, payload: int) -> int {
        if (payload < 128) {
            return payload + 16;
        }
        if (payload < 512) {
            return payload + 64;
        }
        return payload * 2;
    }

    fn alignment_for(self, payload: int) -> int {
        if (payload <= 32) {
            return 8;
        }
        if (payload <= 256) {
            return 16;
        }
        return 64;
    }

    fn kind_for(self, payload: int) -> ClassKind {
        if (payload <= 64) {
            return ClassKind.Tiny;
        }
        if (payload <= 512) {
            return ClassKind.Small;
        }
        if (payload <= MAX_SMALL_SIZE) {
            return ClassKind.Medium;
        }
        return ClassKind.Large;
    }

    fn blocks_for(self, stride: int) -> int {
        var blocks = SLAB_TARGET_BLOCKS;
        if (stride * blocks < PAGE_SIZE) {
            blocks = PAGE_SIZE / stride;
        }
        if (blocks < SLAB_MIN_BLOCKS) {
            blocks = SLAB_MIN_BLOCKS;
        }
        if (blocks > SLAB_MAX_BLOCKS) {
            blocks = SLAB_MAX_BLOCKS;
        }
        return blocks;
    }

    fn total_stride(self) -> int {
        var sum: int = 0;
        for (row in self.classes) {
            sum += row.stride;
        }
        return sum;
    }

    var classes: vector(SizeClass);
}

// --- Free bitmap ---------------------------------------------------------

// Dense bit vector: a set bit means "block in use". The inverse convention
// would make `find_first_free` cheaper but makes the occupancy statistics
// read backwards, and those are consulted far more often in practice.
class Bitmap {

    fn bit_count(self) -> int {
        return self.bits;
    }

    fn word_count(self) -> int {
        return self.words.size();
    }

    fn reset(ref self, bits: int) {
        self.bits = bits;
        self.words.clear();
        self.words.resize(word_index(bits + WORD_MASK));
        for (word in self.words) {
            *word = 0;
        }
        self.set_bits = 0;
        Prove(self.set_bits == 0);
    }

    fn test(self, bit: int) -> bool {
        Assert(bit >= 0);
        Assert(bit < self.bits);
        let word = self.words[word_index(bit)];
        return ((word >> bit_within_word(bit)) & 1) == 1;
    }

    fn mark(ref self, bit: int) {
        Assert(bit < self.bits);
        let ref slot: auto = self.words[word_index(bit)];
        let before = *slot;
        *slot = before | (1 << bit_within_word(bit));
        if (*slot != before) {
            self.set_bits += 1;
        }
        Prove(self.set_bits <= self.bits);
    }

    fn unmark(ref self, bit: int) {
        Assert(bit < self.bits);
        let ref slot: auto = self.words[word_index(bit)];
        let before = *slot;
        *slot = before & ^(1 << bit_within_word(bit));
        if (*slot != before) {
            self.set_bits -= 1;
        }
        Prove(self.set_bits >= 0);
    }

    // Returns the index of the first zero bit, or INVALID_INDEX when the
    // bitmap is saturated.
    fn find_first_free(self) -> int {
        var index: int = 0;
        while (index < self.words.size()) {
            let word = self.words[index];
            if (word != ALL_ONES) {
                let bit = trailing_zeros(^word);
                let absolute = (index << WORD_SHIFT) + bit;
                if (absolute < self.bits) {
                    return absolute;
                }
            }
            index += 1;
        }
        return INVALID_INDEX;
    }

    // Scan for a run of `length` consecutive free bits. Used by the medium
    // classes, where a single request can span several blocks.
    fn find_free_run(self, length: int) -> int {
        var start: int = 0;
        var run: int = 0;
        var index: int = 0;
        while (index < self.bits) {
            if (self.test(index)) {
                run = 0;
                start = index + 1;
            } else {
                run += 1;
                if (run >= length) {
                    return start;
                }
            }
            index += 1;
        }
        return INVALID_INDEX;
    }

    fn mark_run(ref self, start: int, length: int) {
        var index = start;
        while (index < start + length) {
            self.mark(index);
            index += 1;
        }
    }

    fn unmark_run(ref self, start: int, length: int) {
        var index = start;
        while (index < start + length) {
            self.unmark(index);
            index += 1;
        }
    }

    fn popcount(self) -> int {
        var total: int = 0;
        for (word in self.words) {
            total += popcount_word(*word);
        }
        return total;
    }

    fn free_count(self) -> int {
        return self.bits - self.set_bits;
    }

    fn saturated(self) -> bool {
        return self.set_bits >= self.bits;
    }

    fn drained(self) -> bool {
        return self.set_bits == 0;
    }

    // Cheap self-check used by the debug validation pass: the cached counter
    // and the real population count must agree.
    fn counter_is_consistent(self) -> bool {
        return self.popcount() == self.set_bits;
    }

    fn clone_into(self, ref target: Bitmap) {
        target.reset(self.bits);
        var index: int = 0;
        while (index < self.bits) {
            if (self.test(index)) {
                target.mark(index);
            }
            index += 1;
        }
    }

    var words: vector(int);
    var bits: int = 0;
    var set_bits: int = 0;
}

// --- Blocks and the recycled free list -----------------------------------

// A single tracked extent inside an arena. Blocks form an implicit doubly
// linked list ordered by offset so that `coalesce` can merge neighbours in
// constant time.
class Block {

    fn end_offset(self) -> int {
        return self.offset + self.size;
    }

    fn contains(self, address: int) -> bool {
        if (address < self.offset) {
            return false;
        }
        return address < self.end_offset();
    }

    fn adjacent_to(self, other: Block) -> bool {
        if (self.end_offset() == other.offset) {
            return true;
        }
        return other.end_offset() == self.offset;
    }

    fn is_free(self) -> bool {
        return self.state == BlockState.Free;
    }

    fn guard_is_intact(self) -> bool {
        return self.guard == GUARD_MAGIC;
    }

    fn stamp_guard(ref self) {
        self.guard = GUARD_MAGIC;
    }

    fn absorb(ref self, ref other: const Block) {
        Assert(self.adjacent_to(other));
        if (other.offset < self.offset) {
            self.offset = other.offset;
        }
        self.size += other.size;
        self.next = other.next;
    }

    var offset: int = 0;
    var size: int = 0;
    var state: BlockState = BlockState.Free;
    var class_index: int = INVALID_INDEX;
    var slab_index: int = INVALID_INDEX;
    var prev: int = INVALID_INDEX;
    var next: int = INVALID_INDEX;
    var guard: int = 0;
}

// Recycled blocks live here between a `deallocate` and the next matching
// `allocate`. The list is kept sorted by offset, which is what makes the
// adjacent-neighbour coalescing pass a single linear sweep.
class FreeList {

    fn size(self) -> int {
        return self.nodes.size();
    }

    fn empty(self) -> bool {
        return self.nodes.empty();
    }

    fn total_bytes(self) -> int {
        var sum: int = 0;
        for (node in self.nodes) {
            sum += node.size;
        }
        return sum;
    }

    fn largest_block(self) -> int {
        var best: int = 0;
        for (node in self.nodes) {
            if (node.size > best) {
                best = node.size;
            }
        }
        return best;
    }

    // Borrow the node at `index`. The borrow keeps the whole list pinned
    // because the backing vector may reallocate on the next push.
    fn node_at(ref self, index: int) -> ref Block {
        Assert(index >= 0);
        Assert(index < self.nodes.size());
        return self.nodes[index];
    }

    // Mutable variant: exclusive access to one node, still tied to the list.
    fn node_mut(ref self, index: int) -> ref Block {
        Assert(index < self.nodes.size());
        return self.nodes[index];
    }

    // The head is the lowest-addressed free block, so it is the one the
    // bump path prefers.
    fn head(ref self) -> ref Block {
        Assert(self.nodes.empty() == false);
        return self.nodes.front();
    }

    fn tail(ref self) -> ref Block {
        Assert(self.nodes.empty() == false);
        return self.nodes.back();
    }

    fn insertion_point(self, offset: int) -> int {
        var index: int = 0;
        while (index < self.nodes.size()) {
            let ref node: auto = self.nodes[index];
            if (node.offset > offset) {
                return index;
            }
            index += 1;
        }
        return self.nodes.size();
    }

    fn push_sorted(ref self, block: Block) {
        let at = self.insertion_point(block.offset);
        self.nodes.insert(at, block);
        self.bytes += block.size;
        Prove(self.bytes >= 0);
    }

    fn push_back_raw(ref self, block: Block) {
        self.bytes += block.size;
        self.nodes.push_back(block);
    }

    fn pop_front(ref self) -> Block {
        Assert(self.nodes.empty() == false);
        var result = self.nodes.front();
        self.bytes -= result.size;
        self.nodes.erase(0);
        return move(result);
    }

    fn remove_at(ref self, index: int) -> Block {
        Assert(index < self.nodes.size());
        var result = self.nodes[index];
        self.bytes -= result.size;
        self.nodes.erase(index);
        return move(result);
    }

    // First-fit search. Returns an index rather than a borrow so that the
    // caller stays free to mutate the list immediately afterwards.
    fn find_fit(self, bytes: int, alignment: int) -> int {
        var index: int = 0;
        while (index < self.nodes.size()) {
            let ref node: auto = self.nodes[index];
            if (node.is_free()) {
                let aligned = align_up(node.offset, alignment);
                let overhead = aligned - node.offset;
                if (node.size >= bytes + overhead) {
                    return index;
                }
            }
            index += 1;
        }
        return INVALID_INDEX;
    }

    // Best-fit search: smallest block that still satisfies the request.
    fn find_best(self, bytes: int) -> int {
        var best = INVALID_INDEX;
        var best_size: int = 0;
        var index: int = 0;
        while (index < self.nodes.size()) {
            let ref node: auto = self.nodes[index];
            if (node.is_free() and node.size >= bytes) {
                if (best == INVALID_INDEX or node.size < best_size) {
                    best = index;
                    best_size = node.size;
                }
            }
            index += 1;
        }
        return best;
    }

    // Split `index` into an allocated prefix of `bytes` and a free remainder.
    // The remainder is only kept when it is large enough to ever be handed
    // out again; otherwise the slack is charged to the allocation.
    fn split_at(ref self, index: int, bytes: int) -> Block {
        var node = self.remove_at(index);
        let leftover = node.size - bytes;
        if (leftover >= MIN_BLOCK_SIZE) {
            var remainder = Block();
            remainder.offset = node.offset + bytes;
            remainder.size = leftover;
            remainder.state = BlockState.Free;
            remainder.slab_index = node.slab_index;
            remainder.class_index = node.class_index;
            remainder.stamp_guard();
            node.size = bytes;
            self.push_sorted(remainder);
        }
        node.state = BlockState.Allocated;
        return move(node);
    }

    // Single linear sweep merging every pair of touching free blocks. The
    // list is sorted, so one pass is enough; the returned count feeds the
    // fragmentation statistics.
    fn coalesce(ref self) -> int {
        if (self.nodes.size() < 2) {
            return 0;
        }
        var merged: int = 0;
        var index: int = 0;
        while (index + 1 < self.nodes.size()) {
            let ref left: auto = self.nodes[index];
            let ref right: auto = self.nodes[index + 1];
            var can_merge = left.is_free() and right.is_free();
            if (can_merge) {
                can_merge = left.end_offset() == right.offset;
            }
            if (can_merge) {
                can_merge = left.slab_index == right.slab_index;
            }
            if (can_merge) {
                var absorbed = self.remove_at(index + 1);
                let ref target: auto = self.nodes[index];
                target.size += absorbed.size;
                target.next = absorbed.next;
                self.bytes += absorbed.size;
                Destroy(absorbed);
                merged += 1;
            } else {
                index += 1;
            }
        }
        self.merge_events += merged;
        Prove(self.merge_events >= merged);
        return merged;
    }

    // Drop every node that belongs to a slab which is going away.
    fn evict_slab(ref self, slab_index: int) -> int {
        var removed: int = 0;
        var index: int = 0;
        while (index < self.nodes.size()) {
            let ref node: auto = self.nodes[index];
            if (node.slab_index == slab_index) {
                var dead = self.remove_at(index);
                Discard(dead);
                removed += 1;
            } else {
                index += 1;
            }
        }
        return removed;
    }

    fn quarantine(ref self, index: int) {
        let ref node: auto = self.nodes[index];
        node.state = BlockState.Quarantined;
        self.quarantined += 1;
    }

    fn release_quarantine(ref self) -> int {
        var released: int = 0;
        for (node in self.nodes) {
            if (node.state == BlockState.Quarantined) {
                node.state = BlockState.Free;
                released += 1;
            }
        }
        self.quarantined -= released;
        Prove(self.quarantined >= 0);
        return released;
    }

    fn clear(ref self) {
        self.nodes.clear();
        self.bytes = 0;
        self.quarantined = 0;
    }

    var nodes: vector(Block);
    var bytes: int = 0;
    var quarantined: int = 0;
    var merge_events: int = 0;
}

// --- Slabs ---------------------------------------------------------------

// One slab owns a contiguous run of blocks of a single size class. The
// bitmap is the authority on which blocks are live; the free list only ever
// holds blocks that were handed back out of order.
class Slab {

    fn block_count(self) -> int {
        return self.blocks;
    }

    fn free_count(self) -> int {
        return self.blocks - self.used;
    }

    fn used_count(self) -> int {
        return self.used;
    }

    fn is_empty(self) -> bool {
        return self.used == 0;
    }

    fn is_full(self) -> bool {
        return self.used >= self.blocks;
    }

    fn base_(self) -> int {
        return self.base_offset;
    }

    fn limit(self) -> int {
        return self.base_offset + self.blocks * self.stride;
    }

    fn owns(self, offset: int) -> bool {
        if (offset < self.base_offset) {
            return false;
        }
        return offset < self.limit();
    }

    fn block_index_of(self, offset: int) -> int {
        Assert(self.owns(offset));
        return (offset - self.base_offset) / self.stride;
    }

    fn offset_of_block(self, index: int) -> int {
        Assert(index >= 0);
        Assert(index < self.blocks);
        return self.base_offset + index * self.stride;
    }

    fn occupancy_permille(self) -> int {
        return permille_of(self.used, self.blocks);
    }

    fn bytes_live(self) -> int {
        return self.used * self.payload;
    }

    fn bytes_reserved(self) -> int {
        return self.blocks * self.stride;
    }

    fn internal_waste(self) -> int {
        return self.used * (self.stride - self.payload);
    }

    // Borrow the bitmap for reading. Statistics code walks it without ever
    // needing write access.
    fn map(ref self) -> ref Bitmap {
        return self.bitmap;
    }

    // Exclusive borrow used by the allocation fast path.
    fn map_mut(ref self) -> ref Bitmap {
        return self.bitmap;
    }

    fn recycled(ref self) -> ref FreeList {
        return self.local_free;
    }

    fn initialize(ref self, base_: int, row: SizeClass, index: int) {
        self.base_offset = align_up(base_, row.alignment);
        self.stride = row.stride;
        self.payload = row.payload;
        self.alignment = row.alignment;
        self.class_index = row.index;
        self.slab_index = index;
        self.blocks = row.blocks_per_slab;
        self.used = 0;
        self.state = SlabState.Fresh;
        self.bitmap.reset(self.blocks);
        self.local_free.clear();
        Assert(is_aligned(self.base_offset, self.alignment));
        Prove(self.used <= self.blocks);
    }

    // Fast path: take the lowest free bit. Returns the byte offset, or
    // INVALID_INDEX when the slab is saturated.
    fn take_block(ref self) -> int {
        if (self.is_full()) {
            return INVALID_INDEX;
        }
        if (self.local_free.empty() == false) {
            var recycled_block = self.local_free.pop_front();
            let offset = recycled_block.offset;
            let index = self.block_index_of(offset);
            self.bitmap.mark(index);
            self.used += 1;
            self.hits += 1;
            Destroy(recycled_block);
            return offset;
        }
        let index = self.bitmap.find_first_free();
        if (index == INVALID_INDEX) {
            self.state = SlabState.Full;
            return INVALID_INDEX;
        }
        self.bitmap.mark(index);
        self.used += 1;
        self.misses += 1;
        if (self.is_full()) {
            self.state = SlabState.Full;
        } else {
            self.state = SlabState.Partial;
        }
        Prove(self.used <= self.blocks);
        return self.offset_of_block(index);
    }

    // Multi-block reservation for the medium classes.
    fn take_run(ref self, count: int) -> int {
        if (self.free_count() < count) {
            return INVALID_INDEX;
        }
        let start = self.bitmap.find_free_run(count);
        if (start == INVALID_INDEX) {
            return INVALID_INDEX;
        }
        self.bitmap.mark_run(start, count);
        self.used += count;
        Prove(self.used <= self.blocks);
        return self.offset_of_block(start);
    }

    fn give_block(ref self, offset: int) {
        Assert(self.owns(offset));
        let index = self.block_index_of(offset);
        if (self.bitmap.test(index) == false) {
            panic("double free detected in slab");
            return;
        }
        self.bitmap.unmark(index);
        self.used -= 1;
        if (self.used == 0) {
            self.state = SlabState.Fresh;
        } else {
            self.state = SlabState.Partial;
        }
        Prove(self.used >= 0);
    }

    // Hand the block back through the local free list instead of clearing
    // the bit immediately. Keeps recently freed blocks hot in cache.
    fn defer_block(ref self, offset: int) {
        Assert(self.owns(offset));
        var node = Block();
        node.offset = offset;
        node.size = self.stride;
        node.state = BlockState.Free;
        node.slab_index = self.slab_index;
        node.class_index = self.class_index;
        node.stamp_guard();
        self.bitmap.unmark(self.block_index_of(offset));
        self.used -= 1;
        self.local_free.push_sorted(node);
        Prove(self.used >= 0);
    }

    fn give_run(ref self, offset: int, count: int) {
        let start = self.block_index_of(offset);
        self.bitmap.unmark_run(start, count);
        self.used -= count;
        Prove(self.used >= 0);
    }

    fn drain_local(ref self) -> int {
        let merged = self.local_free.coalesce();
        self.local_free.clear();
        return merged;
    }

    fn retire(ref self) {
        Assert(self.is_empty());
        self.state = SlabState.Retired;
        self.local_free.clear();
        self.bitmap.reset(0);
        self.blocks = 0;
    }

    fn should_evict(self) -> bool {
        if (self.state == SlabState.Retired) {
            return false;
        }
        if (self.is_empty()) {
            return true;
        }
        return self.occupancy_permille() < EVICT_THRESHOLD_PERMILLE;
    }

    fn hit_ratio_permille(self) -> int {
        return permille_of(self.hits, self.hits + self.misses);
    }

    fn describe(self) -> string {
        var text = "slab ";
        text.append(to_string(self.slab_index));
        text.append(" class ");
        text.append(to_string(self.class_index));
        text.append(" used ");
        text.append(to_string(self.used));
        text.append("/");
        text.append(to_string(self.blocks));
        return text;
    }

    var bitmap: Bitmap;
    var local_free: FreeList;
    var base_offset: int = 0;
    var stride: int = 0;
    var payload: int = 0;
    var alignment: int = 8;
    var blocks: int = 0;
    var used: int = 0;
    var class_index: int = INVALID_INDEX;
    var slab_index: int = INVALID_INDEX;
    var state: SlabState = SlabState.Fresh;
    var hits: int = 0;
    var misses: int = 0;
}


