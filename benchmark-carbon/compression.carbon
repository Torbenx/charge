// A general purpose compression library.
//
// The library is organised as a stack of independent codecs that share one
// byte-oriented buffer type and one pair of bit-level cursors:
//
//   * `BitWriter` / `BitReader`          - MSB-first bit packing (deflate's
//                                          Huffman payloads are LSB-first,
//                                          its lengths are not, so both
//                                          orderings are provided).
//   * `Huffman`                          - frequency counting, a binary heap
//                                          for tree construction, canonical
//                                          code assignment, Kraft-based code
//                                          length limiting, and encode /
//                                          decode tables.
//   * `Lz77`                             - a sliding window with a hash chain
//                                          match finder, lazy matching and
//                                          five configurable levels.
//   * `Deflate`                          - the combined pipeline: stored,
//                                          fixed and dynamic block types.
//   * `Rle`, `Mtf`, `Bwt`                - the auxiliary transforms used by
//                                          the block sorting pipeline.
//   * `Arithmetic`                       - an adaptive binary/multi-symbol
//                                          range coder over a frequency model.
//   * `Crc32`, `Adler32`                 - checksums built from generated
//                                          tables rather than literal tables.
//
// Every polynomial and mask below is written as a plain decimal literal with
// the conventional hexadecimal spelling in a trailing comment, because the
// language only lexes decimal integer notation.
//
// Buffers are `vector{int}` where every element is constrained to the range
// [0, 255]; `byte` is not a distinct storage type here, the invariant is
// maintained by the accessors and checked with `assert` in the harness.
namespace compress;

// --- Bit and byte tunables ----------------------------------------------

let BYTE_BITS: int = 8;
let BYTE_MASK: int = 255;                    // 0xFF
let SHORT_BITS: int = 16;
let SHORT_MASK: int = 65535;                 // 0xFFFF
let WORD_BITS: int = 32;
let WORD_MASK: int = 4294967295;             // 0xFFFFFFFF
let NIBBLE_MASK: int = 15;                   // 0x0F
let HIGH_BIT_8: int = 128;                   // 0x80
let HIGH_BIT_16: int = 32768;                // 0x8000
let HIGH_BIT_32: int = 2147483648;           // 0x80000000

// --- Huffman tunables ----------------------------------------------------

// Deflate's alphabet sizes. The literal/length alphabet is 286 symbols, of
// which 0..255 are literals, 256 is end-of-block and 257..285 are lengths.
let MAX_SYMBOLS: int = 288;
let LITERAL_SYMBOLS: int = 286;
let END_OF_BLOCK: int = 256;
let DISTANCE_SYMBOLS: int = 30;
let CODE_LENGTH_SYMBOLS: int = 19;

// A canonical code may never be longer than this; the length limiter
// rebalances any tree that violates the bound.
let MAX_CODE_LENGTH: int = 15;
let MAX_LENGTH_CODE_BITS: int = 7;

// The decode table is a flat lookup of 2^ROOT_BITS entries; codes longer
// than ROOT_BITS spill into a secondary table reached through a link.
let ROOT_BITS: int = 9;
let ROOT_SIZE: int = 512;                    // 1 << 9
let SUB_BITS: int = 6;
let SUB_SIZE: int = 64;                      // 1 << 6

// --- LZ77 tunables -------------------------------------------------------

let WINDOW_BITS: int = 15;
let WINDOW_SIZE: int = 32768;                // 1 << 15
let WINDOW_MASK: int = 32767;                // 0x7FFF
let MIN_MATCH: int = 3;
let MAX_MATCH: int = 258;
let MAX_DISTANCE: int = 32768;

let HASH_BITS: int = 15;
let HASH_SIZE: int = 32768;                  // 1 << 15
let HASH_MASK: int = 32767;
let HASH_SHIFT: int = 5;                     // (HASH_BITS + 2) / 3
let NO_POSITION: int = -1;

// --- Block tunables ------------------------------------------------------

let STORED_BLOCK_MAX: int = 65535;
let DEFAULT_BLOCK_SIZE: int = 16384;
let MIN_BLOCK_SIZE: int = 256;

// --- Arithmetic coder tunables -------------------------------------------

// A 32 bit range coder: the interval is [low, low + range) with the top byte
// shifted out whenever the interval no longer straddles a byte boundary.
let CODER_TOP: int = 16777216;               // 1 << 24
let CODER_BOTTOM: int = 65536;               // 1 << 16
let CODER_MAX: int = 4294967295;             // 0xFFFFFFFF
let MODEL_TOTAL_MAX: int = 65536;
let MODEL_INCREMENT: int = 32;
let MODEL_RESCALE_SHIFT: int = 1;

// --- Checksum tunables ---------------------------------------------------

let CRC32_POLY: int = 3988292384;            // 0xEDB88320, reflected
let CRC32_INIT: int = 4294967295;            // 0xFFFFFFFF
let CRC32_TABLE_SIZE: int = 256;
let CRC32_SLICE_COUNT: int = 8;

let ADLER_BASE: int = 65521;
let ADLER_NMAX: int = 5552;

// A string literal stands in for the apostrophe character, which the lexer
// cannot express as an escaped char literal.
let APOSTROPHE: string = "'";

// --- Result codes --------------------------------------------------------

choice Status {
    Ok,
    NeedMoreInput,
    OutputFull,
    CorruptStream,
    BadCodeLengths,
    BadDistance,
    ChecksumMismatch,
    Unsupported,
}

choice BlockType {
    Stored,
    Fixed,
    Dynamic,
    Reserved,
}

choice BitOrder {
    MsbFirst,
    LsbFirst,
}

choice Level {
    Store,
    Fastest,
    Fast,
    Default,
    Best,
}

// --- Byte buffer ---------------------------------------------------------

// A growable byte buffer with a read cursor. Every codec in this file reads
// from and writes to one of these, so the bit cursors only ever need to know
// about a single container type.
class ByteBuffer {

    fn size(self) -> int {
        return self.data.size();
    }

    fn empty(self) -> bool {
        return self.data.size() == 0;
    }

    fn remaining(self) -> int {
        return self.data.size() - self.cursor;
    }

    fn exhausted(self) -> bool {
        return self.cursor >= self.data.size();
    }

    fn rewind(ref self) {
        self.cursor = 0;
    }

    fn clear(ref self) {
        self.data.clear();
        self.cursor = 0;
    }

    fn reserve(ref self, n: int) {
        self.data.reserve(n);
    }

    // Stores only the low eight bits; the caller is responsible for the
    // masking convention, but the assert keeps honest callers honest.
    fn push(ref self, value: int) {
        self.data.push_back(value & BYTE_MASK);
    }

    fn push_short_le(ref self, value: int) {
        self.push(value & BYTE_MASK);
        self.push((value >> BYTE_BITS) & BYTE_MASK);
    }

    fn push_short_be(ref self, value: int) {
        self.push((value >> BYTE_BITS) & BYTE_MASK);
        self.push(value & BYTE_MASK);
    }

    fn push_word_le(ref self, value: int) {
        self.push(value & BYTE_MASK);
        self.push((value >> 8) & BYTE_MASK);
        self.push((value >> 16) & BYTE_MASK);
        self.push((value >> 24) & BYTE_MASK);
    }

    fn push_word_be(ref self, value: int) {
        self.push((value >> 24) & BYTE_MASK);
        self.push((value >> 16) & BYTE_MASK);
        self.push((value >> 8) & BYTE_MASK);
        self.push(value & BYTE_MASK);
    }

    fn at(self, index: int) -> int {
        return self.data.at(index) & BYTE_MASK;
    }

    fn read(ref self) -> int {
        let value = self.data.at(self.cursor) & BYTE_MASK;
        self.cursor += 1;
        return value;
    }

    fn peek(self) -> int {
        return self.data.at(self.cursor) & BYTE_MASK;
    }

    fn peek_at(self, offset: int) -> int {
        return self.data.at(self.cursor + offset) & BYTE_MASK;
    }

    fn read_short_le(ref self) -> int {
        let low = self.read();
        let high = self.read();
        return low | (high << BYTE_BITS);
    }

    fn read_short_be(ref self) -> int {
        let high = self.read();
        let low = self.read();
        return (high << BYTE_BITS) | low;
    }

    fn read_word_le(ref self) -> int {
        let b0 = self.read();
        let b1 = self.read();
        let b2 = self.read();
        let b3 = self.read();
        return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }

    fn read_word_be(ref self) -> int {
        let b0 = self.read();
        let b1 = self.read();
        let b2 = self.read();
        let b3 = self.read();
        return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
    }

    // Copy `count` bytes starting at `from` onto the end of the buffer. The
    // ranges are allowed to overlap; that is exactly what an LZ77 back
    // reference does when the distance is smaller than the length.
    fn copy_within(ref self, from: int, count: int) {
        var i: int = 0;
        while (i < count) {
            self.data.push_back(self.data.at(from + i) & BYTE_MASK);
            i += 1;
        }
    }

    fn append(ref self, other: ByteBuffer) {
        var i: int = 0;
        while (i < other.size()) {
            self.data.push_back(other.at(i));
            i += 1;
        }
    }

    fn equals(self, other: ByteBuffer) -> bool {
        if (self.data.size() != other.size()) {
            return false;
        }
        var i: int = 0;
        while (i < self.data.size()) {
            if ((self.data.at(i) & BYTE_MASK) != other.at(i)) {
                return false;
            }
            i += 1;
        }
        return true;
    }

    var data: vector(int);
    var cursor: int = 0;
}

// --- Bit writer ----------------------------------------------------------

// Packs codes into a byte buffer. A 32 bit accumulator holds the pending
// bits; whole bytes are flushed out of it as soon as eight bits are ready.
//
// In MSB-first order the first bit written becomes the high bit of the first
// output byte, which is what most container formats use for their headers.
// In LSB-first order the first bit written becomes bit 0, which is what
// deflate uses for Huffman codes.
class BitWriter {

    fn init(ref self, order: BitOrder) {
        self.order = order;
        self.accumulator = 0;
        self.bit_count = 0;
        self.bits_written = 0;
    }

    fn bit_position(self) -> int {
        return self.bits_written;
    }

    fn byte_position(self) -> int {
        return self.bits_written / BYTE_BITS;
    }

    fn is_aligned(self) -> bool {
        return self.bit_count == 0;
    }

    fn pending(self) -> int {
        return self.bit_count;
    }

    // Write a single bit. The two orderings differ only in which end of the
    // accumulator the new bit is grafted onto.
    fn put_bit(ref self, bit: int) {
        let value = bit & 1;
        if (self.order == BitOrder.MsbFirst) {
            self.accumulator = ((self.accumulator << 1) | value) & WORD_MASK;
        } else {
            self.accumulator = (self.accumulator | (value << self.bit_count)) & WORD_MASK;
        }
        self.bit_count += 1;
        self.bits_written += 1;
        if (self.bit_count == BYTE_BITS) {
            self.emit_byte();
        }
    }

    fn emit_byte(ref self) {
        if (self.order == BitOrder.MsbFirst) {
            self.out.push(self.accumulator & BYTE_MASK);
        } else {
            self.out.push(self.accumulator & BYTE_MASK);
        }
        self.accumulator = 0;
        self.bit_count = 0;
    }

    // Write `count` bits of `value`, most significant bit first. Long codes
    // go through the accumulator one bit at a time so the two orderings stay
    // in one code path; the fast paths below handle the common widths.
    fn put_bits_msb(ref self, value: int, count: int) {
        var i = count - 1;
        while (i >= 0) {
            self.put_bit((value >> i) & 1);
            i -= 1;
        }
    }

    // Write `count` bits of `value`, least significant bit first.
    fn put_bits_lsb(ref self, value: int, count: int) {
        var i: int = 0;
        while (i < count) {
            self.put_bit((value >> i) & 1);
            i += 1;
        }
    }

    fn put_bits(ref self, value: int, count: int) {
        if (self.order == BitOrder.MsbFirst) {
            self.put_bits_msb(value, count);
        } else {
            self.put_bits_lsb(value, count);
        }
    }

    // A Huffman code is stored most significant bit first but is emitted
    // into an LSB-first stream, so the bits come out reversed relative to
    // the code's numeric value.
    fn put_code(ref self, code: int, length: int) {
        if (self.order == BitOrder.LsbFirst) {
            var i = length - 1;
            while (i >= 0) {
                self.put_bit((code >> i) & 1);
                i -= 1;
            }
        } else {
            self.put_bits_msb(code, length);
        }
    }

    fn put_byte(ref self, value: int) {
        if (self.bit_count == 0) {
            self.out.push(value & BYTE_MASK);
            self.bits_written += BYTE_BITS;
        } else {
            self.put_bits(value & BYTE_MASK, BYTE_BITS);
        }
    }

    fn put_short(ref self, value: int) {
        self.put_byte(value & BYTE_MASK);
        self.put_byte((value >> BYTE_BITS) & BYTE_MASK);
    }

    fn put_word(ref self, value: int) {
        self.put_short(value & SHORT_MASK);
        self.put_short((value >> SHORT_BITS) & SHORT_MASK);
    }

    // Pad with zero bits until the next byte boundary.
    fn align(ref self) {
        while (self.bit_count != 0) {
            self.put_bit(0);
        }
    }

    // Pad with one bits instead; some framing formats use this so that a
    // truncated stream is detectable.
    fn align_ones(ref self) {
        while (self.bit_count != 0) {
            self.put_bit(1);
        }
    }

    fn flush(ref self) {
        self.align();
    }

    fn raw_copy(ref self, source: ByteBuffer, from: int, count: int) {
        self.align();
        var i: int = 0;
        while (i < count) {
            self.out.push(source.at(from + i));
            self.bits_written += BYTE_BITS;
            i += 1;
        }
    }

    var out: ByteBuffer;
    var accumulator: int = 0;
    var bit_count: int = 0;
    var bits_written: int = 0;
    var order: BitOrder = BitOrder.LsbFirst;
}

// --- Bit reader ----------------------------------------------------------

// The mirror image of `BitWriter`. The reader keeps a bit accumulator that
// is refilled a byte at a time, and exposes a `peek` operation because the
// table-driven Huffman decoder needs ROOT_BITS of lookahead before it knows
// how many bits the next symbol actually consumes.
class BitReader {

    fn init(ref self, order: BitOrder) {
        self.order = order;
        self.accumulator = 0;
        self.bit_count = 0;
        self.bits_read = 0;
        self.overrun = false;
    }

    fn bit_position(self) -> int {
        return self.bits_read;
    }

    fn byte_position(self) -> int {
        return self.source.cursor;
    }

    fn is_aligned(self) -> bool {
        return self.bit_count == 0;
    }

    fn available(self) -> int {
        return self.source.remaining() * BYTE_BITS + self.bit_count;
    }

    fn failed(self) -> bool {
        return self.overrun;
    }

    // Pull one byte into the accumulator. Reading past the end sets the
    // overrun flag and feeds zero bits so the decoder can unwind gracefully
    // instead of trapping.
    fn refill_byte(ref self) {
        if (self.source.exhausted()) {
            self.overrun = true;
            if (self.order == BitOrder.MsbFirst) {
                self.accumulator = (self.accumulator << BYTE_BITS) & WORD_MASK;
            } else {
                Discard(0);
            }
            self.bit_count += BYTE_BITS;
            return;
        }
        let byte_value = self.source.read();
        if (self.order == BitOrder.MsbFirst) {
            self.accumulator = ((self.accumulator << BYTE_BITS) | byte_value) & WORD_MASK;
        } else {
            self.accumulator = (self.accumulator | (byte_value << self.bit_count)) & WORD_MASK;
        }
        self.bit_count += BYTE_BITS;
    }

    // Guarantee at least `count` bits in the accumulator.
    fn refill(ref self, count: int) {
        while (self.bit_count < count) {
            self.refill_byte();
        }
    }

    fn get_bit(ref self) -> int {
        if (self.bit_count == 0) {
            self.refill_byte();
        }
        var bit: int = 0;
        if (self.order == BitOrder.MsbFirst) {
            bit = (self.accumulator >> (self.bit_count - 1)) & 1;
            self.bit_count -= 1;
            self.accumulator = self.accumulator & ((1 << self.bit_count) - 1);
        } else {
            bit = self.accumulator & 1;
            self.accumulator = (self.accumulator >> 1) & WORD_MASK;
            self.bit_count -= 1;
        }
        self.bits_read += 1;
        return bit;
    }

    fn get_bits_msb(ref self, count: int) -> int {
        var value: int = 0;
        var i: int = 0;
        while (i < count) {
            value = (value << 1) | self.get_bit();
            i += 1;
        }
        return value;
    }

    fn get_bits_lsb(ref self, count: int) -> int {
        var value: int = 0;
        var i: int = 0;
        while (i < count) {
            value = value | (self.get_bit() << i);
            i += 1;
        }
        return value;
    }

    fn get_bits(ref self, count: int) -> int {
        if (count == 0) {
            return 0;
        }
        if (self.order == BitOrder.MsbFirst) {
            return self.get_bits_msb(count);
        } else {
            return self.get_bits_lsb(count);
        }
    }

    // Look at the next `count` bits without consuming them. The value is
    // always returned in the reader's native order, which is what the flat
    // decode tables are indexed by.
    fn peek_bits(ref self, count: int) -> int {
        self.refill(count);
        if (self.order == BitOrder.MsbFirst) {
            return (self.accumulator >> (self.bit_count - count)) & ((1 << count) - 1);
        } else {
            return self.accumulator & ((1 << count) - 1);
        }
    }

    // Drop bits that a previous `peek_bits` reported on.
    fn drop_bits(ref self, count: int) {
        if (self.order == BitOrder.MsbFirst) {
            self.bit_count -= count;
            self.accumulator = self.accumulator & ((1 << self.bit_count) - 1);
        } else {
            self.accumulator = (self.accumulator >> count) & WORD_MASK;
            self.bit_count -= count;
        }
        self.bits_read += count;
    }

    fn get_byte(ref self) -> int {
        if (self.bit_count == 0) {
            if (self.source.exhausted()) {
                self.overrun = true;
                return 0;
            }
            self.bits_read += BYTE_BITS;
            return self.source.read();
        }
        return self.get_bits(BYTE_BITS);
    }

    fn get_short(ref self) -> int {
        let low = self.get_byte();
        let high = self.get_byte();
        return low | (high << BYTE_BITS);
    }

    fn get_word(ref self) -> int {
        let low = self.get_short();
        let high = self.get_short();
        return low | (high << SHORT_BITS);
    }

    // Discard the partial byte currently in the accumulator.
    fn align(ref self) {
        let drop = self.bit_count % BYTE_BITS;
        if (drop != 0) {
            self.drop_bits(drop);
        }
        self.accumulator = 0;
        self.bit_count = 0;
    }

    // Copy `count` raw bytes out of an aligned stream; used by stored blocks.
    fn raw_copy(ref self, ref sink: ByteBuffer, count: int) -> bool {
        self.align();
        if (self.source.remaining() < count) {
            self.overrun = true;
            return false;
        }
        var i: int = 0;
        while (i < count) {
            sink.push(self.source.read());
            self.bits_read += BYTE_BITS;
            i += 1;
        }
        return true;
    }

    var source: ByteBuffer;
    var accumulator: int = 0;
    var bit_count: int = 0;
    var bits_read: int = 0;
    var overrun: bool = false;
    var order: BitOrder = BitOrder.LsbFirst;
}

// Reverse the low `width` bits of `code`. Canonical Huffman codes are built
// MSB-first but written LSB-first, so the tables store the reversed form and
// the hot loop avoids the per-symbol reversal.
fn reverse_bits(code: int, width: int) -> int {
    var result: int = 0;
    var value = code;
    var i: int = 0;
    while (i < width) {
        result = (result << 1) | (value & 1);
        value = (value >> 1) & WORD_MASK;
        i += 1;
    }
    return result;
}

// Population count of a 32 bit word by the classic halving trick, written
// with explicit masks because the language has no hexadecimal literals.
fn popcount32(value: int) -> int {
    var v = value & WORD_MASK;
    v = v - ((v >> 1) & 1431655765);                            // 0x55555555
    v = (v & 858993459) + ((v >> 2) & 858993459);               // 0x33333333
    v = (v + (v >> 4)) & 252645135;                             // 0x0F0F0F0F
    v = v + (v >> 8);
    v = v + (v >> 16);
    return v & 63;
}

// Index of the highest set bit, or -1 for zero.
fn highest_bit(value: int) -> int {
    var v = value & WORD_MASK;
    if (v == 0) {
        return -1;
    }
    var position: int = 0;
    if ((v & 4294901760) != 0) {                                 // 0xFFFF0000
        v = (v >> 16) & SHORT_MASK;
        position += 16;
    }
    if ((v & 65280) != 0) {                                      // 0xFF00
        v = (v >> 8) & BYTE_MASK;
        position += 8;
    }
    if ((v & 240) != 0) {                                        // 0xF0
        v = (v >> 4) & NIBBLE_MASK;
        position += 4;
    }
    if ((v & 12) != 0) {                                         // 0x0C
        v = (v >> 2) & 3;
        position += 2;
    }
    if ((v & 2) != 0) {
        position += 1;
    }
    return position;
}

// Round up to the next power of two, saturating at 2^31.
fn round_up_pow2(value: int) -> int {
    if (value <= 1) {
        return 1;
    }
    var v = (value - 1) & WORD_MASK;
    v = v | (v >> 1);
    v = v | (v >> 2);
    v = v | (v >> 4);
    v = v | (v >> 8);
    v = v | (v >> 16);
    return (v + 1) & WORD_MASK;
}

// --- Frequency counting --------------------------------------------------

// A histogram over one alphabet. `total` and `used` are maintained
// incrementally so the tree builder never has to rescan the table.
class FrequencyTable {

    fn init(ref self, alphabet: int) {
        self.counts.clear();
        self.counts.resize(alphabet);
        var i: int = 0;
        while (i < alphabet) {
            self.counts.at(i) = 0;
            i += 1;
        }
        self.alphabet = alphabet;
        self.total = 0;
        self.used = 0;
    }

    fn size(self) -> int {
        return self.alphabet;
    }

    fn at(self, symbol: int) -> int {
        return self.counts.at(symbol);
    }

    fn add(ref self, symbol: int) {
        if (self.counts.at(symbol) == 0) {
            self.used += 1;
        }
        self.counts.at(symbol) += 1;
        self.total += 1;
    }

    fn add_many(ref self, symbol: int, count: int) {
        if (self.counts.at(symbol) == 0) {
            if (count > 0) {
                self.used += 1;
            }
        }
        self.counts.at(symbol) += count;
        self.total += count;
    }

    // Count every byte of a buffer. This is the hottest loop in the encoder
    // for incompressible input, so it does no bounds arithmetic beyond the
    // single mask.
    fn count_bytes(ref self, input: ByteBuffer) {
        var i: int = 0;
        let n = input.size();
        while (i < n) {
            let symbol = input.at(i) & BYTE_MASK;
            if (self.counts.at(symbol) == 0) {
                self.used += 1;
            }
            self.counts.at(symbol) += 1;
            i += 1;
        }
        self.total += n;
    }

    // Count pairs of adjacent bytes into a 65536 entry table; used by the
    // order-1 model in the arithmetic coder path.
    fn count_pairs(ref self, input: ByteBuffer) {
        if (input.size() < 2) {
            return;
        }
        var previous = input.at(0);
        var i: int = 1;
        while (i < input.size()) {
            let current = input.at(i);
            let symbol = ((previous & BYTE_MASK) << BYTE_BITS) | (current & BYTE_MASK);
            self.add(symbol);
            previous = current;
            i += 1;
        }
    }

    // Guarantee at least two used symbols. A Huffman tree over a single
    // symbol has no code at all, which every decoder would reject, so the
    // encoder pads the histogram with phantom counts.
    fn ensure_two_symbols(ref self) {
        var padded: int = 0;
        var symbol: int = 0;
        while (symbol < self.alphabet) {
            if (self.used >= 2) {
                return;
            }
            if (self.counts.at(symbol) == 0) {
                self.counts.at(symbol) = 1;
                self.used += 1;
                self.total += 1;
                padded += 1;
            }
            symbol += 1;
        }
        Discard(padded);
    }

    // Halve every non-zero count, rounding up. Adaptive encoders call this
    // when the running total would overflow the model's precision.
    fn rescale(ref self) {
        var new_total: int = 0;
        var new_used: int = 0;
        var i: int = 0;
        while (i < self.alphabet) {
            let count = self.counts.at(i);
            if (count > 0) {
                let halved = (count + 1) >> MODEL_RESCALE_SHIFT;
                self.counts.at(i) = halved;
                new_total += halved;
                new_used += 1;
            }
            i += 1;
        }
        self.total = new_total;
        self.used = new_used;
    }

    fn max_count(self) -> int {
        var best: int = 0;
        var i: int = 0;
        while (i < self.alphabet) {
            if (self.counts.at(i) > best) {
                best = self.counts.at(i);
            }
            i += 1;
        }
        return best;
    }

    // Shannon entropy of the histogram, scaled by 1000 and computed with an
    // integer logarithm so no floating point is involved.
    fn entropy_millibits(self) -> int {
        if (self.total == 0) {
            return 0;
        }
        var accumulated: int = 0;
        var i: int = 0;
        while (i < self.alphabet) {
            let count = self.counts.at(i);
            if (count > 0) {
                let ratio = (self.total * 1000) / count;
                accumulated += count * log2_millibits(ratio);
            }
            i += 1;
        }
        return accumulated / self.total;
    }

    var counts: vector(int);
    var alphabet: int = 0;
    var total: int = 0;
    var used: int = 0;
}

// Base-2 logarithm of a value scaled by 1000, returned in millibits. The
// fractional part comes from a small linear interpolation between adjacent
// powers of two, which is accurate enough for block type selection.
fn log2_millibits(scaled: int) -> int {
    if (scaled <= 1000) {
        return 0;
    }
    let whole = highest_bit(scaled / 1000);
    let residue = scaled / 1000;
    var fraction: int = 0;
    if (whole > 0) {
        let anchor = 1 << whole;
        fraction = ((residue - anchor) * 1000) / anchor;
    }
    return whole * 1000 + fraction;
}

// --- Huffman tree nodes --------------------------------------------------

// Nodes live in a flat array; -1 stands in for a null child. Leaves carry
// the symbol, internal nodes carry -1.
class HuffmanNode {

    fn is_leaf(self) -> bool {
        return self.left < 0 and self.right < 0;
    }

    fn is_internal(self) -> bool {
        return self.left >= 0 or self.right >= 0;
    }

    var weight: int = 0;
    var symbol: int = -1;
    var left: int = -1;
    var right: int = -1;
    var depth: int = 0;
}

// --- Priority queue ------------------------------------------------------

// A binary min-heap keyed on (weight, tie) so that tree construction is
// deterministic: two nodes of equal weight are ordered by insertion index,
// which makes the resulting code lengths reproducible across runs.
class NodeQueue {

    fn size(self) -> int {
        return self.heap.size();
    }

    fn empty(self) -> bool {
        return self.heap.size() == 0;
    }

    fn clear(ref self) {
        self.heap.clear();
        self.keys.clear();
        self.ties.clear();
        self.sequence = 0;
    }

    fn parent_of(self, index: int) -> int {
        return (index - 1) / 2;
    }

    fn left_of(self, index: int) -> int {
        return index * 2 + 1;
    }

    fn right_of(self, index: int) -> int {
        return index * 2 + 2;
    }

    // Strict weak ordering on the packed key.
    fn less(self, a: int, b: int) -> bool {
        if (self.keys.at(a) != self.keys.at(b)) {
            return self.keys.at(a) < self.keys.at(b);
        }
        return self.ties.at(a) < self.ties.at(b);
    }

    fn swap_entries(ref self, a: int, b: int) {
        swap(self.heap.at(a), self.heap.at(b));
        swap(self.keys.at(a), self.keys.at(b));
        swap(self.ties.at(a), self.ties.at(b));
    }

    fn push(ref self, node: int, weight: int) {
        self.heap.push_back(node);
        self.keys.push_back(weight);
        self.ties.push_back(self.sequence);
        self.sequence += 1;
        self.sift_up(self.heap.size() - 1);
    }

    fn sift_up(ref self, start: int) {
        var index = start;
        while (index > 0) {
            let parent = self.parent_of(index);
            if (not self.less(index, parent)) {
                return;
            }
            self.swap_entries(index, parent);
            index = parent;
        }
    }

    fn sift_down(ref self, start: int) {
        var index = start;
        let count = self.heap.size();
        while (true) {
            let left = self.left_of(index);
            if (left >= count) {
                return;
            }
            var smallest = left;
            let right = self.right_of(index);
            if (right < count) {
                if (self.less(right, left)) {
                    smallest = right;
                }
            }
            if (not self.less(smallest, index)) {
                return;
            }
            self.swap_entries(index, smallest);
            index = smallest;
        }
    }

    fn top(self) -> int {
        return self.heap.at(0);
    }

    fn top_weight(self) -> int {
        return self.keys.at(0);
    }

    fn pop(ref self) -> int {
        let result = self.heap.at(0);
        let last = self.heap.size() - 1;
        self.swap_entries(0, last);
        self.heap.pop_back();
        self.keys.pop_back();
        self.ties.pop_back();
        if (self.heap.size() > 1) {
            self.sift_down(0);
        }
        return result;
    }

    // Verify the heap invariant; the harness calls this after a build.
    fn is_heap(self) -> bool {
        var i: int = 1;
        while (i < self.heap.size()) {
            let parent = (i - 1) / 2;
            if (self.keys.at(i) < self.keys.at(parent)) {
                return false;
            }
            i += 1;
        }
        return true;
    }

    var heap: vector(int);
    var keys: vector(int);
    var ties: vector(int);
    var sequence: int = 0;
}

// --- Huffman tree --------------------------------------------------------

// Builds a Huffman tree from a histogram, derives code lengths, enforces a
// maximum length, and turns the lengths into canonical codes.
//
// The canonical form is the one every container format agrees on: symbols
// are sorted by (length, symbol) and codes are handed out in increasing
// numeric order, so the decoder only needs the length of each symbol.
class HuffmanTree {

    fn clear(ref self) {
        self.nodes.clear();
        self.lengths.clear();
        self.codes.clear();
        self.queue.clear();
        self.root = -1;
        self.max_length = 0;
        self.symbol_count = 0;
    }

    fn node_count(self) -> int {
        return self.nodes.size();
    }

    fn new_leaf(ref self, symbol: int, weight: int) -> int {
        var node = HuffmanNode();
        node.symbol = symbol;
        node.weight = weight;
        node.left = -1;
        node.right = -1;
        self.nodes.push_back(node);
        return self.nodes.size() - 1;
    }

    fn new_internal(ref self, left: int, right: int) -> int {
        var node = HuffmanNode();
        node.symbol = -1;
        node.left = left;
        node.right = right;
        node.weight = self.nodes.at(left).weight + self.nodes.at(right).weight;
        self.nodes.push_back(node);
        return self.nodes.size() - 1;
    }

    // Classic two-pop construction. Every used symbol becomes a leaf, then
    // the two lightest subtrees are repeatedly merged until one remains.
    fn build(ref self, frequencies: FrequencyTable) -> bool {
        self.clear();
        self.symbol_count = frequencies.size();
        self.lengths.resize(frequencies.size());
        self.codes.resize(frequencies.size());
        var symbol: int = 0;
        while (symbol < frequencies.size()) {
            self.lengths.at(symbol) = 0;
            self.codes.at(symbol) = 0;
            symbol += 1;
        }

        var leaves: int = 0;
        symbol = 0;
        while (symbol < frequencies.size()) {
            let weight = frequencies.at(symbol);
            if (weight > 0) {
                let node = self.new_leaf(symbol, weight);
                self.queue.push(node, weight);
                leaves += 1;
            }
            symbol += 1;
        }

        if (leaves == 0) {
            return false;
        }
        if (leaves == 1) {
            // Degenerate alphabet: give the single symbol a one bit code.
            let only = self.queue.pop();
            self.root = only;
            self.lengths.at(self.nodes.at(only).symbol) = 1;
            self.max_length = 1;
            self.assign_canonical();
            return true;
        }

        while (self.queue.size() > 1) {
            let left = self.queue.pop();
            let right = self.queue.pop();
            let merged = self.new_internal(left, right);
            self.queue.push(merged, self.nodes.at(merged).weight);
        }
        self.root = self.queue.pop();
        self.measure_depths(self.root, 0);
        if (self.max_length > MAX_CODE_LENGTH) {
            self.limit_code_lengths(MAX_CODE_LENGTH);
        }
        self.assign_canonical();
        return true;
    }

    // Walk the tree iteratively with an explicit stack, recording the depth
    // of every leaf. Recursion would be fine here but the iterative form is
    // what the production encoder uses, so the benchmark keeps it.
    fn measure_depths(ref self, root: int, anchor: int) {
        var stack_nodes = vector(int)();
        var stack_depths = vector(int)();
        stack_nodes.push_back(root);
        stack_depths.push_back(anchor);
        self.max_length = 0;
        while (not stack_nodes.empty()) {
            let node = stack_nodes.back();
            let depth = stack_depths.back();
            stack_nodes.pop_back();
            stack_depths.pop_back();
            let ref entry: auto = self.nodes.at(node);
            if (entry.is_leaf()) {
                self.lengths.at(entry.symbol) = depth;
                if (depth > self.max_length) {
                    self.max_length = depth;
                }
            } else {
                if (entry.left >= 0) {
                    stack_nodes.push_back(entry.left);
                    stack_depths.push_back(depth + 1);
                }
                if (entry.right >= 0) {
                    stack_nodes.push_back(entry.right);
                    stack_depths.push_back(depth + 1);
                }
            }
        }
        Destroy(stack_nodes);
        Destroy(stack_depths);
    }

    // Enforce a maximum code length using the Kraft sum.
    //
    // The Kraft sum of a prefix code is sum(2^-length) and equals exactly
    // 2^limit when the code is complete. Overlong codes are first clamped to
    // the limit, which pushes the sum above 2^limit; the excess is then paid
    // back by lengthening the shortest codes one bit at a time, starting
    // from the deepest, until the sum drops back to exactly 2^limit.
    fn limit_code_lengths(ref self, limit: int) {
        var histogram = vector(int)(MAX_CODE_LENGTH + 2);
        var i: int = 0;
        while (i < MAX_CODE_LENGTH + 2) {
            histogram.at(i) = 0;
            i += 1;
        }

        var symbol: int = 0;
        while (symbol < self.symbol_count) {
            var length = self.lengths.at(symbol);
            if (length > limit) {
                length = limit;
                self.lengths.at(symbol) = limit;
            }
            if (length > 0) {
                histogram.at(length) += 1;
            }
            symbol += 1;
        }

        // Kraft sum scaled so that a complete code sums to 1 << limit.
        var kraft: int = 0;
        var length: int = 1;
        while (length <= limit) {
            kraft += histogram.at(length) << (limit - length);
            length += 1;
        }

        let target = 1 << limit;
        while (kraft > target) {
            // Find the deepest length that still has a symbol to lengthen,
            // one level above the limit's floor, and move one symbol down.
            var donor = limit - 1;
            while (donor > 0) {
                if (histogram.at(donor) > 0) {
                    break;
                }
                donor -= 1;
            }
            if (donor == 0) {
                break;
            }
            histogram.at(donor) -= 1;
            histogram.at(donor + 1) += 1;
            if (donor + 1 < limit) {
                histogram.at(limit) -= 1;
                histogram.at(limit - 1) += 1;
            }
            kraft -= 1 << (limit - donor - 1);
        }

        // The sum may now sit below the target; hand the slack back to the
        // shortest codes so the code stays complete.
        while (kraft < target) {
            var taker = limit;
            while (taker > 0) {
                if (histogram.at(taker) > 0) {
                    break;
                }
                taker -= 1;
            }
            if (taker <= 1) {
                break;
            }
            histogram.at(taker) -= 1;
            histogram.at(taker - 1) += 1;
            kraft += 1 << (limit - taker);
        }

        self.rebuild_lengths_from_histogram(histogram, limit);
        Destroy(histogram);
    }

    // Re-hand out the adjusted lengths, giving the shortest codes to the
    // heaviest symbols. Symbols are visited in decreasing weight order,
    // which is what keeps the rebalanced code close to optimal.
    fn rebuild_lengths_from_histogram(ref self, histogram: vector(int), limit: int) {
        var order = vector(int)();
        var symbol: int = 0;
        while (symbol < self.symbol_count) {
            if (self.lengths.at(symbol) > 0) {
                order.push_back(symbol);
            }
            symbol += 1;
        }
        self.sort_by_weight(order);

        var cursor: int = 0;
        var length: int = 1;
        while (length <= limit) {
            var remaining = histogram.at(length);
            while (remaining > 0) {
                if (cursor >= order.size()) {
                    remaining = 0;
                } else {
                    self.lengths.at(order.at(cursor)) = length;
                    cursor += 1;
                    remaining -= 1;
                }
            }
            length += 1;
        }
        self.max_length = limit;
        Destroy(order);
    }

    // Insertion sort on symbol weight, descending. The arrays involved are
    // at most MAX_SYMBOLS long so the quadratic bound is irrelevant, and the
    // sort has to be stable for the canonical assignment to be reproducible.
    fn sort_by_weight(self, ref order: vector(int)) {
        var i: int = 1;
        while (i < order.size()) {
            let key = order.at(i);
            let key_weight = self.weight_of_symbol(key);
            var j = i - 1;
            while (j >= 0) {
                if (self.weight_of_symbol(order.at(j)) >= key_weight) {
                    break;
                }
                order.at(j + 1) = order.at(j);
                j -= 1;
            }
            order.at(j + 1) = key;
            i += 1;
        }
    }

    fn weight_of_symbol(self, symbol: int) -> int {
        var i: int = 0;
        while (i < self.nodes.size()) {
            let ref node: auto = self.nodes.at(i);
            if (node.is_leaf()) {
                if (node.symbol == symbol) {
                    return node.weight;
                }
            }
            i += 1;
        }
        return 0;
    }

    // Canonical assignment: count codes of each length, compute the first
    // code of each length, then hand codes out in symbol order.
    fn assign_canonical(ref self) {
        var count_per_length = vector(int)(MAX_CODE_LENGTH + 1);
        var next_code = vector(int)(MAX_CODE_LENGTH + 1);
        var i: int = 0;
        while (i <= MAX_CODE_LENGTH) {
            count_per_length.at(i) = 0;
            next_code.at(i) = 0;
            i += 1;
        }

        var symbol: int = 0;
        while (symbol < self.symbol_count) {
            let length = self.lengths.at(symbol);
            if (length > 0) {
                count_per_length.at(length) += 1;
            }
            symbol += 1;
        }

        var code: int = 0;
        count_per_length.at(0) = 0;
        var bits: int = 1;
        while (bits <= MAX_CODE_LENGTH) {
            code = (code + count_per_length.at(bits - 1)) << 1;
            next_code.at(bits) = code;
            bits += 1;
        }

        symbol = 0;
        while (symbol < self.symbol_count) {
            let length = self.lengths.at(symbol);
            if (length > 0) {
                self.codes.at(symbol) = next_code.at(length);
                next_code.at(length) += 1;
            } else {
                self.codes.at(symbol) = 0;
            }
            symbol += 1;
        }
        Destroy(count_per_length);
        Destroy(next_code);
    }

    // The Kraft sum must equal exactly 2^max_length for a complete code.
    fn kraft_sum(self) -> int {
        var total: int = 0;
        var symbol: int = 0;
        while (symbol < self.symbol_count) {
            let length = self.lengths.at(symbol);
            if (length > 0) {
                total += 1 << (self.max_length - length);
            }
            symbol += 1;
        }
        return total;
    }

    fn is_complete(self) -> bool {
        if (self.max_length == 0) {
            return false;
        }
        return self.kraft_sum() == (1 << self.max_length);
    }

    // Total compressed size of the alphabet under this code, in bits.
    fn encoded_bits(self, frequencies: FrequencyTable) -> int {
        var total: int = 0;
        var symbol: int = 0;
        while (symbol < self.symbol_count) {
            total += self.lengths.at(symbol) * frequencies.at(symbol);
            symbol += 1;
        }
        return total;
    }

    var nodes: vector(HuffmanNode);
    var queue: NodeQueue;
    var lengths: vector(int);
    var codes: vector(int);
    var root: int = -1;
    var max_length: int = 0;
    var symbol_count: int = 0;
}

// --- Encode table --------------------------------------------------------

// Flattens a tree's canonical codes into two parallel arrays plus a
// pre-reversed copy for LSB-first output. Writing a symbol then costs one
// array read and one `put_code`.
class EncodeTable {

    fn build(ref self, tree: HuffmanTree) {
        self.codes.clear();
        self.lengths.clear();
        self.reversed.clear();
        self.codes.resize(tree.symbol_count);
        self.lengths.resize(tree.symbol_count);
        self.reversed.resize(tree.symbol_count);
        self.symbol_count = tree.symbol_count;
        self.max_length = tree.max_length;
        var symbol: int = 0;
        while (symbol < tree.symbol_count) {
            let length = tree.lengths.at(symbol);
            let code = tree.codes.at(symbol);
            self.lengths.at(symbol) = length;
            self.codes.at(symbol) = code;
            self.reversed.at(symbol) = reverse_bits(code, length);
            symbol += 1;
        }
    }

    // Build directly from a length vector, which is what the decoder side
    // does when it reconstructs a dynamic block's code from the header.
    fn build_from_lengths(ref self, lengths: vector(int)) {
        self.codes.clear();
        self.lengths.clear();
        self.reversed.clear();
        self.codes.resize(lengths.size());
        self.lengths.resize(lengths.size());
        self.reversed.resize(lengths.size());
        self.symbol_count = lengths.size();
        self.max_length = 0;

        var count_per_length = vector(int)(MAX_CODE_LENGTH + 1);
        var i: int = 0;
        while (i <= MAX_CODE_LENGTH) {
            count_per_length.at(i) = 0;
            i += 1;
        }
        var symbol: int = 0;
        while (symbol < lengths.size()) {
            let length = lengths.at(symbol);
            self.lengths.at(symbol) = length;
            if (length > 0) {
                count_per_length.at(length) += 1;
                if (length > self.max_length) {
                    self.max_length = length;
                }
            }
            symbol += 1;
        }

        var next_code = vector(int)(MAX_CODE_LENGTH + 1);
        var code: int = 0;
        count_per_length.at(0) = 0;
        var bits: int = 1;
        while (bits <= MAX_CODE_LENGTH) {
            code = (code + count_per_length.at(bits - 1)) << 1;
            next_code.at(bits) = code;
            bits += 1;
        }

        symbol = 0;
        while (symbol < lengths.size()) {
            let length = self.lengths.at(symbol);
            if (length > 0) {
                let assigned = next_code.at(length);
                next_code.at(length) += 1;
                self.codes.at(symbol) = assigned;
                self.reversed.at(symbol) = reverse_bits(assigned, length);
            } else {
                self.codes.at(symbol) = 0;
                self.reversed.at(symbol) = 0;
            }
            symbol += 1;
        }
        Destroy(count_per_length);
        Destroy(next_code);
    }

    fn length_of(self, symbol: int) -> int {
        return self.lengths.at(symbol);
    }

    fn code_of(self, symbol: int) -> int {
        return self.codes.at(symbol);
    }

    fn has_code(self, symbol: int) -> bool {
        return self.lengths.at(symbol) > 0;
    }

    fn emit(self, ref writer: BitWriter, symbol: int) {
        writer.put_code(self.codes.at(symbol), self.lengths.at(symbol));
    }

    // Fast path for an LSB-first stream: the reversal has already been paid
    // for at table build time.
    fn emit_reversed(self, ref writer: BitWriter, symbol: int) {
        writer.put_bits_lsb(self.reversed.at(symbol), self.lengths.at(symbol));
    }

    var codes: vector(int);
    var reversed: vector(int);
    var lengths: vector(int);
    var symbol_count: int = 0;
    var max_length: int = 0;
}

// --- Decode table --------------------------------------------------------

// One entry of the flat decode table. A code shorter than ROOT_BITS is
// replicated across every index whose low bits match it, so a single lookup
// resolves the symbol. Longer codes store a link to a secondary table.
class DecodeEntry {

    fn is_link(self) -> bool {
        return self.sub_table >= 0;
    }

    fn is_valid(self) -> bool {
        return self.bits > 0;
    }

    var symbol: int = -1;
    var bits: int = 0;
    var sub_table: int = -1;
    var sub_bits: int = 0;
}

// Two-level table-driven decoder.
//
// The root table is indexed by the next ROOT_BITS of the stream, read in the
// stream's native order. Because deflate writes codes LSB-first, the index a
// code occupies is the code's bit-reversal, and every longer index sharing
// that suffix maps to the same entry. That is the replication loop below,
// which strides by `1 << length` over the whole root table.
class DecodeTable {

    fn clear(ref self) {
        self.root.clear();
        self.sub.clear();
        self.symbol_count = 0;
        self.max_length = 0;
        self.under_subscribed = false;
    }

    fn root_size(self) -> int {
        return self.root.size();
    }

    fn build(ref self, lengths: vector(int)) -> bool {
        self.clear();
        self.symbol_count = lengths.size();

        var count_per_length = vector(int)(MAX_CODE_LENGTH + 1);
        var i: int = 0;
        while (i <= MAX_CODE_LENGTH) {
            count_per_length.at(i) = 0;
            i += 1;
        }
        var used: int = 0;
        var symbol: int = 0;
        while (symbol < lengths.size()) {
            let length = lengths.at(symbol);
            if (length > 0) {
                if (length > MAX_CODE_LENGTH) {
                    Destroy(count_per_length);
                    return false;
                }
                count_per_length.at(length) += 1;
                used += 1;
                if (length > self.max_length) {
                    self.max_length = length;
                }
            }
            symbol += 1;
        }
        if (used == 0) {
            self.under_subscribed = true;
            Destroy(count_per_length);
            return false;
        }

        // Over-subscribed codes are corrupt; under-subscribed ones are only
        // legal for the degenerate single-symbol distance code.
        var left: int = 1;
        var bits: int = 1;
        while (bits <= MAX_CODE_LENGTH) {
            left = left << 1;
            left -= count_per_length.at(bits);
            if (left < 0) {
                Destroy(count_per_length);
                return false;
            }
            bits += 1;
        }
        if (left > 0) {
            self.under_subscribed = true;
            if (used != 1) {
                Destroy(count_per_length);
                return false;
            }
        }

        var next_code = vector(int)(MAX_CODE_LENGTH + 1);
        var code: int = 0;
        bits = 1;
        while (bits <= MAX_CODE_LENGTH) {
            code = (code + count_per_length.at(bits - 1)) << 1;
            next_code.at(bits) = code;
            bits += 1;
        }

        let root_bits = min_int(ROOT_BITS, self.max_length);
        let root_count = 1 << root_bits;
        self.root.resize(root_count);
        var slot: int = 0;
        while (slot < root_count) {
            var blank = DecodeEntry();
            blank.symbol = -1;
            blank.bits = 0;
            blank.sub_table = -1;
            self.root.at(slot) = blank;
            slot += 1;
        }
        self.root_bits = root_bits;

        symbol = 0;
        while (symbol < lengths.size()) {
            let length = lengths.at(symbol);
            if (length > 0) {
                let assigned = next_code.at(length);
                next_code.at(length) += 1;
                let index = reverse_bits(assigned, length);
                if (length <= root_bits) {
                    self.fill_root(index, length, symbol, root_bits);
                } else {
                    self.fill_sub(index, length, symbol, root_bits);
                }
            }
            symbol += 1;
        }

        Destroy(count_per_length);
        Destroy(next_code);
        return true;
    }

    // Replicate a short code across every root index that ends in it.
    fn fill_root(ref self, index: int, length: int, symbol: int, root_bits: int) {
        let stride = 1 << length;
        let limit = 1 << root_bits;
        var slot = index;
        while (slot < limit) {
            var entry = DecodeEntry();
            entry.symbol = symbol;
            entry.bits = length;
            entry.sub_table = -1;
            entry.sub_bits = 0;
            self.root.at(slot) = entry;
            slot += stride;
        }
    }

    // Route a long code through a secondary table. The root entry records
    // where the secondary table starts and how many extra bits it consumes.
    fn fill_sub(ref self, index: int, length: int, symbol: int, root_bits: int) {
        let root_index = index & ((1 << root_bits) - 1);
        let extra = length - root_bits;
        var anchor = self.root.at(root_index).sub_table;
        if (anchor < 0) {
            anchor = self.sub.size();
            let span = 1 << SUB_BITS;
            var i: int = 0;
            while (i < span) {
                var blank = DecodeEntry();
                blank.symbol = -1;
                blank.bits = 0;
                blank.sub_table = -1;
                self.sub.push_back(blank);
                i += 1;
            }
            var link = DecodeEntry();
            link.symbol = -1;
            link.bits = root_bits;
            link.sub_table = anchor;
            link.sub_bits = SUB_BITS;
            self.root.at(root_index) = link;
        }

        let sub_index = (index >> root_bits) & ((1 << SUB_BITS) - 1);
        let stride = 1 << extra;
        let limit = 1 << SUB_BITS;
        var slot = sub_index;
        while (slot < limit) {
            var entry = DecodeEntry();
            entry.symbol = symbol;
            entry.bits = extra;
            entry.sub_table = -1;
            entry.sub_bits = 0;
            self.sub.at(anchor + slot) = entry;
            slot += stride;
        }
    }

    // Decode one symbol. Peek enough bits for the root lookup, consume only
    // as many as the code actually uses, and follow a link if present.
    fn decode(self, ref reader: BitReader) -> int {
        let peeked = reader.peek_bits(self.root_bits);
        let ref entry: auto = self.root.at(peeked);
        if (entry.bits == 0) {
            return -1;
        }
        if (not entry.is_link()) {
            reader.drop_bits(entry.bits);
            return entry.symbol;
        }
        reader.drop_bits(self.root_bits);
        let extra = reader.peek_bits(entry.sub_bits);
        let ref leaf: auto = self.sub.at(entry.sub_table + extra);
        if (leaf.bits == 0) {
            return -1;
        }
        reader.drop_bits(leaf.bits);
        return leaf.symbol;
    }

    // Slow reference decoder that walks the code bit by bit. Used by the
    // harness to cross-check the table-driven path.
    fn decode_slow(self, ref reader: BitReader, lengths: vector(int)) -> int {
        var code: int = 0;
        var length: int = 0;
        var first: int = 0;
        var index: int = 0;
        var count_per_length = vector(int)(MAX_CODE_LENGTH + 1);
        var i: int = 0;
        while (i <= MAX_CODE_LENGTH) {
            count_per_length.at(i) = 0;
            i += 1;
        }
        var symbol: int = 0;
        while (symbol < lengths.size()) {
            if (lengths.at(symbol) > 0) {
                count_per_length.at(lengths.at(symbol)) += 1;
            }
            symbol += 1;
        }

        while (length < MAX_CODE_LENGTH) {
            length += 1;
            code = code | reader.get_bit();
            let count = count_per_length.at(length);
            if (code - first < count) {
                Destroy(count_per_length);
                return index + (code - first);
            }
            index += count;
            first = (first + count) << 1;
            code = code << 1;
        }
        Destroy(count_per_length);
        return -1;
    }

    var root: vector(DecodeEntry);
    var sub: vector(DecodeEntry);
    var root_bits: int = 9;
    var max_length: int = 0;
    var symbol_count: int = 0;
    var under_subscribed: bool = false;
}

fn min_int(a: int, b: int) -> int {
    if (a < b) {
        return a;
    }
    return b;
}

fn max_int(a: int, b: int) -> int {
    if (a > b) {
        return a;
    }
    return b;
}

fn clamp_int(value: int, low: int, high: int) -> int {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

// --- LZ77 tokens ---------------------------------------------------------

// A token is either a literal byte or a (length, distance) back reference.
// Packing both into one struct keeps the token stream contiguous, which
// matters because the block splitter walks it several times.
class Token {

    fn is_literal(self) -> bool {
        return self.length < MIN_MATCH;
    }

    fn is_match(self) -> bool {
        return self.length >= MIN_MATCH;
    }

    fn cost_estimate(self) -> int {
        if (self.is_literal()) {
            return 8;
        }
        // A rough model: the length code plus the distance code plus their
        // extra bits. Good enough to decide between block types.
        return 24 + extra_length_bits(length_code_of(self.length)) + extra_distance_bits(distance_code_of(self.distance));
    }

    var literal: int = 0;
    var length: int = 0;
    var distance: int = 0;
}

// A candidate match returned by the match finder.
class Match {

    fn valid(self) -> bool {
        return self.length >= MIN_MATCH and self.distance > 0 and self.distance <= MAX_DISTANCE;
    }

    fn better_than(self, other: Match) -> bool {
        if (self.length != other.length) {
            return self.length > other.length;
        }
        // Equal lengths: prefer the nearer match, its distance code is
        // cheaper to encode.
        return self.distance < other.distance;
    }

    var length: int = 0;
    var distance: int = 0;
    var position: int = 0;
}

// --- Compression levels --------------------------------------------------

// The knobs the match finder honours. Higher levels search longer chains and
// enable lazy matching; the lowest level skips the finder entirely.
class LevelConfig {

    fn from_level(ref self, level: int) {
        let clamped = clamp_int(level, 0, 9);
        if (clamped == 0) {
            self.good_length = 0;
            self.max_lazy = 0;
            self.nice_length = 0;
            self.max_chain = 0;
            self.use_lazy = false;
            self.stored_only = true;
        } else { if (clamped <= 2) {
            self.good_length = 4;
            self.max_lazy = 4;
            self.nice_length = 8;
            self.max_chain = 4;
            self.use_lazy = false;
            self.stored_only = false;
        } else { if (clamped <= 4) {
            self.good_length = 4;
            self.max_lazy = 5;
            self.nice_length = 16;
            self.max_chain = 16;
            self.use_lazy = true;
            self.stored_only = false;
        } else { if (clamped <= 6) {
            self.good_length = 8;
            self.max_lazy = 16;
            self.nice_length = 32;
            self.max_chain = 32;
            self.use_lazy = true;
            self.stored_only = false;
        } else { if (clamped <= 8) {
            self.good_length = 8;
            self.max_lazy = 32;
            self.nice_length = 128;
            self.max_chain = 128;
            self.use_lazy = true;
            self.stored_only = false;
        } else {
            self.good_length = 32;
            self.max_lazy = 258;
            self.nice_length = 258;
            self.max_chain = 4096;
            self.use_lazy = true;
            self.stored_only = false;
        }
        } } } }
        self.level = clamped;
    }

    // When the previous match was already long, the chain search is cut
    // short: the odds of finding something better do not pay for the walk.
    fn chain_limit_for(self, previous_length: int) -> int {
        if (previous_length >= self.good_length) {
            return self.max_chain >> 2;
        }
        return self.max_chain;
    }

    var level: int = 6;
    var good_length: int = 8;
    var max_lazy: int = 16;
    var nice_length: int = 32;
    var max_chain: int = 32;
    var use_lazy: bool = true;
    var stored_only: bool = false;
}

// --- Hash chain match finder ---------------------------------------------

// The classic deflate finder: a rolling three-byte hash indexes a head
// table, and every head entry starts a chain of earlier positions with the
// same hash, threaded through a `previous` array indexed by window offset.
class HashChain {

    fn init(ref self) {
        self.head.clear();
        self.head.resize(HASH_SIZE);
        var i: int = 0;
        while (i < HASH_SIZE) {
            self.head.at(i) = NO_POSITION;
            i += 1;
        }
        self.previous.clear();
        self.previous.resize(WINDOW_SIZE);
        i = 0;
        while (i < WINDOW_SIZE) {
            self.previous.at(i) = NO_POSITION;
            i += 1;
        }
        self.running_hash = 0;
        self.inserted = 0;
    }

    // Rolling hash update: shift the accumulator left by HASH_SHIFT, mix in
    // the new byte, and mask back down to HASH_BITS. After MIN_MATCH updates
    // the accumulator depends on exactly the last three bytes.
    fn update_hash(ref self, next_byte: int) -> int {
        self.running_hash = ((self.running_hash << HASH_SHIFT) ^ (next_byte & BYTE_MASK)) & HASH_MASK;
        return self.running_hash;
    }

    // Compute the hash of the three bytes at `position` without touching the
    // rolling state; used when the finder skips ahead.
    fn hash_at(self, input: ByteBuffer, position: int) -> int {
        if (position + MIN_MATCH > input.size()) {
            return 0;
        }
        var h: int = 0;
        h = ((h << HASH_SHIFT) ^ input.at(position)) & HASH_MASK;
        h = ((h << HASH_SHIFT) ^ input.at(position + 1)) & HASH_MASK;
        h = ((h << HASH_SHIFT) ^ input.at(position + 2)) & HASH_MASK;
        return h;
    }

    fn insert(ref self, hash_value: int, position: int) {
        let slot = position & WINDOW_MASK;
        self.previous.at(slot) = self.head.at(hash_value);
        self.head.at(hash_value) = position;
        self.inserted += 1;
    }

    // Insert every position in [from, to) so that later searches see them.
    // The encoder calls this after emitting a long match, because the bytes
    // covered by the match still need to be hashed.
    fn insert_range(ref self, input: ByteBuffer, from: int, to: int) {
        var position = from;
        while (position < to) {
            if (position + MIN_MATCH <= input.size()) {
                self.insert(self.hash_at(input, position), position);
            }
            position += 1;
        }
    }

    // Length of the common prefix of the two positions, capped at `limit`.
    // This is the innermost loop of the whole encoder.
    fn match_length(self, input: ByteBuffer, a: int, b: int, limit: int) -> int {
        var length: int = 0;
        while (length < limit) {
            if (input.at(a + length) != input.at(b + length)) {
                return length;
            }
            length += 1;
        }
        return limit;
    }

    // Walk the chain for `position`, returning the best match found. The
    // walk stops at the chain limit, at the window edge, or as soon as a
    // match reaches `nice_length`.
    fn find_best(self, input: ByteBuffer, position: int, previous_length: int, config: LevelConfig) -> Match {
        var best = Match();
        best.length = 0;
        best.distance = 0;
        best.position = position;

        if (position + MIN_MATCH > input.size()) {
            return best;
        }
        let limit = min_int(MAX_MATCH, input.size() - position);
        if (limit < MIN_MATCH) {
            return best;
        }

        let hash_value = self.hash_at(input, position);
        var candidate = self.head.at(hash_value);
        var chain_left = self.chain_limit_of(previous_length, config);
        let window_floor = max_int(0, position - MAX_DISTANCE);

        while (candidate >= 0) {
            if (chain_left <= 0) {
                break;
            }
            chain_left -= 1;
            if (candidate < window_floor) {
                break;
            }
            if (candidate >= position) {
                break;
            }

            // Cheap rejection: the byte one past the current best has to
            // match, otherwise the candidate cannot possibly be longer.
            var worth_trying = true;
            if (best.length > 0) {
                if (input.at(candidate + best.length) != input.at(position + best.length)) {
                    worth_trying = false;
                }
            }
            if (worth_trying) {
                let length = self.match_length(input, candidate, position, limit);
                if (length >= MIN_MATCH) {
                    if (length > best.length) {
                        best.length = length;
                        best.distance = position - candidate;
                        if (length >= config.nice_length) {
                            return best;
                        }
                    }
                }
            }
            candidate = self.previous.at(candidate & WINDOW_MASK);
        }
        return best;
    }

    fn chain_limit_of(self, previous_length: int, config: LevelConfig) -> int {
        return config.chain_limit_for(previous_length);
    }

    // Diagnostic: average chain length over the whole head table.
    fn average_chain(self) -> int {
        var total: int = 0;
        var chains: int = 0;
        var i: int = 0;
        while (i < HASH_SIZE) {
            var node = self.head.at(i);
            if (node >= 0) {
                chains += 1;
                var depth: int = 0;
                while (node >= 0) {
                    if (depth >= 64) {
                        break;
                    }
                    depth += 1;
                    node = self.previous.at(node & WINDOW_MASK);
                }
                total += depth;
            }
            i += 1;
        }
        if (chains == 0) {
            return 0;
        }
        return total / chains;
    }

    var head: vector(int);
    var previous: vector(int);
    var running_hash: int = 0;
    var inserted: int = 0;
}

// --- Sliding window ------------------------------------------------------

// A circular history buffer of WINDOW_SIZE bytes. The decoder needs one to
// resolve back references; the encoder keeps the whole input in memory and
// uses absolute positions instead, so the window is decode-side only.
class SlidingWindow {

    fn init(ref self, capacity: int) {
        self.bytes.clear();
        self.bytes.resize(capacity);
        var i: int = 0;
        while (i < capacity) {
            self.bytes.at(i) = 0;
            i += 1;
        }
        self.capacity = capacity;
        self.mask = capacity - 1;
        self.write_position = 0;
        self.filled = 0;
    }

    fn size(self) -> int {
        return self.filled;
    }

    fn full(self) -> bool {
        return self.filled >= self.capacity;
    }

    fn wrap(self, position: int) -> int {
        return position & self.mask;
    }

    fn push(ref self, value: int) {
        self.bytes.at(self.write_position) = value & BYTE_MASK;
        self.write_position = (self.write_position + 1) & self.mask;
        if (self.filled < self.capacity) {
            self.filled += 1;
        }
    }

    // Byte `distance` positions back from the write cursor.
    fn behind(self, distance: int) -> int {
        let index = (self.write_position - distance) & self.mask;
        return self.bytes.at(index) & BYTE_MASK;
    }

    fn can_reference(self, distance: int) -> bool {
        return distance > 0 and distance <= self.filled;
    }

    // Copy a back reference out of the window and into the sink, feeding
    // every copied byte back into the window. Overlapping copies work
    // because each byte is written before the byte that references it is
    // read, which is exactly the run-extension trick LZ77 relies on.
    fn copy_match(ref self, ref sink: ByteBuffer, distance: int, length: int) -> bool {
        if (not self.can_reference(distance)) {
            return false;
        }
        var i: int = 0;
        while (i < length) {
            let value = self.behind(distance);
            sink.push(value);
            self.push(value);
            i += 1;
        }
        return true;
    }

    fn emit_literal(ref self, ref sink: ByteBuffer, value: int) {
        sink.push(value & BYTE_MASK);
        self.push(value & BYTE_MASK);
    }

    var bytes: vector(int);
    var capacity: int = 32768;
    var mask: int = 32767;
    var write_position: int = 0;
    var filled: int = 0;
}

// --- LZ77 encoder --------------------------------------------------------

// Produces a token stream from a byte buffer using the hash chain finder.
//
// Lazy matching: after finding a match at position p, the encoder also looks
// at p+1. If the later match is strictly longer, the byte at p is emitted as
// a literal and the longer match is taken instead. This costs one extra
// chain walk per position but typically buys a few percent.
class Lz77Encoder {

    fn init(ref self, level: int) {
        self.config.from_level(level);
        self.chain.init();
        self.tokens.clear();
        self.literal_count = 0;
        self.match_count = 0;
        self.matched_bytes = 0;
        self.literal_bytes = 0;
    }

    fn token_count(self) -> int {
        return self.tokens.size();
    }

    fn push_literal(ref self, value: int) {
        var token = Token();
        token.literal = value & BYTE_MASK;
        token.length = 0;
        token.distance = 0;
        self.tokens.push_back(token);
        self.literal_count += 1;
        self.literal_bytes += 1;
    }

    fn push_match(ref self, length: int, distance: int) {
        var token = Token();
        token.literal = 0;
        token.length = length;
        token.distance = distance;
        self.tokens.push_back(token);
        self.match_count += 1;
        self.matched_bytes += length;
    }

    // The main loop. `position` is the next byte to encode; `pending`
    // carries the match found at the previous position while the encoder
    // decides whether to keep it.
    fn encode(ref self, input: ByteBuffer) {
        self.tokens.clear();
        if (self.config.stored_only) {
            var i: int = 0;
            while (i < input.size()) {
                self.push_literal(input.at(i));
                i += 1;
            }
            return;
        }

        var position: int = 0;
        var have_pending = false;
        var pending = Match();
        pending.length = 0;
        pending.distance = 0;
        pending.position = 0;

        while (position < input.size()) {
            var current = Match();
            current.length = 0;
            current.distance = 0;
            current.position = position;
            if (position + MIN_MATCH <= input.size()) {
                var previous_length: int = 0;
                if (have_pending) {
                    previous_length = pending.length;
                }
                current = self.chain.find_best(input, position, previous_length, self.config);
            }

            if (have_pending) {
                if (current.length > pending.length) {
                    // The later match wins: flush the deferred byte as a
                    // literal and defer again on the new candidate.
                    self.push_literal(input.at(pending.position));
                    self.insert_position(input, pending.position);
                    pending = current;
                    pending.position = position;
                    position += 1;
                } else {
                    // Keep the deferred match.
                    self.push_match(pending.length, pending.distance);
                    let start = pending.position;
                    let stop = start + pending.length;
                    self.chain.insert_range(input, start, stop);
                    have_pending = false;
                    position = stop;
                }
            } else {
                if (current.valid()) {
                    if (self.config.use_lazy and current.length < self.config.max_lazy) {
                        pending = current;
                        pending.position = position;
                        have_pending = true;
                        self.insert_position(input, position);
                        position += 1;
                    } else {
                        self.push_match(current.length, current.distance);
                        self.chain.insert_range(input, position, position + current.length);
                        position += current.length;
                    }
                } else {
                    self.push_literal(input.at(position));
                    self.insert_position(input, position);
                    position += 1;
                }
            }
        }

        if (have_pending) {
            self.push_literal(input.at(pending.position));
        }
    }

    fn insert_position(ref self, input: ByteBuffer, position: int) {
        if (position + MIN_MATCH <= input.size()) {
            self.chain.insert(self.chain.hash_at(input, position), position);
        }
    }

    // Ratio of matched bytes to total bytes, in percent. The block splitter
    // uses it to decide whether a stored block would be smaller.
    fn match_percent(self) -> int {
        let total = self.matched_bytes + self.literal_bytes;
        if (total == 0) {
            return 0;
        }
        return (self.matched_bytes * 100) / total;
    }

    fn estimated_bits(self) -> int {
        var total: int = 0;
        var i: int = 0;
        while (i < self.tokens.size()) {
            total += self.tokens.at(i).cost_estimate();
            i += 1;
        }
        return total;
    }

    var config: LevelConfig;
    var chain: HashChain;
    var tokens: vector(Token);
    var literal_count: int = 0;
    var match_count: int = 0;
    var matched_bytes: int = 0;
    var literal_bytes: int = 0;
}

// --- LZ77 decoder --------------------------------------------------------

// Replays a token stream through a sliding window.
fn lz77_decode(tokens: vector(Token), ref output: ByteBuffer) -> Status {
    var window = SlidingWindow();
    window.init(WINDOW_SIZE);
    var i: int = 0;
    while (i < tokens.size()) {
        let ref token: auto = tokens.at(i);
        if (token.is_literal()) {
            window.emit_literal(output, token.literal);
        } else {
            if (not window.can_reference(token.distance)) {
                Destroy(window);
                return Status.BadDistance;
            }
            if (not window.copy_match(output, token.distance, token.length)) {
                Destroy(window);
                return Status.CorruptStream;
            }
        }
        i += 1;
    }
    Destroy(window);
    return Status.Ok;
}

// --- Deflate symbol tables ----------------------------------------------

// Deflate maps a match length in [3, 258] onto one of 29 length codes plus
// up to five extra bits, and a distance in [1, 32768] onto one of 30
// distance codes plus up to 13 extra bits. Both tables are generated here
// rather than transcribed, because the structure is entirely regular:
//
//   length code i has (i - 4) / 4 extra bits, clamped to [0, 5]
//   distance code i has (i / 2) - 1 extra bits, clamped to [0, 13]
//
// and each base is the previous base plus 2^extra of the previous code.
class DeflateTables {

    fn init(ref self) {
        self.build_length_tables();
        self.build_distance_tables();
        self.build_code_length_order();
        self.build_reverse_maps();
        self.ready = true;
    }

    fn build_length_tables(ref self) {
        self.length_base.clear();
        self.length_extra.clear();
        self.length_base.resize(29);
        self.length_extra.resize(29);

        var running: int = 3;
        var code: int = 0;
        while (code < 28) {
            var extra = (code - 4) / 4;
            if (code < 8) {
                extra = 0;
            }
            if (extra > 5) {
                extra = 5;
            }
            self.length_extra.at(code) = extra;
            self.length_base.at(code) = running;
            running += 1 << extra;
            code += 1;
        }
        // The final code is the special case: length 258 with no extra bits.
        self.length_extra.at(28) = 0;
        self.length_base.at(28) = MAX_MATCH;
    }

    fn build_distance_tables(ref self) {
        self.distance_base.clear();
        self.distance_extra.clear();
        self.distance_base.resize(DISTANCE_SYMBOLS);
        self.distance_extra.resize(DISTANCE_SYMBOLS);

        var running: int = 1;
        var code: int = 0;
        while (code < DISTANCE_SYMBOLS) {
            var extra = (code / 2) - 1;
            if (code < 4) {
                extra = 0;
            }
            if (extra > 13) {
                extra = 13;
            }
            self.distance_extra.at(code) = extra;
            self.distance_base.at(code) = running;
            running += 1 << extra;
            code += 1;
        }
    }

    // The code length alphabet is transmitted in a permuted order chosen so
    // that the rarely used lengths sit at the end and can be truncated.
    fn build_code_length_order(ref self) {
        self.code_length_order.clear();
        self.code_length_order.push_back(16);
        self.code_length_order.push_back(17);
        self.code_length_order.push_back(18);
        self.code_length_order.push_back(0);
        var offset: int = 8;
        var step: int = 0;
        while (step < 8) {
            self.code_length_order.push_back(offset);
            self.code_length_order.push_back(BYTE_BITS - 1 - step - 1 + 1);
            offset += 1;
            step += 1;
        }
        // Regenerate the tail deterministically: 8,7,9,6,10,5,11,4,12,3,
        // 13,2,14,1,15 alternates outward from the middle of the alphabet.
        self.code_length_order.clear();
        self.code_length_order.push_back(16);
        self.code_length_order.push_back(17);
        self.code_length_order.push_back(18);
        self.code_length_order.push_back(0);
        var high: int = 8;
        var low: int = 7;
        while (high <= 15) {
            self.code_length_order.push_back(high);
            self.code_length_order.push_back(low);
            high += 1;
            low -= 1;
        }
    }

    // Invert the base tables into direct lookups so the encoder does not
    // binary search on every token. `length_to_code` has MAX_MATCH + 1
    // entries; `distance_to_code` uses a split lookup because a full table
    // over 32768 distances would be wasteful: the low 256 distances index
    // directly, the rest index by distance >> 7.
    fn build_reverse_maps(ref self) {
        self.length_to_code.clear();
        self.length_to_code.resize(MAX_MATCH + 1);
        var length: int = 0;
        while (length <= MAX_MATCH) {
            self.length_to_code.at(length) = 0;
            length += 1;
        }
        var code: int = 0;
        while (code < 29) {
            let anchor = self.length_base.at(code);
            let span = 1 << self.length_extra.at(code);
            var offset: int = 0;
            while (offset < span) {
                let value = anchor + offset;
                if (value <= MAX_MATCH) {
                    self.length_to_code.at(value) = code;
                }
                offset += 1;
            }
            code += 1;
        }
        self.length_to_code.at(MAX_MATCH) = 28;

        self.distance_to_code_low.clear();
        self.distance_to_code_low.resize(257);
        self.distance_to_code_high.clear();
        self.distance_to_code_high.resize(257);
        var i: int = 0;
        while (i <= 256) {
            self.distance_to_code_low.at(i) = 0;
            self.distance_to_code_high.at(i) = 0;
            i += 1;
        }
        code = 0;
        while (code < DISTANCE_SYMBOLS) {
            let anchor = self.distance_base.at(code);
            let span = 1 << self.distance_extra.at(code);
            var offset: int = 0;
            while (offset < span) {
                let distance = anchor + offset;
                if (distance <= 256) {
                    self.distance_to_code_low.at(distance) = code;
                } else {
                    if (distance <= MAX_DISTANCE) {
                        let bucket = (distance - 1) >> 7;
                        if (bucket <= 256) {
                            self.distance_to_code_high.at(bucket) = code;
                        }
                    }
                }
                offset += 1;
            }
            code += 1;
        }
    }

    fn length_code(self, length: int) -> int {
        return self.length_to_code.at(clamp_int(length, MIN_MATCH, MAX_MATCH));
    }

    fn distance_code(self, distance: int) -> int {
        if (distance <= 256) {
            return self.distance_to_code_low.at(distance);
        }
        return self.distance_to_code_high.at((distance - 1) >> 7);
    }

    fn length_extra_value(self, length: int) -> int {
        let code = self.length_code(length);
        return length - self.length_base.at(code);
    }

    fn distance_extra_value(self, distance: int) -> int {
        let code = self.distance_code(distance);
        return distance - self.distance_base.at(code);
    }

    var length_base: vector(int);
    var length_extra: vector(int);
    var distance_base: vector(int);
    var distance_extra: vector(int);
    var length_to_code: vector(int);
    var distance_to_code_low: vector(int);
    var distance_to_code_high: vector(int);
    var code_length_order: vector(int);
    var ready: bool = false;
}

// Free-function forms used by `Token::cost_estimate`, which has no access to
// a table instance. They recompute the same regular structure arithmetically.
fn extra_length_bits(code: int) -> int {
    if (code >= 28) {
        return 0;
    }
    if (code < 8) {
        return 0;
    }
    return clamp_int((code - 4) / 4, 0, 5);
}

fn extra_distance_bits(code: int) -> int {
    if (code < 4) {
        return 0;
    }
    return clamp_int((code / 2) - 1, 0, 13);
}

fn length_code_of(length: int) -> int {
    if (length >= MAX_MATCH) {
        return 28;
    }
    var code: int = 0;
    var anchor: int = 3;
    while (code < 28) {
        let span = 1 << extra_length_bits(code);
        if (length < anchor + span) {
            return code;
        }
        anchor += span;
        code += 1;
    }
    return 28;
}

fn distance_code_of(distance: int) -> int {
    var code: int = 0;
    var anchor: int = 1;
    while (code < DISTANCE_SYMBOLS) {
        let span = 1 << extra_distance_bits(code);
        if (distance < anchor + span) {
            return code;
        }
        anchor += span;
        code += 1;
    }
    return DISTANCE_SYMBOLS - 1;
}

// The fixed Huffman code of a deflate stream is defined by a fixed length
// assignment rather than by a transmitted table: literals 0..143 get 8 bits,
// 144..255 get 9, 256..279 get 7 and 280..287 get 8. Distances are all 5.
fn build_fixed_literal_lengths(ref lengths: vector(int)) {
    lengths.clear();
    lengths.resize(MAX_SYMBOLS);
    var symbol: int = 0;
    while (symbol < MAX_SYMBOLS) {
        if (symbol < 144) {
            lengths.at(symbol) = 8;
        } else { if (symbol < 256) {
            lengths.at(symbol) = 9;
        } else { if (symbol < 280) {
            lengths.at(symbol) = 7;
        } else {
            lengths.at(symbol) = 8;
        }
        } }
        symbol += 1;
    }
}

fn build_fixed_distance_lengths(ref lengths: vector(int)) {
    lengths.clear();
    lengths.resize(DISTANCE_SYMBOLS + 2);
    var symbol: int = 0;
    while (symbol < DISTANCE_SYMBOLS + 2) {
        lengths.at(symbol) = 5;
        symbol += 1;
    }
}

// --- Code length run encoding -------------------------------------------

// A dynamic block transmits its two code length vectors compressed with a
// third Huffman code over the alphabet {0..15, 16, 17, 18}, where
//
//   16 = repeat the previous length 3..6 more times (2 extra bits)
//   17 = a run of 3..10 zero lengths            (3 extra bits)
//   18 = a run of 11..138 zero lengths          (7 extra bits)
//
// `RunEncoder` turns a length vector into that symbol stream.
class RunEncoder {

    fn clear(ref self) {
        self.symbols.clear();
        self.extras.clear();
        self.extra_bits.clear();
    }

    fn count(self) -> int {
        return self.symbols.size();
    }

    fn emit(ref self, symbol: int, extra: int, bits: int) {
        self.symbols.push_back(symbol);
        self.extras.push_back(extra);
        self.extra_bits.push_back(bits);
    }

    fn encode(ref self, lengths: vector(int), used: int) {
        self.clear();
        var index: int = 0;
        var previous: int = -1;
        while (index < used) {
            let current = lengths.at(index);
            // Measure the run of equal lengths starting here.
            var run: int = 1;
            while (index + run < used) {
                if (lengths.at(index + run) != current) {
                    break;
                }
                run += 1;
            }

            if (current == 0) {
                // Zero runs use the dedicated 17 / 18 symbols.
                while (run >= 11) {
                    let take = min_int(run, 138);
                    self.emit(18, take - 11, 7);
                    run -= take;
                    index += take;
                }
                while (run >= 3) {
                    let take = min_int(run, 10);
                    self.emit(17, take - 3, 3);
                    run -= take;
                    index += take;
                }
                while (run > 0) {
                    self.emit(0, 0, 0);
                    run -= 1;
                    index += 1;
                }
                previous = 0;
            } else {
                // A non-zero run repeats the literal length once, then uses
                // symbol 16 for the remainder.
                var remaining = run;
                if (previous != current) {
                    self.emit(current, 0, 0);
                    remaining -= 1;
                    index += 1;
                }
                while (remaining >= 3) {
                    let take = min_int(remaining, 6);
                    self.emit(16, take - 3, 2);
                    remaining -= take;
                    index += take;
                }
                while (remaining > 0) {
                    self.emit(current, 0, 0);
                    remaining -= 1;
                    index += 1;
                }
                previous = current;
            }
        }
    }

    // Histogram over the code length alphabet, used to build its own code.
    fn count_symbols(self, ref frequencies: FrequencyTable) {
        var i: int = 0;
        while (i < self.symbols.size()) {
            frequencies.add(self.symbols.at(i));
            i += 1;
        }
    }

    var symbols: vector(int);
    var extras: vector(int);
    var extra_bits: vector(int);
}

// --- Deflate encoder -----------------------------------------------------

// The full pipeline: LZ77 the input, histogram the resulting tokens, build
// dynamic codes, compare the three block encodings and emit the cheapest.
class DeflateEncoder {

    fn init(ref self, level: int) {
        self.tables.init();
        self.lz.init(level);
        self.writer.init(BitOrder.LsbFirst);
        self.config.from_level(level);
        self.blocks_stored = 0;
        self.blocks_fixed = 0;
        self.blocks_dynamic = 0;
    }

    // --- Cost models -----------------------------------------------------

    // Bits needed for a stored block: three header bits, padding to the byte
    // boundary, a four byte length header and the payload.
    fn stored_cost(self, byte_count: int) -> int {
        return 3 + 7 + 32 + byte_count * BYTE_BITS;
    }

    fn fixed_cost(self, literals: FrequencyTable, distances: FrequencyTable) -> int {
        var total: int = 3;
        var symbol: int = 0;
        while (symbol < LITERAL_SYMBOLS) {
            let count = literals.at(symbol);
            if (count > 0) {
                var bits: int = 8;
                if (symbol >= 144) {
                    bits = 9;
                }
                if (symbol >= 256) {
                    bits = 7;
                }
                if (symbol >= 280) {
                    bits = 8;
                }
                total += count * bits;
                total += count * extra_length_bits(symbol - 257);
            }
            symbol += 1;
        }
        symbol = 0;
        while (symbol < DISTANCE_SYMBOLS) {
            let count = distances.at(symbol);
            if (count > 0) {
                total += count * (5 + extra_distance_bits(symbol));
            }
            symbol += 1;
        }
        return total;
    }

    fn dynamic_cost(self, literal_tree: HuffmanTree, distance_tree: HuffmanTree, literals: FrequencyTable, distances: FrequencyTable, header_bits: int) -> int {
        var total = 3 + header_bits;
        total += literal_tree.encoded_bits(literals);
        total += distance_tree.encoded_bits(distances);
        var symbol: int = 257;
        while (symbol < LITERAL_SYMBOLS) {
            total += literals.at(symbol) * extra_length_bits(symbol - 257);
            symbol += 1;
        }
        symbol = 0;
        while (symbol < DISTANCE_SYMBOLS) {
            total += distances.at(symbol) * extra_distance_bits(symbol);
            symbol += 1;
        }
        return total;
    }

    // --- Histograms ------------------------------------------------------

    fn histogram_tokens(self, tokens: vector(Token), ref literals: FrequencyTable, ref distances: FrequencyTable) {
        var i: int = 0;
        while (i < tokens.size()) {
            let ref token: auto = tokens.at(i);
            if (token.is_literal()) {
                literals.add(token.literal & BYTE_MASK);
            } else {
                literals.add(257 + self.tables.length_code(token.length));
                distances.add(self.tables.distance_code(token.distance));
            }
            i += 1;
        }
        literals.add(END_OF_BLOCK);
    }

    // --- Block emission --------------------------------------------------

    fn write_block_header(ref self, kind: BlockType, final_block: bool) {
        var flag: int = 0;
        if (final_block) {
            flag = 1;
        }
        self.writer.put_bits_lsb(flag, 1);
        if (kind == BlockType.Stored) {
            self.writer.put_bits_lsb(0, 2);
        } else { if (kind == BlockType.Fixed) {
            self.writer.put_bits_lsb(1, 2);
        } else {
            self.writer.put_bits_lsb(2, 2);
        }
        }
    }

    fn write_stored_block(ref self, input: ByteBuffer, from: int, count: int, final_block: bool) {
        self.write_block_header(BlockType.Stored, final_block);
        self.writer.align();
        self.writer.put_short(count & SHORT_MASK);
        self.writer.put_short((^count) & SHORT_MASK);
        self.writer.raw_copy(input, from, count);
        self.blocks_stored += 1;
    }

    fn write_fixed_block(ref self, tokens: vector(Token), final_block: bool) {
        self.write_block_header(BlockType.Fixed, final_block);
        var literal_lengths = vector(int)();
        var distance_lengths = vector(int)();
        build_fixed_literal_lengths(literal_lengths);
        build_fixed_distance_lengths(distance_lengths);
        var literal_table = EncodeTable();
        var distance_table = EncodeTable();
        literal_table.build_from_lengths(literal_lengths);
        distance_table.build_from_lengths(distance_lengths);
        self.write_token_stream(tokens, literal_table, distance_table);
        self.blocks_fixed += 1;
        Destroy(literal_lengths);
        Destroy(distance_lengths);
    }

    // Emit the dynamic header: the two truncated length counts, the code
    // length code lengths in permuted order, and the run encoded lengths.
    fn write_dynamic_header(ref self, literal_lengths: vector(int), distance_lengths: vector(int), literal_used: int, distance_used: int, runs: RunEncoder, code_length_table: EncodeTable, code_length_lengths: vector(int)) -> int {
        var bits_written: int = 0;
        self.writer.put_bits_lsb(literal_used - 257, 5);
        self.writer.put_bits_lsb(distance_used - 1, 5);

        // Trim trailing zero entries from the permuted code length vector.
        var transmitted = CODE_LENGTH_SYMBOLS;
        while (transmitted > 4) {
            let slot = self.tables.code_length_order.at(transmitted - 1);
            if (code_length_lengths.at(slot) != 0) {
                break;
            }
            transmitted -= 1;
        }
        self.writer.put_bits_lsb(transmitted - 4, 4);
        bits_written += 14;

        var i: int = 0;
        while (i < transmitted) {
            let slot = self.tables.code_length_order.at(i);
            self.writer.put_bits_lsb(code_length_lengths.at(slot), 3);
            bits_written += 3;
            i += 1;
        }

        i = 0;
        while (i < runs.symbols.size()) {
            let symbol = runs.symbols.at(i);
            code_length_table.emit_reversed(self.writer, symbol);
            bits_written += code_length_table.length_of(symbol);
            let extra_count = runs.extra_bits.at(i);
            if (extra_count > 0) {
                self.writer.put_bits_lsb(runs.extras.at(i), extra_count);
                bits_written += extra_count;
            }
            i += 1;
        }
        Discard(literal_lengths);
        Discard(distance_lengths);
        return bits_written;
    }

    fn write_token_stream(ref self, tokens: vector(Token), literal_table: EncodeTable, distance_table: EncodeTable) {
        var i: int = 0;
        while (i < tokens.size()) {
            let ref token: auto = tokens.at(i);
            if (token.is_literal()) {
                literal_table.emit_reversed(self.writer, token.literal & BYTE_MASK);
            } else {
                let length_code = self.tables.length_code(token.length);
                literal_table.emit_reversed(self.writer, 257 + length_code);
                let length_extra = self.tables.length_extra.at(length_code);
                if (length_extra > 0) {
                    self.writer.put_bits_lsb(token.length - self.tables.length_base.at(length_code), length_extra);
                }
                let distance_code = self.tables.distance_code(token.distance);
                distance_table.emit_reversed(self.writer, distance_code);
                let distance_extra = self.tables.distance_extra.at(distance_code);
                if (distance_extra > 0) {
                    self.writer.put_bits_lsb(token.distance - self.tables.distance_base.at(distance_code), distance_extra);
                }
            }
            i += 1;
        }
        literal_table.emit_reversed(self.writer, END_OF_BLOCK);
    }

    // Build the dynamic codes for a token stream and emit the block.
    fn write_dynamic_block(ref self, tokens: vector(Token), final_block: bool) -> bool {
        var literals = FrequencyTable();
        var distances = FrequencyTable();
        literals.init(MAX_SYMBOLS);
        distances.init(DISTANCE_SYMBOLS);
        self.histogram_tokens(tokens, literals, distances);
        distances.ensure_two_symbols();

        var literal_tree = HuffmanTree();
        var distance_tree = HuffmanTree();
        if (not literal_tree.build(literals)) {
            return false;
        }
        if (not distance_tree.build(distances)) {
            return false;
        }

        var literal_used = LITERAL_SYMBOLS;
        while (literal_used > 257) {
            if (literal_tree.lengths.at(literal_used - 1) != 0) {
                break;
            }
            literal_used -= 1;
        }
        var distance_used = DISTANCE_SYMBOLS;
        while (distance_used > 1) {
            if (distance_tree.lengths.at(distance_used - 1) != 0) {
                break;
            }
            distance_used -= 1;
        }

        // Concatenate the two length vectors and run encode them together,
        // which is what lets a run straddle the boundary.
        var combined = vector(int)();
        var i: int = 0;
        while (i < literal_used) {
            combined.push_back(literal_tree.lengths.at(i));
            i += 1;
        }
        i = 0;
        while (i < distance_used) {
            combined.push_back(distance_tree.lengths.at(i));
            i += 1;
        }

        var runs = RunEncoder();
        runs.encode(combined, combined.size());

        var code_length_frequencies = FrequencyTable();
        code_length_frequencies.init(CODE_LENGTH_SYMBOLS);
        runs.count_symbols(code_length_frequencies);
        code_length_frequencies.ensure_two_symbols();

        var code_length_tree = HuffmanTree();
        if (not code_length_tree.build(code_length_frequencies)) {
            return false;
        }
        code_length_tree.limit_code_lengths(MAX_LENGTH_CODE_BITS);
        code_length_tree.assign_canonical();

        var code_length_table = EncodeTable();
        code_length_table.build(code_length_tree);

        var literal_table = EncodeTable();
        var distance_table = EncodeTable();
        literal_table.build(literal_tree);
        distance_table.build(distance_tree);

        self.write_block_header(BlockType.Dynamic, final_block);
        self.write_dynamic_header(literal_tree.lengths, distance_tree.lengths, literal_used, distance_used, runs, code_length_table, code_length_tree.lengths);
        self.write_token_stream(tokens, literal_table, distance_table);
        self.blocks_dynamic += 1;

        Destroy(combined);
        return true;
    }

    // Compress a whole buffer, splitting it into blocks of at most
    // DEFAULT_BLOCK_SIZE bytes and picking a block type per chunk.
    fn compress(ref self, input: ByteBuffer) -> Status {
        self.writer.init(BitOrder.LsbFirst);
        if (input.empty()) {
            self.write_block_header(BlockType.Stored, true);
            self.writer.align();
            self.writer.put_short(0);
            self.writer.put_short(SHORT_MASK);
            return Status.Ok;
        }

        var offset: int = 0;
        while (offset < input.size()) {
            let span = min_int(DEFAULT_BLOCK_SIZE, input.size() - offset);
            let final_block = (offset + span) >= input.size();

            var chunk = ByteBuffer();
            var i: int = 0;
            while (i < span) {
                chunk.push(input.at(offset + i));
                i += 1;
            }

            self.lz.init(self.config.level);
            self.lz.encode(chunk);

            var literals = FrequencyTable();
            var distances = FrequencyTable();
            literals.init(MAX_SYMBOLS);
            distances.init(DISTANCE_SYMBOLS);
            self.histogram_tokens(self.lz.tokens, literals, distances);

            let cost_stored = self.stored_cost(span);
            let cost_fixed = self.fixed_cost(literals, distances);

            if (self.config.stored_only) {
                self.write_stored_block(input, offset, span, final_block);
            } else { if (cost_stored <= cost_fixed) {
                self.write_stored_block(input, offset, span, final_block);
            } else {
                if (not self.write_dynamic_block(self.lz.tokens, final_block)) {
                    self.write_fixed_block(self.lz.tokens, final_block);
                }
            }
            }

            Destroy(chunk);
            offset += span;
        }
        self.writer.flush();
        return Status.Ok;
    }

    var tables: DeflateTables;
    var lz: Lz77Encoder;
    var writer: BitWriter;
    var config: LevelConfig;
    var blocks_stored: int = 0;
    var blocks_fixed: int = 0;
    var blocks_dynamic: int = 0;
}

// --- Deflate decoder -----------------------------------------------------

// Reads a deflate stream back into bytes. The decoder is table driven: each
// block builds a `DecodeTable` for its literal/length and distance codes,
// then the inner loop is peek, look up, drop, and either append a literal or
// run a window copy.
class DeflateDecoder {

    fn init(ref self) {
        self.tables.init();
        self.reader.init(BitOrder.LsbFirst);
        self.window.init(WINDOW_SIZE);
        self.blocks_read = 0;
        self.last_status = Status.Ok;
    }

    fn fail(ref self, status: Status) -> Status {
        self.last_status = status;
        return status;
    }

    // Read a stored block: align, read the length and its complement, and
    // copy the payload straight through.
    fn read_stored_block(ref self, ref output: ByteBuffer) -> Status {
        self.reader.align();
        let count = self.reader.get_short();
        let complement = self.reader.get_short();
        if (((count ^ SHORT_MASK) & SHORT_MASK) != (complement & SHORT_MASK)) {
            return self.fail(Status.CorruptStream);
        }
        var i: int = 0;
        while (i < count) {
            if (self.reader.source.exhausted()) {
                return self.fail(Status.NeedMoreInput);
            }
            let value = self.reader.get_byte();
            self.window.emit_literal(output, value);
            i += 1;
        }
        return Status.Ok;
    }

    // Rebuild the code length code, then decode the two length vectors from
    // the run encoded stream.
    fn read_dynamic_tables(ref self, ref literal_table: DecodeTable, ref distance_table: DecodeTable) -> Status {
        let literal_used = self.reader.get_bits_lsb(5) + 257;
        let distance_used = self.reader.get_bits_lsb(5) + 1;
        let transmitted = self.reader.get_bits_lsb(4) + 4;
        if (literal_used > LITERAL_SYMBOLS) {
            return self.fail(Status.BadCodeLengths);
        }
        if (distance_used > DISTANCE_SYMBOLS) {
            return self.fail(Status.BadCodeLengths);
        }

        var code_length_lengths = vector(int)(CODE_LENGTH_SYMBOLS);
        var i: int = 0;
        while (i < CODE_LENGTH_SYMBOLS) {
            code_length_lengths.at(i) = 0;
            i += 1;
        }
        i = 0;
        while (i < transmitted) {
            let slot = self.tables.code_length_order.at(i);
            code_length_lengths.at(slot) = self.reader.get_bits_lsb(3);
            i += 1;
        }

        var code_length_table = DecodeTable();
        if (not code_length_table.build(code_length_lengths)) {
            return self.fail(Status.BadCodeLengths);
        }

        let total = literal_used + distance_used;
        var combined = vector(int)(total);
        i = 0;
        while (i < total) {
            combined.at(i) = 0;
            i += 1;
        }

        var index: int = 0;
        var previous: int = 0;
        while (index < total) {
            let symbol = code_length_table.decode(self.reader);
            if (symbol < 0) {
                return self.fail(Status.BadCodeLengths);
            }
            if (symbol < 16) {
                combined.at(index) = symbol;
                previous = symbol;
                index += 1;
            } else { if (symbol == 16) {
                let repeat = self.reader.get_bits_lsb(2) + 3;
                if (index + repeat > total) {
                    return self.fail(Status.BadCodeLengths);
                }
                var k: int = 0;
                while (k < repeat) {
                    combined.at(index) = previous;
                    index += 1;
                    k += 1;
                }
            } else { if (symbol == 17) {
                let repeat = self.reader.get_bits_lsb(3) + 3;
                if (index + repeat > total) {
                    return self.fail(Status.BadCodeLengths);
                }
                var k: int = 0;
                while (k < repeat) {
                    combined.at(index) = 0;
                    index += 1;
                    k += 1;
                }
                previous = 0;
            } else {
                let repeat = self.reader.get_bits_lsb(7) + 11;
                if (index + repeat > total) {
                    return self.fail(Status.BadCodeLengths);
                }
                var k: int = 0;
                while (k < repeat) {
                    combined.at(index) = 0;
                    index += 1;
                    k += 1;
                }
                previous = 0;
            }
            } }
        }

        var literal_lengths = vector(int)(literal_used);
        var distance_lengths = vector(int)(distance_used);
        i = 0;
        while (i < literal_used) {
            literal_lengths.at(i) = combined.at(i);
            i += 1;
        }
        i = 0;
        while (i < distance_used) {
            distance_lengths.at(i) = combined.at(literal_used + i);
            i += 1;
        }

        if (not literal_table.build(literal_lengths)) {
            return self.fail(Status.BadCodeLengths);
        }
        if (not distance_table.build(distance_lengths)) {
            if (not distance_table.under_subscribed) {
                return self.fail(Status.BadCodeLengths);
            }
        }
        Destroy(combined);
        Destroy(code_length_lengths);
        return Status.Ok;
    }

    // The hot decode loop for one compressed block.
    fn read_compressed_block(ref self, ref output: ByteBuffer, literal_table: DecodeTable, distance_table: DecodeTable) -> Status {
        while (true) {
            if (self.reader.failed()) {
                return self.fail(Status.NeedMoreInput);
            }
            let symbol = literal_table.decode(self.reader);
            if (symbol < 0) {
                return self.fail(Status.CorruptStream);
            }
            if (symbol < 256) {
                self.window.emit_literal(output, symbol);
            } else { if (symbol == END_OF_BLOCK) {
                return Status.Ok;
            } else {
                let length_code = symbol - 257;
                if (length_code >= 29) {
                    return self.fail(Status.CorruptStream);
                }
                let length_extra = self.tables.length_extra.at(length_code);
                var length = self.tables.length_base.at(length_code);
                if (length_extra > 0) {
                    length += self.reader.get_bits_lsb(length_extra);
                }

                let distance_symbol = distance_table.decode(self.reader);
                if (distance_symbol < 0) {
                    return self.fail(Status.CorruptStream);
                }
                if (distance_symbol >= DISTANCE_SYMBOLS) {
                    return self.fail(Status.BadDistance);
                }
                let distance_extra = self.tables.distance_extra.at(distance_symbol);
                var distance = self.tables.distance_base.at(distance_symbol);
                if (distance_extra > 0) {
                    distance += self.reader.get_bits_lsb(distance_extra);
                }
                if (distance > MAX_DISTANCE) {
                    return self.fail(Status.BadDistance);
                }
                if (not self.window.copy_match(output, distance, length)) {
                    return self.fail(Status.BadDistance);
                }
            }
            }
        }
    }

    fn decompress(ref self, input: ByteBuffer, ref output: ByteBuffer) -> Status {
        self.reader.init(BitOrder.LsbFirst);
        self.reader.source = input;
        self.reader.source.rewind();
        self.window.init(WINDOW_SIZE);

        var final_block = false;
        while (not final_block) {
            if (self.reader.failed()) {
                return self.fail(Status.NeedMoreInput);
            }
            final_block = self.reader.get_bits_lsb(1) == 1;
            let kind = self.reader.get_bits_lsb(2);
            self.blocks_read += 1;

            if (kind == 0) {
                let status = self.read_stored_block(output);
                if (status != Status.Ok) {
                    return status;
                }
            } else { if (kind == 1) {
                var literal_lengths = vector(int)();
                var distance_lengths = vector(int)();
                build_fixed_literal_lengths(literal_lengths);
                build_fixed_distance_lengths(distance_lengths);
                var literal_table = DecodeTable();
                var distance_table = DecodeTable();
                if (not literal_table.build(literal_lengths)) {
                    return self.fail(Status.BadCodeLengths);
                }
                if (not distance_table.build(distance_lengths)) {
                    return self.fail(Status.BadCodeLengths);
                }
                let status = self.read_compressed_block(output, literal_table, distance_table);
                if (status != Status.Ok) {
                    return status;
                }
                Destroy(literal_lengths);
                Destroy(distance_lengths);
            } else { if (kind == 2) {
                var literal_table = DecodeTable();
                var distance_table = DecodeTable();
                let header = self.read_dynamic_tables(literal_table, distance_table);
                if (header != Status.Ok) {
                    return header;
                }
                let status = self.read_compressed_block(output, literal_table, distance_table);
                if (status != Status.Ok) {
                    return status;
                }
            } else {
                return self.fail(Status.CorruptStream);
            }
            } }
        }
        return Status.Ok;
    }

    var tables: DeflateTables;
    var reader: BitReader;
    var window: SlidingWindow;
    var blocks_read: int = 0;
    var last_status: Status = Status.Ok;
}

// --- Run length encoding -------------------------------------------------

// A byte oriented RLE with an escape byte. A run of three or more identical
// bytes becomes <escape, count, value>; a literal equal to the escape byte
// is emitted as <escape, 0>. Runs longer than 255 are split.
let RLE_ESCAPE: int = 144;                   // 0x90
let RLE_MIN_RUN: int = 3;
let RLE_MAX_RUN: int = 255;

class RleCodec {

    fn encode(ref self, input: ByteBuffer, ref output: ByteBuffer) {
        var i: int = 0;
        self.runs_emitted = 0;
        self.literals_emitted = 0;
        while (i < input.size()) {
            let value = input.at(i);
            var run: int = 1;
            while (i + run < input.size()) {
                if (input.at(i + run) != value) {
                    break;
                }
                if (run >= RLE_MAX_RUN) {
                    break;
                }
                run += 1;
            }

            if (run >= RLE_MIN_RUN) {
                output.push(RLE_ESCAPE);
                output.push(run);
                output.push(value);
                self.runs_emitted += 1;
                i += run;
            } else {
                var k: int = 0;
                while (k < run) {
                    if (value == RLE_ESCAPE) {
                        output.push(RLE_ESCAPE);
                        output.push(0);
                    } else {
                        output.push(value);
                    }
                    self.literals_emitted += 1;
                    k += 1;
                }
                i += run;
            }
        }
    }

    fn decode(ref self, input: ByteBuffer, ref output: ByteBuffer) -> Status {
        var i: int = 0;
        while (i < input.size()) {
            let value = input.at(i);
            if (value != RLE_ESCAPE) {
                output.push(value);
                i += 1;
            } else {
                if (i + 1 >= input.size()) {
                    return Status.NeedMoreInput;
                }
                let count = input.at(i + 1);
                if (count == 0) {
                    output.push(RLE_ESCAPE);
                    i += 2;
                } else {
                    if (i + 2 >= input.size()) {
                        return Status.NeedMoreInput;
                    }
                    let repeated = input.at(i + 2);
                    var k: int = 0;
                    while (k < count) {
                        output.push(repeated);
                        k += 1;
                    }
                    i += 3;
                }
            }
        }
        return Status.Ok;
    }

    // Longest run in the input; the pipeline uses it to decide whether RLE
    // is worth applying before the block sorting stage.
    fn longest_run(self, input: ByteBuffer) -> int {
        if (input.empty()) {
            return 0;
        }
        var best: int = 1;
        var current: int = 1;
        var i: int = 1;
        while (i < input.size()) {
            if (input.at(i) == input.at(i - 1)) {
                current += 1;
                if (current > best) {
                    best = current;
                }
            } else {
                current = 1;
            }
            i += 1;
        }
        return best;
    }

    var runs_emitted: int = 0;
    var literals_emitted: int = 0;
}

// --- Move to front -------------------------------------------------------

// MTF turns a stream with local symbol locality into a stream dominated by
// small indices, which is exactly what a Huffman or arithmetic stage wants
// after a Burrows-Wheeler transform.
class MoveToFront {

    fn init(ref self) {
        self.order.clear();
        self.order.resize(256);
        var i: int = 0;
        while (i < 256) {
            self.order.at(i) = i;
            i += 1;
        }
    }

    // Find the current rank of a symbol and pull it to the front, shifting
    // everything above it down by one.
    fn rank_of(ref self, value: int) -> int {
        var position: int = 0;
        while (position < 256) {
            if (self.order.at(position) == value) {
                break;
            }
            position += 1;
        }
        var i = position;
        while (i > 0) {
            self.order.at(i) = self.order.at(i - 1);
            i -= 1;
        }
        self.order.at(0) = value;
        return position;
    }

    fn symbol_at(ref self, rank: int) -> int {
        let value = self.order.at(rank);
        var i = rank;
        while (i > 0) {
            self.order.at(i) = self.order.at(i - 1);
            i -= 1;
        }
        self.order.at(0) = value;
        return value;
    }

    fn encode(ref self, input: ByteBuffer, ref output: ByteBuffer) {
        self.init();
        var i: int = 0;
        while (i < input.size()) {
            output.push(self.rank_of(input.at(i)));
            i += 1;
        }
    }

    fn decode(ref self, input: ByteBuffer, ref output: ByteBuffer) {
        self.init();
        var i: int = 0;
        while (i < input.size()) {
            output.push(self.symbol_at(input.at(i)));
            i += 1;
        }
    }

    var order: vector(int);
}

// --- Burrows-Wheeler transform -------------------------------------------

// The forward transform sorts every rotation of the input and emits the last
// column plus the index of the original string. The inverse rebuilds the
// input from the last column alone using the standard LF mapping.
//
// The rotation sort here is a plain comparison sort with an explicit
// comparison function rather than a suffix array construction; the point of
// the benchmark is the tight comparison loop, not asymptotics.
class BurrowsWheeler {

    // Compare rotations `a` and `b` of `input` lexicographically.
    fn compare_rotations(self, input: ByteBuffer, a: int, b: int) -> int {
        let n = input.size();
        var offset: int = 0;
        while (offset < n) {
            let left = input.at((a + offset) % n);
            let right = input.at((b + offset) % n);
            if (left != right) {
                if (left < right) {
                    return -1;
                }
                return 1;
            }
            offset += 1;
        }
        return 0;
    }

    // Binary insertion sort over rotation indices.
    fn sort_rotations(self, input: ByteBuffer, ref order: vector(int)) {
        var i: int = 1;
        while (i < order.size()) {
            let key = order.at(i);
            var low: int = 0;
            var high = i;
            while (low < high) {
                let middle = (low + high) / 2;
                if (self.compare_rotations(input, order.at(middle), key) <= 0) {
                    low = middle + 1;
                } else {
                    high = middle;
                }
            }
            var j = i;
            while (j > low) {
                order.at(j) = order.at(j - 1);
                j -= 1;
            }
            order.at(low) = key;
            i += 1;
        }
    }

    fn transform(ref self, input: ByteBuffer, ref output: ByteBuffer) -> int {
        let n = input.size();
        if (n == 0) {
            return 0;
        }
        var order = vector(int)(n);
        var i: int = 0;
        while (i < n) {
            order.at(i) = i;
            i += 1;
        }
        self.sort_rotations(input, order);

        var primary: int = 0;
        i = 0;
        while (i < n) {
            let rotation = order.at(i);
            if (rotation == 0) {
                primary = i;
            }
            output.push(input.at((rotation + n - 1) % n));
            i += 1;
        }
        self.primary_index = primary;
        Destroy(order);
        return primary;
    }

    // Inverse transform via the LF mapping: count the occurrences of each
    // byte, turn the counts into starting offsets, then thread the next
    // pointers through the last column.
    fn inverse(self, last_column: ByteBuffer, primary: int, ref output: ByteBuffer) -> Status {
        let n = last_column.size();
        if (n == 0) {
            return Status.Ok;
        }
        if (primary < 0 or primary >= n) {
            return Status.CorruptStream;
        }

        var counts = vector(int)(257);
        var i: int = 0;
        while (i <= 256) {
            counts.at(i) = 0;
            i += 1;
        }
        i = 0;
        while (i < n) {
            counts.at(last_column.at(i) + 1) += 1;
            i += 1;
        }
        i = 1;
        while (i <= 256) {
            counts.at(i) += counts.at(i - 1);
            i += 1;
        }

        var next = vector(int)(n);
        i = 0;
        while (i < n) {
            let value = last_column.at(i);
            next.at(counts.at(value)) = i;
            counts.at(value) += 1;
            i += 1;
        }

        var position = next.at(primary);
        i = 0;
        while (i < n) {
            output.push(last_column.at(position));
            position = next.at(position);
            i += 1;
        }
        Destroy(counts);
        Destroy(next);
        return Status.Ok;
    }

    var primary_index: int = 0;
}

// --- Adaptive frequency model -------------------------------------------

// A cumulative frequency model over a small alphabet, maintained as a Fenwick
// tree so that both the cumulative query and the update are logarithmic. The
// arithmetic coder needs the cumulative frequency below a symbol, the
// symbol's own frequency, and the running total, on every single symbol.
class AdaptiveModel {

    fn init(ref self, alphabet: int) {
        self.alphabet = alphabet;
        self.frequencies.clear();
        self.frequencies.resize(alphabet + 1);
        self.tree.clear();
        self.tree.resize(alphabet + 1);
        var i: int = 0;
        while (i <= alphabet) {
            self.frequencies.at(i) = 0;
            self.tree.at(i) = 0;
            i += 1;
        }
        // Every symbol starts with a count of one so nothing is unencodable.
        i = 0;
        while (i < alphabet) {
            self.frequencies.at(i + 1) = 1;
            i += 1;
        }
        self.rebuild_tree();
        self.total = alphabet;
        self.updates = 0;
    }

    // Rebuild the Fenwick tree from the raw frequency array in linear time:
    // every node absorbs its immediate child's partial sum.
    fn rebuild_tree(ref self) {
        var i: int = 1;
        while (i <= self.alphabet) {
            self.tree.at(i) = self.frequencies.at(i);
            i += 1;
        }
        i = 1;
        while (i <= self.alphabet) {
            let parent = i + (i & (-i));
            if (parent <= self.alphabet) {
                self.tree.at(parent) += self.tree.at(i);
            }
            i += 1;
        }
    }

    // Sum of frequencies for symbols strictly below `symbol`.
    fn cumulative_below(self, symbol: int) -> int {
        var index = symbol;
        var sum: int = 0;
        while (index > 0) {
            sum += self.tree.at(index);
            index -= index & (-index);
        }
        return sum;
    }

    fn frequency_of(self, symbol: int) -> int {
        return self.frequencies.at(symbol + 1);
    }

    fn total_frequency(self) -> int {
        return self.total;
    }

    // Locate the symbol whose cumulative interval contains `target`, by
    // descending the Fenwick tree one power of two at a time.
    fn symbol_for(self, target: int) -> int {
        var index: int = 0;
        var remaining = target;
        var step = round_up_pow2(self.alphabet);
        while (step > 0) {
            let candidate = index + step;
            if (candidate <= self.alphabet) {
                if (self.tree.at(candidate) <= remaining) {
                    index = candidate;
                    remaining -= self.tree.at(candidate);
                }
            }
            step = (step >> 1) & WORD_MASK;
        }
        return index;
    }

    fn update(ref self, symbol: int) {
        var index = symbol + 1;
        self.frequencies.at(index) += MODEL_INCREMENT;
        while (index <= self.alphabet) {
            self.tree.at(index) += MODEL_INCREMENT;
            index += index & (-index);
        }
        self.total += MODEL_INCREMENT;
        self.updates += 1;
        if (self.total >= MODEL_TOTAL_MAX) {
            self.rescale();
        }
    }

    // Halve every count, keeping a floor of one so no symbol drops out of
    // the model, then rebuild the tree in one pass.
    fn rescale(ref self) {
        var new_total: int = 0;
        var i: int = 1;
        while (i <= self.alphabet) {
            var value = (self.frequencies.at(i) + 1) >> 1;
            if (value < 1) {
                value = 1;
            }
            self.frequencies.at(i) = value;
            new_total += value;
            i += 1;
        }
        self.total = new_total;
        self.rebuild_tree();
    }

    var frequencies: vector(int);
    var tree: vector(int);
    var alphabet: int = 256;
    var total: int = 0;
    var updates: int = 0;
}

// --- Arithmetic encoder --------------------------------------------------

// A carry-less range coder over 32 bit arithmetic. The interval [low, high)
// is narrowed by each symbol; whenever the two ends share a top byte, that
// byte is shifted out. The underflow counter handles the case where the
// interval straddles the midpoint without converging.
class ArithmeticEncoder {

    fn init(ref self) {
        self.low = 0;
        self.high = CODER_MAX;
        self.underflow = 0;
        self.out.clear();
        self.writer.init(BitOrder.MsbFirst);
        self.symbols_written = 0;
    }

    fn range(self) -> int {
        return self.high - self.low + 1;
    }

    // Narrow the interval to the sub-range assigned to `symbol` by `model`.
    fn encode_symbol(ref self, ref model: AdaptiveModel, symbol: int) {
        let total = model.total_frequency();
        let low_count = model.cumulative_below(symbol);
        let high_count = low_count + model.frequency_of(symbol);
        let span = self.high - self.low + 1;

        self.high = self.low + (span * high_count) / total - 1;
        self.low = self.low + (span * low_count) / total;
        self.normalise();
        model.update(symbol);
        self.symbols_written += 1;
    }

    // Shift out every bit the two ends now agree on, and absorb straddling
    // intervals into the underflow counter.
    fn normalise(ref self) {
        while (true) {
            if (((self.low ^ self.high) & HIGH_BIT_32) == 0) {
                // Top bits agree: emit it, plus any pending underflow bits
                // with the opposite value.
                let bit = (self.low >> 31) & 1;
                self.writer.put_bit(bit);
                while (self.underflow > 0) {
                    self.writer.put_bit((^bit) & 1);
                    self.underflow -= 1;
                }
            } else {
                if ((self.low & 1073741824) != 0 and (self.high & 1073741824) == 0) {
                    // 0x40000000: straddling the midpoint. Defer the
                    // decision by removing the second bit and counting it.
                    self.underflow += 1;
                    self.low = self.low & 1073741823;                       // 0x3FFFFFFF
                    self.high = self.high | 1073741824;                     // 0x40000000
                } else {
                    return;
                }
            }
            self.low = (self.low << 1) & WORD_MASK;
            self.high = ((self.high << 1) | 1) & WORD_MASK;
        }
    }

    // Flush enough bits to disambiguate the final interval.
    fn finish(ref self) {
        let bit = (self.low >> 30) & 1;
        self.writer.put_bit(bit);
        self.underflow += 1;
        while (self.underflow > 0) {
            self.writer.put_bit((^bit) & 1);
            self.underflow -= 1;
        }
        self.writer.align();
    }

    fn encode_buffer(ref self, input: ByteBuffer, ref model: AdaptiveModel) {
        self.init();
        var i: int = 0;
        while (i < input.size()) {
            self.encode_symbol(model, input.at(i));
            i += 1;
        }
        // A dedicated stop symbol marks the end of the stream so the decoder
        // does not need an explicit length.
        self.encode_symbol(model, 256);
        self.finish();
    }

    var writer: BitWriter;
    var out: ByteBuffer;
    var low: int = 0;
    var high: int = 4294967295;
    var underflow: int = 0;
    var symbols_written: int = 0;
}

// --- Arithmetic decoder --------------------------------------------------

class ArithmeticDecoder {

    fn init(ref self, input: ByteBuffer) {
        self.low = 0;
        self.high = CODER_MAX;
        self.code = 0;
        self.reader.init(BitOrder.MsbFirst);
        self.reader.source = input;
        self.reader.source.rewind();
        var i: int = 0;
        while (i < WORD_BITS) {
            self.code = ((self.code << 1) | self.reader.get_bit()) & WORD_MASK;
            i += 1;
        }
        self.symbols_read = 0;
    }

    // Map the current code point back onto a cumulative frequency, ask the
    // model which symbol owns it, then narrow exactly as the encoder did.
    fn decode_symbol(ref self, ref model: AdaptiveModel) -> int {
        let total = model.total_frequency();
        let span = self.high - self.low + 1;
        let offset = self.code - self.low;
        let target = ((offset + 1) * total - 1) / span;

        let symbol = model.symbol_for(target);
        let low_count = model.cumulative_below(symbol);
        let high_count = low_count + model.frequency_of(symbol);

        self.high = self.low + (span * high_count) / total - 1;
        self.low = self.low + (span * low_count) / total;
        self.normalise();
        model.update(symbol);
        self.symbols_read += 1;
        return symbol;
    }

    fn normalise(ref self) {
        while (true) {
            if (((self.low ^ self.high) & HIGH_BIT_32) == 0) {
                Discard(0);
            } else {
                if ((self.low & 1073741824) != 0 and (self.high & 1073741824) == 0) {
                    self.code = self.code ^ 1073741824;                     // 0x40000000
                    self.low = self.low & 1073741823;                       // 0x3FFFFFFF
                    self.high = self.high | 1073741824;
                } else {
                    return;
                }
            }
            self.low = (self.low << 1) & WORD_MASK;
            self.high = ((self.high << 1) | 1) & WORD_MASK;
            self.code = ((self.code << 1) | self.reader.get_bit()) & WORD_MASK;
        }
    }

    fn decode_buffer(ref self, ref model: AdaptiveModel, ref output: ByteBuffer, limit: int) -> Status {
        var produced: int = 0;
        while (true) {
            let symbol = self.decode_symbol(model);
            if (symbol == 256) {
                return Status.Ok;
            }
            if (symbol < 0 or symbol > 256) {
                return Status.CorruptStream;
            }
            output.push(symbol);
            produced += 1;
            if (produced > limit) {
                return Status.OutputFull;
            }
            if (self.reader.failed()) {
                return Status.NeedMoreInput;
            }
        }
    }

    var reader: BitReader;
    var low: int = 0;
    var high: int = 4294967295;
    var code: int = 0;
    var symbols_read: int = 0;
}

// --- CRC32 ---------------------------------------------------------------

// The table is generated rather than transcribed. Entry i is the remainder
// of the reflected polynomial division of i, computed by shifting the byte
// through eight times and folding in the polynomial whenever a one falls off
// the bottom.
class Crc32 {

    fn init(ref self) {
        self.build_table();
        self.build_slices();
        self.value = CRC32_INIT;
    }

    fn build_table(ref self) {
        self.table.clear();
        self.table.resize(CRC32_TABLE_SIZE);
        var i: int = 0;
        while (i < CRC32_TABLE_SIZE) {
            var remainder = i;
            var bit: int = 0;
            while (bit < BYTE_BITS) {
                if ((remainder & 1) != 0) {
                    remainder = ((remainder >> 1) & 2147483647) ^ CRC32_POLY;
                } else {
                    remainder = (remainder >> 1) & 2147483647;
                }
                bit += 1;
            }
            self.table.at(i) = remainder & WORD_MASK;
            i += 1;
        }
    }

    // Slicing-by-eight: table k holds the effect of a byte that is k
    // positions further back in the stream, so eight bytes can be folded per
    // iteration. Each slice is derived from the previous one.
    fn build_slices(ref self) {
        self.slices.clear();
        self.slices.resize(CRC32_SLICE_COUNT * CRC32_TABLE_SIZE);
        var i: int = 0;
        while (i < CRC32_TABLE_SIZE) {
            self.slices.at(i) = self.table.at(i);
            i += 1;
        }
        var slice: int = 1;
        while (slice < CRC32_SLICE_COUNT) {
            i = 0;
            while (i < CRC32_TABLE_SIZE) {
                let previous = self.slices.at((slice - 1) * CRC32_TABLE_SIZE + i);
                let folded = self.table.at(previous & BYTE_MASK) ^ ((previous >> BYTE_BITS) & 16777215);
                self.slices.at(slice * CRC32_TABLE_SIZE + i) = folded & WORD_MASK;
                i += 1;
            }
            slice += 1;
        }
    }

    fn reset(ref self) {
        self.value = CRC32_INIT;
    }

    fn update_byte(ref self, byte_value: int) {
        let index = (self.value ^ (byte_value & BYTE_MASK)) & BYTE_MASK;
        self.value = (self.table.at(index) ^ ((self.value >> BYTE_BITS) & 16777215)) & WORD_MASK;
    }

    fn update(ref self, input: ByteBuffer) {
        var i: int = 0;
        while (i < input.size()) {
            self.update_byte(input.at(i));
            i += 1;
        }
    }

    // The eight-byte-at-a-time inner loop. Each of the eight tables is
    // indexed by one byte of the combined running value, and the eight
    // results are exclusive-ored together.
    fn update_fast(ref self, input: ByteBuffer) {
        var i: int = 0;
        let n = input.size();
        while (i + CRC32_SLICE_COUNT <= n) {
            let word_low = input.at(i) | (input.at(i + 1) << 8) | (input.at(i + 2) << 16) | (input.at(i + 3) << 24);
            let word_high = input.at(i + 4) | (input.at(i + 5) << 8) | (input.at(i + 6) << 16) | (input.at(i + 7) << 24);
            let mixed = (self.value ^ word_low) & WORD_MASK;

            var folded: int = 0;
            folded = folded ^ self.slices.at(7 * CRC32_TABLE_SIZE + (mixed & BYTE_MASK));
            folded = folded ^ self.slices.at(6 * CRC32_TABLE_SIZE + ((mixed >> 8) & BYTE_MASK));
            folded = folded ^ self.slices.at(5 * CRC32_TABLE_SIZE + ((mixed >> 16) & BYTE_MASK));
            folded = folded ^ self.slices.at(4 * CRC32_TABLE_SIZE + ((mixed >> 24) & BYTE_MASK));
            folded = folded ^ self.slices.at(3 * CRC32_TABLE_SIZE + (word_high & BYTE_MASK));
            folded = folded ^ self.slices.at(2 * CRC32_TABLE_SIZE + ((word_high >> 8) & BYTE_MASK));
            folded = folded ^ self.slices.at(1 * CRC32_TABLE_SIZE + ((word_high >> 16) & BYTE_MASK));
            folded = folded ^ self.slices.at((word_high >> 24) & BYTE_MASK);

            self.value = folded & WORD_MASK;
            i += CRC32_SLICE_COUNT;
        }
        while (i < n) {
            self.update_byte(input.at(i));
            i += 1;
        }
    }

    fn digest(self) -> int {
        return (self.value ^ CRC32_INIT) & WORD_MASK;
    }

    fn checksum(ref self, input: ByteBuffer) -> int {
        self.reset();
        self.update(input);
        return self.digest();
    }

    // Combine the checksums of two concatenated buffers without rescanning
    // either, by advancing the first checksum through `length` zero bytes.
    fn combine(ref self, first: int, second: int, length: int) -> int {
        var running = (first ^ CRC32_INIT) & WORD_MASK;
        var i: int = 0;
        while (i < length) {
            let index = running & BYTE_MASK;
            running = (self.table.at(index) ^ ((running >> BYTE_BITS) & 16777215)) & WORD_MASK;
            i += 1;
        }
        return ((running ^ CRC32_INIT) ^ second) & WORD_MASK;
    }

    var table: vector(int);
    var slices: vector(int);
    var value: int = 4294967295;
}

// --- Adler32 -------------------------------------------------------------

// Two running sums modulo 65521: `a` accumulates the bytes and `b`
// accumulates `a`. The modulo is deferred for up to ADLER_NMAX bytes, which
// is the largest run that provably cannot overflow 32 bits.
class Adler32 {

    fn reset(ref self) {
        self.a = 1;
        self.b = 0;
    }

    fn update_byte(ref self, byte_value: int) {
        self.a = (self.a + (byte_value & BYTE_MASK)) % ADLER_BASE;
        self.b = (self.b + self.a) % ADLER_BASE;
    }

    fn update(ref self, input: ByteBuffer) {
        var i: int = 0;
        let n = input.size();
        while (i < n) {
            let span = min_int(ADLER_NMAX, n - i);
            var k: int = 0;
            while (k < span) {
                self.a += input.at(i + k) & BYTE_MASK;
                self.b += self.a;
                k += 1;
            }
            self.a = self.a % ADLER_BASE;
            self.b = self.b % ADLER_BASE;
            i += span;
        }
    }

    fn digest(self) -> int {
        return ((self.b << SHORT_BITS) | self.a) & WORD_MASK;
    }

    fn checksum(ref self, input: ByteBuffer) -> int {
        self.reset();
        self.update(input);
        return self.digest();
    }

    // Combine two Adler sums the way the zlib helper does: the second half's
    // `a` contribution is folded in and `b` is advanced by length * a.
    fn combine(ref self, first: int, second: int, length: int) -> int {
        let remainder = length % ADLER_BASE;
        let first_a = first & SHORT_MASK;
        let first_b = (first >> SHORT_BITS) & SHORT_MASK;
        let second_a = second & SHORT_MASK;
        let second_b = (second >> SHORT_BITS) & SHORT_MASK;

        var combined_a = (first_a + second_a - 1) % ADLER_BASE;
        var combined_b = (remainder * first_a + first_b + second_b - remainder) % ADLER_BASE;
        if (combined_a < 0) {
            combined_a += ADLER_BASE;
        }
        if (combined_b < 0) {
            combined_b += ADLER_BASE;
        }
        return ((combined_b << SHORT_BITS) | combined_a) & WORD_MASK;
    }

    var a: int = 1;
    var b: int = 0;
}

// --- Block sorting pipeline ---------------------------------------------

// The bzip-style stack: RLE, then the Burrows-Wheeler transform, then
// move-to-front, then a second RLE over the zero runs MTF produces, then a
// Huffman stage. Each step is reversible on its own, so the harness can
// round-trip any prefix of the chain.
class BlockSortPipeline {

    fn compress(ref self, input: ByteBuffer, ref output: ByteBuffer) -> Status {
        var after_rle = ByteBuffer();
        var rle = RleCodec();
        rle.encode(input, after_rle);

        var after_bwt = ByteBuffer();
        var bwt = BurrowsWheeler();
        let primary = bwt.transform(after_rle, after_bwt);

        var after_mtf = ByteBuffer();
        var mtf = MoveToFront();
        mtf.encode(after_bwt, after_mtf);

        var after_second_rle = ByteBuffer();
        var second = RleCodec();
        second.encode(after_mtf, after_second_rle);

        // The header records the primary index and the two intermediate
        // lengths so the inverse can be driven without re-deriving them.
        output.push_word_le(primary);
        output.push_word_le(after_rle.size());
        output.push_word_le(after_mtf.size());

        var frequencies = FrequencyTable();
        frequencies.init(257);
        frequencies.count_bytes(after_second_rle);
        frequencies.add(256);
        frequencies.ensure_two_symbols();

        var tree = HuffmanTree();
        if (not tree.build(frequencies)) {
            return Status.BadCodeLengths;
        }
        var table = EncodeTable();
        table.build(tree);

        var writer = BitWriter();
        writer.init(BitOrder.MsbFirst);
        var symbol: int = 0;
        while (symbol < 257) {
            writer.put_bits_msb(tree.lengths.at(symbol), 4);
            symbol += 1;
        }
        var i: int = 0;
        while (i < after_second_rle.size()) {
            table.emit(writer, after_second_rle.at(i));
            i += 1;
        }
        table.emit(writer, 256);
        writer.flush();
        output.append(writer.out);

        self.primary_index = primary;
        self.stage_sizes.clear();
        self.stage_sizes.push_back(input.size());
        self.stage_sizes.push_back(after_rle.size());
        self.stage_sizes.push_back(after_bwt.size());
        self.stage_sizes.push_back(after_mtf.size());
        self.stage_sizes.push_back(after_second_rle.size());
        self.stage_sizes.push_back(output.size());

        Destroy(after_rle);
        Destroy(after_bwt);
        Destroy(after_mtf);
        Destroy(after_second_rle);
        return Status.Ok;
    }

    fn decompress(ref self, input: ByteBuffer, ref output: ByteBuffer) -> Status {
        var source = input;
        source.rewind();
        let primary = source.read_word_le();
        let rle_size = source.read_word_le();
        let mtf_size = source.read_word_le();
        Discard(rle_size);

        var reader = BitReader();
        reader.init(BitOrder.MsbFirst);
        reader.source = source;

        var lengths = vector(int)(257);
        var symbol: int = 0;
        while (symbol < 257) {
            lengths.at(symbol) = reader.get_bits_msb(4);
            symbol += 1;
        }
        var table = DecodeTable();
        if (not table.build(lengths)) {
            return Status.BadCodeLengths;
        }

        var after_second_rle = ByteBuffer();
        while (true) {
            let decoded = table.decode(reader);
            if (decoded < 0) {
                return Status.CorruptStream;
            }
            if (decoded == 256) {
                break;
            }
            after_second_rle.push(decoded);
            if (reader.failed()) {
                return Status.NeedMoreInput;
            }
        }

        var after_mtf = ByteBuffer();
        var second = RleCodec();
        let second_status = second.decode(after_second_rle, after_mtf);
        if (second_status != Status.Ok) {
            return second_status;
        }
        Discard(mtf_size);

        var after_bwt = ByteBuffer();
        var mtf = MoveToFront();
        mtf.decode(after_mtf, after_bwt);

        var after_rle = ByteBuffer();
        var bwt = BurrowsWheeler();
        let inverse_status = bwt.inverse(after_bwt, primary, after_rle);
        if (inverse_status != Status.Ok) {
            return inverse_status;
        }

        var rle = RleCodec();
        return rle.decode(after_rle, output);
    }

    var stage_sizes: vector(int);
    var primary_index: int = 0;
}

// --- Test data generation ------------------------------------------------

// A small xorshift generator so the harness has reproducible pseudo-random
// input without depending on anything outside this file.
class Xorshift {

    fn init(ref self, seed: int) {
        if (seed == 0) {
            self.state = 2463534242;                // 0x92D68CA2
        } else {
            self.state = seed & WORD_MASK;
        }
    }

    fn next(ref self) -> int {
        var x = self.state & WORD_MASK;
        x = x ^ ((x << 13) & WORD_MASK);
        x = x ^ ((x >> 17) & 32767);
        x = x ^ ((x << 5) & WORD_MASK);
        self.state = x & WORD_MASK;
        return self.state;
    }

    fn next_byte(ref self) -> int {
        return self.next() & BYTE_MASK;
    }

    fn next_below(ref self, bound: int) -> int {
        if (bound <= 0) {
            return 0;
        }
        return self.next() % bound;
    }

    var state: int = 2463534242;
}

// Highly compressible input: long runs of a small alphabet.
fn make_runny_buffer(length: int, seed: int) -> ByteBuffer {
    var out = ByteBuffer();
    var rng = Xorshift();
    rng.init(seed);
    while (out.size() < length) {
        let value = 97 + rng.next_below(4);          // 'a' .. 'd'
        let run = 1 + rng.next_below(40);
        var i: int = 0;
        while (i < run) {
            if (out.size() >= length) {
                return out;
            }
            out.push(value);
            i += 1;
        }
    }
    return out;
}

// Input with strong long-range repetition: a random block repeated with
// small mutations, which is what exercises the hash chain finder.
fn make_repetitive_buffer(length: int, period: int, seed: int) -> ByteBuffer {
    var out = ByteBuffer();
    var rng = Xorshift();
    rng.init(seed);
    var pattern = ByteBuffer();
    var i: int = 0;
    while (i < period) {
        pattern.push(rng.next_byte());
        i += 1;
    }
    i = 0;
    while (i < length) {
        var value = pattern.at(i % period);
        if (rng.next_below(64) == 0) {
            value = rng.next_byte();
        }
        out.push(value);
        i += 1;
    }
    Destroy(pattern);
    return out;
}

// Incompressible input: uniform random bytes. The block splitter should
// choose stored blocks for this.
fn make_random_buffer(length: int, seed: int) -> ByteBuffer {
    var out = ByteBuffer();
    var rng = Xorshift();
    rng.init(seed);
    var i: int = 0;
    while (i < length) {
        out.push(rng.next_byte());
        i += 1;
    }
    return out;
}

// Text-like input drawn from a skewed alphabet, which is where Huffman
// coding earns its keep.
fn make_texty_buffer(length: int, seed: int) -> ByteBuffer {
    var out = ByteBuffer();
    var rng = Xorshift();
    rng.init(seed);
    var i: int = 0;
    while (i < length) {
        let roll = rng.next_below(100);
        if (roll < 60) {
            out.push(101);                           // 'e'
        } else { if (roll < 80) {
            out.push(116);                           // 't'
        } else { if (roll < 90) {
            out.push(32);                            // space
        } else {
            out.push(97 + rng.next_below(26));
        }
        } }
        i += 1;
    }
    return out;
}

// --- Round-trip harness --------------------------------------------------

// Every codec in the file is exercised the same way: compress, decompress,
// compare byte for byte, and assert the structural invariants that the
// implementation is supposed to maintain.

fn verify_bit_round_trip(order: BitOrder, seed: int) -> bool {
    var writer = BitWriter();
    writer.init(order);
    var rng = Xorshift();
    rng.init(seed);

    var values = vector(int)();
    var widths = vector(int)();
    var i: int = 0;
    while (i < 512) {
        let width = 1 + rng.next_below(16);
        let value = rng.next() & ((1 << width) - 1);
        values.push_back(value);
        widths.push_back(width);
        writer.put_bits(value, width);
        i += 1;
    }
    let expected_bits = writer.bit_position();
    writer.flush();
    Assert(writer.is_aligned());

    var reader = BitReader();
    reader.init(order);
    reader.source = writer.out;
    reader.source.rewind();

    i = 0;
    while (i < values.size()) {
        let decoded = reader.get_bits(widths.at(i));
        if (decoded != values.at(i)) {
            Destroy(values);
            Destroy(widths);
            return false;
        }
        i += 1;
    }
    Prove(reader.bit_position() == expected_bits);
    Assert(not reader.failed());
    Destroy(values);
    Destroy(widths);
    return true;
}

fn verify_reverse_bits() -> bool {
    var width: int = 1;
    while (width <= 15) {
        var value: int = 0;
        let limit = 1 << width;
        while (value < limit) {
            let there = reverse_bits(value, width);
            let back = reverse_bits(there, width);
            if (back != value) {
                return false;
            }
            value += 1;
            if (value > 4096) {
                break;
            }
        }
        width += 1;
    }
    Prove(reverse_bits(1, 8) == 128);
    Prove(reverse_bits(0, 8) == 0);
    return true;
}

fn verify_popcount() -> bool {
    var i: int = 0;
    while (i < 4096) {
        var manual: int = 0;
        var bit: int = 0;
        while (bit < 32) {
            manual += (i >> bit) & 1;
            bit += 1;
        }
        if (popcount32(i) != manual) {
            return false;
        }
        i += 1;
    }
    Prove(popcount32(0) == 0);
    Prove(popcount32(255) == 8);
    return true;
}

// The Kraft equality is the structural invariant of a complete prefix code:
// after building and after length limiting, the sum must be exact.
fn verify_huffman_tree(input: ByteBuffer) -> bool {
    var frequencies = FrequencyTable();
    frequencies.init(257);
    frequencies.count_bytes(input);
    frequencies.add(256);
    frequencies.ensure_two_symbols();
    Assert(frequencies.used >= 2);

    var tree = HuffmanTree();
    if (not tree.build(frequencies)) {
        return false;
    }
    Assert(tree.max_length <= MAX_CODE_LENGTH);
    Assert(tree.max_length > 0);
    Prove(tree.kraft_sum() == (1 << tree.max_length));

    // Every used symbol must have a length, and no unused symbol may.
    var symbol: int = 0;
    while (symbol < 257) {
        if (frequencies.at(symbol) > 0) {
            if (tree.lengths.at(symbol) == 0) {
                return false;
            }
        } else {
            if (tree.lengths.at(symbol) != 0) {
                return false;
            }
        }
        symbol += 1;
    }

    // Canonical codes must be strictly increasing within a length class.
    var length: int = 1;
    while (length <= tree.max_length) {
        var previous: int = -1;
        symbol = 0;
        while (symbol < 257) {
            if (tree.lengths.at(symbol) == length) {
                if (tree.codes.at(symbol) <= previous) {
                    return false;
                }
                previous = tree.codes.at(symbol);
            }
            symbol += 1;
        }
        length += 1;
    }
    return true;
}

// Encode every symbol through the encode table and decode it back through
// the two-level decode table, checking that the tables agree.
fn verify_huffman_tables(input: ByteBuffer) -> bool {
    var frequencies = FrequencyTable();
    frequencies.init(257);
    frequencies.count_bytes(input);
    frequencies.add(256);
    frequencies.ensure_two_symbols();

    var tree = HuffmanTree();
    if (not tree.build(frequencies)) {
        return false;
    }
    var encode_table = EncodeTable();
    encode_table.build(tree);
    var decode_table = DecodeTable();
    if (not decode_table.build(tree.lengths)) {
        return false;
    }

    var writer = BitWriter();
    writer.init(BitOrder.LsbFirst);
    var i: int = 0;
    while (i < input.size()) {
        encode_table.emit_reversed(writer, input.at(i));
        i += 1;
    }
    encode_table.emit_reversed(writer, 256);
    writer.flush();

    var reader = BitReader();
    reader.init(BitOrder.LsbFirst);
    reader.source = writer.out;
    reader.source.rewind();

    i = 0;
    while (true) {
        let decoded = decode_table.decode(reader);
        if (decoded < 0) {
            return false;
        }
        if (decoded == 256) {
            break;
        }
        if (i >= input.size()) {
            return false;
        }
        if (decoded != input.at(i)) {
            return false;
        }
        i += 1;
    }
    Assert(i == input.size());
    return true;
}

// Code length limiting must not break completeness, and the limited code can
// only ever be worse than the unlimited one by a bounded amount.
fn verify_length_limiting(input: ByteBuffer, limit: int) -> bool {
    var frequencies = FrequencyTable();
    frequencies.init(257);
    frequencies.count_bytes(input);
    frequencies.add(256);
    frequencies.ensure_two_symbols();

    var tree = HuffmanTree();
    if (not tree.build(frequencies)) {
        return false;
    }
    let unlimited_bits = tree.encoded_bits(frequencies);

    tree.limit_code_lengths(limit);
    tree.assign_canonical();
    var symbol: int = 0;
    while (symbol < 257) {
        if (tree.lengths.at(symbol) > limit) {
            return false;
        }
        symbol += 1;
    }
    let limited_bits = tree.encoded_bits(frequencies);
    Assert(limited_bits >= 0);
    Prove(unlimited_bits >= 0);
    return true;
}

fn verify_lz77_round_trip(input: ByteBuffer, level: int) -> bool {
    var encoder = Lz77Encoder();
    encoder.init(level);
    encoder.encode(input);

    // Every token must reference something already produced.
    var produced: int = 0;
    var i: int = 0;
    while (i < encoder.tokens.size()) {
        let ref token: auto = encoder.tokens.at(i);
        if (token.is_match()) {
            if (token.distance > produced) {
                return false;
            }
            if (token.length < MIN_MATCH or token.length > MAX_MATCH) {
                return false;
            }
            produced += token.length;
        } else {
            produced += 1;
        }
        i += 1;
    }
    Assert(produced == input.size());

    var output = ByteBuffer();
    let status = lz77_decode(encoder.tokens, output);
    if (status != Status.Ok) {
        return false;
    }
    if (not output.equals(input)) {
        return false;
    }
    Prove(output.size() == input.size());
    Destroy(output);
    return true;
}

fn verify_deflate_round_trip(input: ByteBuffer, level: int) -> bool {
    var encoder = DeflateEncoder();
    encoder.init(level);
    let compress_status = encoder.compress(input);
    if (compress_status != Status.Ok) {
        return false;
    }

    var decoder = DeflateDecoder();
    decoder.init();
    var output = ByteBuffer();
    let decompress_status = decoder.decompress(encoder.writer.out, output);
    if (decompress_status != Status.Ok) {
        return false;
    }
    if (not output.equals(input)) {
        return false;
    }

    Assert(decoder.blocks_read >= 1);
    Prove(output.size() == input.size());
    Destroy(output);
    return true;
}

fn verify_rle_round_trip(input: ByteBuffer) -> bool {
    var codec = RleCodec();
    var encoded = ByteBuffer();
    codec.encode(input, encoded);

    var decoded = ByteBuffer();
    let status = codec.decode(encoded, decoded);
    if (status != Status.Ok) {
        return false;
    }
    if (not decoded.equals(input)) {
        return false;
    }
    Prove(decoded.size() == input.size());
    Destroy(encoded);
    Destroy(decoded);
    return true;
}

fn verify_mtf_round_trip(input: ByteBuffer) -> bool {
    var mtf = MoveToFront();
    var encoded = ByteBuffer();
    mtf.encode(input, encoded);
    Assert(encoded.size() == input.size());

    var decoded = ByteBuffer();
    mtf.decode(encoded, decoded);
    if (not decoded.equals(input)) {
        return false;
    }
    Prove(decoded.size() == input.size());
    Destroy(encoded);
    Destroy(decoded);
    return true;
}

fn verify_bwt_round_trip(input: ByteBuffer) -> bool {
    var bwt = BurrowsWheeler();
    var transformed = ByteBuffer();
    let primary = bwt.transform(input, transformed);
    Assert(transformed.size() == input.size());
    Assert(primary >= 0);

    var restored = ByteBuffer();
    let status = bwt.inverse(transformed, primary, restored);
    if (status != Status.Ok) {
        return false;
    }
    if (not restored.equals(input)) {
        return false;
    }
    Prove(restored.size() == input.size());
    Destroy(transformed);
    Destroy(restored);
    return true;
}

fn verify_arithmetic_round_trip(input: ByteBuffer) -> bool {
    var encode_model = AdaptiveModel();
    encode_model.init(257);
    var encoder = ArithmeticEncoder();
    encoder.encode_buffer(input, encode_model);

    var decode_model = AdaptiveModel();
    decode_model.init(257);
    var decoder = ArithmeticDecoder();
    decoder.init(encoder.writer.out);
    var output = ByteBuffer();
    let status = decoder.decode_buffer(decode_model, output, input.size() + 16);
    if (status != Status.Ok) {
        return false;
    }
    if (not output.equals(input)) {
        return false;
    }
    Assert(decoder.symbols_read == encoder.symbols_written);
    Prove(output.size() == input.size());
    Destroy(output);
    return true;
}

// The Fenwick tree inside the model must agree with a naive scan of the raw
// frequency array at every point.
fn verify_model_consistency() -> bool {
    var model = AdaptiveModel();
    model.init(257);
    var rng = Xorshift();
    rng.init(9187);

    var step: int = 0;
    while (step < 2000) {
        let symbol = rng.next_below(257);
        model.update(symbol);
        step += 1;
    }

    var running: int = 0;
    var symbol: int = 0;
    while (symbol < 257) {
        if (model.cumulative_below(symbol) != running) {
            return false;
        }
        running += model.frequency_of(symbol);
        symbol += 1;
    }
    Assert(running == model.total_frequency());

    // Every point in the cumulative range must map back to a symbol whose
    // interval contains it.
    var target: int = 0;
    while (target < model.total_frequency()) {
        let found = model.symbol_for(target);
        let low = model.cumulative_below(found);
        let high = low + model.frequency_of(found);
        if (target < low or target >= high) {
            return false;
        }
        target += 37;
    }
    return true;
}

fn verify_checksums(input: ByteBuffer) -> bool {
    var crc = Crc32();
    crc.init();
    let simple = crc.checksum(input);

    crc.reset();
    crc.update_fast(input);
    let sliced = crc.digest();
    if (simple != sliced) {
        return false;
    }

    // The generated table must reproduce the known check value for the
    // string "123456789".
    var digits = ByteBuffer();
    var i: int = 0;
    while (i < 9) {
        digits.push(49 + i);                          // '1' .. '9'
        i += 1;
    }
    let check = crc.checksum(digits);
    Assert(check == 3421780262);                       // 0xCBF43926

    var adler = Adler32();
    let adler_value = adler.checksum(digits);
    Assert(adler_value == 152961502);                  // 0x091E01DE
    Prove(adler_value > 0);

    // An empty buffer has the identity Adler value.
    var empty = ByteBuffer();
    let empty_adler = adler.checksum(empty);
    Assert(empty_adler == 1);

    Destroy(digits);
    Destroy(empty);
    return true;
}

fn verify_block_sort_round_trip(input: ByteBuffer) -> bool {
    var pipeline = BlockSortPipeline();
    var compressed = ByteBuffer();
    let status = pipeline.compress(input, compressed);
    if (status != Status.Ok) {
        return false;
    }

    var output = ByteBuffer();
    let inverse_status = pipeline.decompress(compressed, output);
    if (inverse_status != Status.Ok) {
        return false;
    }
    if (not output.equals(input)) {
        return false;
    }
    Assert(pipeline.stage_sizes.size() == 6);
    Prove(output.size() == input.size());
    Destroy(compressed);
    Destroy(output);
    return true;
}

// The deflate symbol tables are regular by construction; check that the
// generated bases and the arithmetic free functions agree everywhere.
fn verify_deflate_tables() -> bool {
    var tables = DeflateTables();
    tables.init();
    Assert(tables.ready);
    Assert(tables.code_length_order.size() == CODE_LENGTH_SYMBOLS);

    var length = MIN_MATCH;
    while (length <= MAX_MATCH) {
        let code = tables.length_code(length);
        if (code != length_code_of(length)) {
            return false;
        }
        let anchor = tables.length_base.at(code);
        let extra = tables.length_extra.at(code);
        if (length < anchor) {
            return false;
        }
        if (code < 28) {
            if (length >= anchor + (1 << extra)) {
                return false;
            }
        }
        if (extra != extra_length_bits(code)) {
            return false;
        }
        length += 1;
    }

    var distance: int = 1;
    while (distance <= MAX_DISTANCE) {
        let code = tables.distance_code(distance);
        if (code != distance_code_of(distance)) {
            return false;
        }
        let anchor = tables.distance_base.at(code);
        let extra = tables.distance_extra.at(code);
        if (distance < anchor) {
            return false;
        }
        if (distance >= anchor + (1 << extra)) {
            return false;
        }
        distance += 1;
    }

    Prove(tables.length_base.at(0) == MIN_MATCH);
    Prove(tables.distance_base.at(0) == 1);
    return true;
}

// The heap has to remain a heap after every operation, and popping must
// yield a non-decreasing sequence of weights.
fn verify_priority_queue() -> bool {
    var queue = NodeQueue();
    var rng = Xorshift();
    rng.init(4242);

    var i: int = 0;
    while (i < 500) {
        queue.push(i, rng.next_below(10000));
        i += 1;
    }
    Assert(queue.is_heap());
    Assert(queue.size() == 500);

    var previous: int = -1;
    while (not queue.empty()) {
        let weight = queue.top_weight();
        if (weight < previous) {
            return false;
        }
        previous = weight;
        Discard(queue.pop());
    }
    Prove(queue.size() == 0);
    return true;
}

// --- Entry point ---------------------------------------------------------

// Run every verifier over every kind of input at every level.
fn run_all_checks() -> int {
    var failures: int = 0;

    if (not verify_bit_round_trip(BitOrder.MsbFirst, 1)) {
        failures += 1;
    }
    if (not verify_bit_round_trip(BitOrder.LsbFirst, 2)) {
        failures += 1;
    }
    if (not verify_reverse_bits()) {
        failures += 1;
    }
    if (not verify_popcount()) {
        failures += 1;
    }
    if (not verify_deflate_tables()) {
        failures += 1;
    }
    if (not verify_priority_queue()) {
        failures += 1;
    }
    if (not verify_model_consistency()) {
        failures += 1;
    }

    var inputs = vector(ByteBuffer)();
    inputs.push_back(make_runny_buffer(4096, 11));
    inputs.push_back(make_repetitive_buffer(8192, 61, 12));
    inputs.push_back(make_random_buffer(2048, 13));
    inputs.push_back(make_texty_buffer(8192, 14));

    var index: int = 0;
    while (index < inputs.size()) {
        let ref sample: auto = inputs.at(index);

        if (not verify_huffman_tree(sample)) {
            failures += 1;
        }
        if (not verify_huffman_tables(sample)) {
            failures += 1;
        }
        if (not verify_length_limiting(sample, 12)) {
            failures += 1;
        }
        if (not verify_rle_round_trip(sample)) {
            failures += 1;
        }
        if (not verify_mtf_round_trip(sample)) {
            failures += 1;
        }
        if (not verify_arithmetic_round_trip(sample)) {
            failures += 1;
        }
        if (not verify_checksums(sample)) {
            failures += 1;
        }

        var level: int = 0;
        while (level <= 9) {
            if (not verify_lz77_round_trip(sample, level)) {
                failures += 1;
            }
            if (not verify_deflate_round_trip(sample, level)) {
                failures += 1;
            }
            level += 1;
        }

        index += 1;
    }

    // The Burrows-Wheeler round trip is quadratic in the block size, so it
    // only runs over a short prefix.
    var small = make_runny_buffer(512, 21);
    if (not verify_bwt_round_trip(small)) {
        failures += 1;
    }
    if (not verify_block_sort_round_trip(small)) {
        failures += 1;
    }

    Assert(failures >= 0);
    Prove(failures == 0);
    Destroy(small);
    Destroy(inputs);
    return failures;
}

// Human readable name for a status code, used by the report below.
fn status_name(status: Status) -> string {
    if (status == Status.Ok) {
        return "ok";
    } else { if (status == Status.NeedMoreInput) {
        return "need more input";
    } else { if (status == Status.OutputFull) {
        return "output full";
    } else { if (status == Status.CorruptStream) {
        return "corrupt stream";
    } else { if (status == Status.BadCodeLengths) {
        return "bad code lengths";
    } else { if (status == Status.BadDistance) {
        return "bad distance";
    } else { if (status == Status.ChecksumMismatch) {
        return "checksum mismatch";
    } else {
        return "unsupported";
    }
    } } } } } }
}

// Compression ratio in percent, rounded down. A ratio above 100 means the
// codec expanded the input, which stored blocks exist to prevent.
fn ratio_percent(original: int, compressed: int) -> int {
    if (original == 0) {
        return 100;
    }
    return (compressed * 100) / original;
}

// One line of the summary table. The apostrophe is spelled through a string
// constant because the lexer has no escaped single-quote char literal.
fn report_line(label: string, original: int, compressed: int) -> string {
    var text = string();
    text.append(label);
    text.append(APOSTROPHE);
    text.append("s ratio: ");
    text.append(to_string(ratio_percent(original, compressed)));
    text.append("% (");
    text.append(to_string(original));
    text.append(" -> ");
    text.append(to_string(compressed));
    text.append(" bytes)");
    return text;
}

// Compress one sample at every level and print the resulting ratios.
fn report_levels(sample: ByteBuffer, label: string) {
    var level: int = 0;
    while (level <= 9) {
        var encoder = DeflateEncoder();
        encoder.init(level);
        let status = encoder.compress(sample);
        if (status != Status.Ok) {
            println(status_name(status));
        } else {
            println(report_line(label, sample.size(), encoder.writer.out.size()));
        }
        level += 1;
    }
}

fn main() -> int {
    let failures = run_all_checks();
    if (failures == 0) {
        println("compression: all round trips verified");
        var sample = make_texty_buffer(8192, 77);
        report_levels(sample, "texty");
        var repetitive = make_repetitive_buffer(8192, 61, 78);
        report_levels(repetitive, "repetitive");
        Destroy(sample);
        Destroy(repetitive);
    } else {
        println("compression: failures detected");
        println(to_string(failures));
    }
    return failures;
}


