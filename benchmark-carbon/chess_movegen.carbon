// A bitboard chess move generator.
//
// The board is represented twice over: a dense `squares` array that maps each
// of the 64 squares to a piece code, and a set of twelve bitboards (one per
// colour/kind pair) plus two colour unions and one occupancy union. Every
// query that a search needs -- "which knights of mine can reach e5", "is my
// king attacked", "which of my pieces are pinned" -- is then a handful of
// shifts, ands and population counts rather than a loop over squares.
//
// A bitboard is a 64 bit integer whose bit `s` is set when square `s` is
// occupied. Square numbering is little-endian rank-file: a1 is 0, h1 is 7,
// a8 is 56, h8 is 63, so `square = rank * 8 + file`. Shifting a bitboard
// north by one rank is `bb << 8`, south is `bb >> 8`; the east/west shifts
// additionally mask off the file that would wrap around the edge.
//
// Because the lexer only accepts plain decimal integer literals, every mask
// wider than a couple of digits is *built* at initialisation time by a loop
// that ors in `1 << square`, rather than being spelled out as a giant decimal
// constant. The handful of literals that survive carry a `// 0x..` comment.
//
// Nothing here allocates during move generation: the caller passes a
// MoveList that owns its storage and generation appends into it.
namespace chess;

// --- Geometry constants --------------------------------------------------

let BOARD_FILES: int = 8;
let BOARD_RANKS: int = 8;
let SQUARE_COUNT: int = 64;
let COLOR_COUNT: int = 2;
let PIECE_KIND_COUNT: int = 6;
let PIECE_CODE_COUNT: int = 12;

// Sentinel used wherever a square index is optional (en passant target,
// the square of a missing king, the "no pin" ray origin).
let NO_SQUARE: int = -1;

// Bit widths of the packed square index. A square needs six bits, which is
// what the move encoding below leans on heavily.
let SQUARE_BITS: int = 6;
let SQUARE_MASK: int = 63;             // 0x3F
let FILE_MASK_BITS: int = 7;           // 0x07
let RANK_SHIFT: int = 3;
let BYTE_MASK: int = 255;              // 0xFF
let WORD_MASK: int = 65535;            // 0xFFFF

// A bitboard with every bit set. Two's complement gives us all ones for the
// cost of a single character, which beats a twenty digit decimal literal.
let FULL_BOARD: int = -1;
let EMPTY_BOARD: int = 0;

// Directions, expressed as signed square deltas. The east/west members only
// behave when combined with the wrap masks below.
let DIR_NORTH: int = 8;
let DIR_SOUTH: int = -8;
let DIR_EAST: int = 1;
let DIR_WEST: int = -1;
let DIR_NORTH_EAST: int = 9;
let DIR_NORTH_WEST: int = 7;
let DIR_SOUTH_EAST: int = -7;
let DIR_SOUTH_WEST: int = -9;

// Ray indices into the RAY_ATTACKS table. The order matches the direction
// constants above so that a loop over 0..8 walks the compass clockwise from
// north.
let RAY_NORTH: int = 0;
let RAY_NORTH_EAST: int = 1;
let RAY_EAST: int = 2;
let RAY_SOUTH_EAST: int = 3;
let RAY_SOUTH: int = 4;
let RAY_SOUTH_WEST: int = 5;
let RAY_WEST: int = 6;
let RAY_NORTH_WEST: int = 7;
let RAY_COUNT: int = 8;

// The four rook rays and the four bishop rays, split so the sliding attack
// helpers can loop over exactly the ones they need.
let ROOK_RAY_COUNT: int = 4;
let BISHOP_RAY_COUNT: int = 4;

// Named squares that the castling code needs by hand.
let SQ_A1: int = 0;
let SQ_B1: int = 1;
let SQ_C1: int = 2;
let SQ_D1: int = 3;
let SQ_E1: int = 4;
let SQ_F1: int = 5;
let SQ_G1: int = 6;
let SQ_H1: int = 7;
let SQ_A8: int = 56;
let SQ_B8: int = 57;
let SQ_C8: int = 58;
let SQ_D8: int = 59;
let SQ_E8: int = 60;
let SQ_F8: int = 61;
let SQ_G8: int = 62;
let SQ_H8: int = 63;

// Castling right bits, one per castle.
let CASTLE_WHITE_KING: int = 1;        // 0x1
let CASTLE_WHITE_QUEEN: int = 2;       // 0x2
let CASTLE_BLACK_KING: int = 4;        // 0x4
let CASTLE_BLACK_QUEEN: int = 8;       // 0x8
let CASTLE_WHITE_ANY: int = 3;         // 0x3
let CASTLE_BLACK_ANY: int = 12;        // 0xC
let CASTLE_ALL: int = 15;              // 0xF
let CASTLE_NONE: int = 0;

// Move list sizing. 218 is the largest number of legal moves any legal
// position is known to admit; the extra headroom absorbs pseudo-legal moves
// that the legality filter later drops.
let MAX_MOVES: int = 256;
let MAX_PLY: int = 512;
let MAX_GAME_MOVES: int = 1024;

// Halfmove clock at which the fifty move rule fires (in plies).
let FIFTY_MOVE_PLIES: int = 100;

// ASCII code points, spelled in decimal because character arithmetic is
// what FEN parsing is made of.
let CHAR_ZERO: int = 48;               // '0'
let CHAR_NINE: int = 57;               // '9'
let CHAR_LOWER_A: int = 97;            // 'a'
let CHAR_LOWER_H: int = 104;           // 'h'
let CHAR_UPPER_A: int = 65;            // 'A'
let CHAR_SPACE: int = 32;              // ' '
let CHAR_SLASH: int = 47;              // '/'
let CHAR_DASH: int = 45;               // '-'
let CASE_BIT: int = 32;                // 0x20, the ASCII case flag

// --- Instrumentation ------------------------------------------------------

// Counters the perft driver prints. They are globals rather than fields so
// that the generator itself stays free of bookkeeping.
var generated_move_count: int = 0;
var legality_test_count: int = 0;
var attack_query_count: int = 0;
var make_move_count: int = 0;

// --- Enumerations ---------------------------------------------------------

// Side to move. The numeric values matter: they index the colour-indexed
// tables directly, and `1 - color` flips a side.
choice Color {
    White,
    Black,
    ColorNone,
}

// Piece kinds, ordered by convention from pawn to king. The value is the
// index into the per-kind bitboard array and into the material table.
choice PieceKind {
    Pawn,
    Knight,
    Bishop,
    Rook,
    Queen,
    King,
    KindNone,
}

// A piece code packs colour and kind: white pieces are 0..5, black pieces
// are 6..11, and 12 means "empty square".
choice PieceCode {
    WhitePawn,
    WhiteKnight,
    WhiteBishop,
    WhiteRook,
    WhiteQueen,
    WhiteKing,
    BlackPawn,
    BlackKnight,
    BlackBishop,
    BlackRook,
    BlackQueen,
    BlackKing,
    NoPiece,
}

// Move classification, stored in the four flag bits of an encoded move.
choice MoveKind {
    Quiet,
    DoublePush,
    KingCastle,
    QueenCastle,
    Capture,
    EnPassant,
    PromoKnight,
    PromoBishop,
    PromoRook,
    PromoQueen,
    PromoCaptureKnight,
    PromoCaptureBishop,
    PromoCaptureRook,
    PromoCaptureQueen,
}

// What a generation pass should emit.
choice GenKind {
    GenAll,
    GenCaptures,
    GenQuiets,
    GenEvasions,
}

// Terminal states the perft driver and the SAN formatter care about.
choice GameResult {
    Ongoing,
    WhiteWins,
    BlackWins,
    DrawStalemate,
    DrawFiftyMove,
    DrawInsufficient,
    DrawRepetition,
}

// --- Square, file and rank helpers ---------------------------------------

// Squares are little-endian rank-file: square = rank * 8 + file, so the file
// lives in the low three bits and the rank in the next three.
fn make_square(file: int, rank: int) -> int {
    return (rank << RANK_SHIFT) | (file & FILE_MASK_BITS);
}

fn file_of(square: int) -> int {
    return square & FILE_MASK_BITS;
}

fn rank_of(square: int) -> int {
    return (square >> RANK_SHIFT) & FILE_MASK_BITS;
}

// True when the index names a real square. Sliding generation relies on this
// only as a cheap first filter; the wrap check below does the real work.
fn square_valid(square: int) -> bool {
    return square >= 0 and square < SQUARE_COUNT;
}

// A one-step move from `from` to `to` is legal geometry only when it does not
// jump the board edge, i.e. when the file distance stays within `max_file`.
fn step_valid(from: int, to: int, max_file: int) -> bool {
    if (to < 0 or to >= SQUARE_COUNT) {
        return false;
    }
    let df = file_of(to) - file_of(from);
    var spread = df;
    if (spread < 0) {
        spread = -spread;
    }
    return spread <= max_file;
}

// Chebyshev distance, the metric a king actually walks in.
fn square_distance(a: int, b: int) -> int {
    var df = file_of(a) - file_of(b);
    var dr = rank_of(a) - rank_of(b);
    if (df < 0) {
        df = -df;
    }
    if (dr < 0) {
        dr = -dr;
    }
    if (df > dr) {
        return df;
    }
    return dr;
}

// Manhattan distance, used by the endgame king-proximity term.
fn manhattan_distance(a: int, b: int) -> int {
    var df = file_of(a) - file_of(b);
    var dr = rank_of(a) - rank_of(b);
    if (df < 0) {
        df = -df;
    }
    if (dr < 0) {
        dr = -dr;
    }
    return df + dr;
}

// Mirror a square across the horizontal axis: a1 becomes a8. The piece-square
// tables are stored from white's point of view and flipped for black with
// exactly this, which is why it is a single xor.
fn flip_rank(square: int) -> int {
    return square ^ 56;                   // 0x38, the rank bits
}

// Mirror across the vertical axis: a1 becomes h1.
fn flip_file(square: int) -> int {
    return square ^ 7;                    // 0x07, the file bits
}

// Rotate 180 degrees.
fn flip_square(square: int) -> int {
    return square ^ 63;                   // 0x3F, both halves at once
}

// The colour of a square, in the light/dark sense. Used by the opposite
// coloured bishops draw heuristic.
fn square_is_dark(square: int) -> bool {
    return ((file_of(square) + rank_of(square)) & 1) == 0;
}

// The rank a pawn of `color` starts on, and the rank it promotes on.
fn pawn_start_rank(color: Color) -> int {
    if (color == Color.White) {
        return 1;
    }
    return 6;
}

fn pawn_promotion_rank(color: Color) -> int {
    if (color == Color.White) {
        return 7;
    }
    return 0;
}

// The rank a double push lands on, which is also the only rank on which an
// en passant target square may ever appear.
fn pawn_double_rank(color: Color) -> int {
    if (color == Color.White) {
        return 3;
    }
    return 4;
}

// The forward direction for a pawn of `color`, as a square delta.
fn pawn_push_delta(color: Color) -> int {
    if (color == Color.White) {
        return DIR_NORTH;
    }
    return DIR_SOUTH;
}

// The rank from white's point of view, so that evaluation code can talk
// about "the seventh rank" without branching on colour.
fn relative_rank(color: Color, square: int) -> int {
    if (color == Color.White) {
        return rank_of(square);
    }
    return 7 - rank_of(square);
}

fn relative_square(color: Color, square: int) -> int {
    if (color == Color.White) {
        return square;
    }
    return flip_rank(square);
}

// --- Colour and piece code helpers ---------------------------------------

fn color_index(color: Color) -> int {
    if (color == Color.Black) {
        return 1;
    }
    return 0;
}

fn color_from_index(index: int) -> Color {
    if (index == 1) {
        return Color.Black;
    }
    return Color.White;
}

fn opposite_color(color: Color) -> Color {
    if (color == Color.White) {
        return Color.Black;
    }
    return Color.White;
}

fn kind_index(kind: PieceKind) -> int {
    if (kind == PieceKind.Pawn) {
        return 0;
    } else { if (kind == PieceKind.Knight) {
        return 1;
    } else { if (kind == PieceKind.Bishop) {
        return 2;
    } else { if (kind == PieceKind.Rook) {
        return 3;
    } else { if (kind == PieceKind.Queen) {
        return 4;
    } else { if (kind == PieceKind.King) {
        return 5;
    } else {
        return 6;
    } } } } } }
}

fn kind_from_index(index: int) -> PieceKind {
    if (index == 0) {
        return PieceKind.Pawn;
    } else { if (index == 1) {
        return PieceKind.Knight;
    } else { if (index == 2) {
        return PieceKind.Bishop;
    } else { if (index == 3) {
        return PieceKind.Rook;
    } else { if (index == 4) {
        return PieceKind.Queen;
    } else { if (index == 5) {
        return PieceKind.King;
    } else {
        return PieceKind.KindNone;
    } } } } } }
}

// Pack colour and kind into the 0..11 piece code used by `squares`.
fn make_piece(color: Color, kind: PieceKind) -> int {
    return color_index(color) * PIECE_KIND_COUNT + kind_index(kind);
}

fn piece_color(piece: int) -> Color {
    if (piece >= PIECE_CODE_COUNT or piece < 0) {
        return Color.ColorNone;
    }
    if (piece >= PIECE_KIND_COUNT) {
        return Color.Black;
    }
    return Color.White;
}

fn piece_kind(piece: int) -> PieceKind {
    if (piece >= PIECE_CODE_COUNT or piece < 0) {
        return PieceKind.KindNone;
    }
    return kind_from_index(piece % PIECE_KIND_COUNT);
}

fn piece_is_empty(piece: int) -> bool {
    return piece >= PIECE_CODE_COUNT or piece < 0;
}

// Slider classification, used by the pin detector to pick a ray family.
fn kind_is_slider(kind: PieceKind) -> bool {
    return kind == PieceKind.Bishop or kind == PieceKind.Rook or kind == PieceKind.Queen;
}

fn kind_is_diagonal_slider(kind: PieceKind) -> bool {
    return kind == PieceKind.Bishop or kind == PieceKind.Queen;
}

fn kind_is_straight_slider(kind: PieceKind) -> bool {
    return kind == PieceKind.Rook or kind == PieceKind.Queen;
}

// --- Raw bitboard primitives ---------------------------------------------

// The SWAR population count constants. These are the only wide literals in
// the file; everything else is assembled by a loop.
let SWAR_K1: int = 6148914691236517205;   // 0x5555555555555555
let SWAR_K2: int = 3689348814741910323;   // 0x3333333333333333
let SWAR_K4: int = 1085102592571150095;   // 0x0F0F0F0F0F0F0F0F
let SWAR_KF: int = 72340172838076673;     // 0x0101010101010101

// The de Bruijn sequence used by the forward bit scan, plus the shift that
// lifts the product's high six bits into place.
let DEBRUIJN64: int = 285870213051353481; // 0x03F79D71B4CB0A89
let DEBRUIJN_SHIFT: int = 58;

// Filled by init_debruijn_table(): index[(isolated * DEBRUIJN64) >> 58] is
// the square number of the isolated bit.
var DEBRUIJN_INDEX: vector(int) = vector(int)(SQUARE_COUNT);

// A single set bit at `square`.
fn square_bit(square: int) -> int {
    return 1 << square;
}

// Population count via the standard five-step SWAR fold. Each step halves
// the number of independent counters while doubling their width.
fn popcount(bb: int) -> int {
    var x = bb;
    x = x - ((x >> 1) & SWAR_K1);
    x = (x & SWAR_K2) + ((x >> 2) & SWAR_K2);
    x = (x + (x >> 4)) & SWAR_K4;
    x = x * SWAR_KF;
    return (x >> 56) & BYTE_MASK;
}

// A branch-free "more than one bit set" test: clearing the lowest set bit
// leaves zero exactly when the board was a singleton.
fn has_single_bit(bb: int) -> bool {
    return bb != 0 and (bb & (bb - 1)) == 0;
}

fn is_empty_board(bb: int) -> bool {
    return bb == 0;
}

// Isolate the least significant set bit. Two's complement makes `-bb` the
// complement plus one, so the and keeps exactly the lowest one bit.
fn lowest_bit(bb: int) -> int {
    return bb & -bb;
}

// Clear the least significant set bit.
fn clear_lowest_bit(bb: int) -> int {
    return bb & (bb - 1);
}

// Index of the least significant set bit, by de Bruijn multiplication.
fn bit_scan_forward(bb: int) -> int {
    Assert(bb != 0);
    let isolated = bb & -bb;
    let folded = (isolated * DEBRUIJN64) >> DEBRUIJN_SHIFT;
    return DEBRUIJN_INDEX.at(folded & SQUARE_MASK);
}

// Portable fallback used while the de Bruijn table is still being built.
fn bit_scan_forward_slow(bb: int) -> int {
    var x = bb;
    var index: int = 0;
    while (x != 0) {
        if ((x & 1) != 0) {
            return index;
        }
        x >>= 1;
        index += 1;
    }
    return NO_SQUARE;
}

// Index of the most significant set bit, found by smearing every bit to the
// right of the highest one and then counting the result.
fn bit_scan_reverse(bb: int) -> int {
    Assert(bb != 0);
    var x = bb;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x |= x >> 32;
    return popcount(x) - 1;
}

// Pop the lowest set bit out of `bb`, returning its square index. The caller
// keeps the residue, which is why `bb` is taken by unique reference.
fn pop_lowest_square(ref bb: int) -> int {
    let square = bit_scan_forward(*bb);
    *bb = clear_lowest_bit(*bb);
    return square;
}

// Reverse the bits of a bitboard, which rotates the board 180 degrees.
fn mirror_board(bb: int) -> int {
    var x = bb;
    x = ((x >> 1) & SWAR_K1) | ((x & SWAR_K1) << 1);
    x = ((x >> 2) & SWAR_K2) | ((x & SWAR_K2) << 2);
    x = ((x >> 4) & SWAR_K4) | ((x & SWAR_K4) << 4);
    x = ((x >> 8) & 71777214294589695) | ((x & 71777214294589695) << 8);
    x = ((x >> 16) & 281470681808895) | ((x & 281470681808895) << 16);
    x = (x >> 32) | (x << 32);
    return x;
}

// Swap the byte order, which flips the board vertically because each byte is
// one rank.
fn flip_vertical(bb: int) -> int {
    var x = bb;
    x = ((x >> 8) & 71777214294589695) | ((x & 71777214294589695) << 8);
    x = ((x >> 16) & 281470681808895) | ((x & 281470681808895) << 16);
    x = (x >> 32) | (x << 32);
    return x;
}

// --- Edge masks and directional shifts -----------------------------------

// One bit per square, so that hot code can index instead of shifting.
var SQUARE_BB: vector(int) = vector(int)(SQUARE_COUNT);

// FILE_MASKS[f] has every square on file f set; RANK_MASKS[r] likewise.
var FILE_MASKS: vector(int) = vector(int)(BOARD_FILES);
var RANK_MASKS: vector(int) = vector(int)(BOARD_RANKS);

// The union of file f-1 and file f+1, clipped at the edges. The pawn
// structure terms use it for isolated and neighbouring pawn tests.
var ADJACENT_FILES: vector(int) = vector(int)(BOARD_FILES);

// Complements of the outermost files, used to kill wrap-around in the
// east/west shifts.
var NOT_FILE_A: int = 0;
var NOT_FILE_H: int = 0;
var NOT_FILE_AB: int = 0;
var NOT_FILE_GH: int = 0;

// The board edge and the sixteen central squares, used by evaluation.
var BOARD_EDGES: int = 0;
var BOARD_CENTER: int = 0;
var LIGHT_SQUARES: int = 0;
var DARK_SQUARES: int = 0;

// Chebyshev distance between every pair of squares, precomputed because the
// endgame terms ask for it once per piece per node.
var DISTANCE_TABLE: vector(vector(int)) = vector(vector(int))(SQUARE_COUNT);

// Build every file, rank and edge mask with explicit nested loops rather
// than with wide decimal literals.
fn init_square_masks() {
    var square: int = 0;
    while (square < SQUARE_COUNT) {
        SQUARE_BB.at(square) = 1 << square;
        square += 1;
    }

    var file: int = 0;
    while (file < BOARD_FILES) {
        var mask: int = 0;
        var rank: int = 0;
        while (rank < BOARD_RANKS) {
            mask |= 1 << make_square(file, rank);
            rank += 1;
        }
        FILE_MASKS.at(file) = mask;
        file += 1;
    }

    var rank: int = 0;
    while (rank < BOARD_RANKS) {
        var mask: int = 0;
        var f: int = 0;
        while (f < BOARD_FILES) {
            mask |= 1 << make_square(f, rank);
            f += 1;
        }
        RANK_MASKS.at(rank) = mask;
        rank += 1;
    }

    var af: int = 0;
    while (af < BOARD_FILES) {
        var mask: int = 0;
        if (af > 0) {
            mask |= FILE_MASKS.at(af - 1);
        }
        if (af < BOARD_FILES - 1) {
            mask |= FILE_MASKS.at(af + 1);
        }
        ADJACENT_FILES.at(af) = mask;
        af += 1;
    }

    NOT_FILE_A = FULL_BOARD ^ FILE_MASKS.at(0);
    NOT_FILE_H = FULL_BOARD ^ FILE_MASKS.at(7);
    NOT_FILE_AB = FULL_BOARD ^ (FILE_MASKS.at(0) | FILE_MASKS.at(1));
    NOT_FILE_GH = FULL_BOARD ^ (FILE_MASKS.at(6) | FILE_MASKS.at(7));

    BOARD_EDGES = FILE_MASKS.at(0) | FILE_MASKS.at(7) | RANK_MASKS.at(0) | RANK_MASKS.at(7);
    BOARD_CENTER = (FILE_MASKS.at(2) | FILE_MASKS.at(3) | FILE_MASKS.at(4) | FILE_MASKS.at(5))
        & (RANK_MASKS.at(2) | RANK_MASKS.at(3) | RANK_MASKS.at(4) | RANK_MASKS.at(5));

    var light: int = 0;
    var dark: int = 0;
    var s: int = 0;
    while (s < SQUARE_COUNT) {
        if (square_is_dark(s)) {
            dark |= 1 << s;
        } else {
            light |= 1 << s;
        }
        s += 1;
    }
    LIGHT_SQUARES = light;
    DARK_SQUARES = dark;
}

// The de Bruijn index table: multiply each isolated bit by the sequence and
// record where the top six bits land.
fn init_debruijn_table() {
    var square: int = 0;
    while (square < SQUARE_COUNT) {
        let isolated = 1 << square;
        let folded = (isolated * DEBRUIJN64) >> DEBRUIJN_SHIFT;
        DEBRUIJN_INDEX.at(folded & SQUARE_MASK) = square;
        square += 1;
    }
}

// A full 64 by 64 distance matrix, filled by the obvious double loop.
fn init_distance_table() {
    var from: int = 0;
    while (from < SQUARE_COUNT) {
        DISTANCE_TABLE.at(from) = vector(int)(SQUARE_COUNT);
        var to: int = 0;
        while (to < SQUARE_COUNT) {
            DISTANCE_TABLE.at(from).at(to) = square_distance(from, to);
            to += 1;
        }
        from += 1;
    }
}

// Directional shifts. The north and south shifts cannot wrap because the
// bits simply fall off the end of the word; the lateral ones must first
// remove the file that would reappear on the other side.
fn shift_north(bb: int) -> int {
    return bb << 8;
}

fn shift_south(bb: int) -> int {
    return (bb >> 8) & 72057594037927935;   // 0x00FFFFFFFFFFFFFF
}

fn shift_east(bb: int) -> int {
    return (bb & NOT_FILE_H) << 1;
}

fn shift_west(bb: int) -> int {
    return (bb & NOT_FILE_A) >> 1;
}

fn shift_north_east(bb: int) -> int {
    return (bb & NOT_FILE_H) << 9;
}

fn shift_north_west(bb: int) -> int {
    return (bb & NOT_FILE_A) << 7;
}

fn shift_south_east(bb: int) -> int {
    return ((bb & NOT_FILE_H) >> 7) & 72057594037927935;
}

fn shift_south_west(bb: int) -> int {
    return ((bb & NOT_FILE_A) >> 9) & 72057594037927935;
}

// Shift by a compass index, so that the ray builders can loop.
fn shift_by_ray(bb: int, ray: int) -> int {
    if (ray == RAY_NORTH) {
        return shift_north(bb);
    } else { if (ray == RAY_NORTH_EAST) {
        return shift_north_east(bb);
    } else { if (ray == RAY_EAST) {
        return shift_east(bb);
    } else { if (ray == RAY_SOUTH_EAST) {
        return shift_south_east(bb);
    } else { if (ray == RAY_SOUTH) {
        return shift_south(bb);
    } else { if (ray == RAY_SOUTH_WEST) {
        return shift_south_west(bb);
    } else { if (ray == RAY_WEST) {
        return shift_west(bb);
    } else {
        return shift_north_west(bb);
    } } } } } } }
}

// Forward fill: smear every set bit north (or south) to the end of its file.
fn fill_north(bb: int) -> int {
    var x = bb;
    x |= x << 8;
    x |= x << 16;
    x |= x << 32;
    return x;
}

fn fill_south(bb: int) -> int {
    var x = bb;
    x |= (x >> 8) & 72057594037927935;
    x |= (x >> 16) & 281474976710655;       // 0x0000FFFFFFFFFFFF
    x |= (x >> 32) & 4294967295;            // 0x00000000FFFFFFFF
    return x;
}

// Every file that carries at least one bit, smeared over its whole length.
fn file_fill(bb: int) -> int {
    return fill_north(bb) | fill_south(bb);
}

// The squares strictly in front of `bb` for the given colour.
fn front_span(color: Color, bb: int) -> int {
    if (color == Color.White) {
        return fill_north(shift_north(bb));
    }
    return fill_south(shift_south(bb));
}

// --- Bitboard value type --------------------------------------------------

// A thin wrapper over the raw 64 bit word. The generator works on raw ints
// in its inner loops -- there is no reason to pay for a wrapper there -- but
// the board state and the public API hand out Bitboards so that a stray
// integer cannot be mistaken for a set of squares.
class Bitboard {

    fn raw(self) -> int {
        return self.bits;
    }

    fn empty(self) -> bool {
        return self.bits == 0;
    }

    fn any(self) -> bool {
        return self.bits != 0;
    }

    fn count(self) -> int {
        return popcount(self.bits);
    }

    fn single(self) -> bool {
        return has_single_bit(self.bits);
    }

    fn test(self, square: int) -> bool {
        return (self.bits & (1 << square)) != 0;
    }

    fn set(ref self, square: int) {
        self.bits |= 1 << square;
    }

    fn clear(ref self, square: int) {
        self.bits &= FULL_BOARD ^ (1 << square);
    }

    fn toggle(ref self, square: int) {
        self.bits ^= 1 << square;
    }

    // Move a bit from `from` to `to` in one xor, the operation make_move
    // performs more than any other.
    fn move_bit(ref self, from: int, to: int) {
        self.bits ^= (1 << from) | (1 << to);
    }

    fn lowest(self) -> int {
        if (self.bits == 0) {
            return NO_SQUARE;
        }
        return bit_scan_forward(self.bits);
    }

    fn highest(self) -> int {
        if (self.bits == 0) {
            return NO_SQUARE;
        }
        return bit_scan_reverse(self.bits);
    }

    fn pop_lowest(ref self) -> int {
        if (self.bits == 0) {
            return NO_SQUARE;
        }
        let square = bit_scan_forward(self.bits);
        self.bits &= self.bits - 1;
        return square;
    }

    fn union_with(self, other: Bitboard) -> Bitboard {
        return Bitboard({.bits = self.bits | other.bits});
    }

    fn intersect(self, other: Bitboard) -> Bitboard {
        return Bitboard({.bits = self.bits & other.bits});
    }

    fn difference(self, other: Bitboard) -> Bitboard {
        return Bitboard({.bits = self.bits & (FULL_BOARD ^ other.bits)});
    }

    fn complement(self) -> Bitboard {
        return Bitboard({.bits = FULL_BOARD ^ self.bits});
    }

    fn overlaps(self, other: Bitboard) -> bool {
        return (self.bits & other.bits) != 0;
    }

    fn mirrored(self) -> Bitboard {
        return Bitboard({.bits = mirror_board(self.bits)});
    }

    fn flipped(self) -> Bitboard {
        return Bitboard({.bits = flip_vertical(self.bits)});
    }

    // Render the board as eight lines of eight characters, rank eight first,
    // which is the layout a human expects to read.
    fn to_text(self) -> string {
        var out: string;
        var rank: int = BOARD_RANKS - 1;
        while (rank >= 0) {
            var file: int = 0;
            while (file < BOARD_FILES) {
                if (self.test(make_square(file, rank))) {
                    out.push_back('1');
                } else {
                    out.push_back('.');
                }
                file += 1;
            }
            out.push_back('\n');
            rank -= 1;
        }
        return out;
    }

    var bits: int = 0;
}

fn board_of_square(square: int) -> Bitboard {
    return Bitboard({.bits = 1 << square});
}

fn board_of_file(file: int) -> Bitboard {
    return Bitboard({.bits = FILE_MASKS.at(file)});
}

fn board_of_rank(rank: int) -> Bitboard {
    return Bitboard({.bits = RANK_MASKS.at(rank)});
}

impl Bitboard as Core.BitOrWith(Bitboard) where .Result = Bitboard {
  fn Op(self, other: Bitboard) -> Bitboard {
      return Bitboard({.bits = self.bits | other.bits});
  }
}
impl Bitboard as Core.BitAndWith(Bitboard) where .Result = Bitboard {
  fn Op(self, other: Bitboard) -> Bitboard {
      return Bitboard({.bits = self.bits & other.bits});
  }
}
impl Bitboard as Core.BitXorWith(Bitboard) where .Result = Bitboard {
  fn Op(self, other: Bitboard) -> Bitboard {
      return Bitboard({.bits = self.bits ^ other.bits});
  }
}
impl Bitboard as Core.EqWith(Bitboard) {
  fn Equal(self, other: Bitboard) -> bool {
      return self.bits == other.bits;
  }
}

// --- Precomputed attack tables -------------------------------------------

// Leaper attacks, one entry per square.
var KNIGHT_ATTACKS: vector(int) = vector(int)(SQUARE_COUNT);
var KING_ATTACKS: vector(int) = vector(int)(SQUARE_COUNT);

// Pawn tables are colour indexed: PAWN_ATTACKS[c][s] is the set of squares a
// pawn of colour c standing on s attacks, PAWN_PUSHES[c][s] the single push.
var PAWN_ATTACKS: vector(vector(int)) = vector(vector(int))(COLOR_COUNT);
var PAWN_PUSHES: vector(vector(int)) = vector(vector(int))(COLOR_COUNT);

// RAY_ATTACKS[direction][square] is every square reachable from `square` in
// that direction on an otherwise empty board, excluding `square` itself.
var RAY_ATTACKS: vector(vector(int)) = vector(vector(int))(RAY_COUNT);

// Empty-board attack sets for the sliders, i.e. the union of their rays.
var BISHOP_PSEUDO: vector(int) = vector(int)(SQUARE_COUNT);
var ROOK_PSEUDO: vector(int) = vector(int)(SQUARE_COUNT);
var QUEEN_PSEUDO: vector(int) = vector(int)(SQUARE_COUNT);

// Relevant occupancy masks: the ray squares that can actually block, i.e.
// the pseudo attacks with the board edge trimmed off each ray.
var BISHOP_RELEVANT: vector(int) = vector(int)(SQUARE_COUNT);
var ROOK_RELEVANT: vector(int) = vector(int)(SQUARE_COUNT);

// BETWEEN[a][b] is the open segment strictly between two aligned squares and
// is empty when they are not aligned. LINE_THROUGH[a][b] is the whole line
// they share, which is what the pin resolver needs.
var BETWEEN: vector(vector(int)) = vector(vector(int))(SQUARE_COUNT);
var LINE_THROUGH: vector(vector(int)) = vector(vector(int))(SQUARE_COUNT);

// Knight and king offsets, expressed as (file, rank) pairs so the loops can
// reject an off-board step without any wrap masking.
var KNIGHT_FILE_DELTA: vector(int) = vector(int)(8);
var KNIGHT_RANK_DELTA: vector(int) = vector(int)(8);
var KING_FILE_DELTA: vector(int) = vector(int)(8);
var KING_RANK_DELTA: vector(int) = vector(int)(8);
var RAY_FILE_DELTA: vector(int) = vector(int)(RAY_COUNT);
var RAY_RANK_DELTA: vector(int) = vector(int)(RAY_COUNT);

// The eight knight jumps, in clockwise order starting from north-north-east.
fn init_offset_tables() {
    KNIGHT_FILE_DELTA.at(0) = 1;
    KNIGHT_RANK_DELTA.at(0) = 2;
    KNIGHT_FILE_DELTA.at(1) = 2;
    KNIGHT_RANK_DELTA.at(1) = 1;
    KNIGHT_FILE_DELTA.at(2) = 2;
    KNIGHT_RANK_DELTA.at(2) = -1;
    KNIGHT_FILE_DELTA.at(3) = 1;
    KNIGHT_RANK_DELTA.at(3) = -2;
    KNIGHT_FILE_DELTA.at(4) = -1;
    KNIGHT_RANK_DELTA.at(4) = -2;
    KNIGHT_FILE_DELTA.at(5) = -2;
    KNIGHT_RANK_DELTA.at(5) = -1;
    KNIGHT_FILE_DELTA.at(6) = -2;
    KNIGHT_RANK_DELTA.at(6) = 1;
    KNIGHT_FILE_DELTA.at(7) = -1;
    KNIGHT_RANK_DELTA.at(7) = 2;

    // The king's eight neighbours, same clockwise order starting north.
    KING_FILE_DELTA.at(0) = 0;
    KING_RANK_DELTA.at(0) = 1;
    KING_FILE_DELTA.at(1) = 1;
    KING_RANK_DELTA.at(1) = 1;
    KING_FILE_DELTA.at(2) = 1;
    KING_RANK_DELTA.at(2) = 0;
    KING_FILE_DELTA.at(3) = 1;
    KING_RANK_DELTA.at(3) = -1;
    KING_FILE_DELTA.at(4) = 0;
    KING_RANK_DELTA.at(4) = -1;
    KING_FILE_DELTA.at(5) = -1;
    KING_RANK_DELTA.at(5) = -1;
    KING_FILE_DELTA.at(6) = -1;
    KING_RANK_DELTA.at(6) = 0;
    KING_FILE_DELTA.at(7) = -1;
    KING_RANK_DELTA.at(7) = 1;

    // Ray steps, in the same compass order as the RAY_* indices.
    RAY_FILE_DELTA.at(RAY_NORTH) = 0;
    RAY_RANK_DELTA.at(RAY_NORTH) = 1;
    RAY_FILE_DELTA.at(RAY_NORTH_EAST) = 1;
    RAY_RANK_DELTA.at(RAY_NORTH_EAST) = 1;
    RAY_FILE_DELTA.at(RAY_EAST) = 1;
    RAY_RANK_DELTA.at(RAY_EAST) = 0;
    RAY_FILE_DELTA.at(RAY_SOUTH_EAST) = 1;
    RAY_RANK_DELTA.at(RAY_SOUTH_EAST) = -1;
    RAY_FILE_DELTA.at(RAY_SOUTH) = 0;
    RAY_RANK_DELTA.at(RAY_SOUTH) = -1;
    RAY_FILE_DELTA.at(RAY_SOUTH_WEST) = -1;
    RAY_RANK_DELTA.at(RAY_SOUTH_WEST) = -1;
    RAY_FILE_DELTA.at(RAY_WEST) = -1;
    RAY_RANK_DELTA.at(RAY_WEST) = 0;
    RAY_FILE_DELTA.at(RAY_NORTH_WEST) = -1;
    RAY_RANK_DELTA.at(RAY_NORTH_WEST) = 1;
}

// Knight attacks, built by walking every square as a (file, rank) pair and
// then every jump as a (df, dr) pair. Steps that leave the board are simply
// not ored in, which is the whole of the wrap handling.
fn init_knight_attacks() {
    var rank: int = 0;
    while (rank < BOARD_RANKS) {
        var file: int = 0;
        while (file < BOARD_FILES) {
            let from = make_square(file, rank);
            var mask: int = 0;
            var jump: int = 0;
            while (jump < 8) {
                let tf = file + KNIGHT_FILE_DELTA.at(jump);
                let tr = rank + KNIGHT_RANK_DELTA.at(jump);
                if (tf >= 0 and tf < BOARD_FILES and tr >= 0 and tr < BOARD_RANKS) {
                    mask |= 1 << make_square(tf, tr);
                }
                jump += 1;
            }
            KNIGHT_ATTACKS.at(from) = mask;
            file += 1;
        }
        rank += 1;
    }
}

// King attacks, the same shape with a different offset table.
fn init_king_attacks() {
    var rank: int = 0;
    while (rank < BOARD_RANKS) {
        var file: int = 0;
        while (file < BOARD_FILES) {
            let from = make_square(file, rank);
            var mask: int = 0;
            var step: int = 0;
            while (step < 8) {
                let tf = file + KING_FILE_DELTA.at(step);
                let tr = rank + KING_RANK_DELTA.at(step);
                if (tf >= 0 and tf < BOARD_FILES and tr >= 0 and tr < BOARD_RANKS) {
                    mask |= 1 << make_square(tf, tr);
                }
                step += 1;
            }
            KING_ATTACKS.at(from) = mask;
            file += 1;
        }
        rank += 1;
    }
}

// Pawn attacks and pushes for both colours. A pawn on the last rank has no
// moves at all, which falls out of the rank bounds check.
fn init_pawn_attacks() {
    var color: int = 0;
    while (color < COLOR_COUNT) {
        PAWN_ATTACKS.at(color) = vector(int)(SQUARE_COUNT);
        PAWN_PUSHES.at(color) = vector(int)(SQUARE_COUNT);
        var forward: int = 1;
        if (color == 1) {
            forward = -1;
        }
        var rank: int = 0;
        while (rank < BOARD_RANKS) {
            var file: int = 0;
            while (file < BOARD_FILES) {
                let from = make_square(file, rank);
                let tr = rank + forward;
                var attacks: int = 0;
                var pushes: int = 0;
                if (tr >= 0 and tr < BOARD_RANKS) {
                    pushes |= 1 << make_square(file, tr);
                    if (file > 0) {
                        attacks |= 1 << make_square(file - 1, tr);
                    }
                    if (file < BOARD_FILES - 1) {
                        attacks |= 1 << make_square(file + 1, tr);
                    }
                }
                PAWN_ATTACKS.at(color).at(from) = attacks;
                PAWN_PUSHES.at(color).at(from) = pushes;
                file += 1;
            }
            rank += 1;
        }
        color += 1;
    }
}

// The eight rays from every square. The inner loop walks in (file, rank)
// space so the ray stops at the edge instead of wrapping onto the next rank.
fn init_ray_attacks() {
    var ray: int = 0;
    while (ray < RAY_COUNT) {
        RAY_ATTACKS.at(ray) = vector(int)(SQUARE_COUNT);
        let df = RAY_FILE_DELTA.at(ray);
        let dr = RAY_RANK_DELTA.at(ray);
        var rank: int = 0;
        while (rank < BOARD_RANKS) {
            var file: int = 0;
            while (file < BOARD_FILES) {
                let from = make_square(file, rank);
                var mask: int = 0;
                var tf = file + df;
                var tr = rank + dr;
                while (tf >= 0 and tf < BOARD_FILES and tr >= 0 and tr < BOARD_RANKS) {
                    mask |= 1 << make_square(tf, tr);
                    tf += df;
                    tr += dr;
                }
                RAY_ATTACKS.at(ray).at(from) = mask;
                file += 1;
            }
            rank += 1;
        }
        ray += 1;
    }
}

// Empty-board slider attacks and the trimmed occupancy masks. A blocker on
// the edge of a ray can never shadow anything, so it is excluded.
fn init_slider_masks() {
    var square: int = 0;
    while (square < SQUARE_COUNT) {
        let file = file_of(square);
        let rank = rank_of(square);

        var bishop: int = 0;
        bishop |= RAY_ATTACKS.at(RAY_NORTH_EAST).at(square);
        bishop |= RAY_ATTACKS.at(RAY_SOUTH_EAST).at(square);
        bishop |= RAY_ATTACKS.at(RAY_SOUTH_WEST).at(square);
        bishop |= RAY_ATTACKS.at(RAY_NORTH_WEST).at(square);
        BISHOP_PSEUDO.at(square) = bishop;

        var rook: int = 0;
        rook |= RAY_ATTACKS.at(RAY_NORTH).at(square);
        rook |= RAY_ATTACKS.at(RAY_EAST).at(square);
        rook |= RAY_ATTACKS.at(RAY_SOUTH).at(square);
        rook |= RAY_ATTACKS.at(RAY_WEST).at(square);
        ROOK_PSEUDO.at(square) = rook;

        QUEEN_PSEUDO.at(square) = bishop | rook;

        // A diagonal blocker matters only away from the rim.
        BISHOP_RELEVANT.at(square) = bishop & (FULL_BOARD ^ BOARD_EDGES);

        // For a rook the trimming is per direction: the a-file matters for a
        // rook on the a-file, so only the far end of each ray comes off.
        var relevant: int = rook;
        if (file != 0) {
            relevant &= FULL_BOARD ^ FILE_MASKS.at(0);
        }
        if (file != BOARD_FILES - 1) {
            relevant &= FULL_BOARD ^ FILE_MASKS.at(BOARD_FILES - 1);
        }
        if (rank != 0) {
            relevant &= FULL_BOARD ^ RANK_MASKS.at(0);
        }
        if (rank != BOARD_RANKS - 1) {
            relevant &= FULL_BOARD ^ RANK_MASKS.at(BOARD_RANKS - 1);
        }
        ROOK_RELEVANT.at(square) = relevant;

        square += 1;
    }
}

// BETWEEN and LINE_THROUGH, the two tables the check and pin logic lives on.
// For each ordered pair the loop walks the eight rays from `from` looking for
// `to`; if it is found the accumulated segment is the answer.
fn init_between_tables() {
    var from: int = 0;
    while (from < SQUARE_COUNT) {
        BETWEEN.at(from) = vector(int)(SQUARE_COUNT);
        LINE_THROUGH.at(from) = vector(int)(SQUARE_COUNT);
        var to: int = 0;
        while (to < SQUARE_COUNT) {
            BETWEEN.at(from).at(to) = 0;
            LINE_THROUGH.at(from).at(to) = 0;
            to += 1;
        }
        from += 1;
    }

    var origin: int = 0;
    while (origin < SQUARE_COUNT) {
        var ray: int = 0;
        while (ray < RAY_COUNT) {
            let df = RAY_FILE_DELTA.at(ray);
            let dr = RAY_RANK_DELTA.at(ray);
            var segment: int = 0;
            var tf = file_of(origin) + df;
            var tr = rank_of(origin) + dr;
            while (tf >= 0 and tf < BOARD_FILES and tr >= 0 and tr < BOARD_RANKS) {
                let target = make_square(tf, tr);
                BETWEEN.at(origin).at(target) = segment;

                // The shared line is the ray plus its opposite plus both
                // endpoints; the opposite ray index is `ray + 4` modulo 8.
                let back = (ray + 4) % RAY_COUNT;
                LINE_THROUGH.at(origin).at(target) = RAY_ATTACKS.at(ray).at(origin)
                    | RAY_ATTACKS.at(back).at(origin)
                    | (1 << origin)
                    | (1 << target);

                segment |= 1 << target;
                tf += df;
                tr += dr;
            }
            ray += 1;
        }
        origin += 1;
    }
}

// Run every table builder once, in dependency order.
fn init_tables() {
    init_square_masks();
    init_debruijn_table();
    init_distance_table();
    init_offset_tables();
    init_knight_attacks();
    init_king_attacks();
    init_pawn_attacks();
    init_ray_attacks();
    init_slider_masks();
    init_between_tables();
}

// --- Sliding piece attacks ------------------------------------------------

// A positive ray is one whose squares have higher indices than the origin
// (north, north-east, east, north-west); the blocker to keep is then the
// *lowest* set bit of the intersection. For the negative rays it is the
// highest. This is the classical approach: no magics, one bit scan per ray.
fn ray_is_positive(ray: int) -> bool {
    return ray == RAY_NORTH or ray == RAY_NORTH_EAST or ray == RAY_EAST or ray == RAY_NORTH_WEST;
}

// Attacks along one ray, stopping on (and including) the first blocker.
fn ray_attacks_from(square: int, ray: int, occupancy: int) -> int {
    var attacks = RAY_ATTACKS.at(ray).at(square);
    let blockers = attacks & occupancy;
    if (blockers == 0) {
        return attacks;
    }
    if (ray_is_positive(ray)) {
        let first = bit_scan_forward(blockers);
        attacks = attacks & (FULL_BOARD ^ RAY_ATTACKS.at(ray).at(first));
    } else {
        let first = bit_scan_reverse(blockers);
        attacks = attacks & (FULL_BOARD ^ RAY_ATTACKS.at(ray).at(first));
    }
    return attacks;
}

// Rook attacks: the four straight rays, unrolled because the loop overhead
// is a measurable fraction of the work at this size.
fn rook_attacks(square: int, occupancy: int) -> int {
    attack_query_count += 1;
    var attacks: int = 0;
    attacks |= ray_attacks_from(square, RAY_NORTH, occupancy);
    attacks |= ray_attacks_from(square, RAY_EAST, occupancy);
    attacks |= ray_attacks_from(square, RAY_SOUTH, occupancy);
    attacks |= ray_attacks_from(square, RAY_WEST, occupancy);
    return attacks;
}

fn bishop_attacks(square: int, occupancy: int) -> int {
    attack_query_count += 1;
    var attacks: int = 0;
    attacks |= ray_attacks_from(square, RAY_NORTH_EAST, occupancy);
    attacks |= ray_attacks_from(square, RAY_SOUTH_EAST, occupancy);
    attacks |= ray_attacks_from(square, RAY_SOUTH_WEST, occupancy);
    attacks |= ray_attacks_from(square, RAY_NORTH_WEST, occupancy);
    return attacks;
}

fn queen_attacks(square: int, occupancy: int) -> int {
    return rook_attacks(square, occupancy) | bishop_attacks(square, occupancy);
}

// Attacks of an arbitrary kind, used by the generic generation loop and by
// static exchange evaluation.
fn attacks_of_kind(kind: PieceKind, color: Color, square: int, occupancy: int) -> int {
    if (kind == PieceKind.Pawn) {
        return PAWN_ATTACKS.at(color_index(color)).at(square);
    } else { if (kind == PieceKind.Knight) {
        return KNIGHT_ATTACKS.at(square);
    } else { if (kind == PieceKind.Bishop) {
        return bishop_attacks(square, occupancy);
    } else { if (kind == PieceKind.Rook) {
        return rook_attacks(square, occupancy);
    } else { if (kind == PieceKind.Queen) {
        return queen_attacks(square, occupancy);
    } else { if (kind == PieceKind.King) {
        return KING_ATTACKS.at(square);
    } else {
        return 0;
    } } } } } }
}

// Attacks of a whole set of pawns at once, which is how the evaluation and
// the king-safety terms want them.
fn pawn_attack_span(color: Color, pawns: int) -> int {
    if (color == Color.White) {
        return shift_north_east(pawns) | shift_north_west(pawns);
    }
    return shift_south_east(pawns) | shift_south_west(pawns);
}

// Squares attacked by two pawns at once, i.e. the defended points of a
// pawn chain.
fn pawn_double_attacks(color: Color, pawns: int) -> int {
    if (color == Color.White) {
        return shift_north_east(pawns) & shift_north_west(pawns);
    }
    return shift_south_east(pawns) & shift_south_west(pawns);
}

// Single and double pushes for a whole pawn set, given the empty squares.
fn pawn_single_pushes(color: Color, pawns: int, empty: int) -> int {
    if (color == Color.White) {
        return shift_north(pawns) & empty;
    }
    return shift_south(pawns) & empty;
}

fn pawn_double_pushes(color: Color, pawns: int, empty: int) -> int {
    let single = pawn_single_pushes(color, pawns, empty);
    if (color == Color.White) {
        return shift_north(single) & empty & RANK_MASKS.at(3);
    }
    return shift_south(single) & empty & RANK_MASKS.at(4);
}

// --- Move encoding --------------------------------------------------------

// A move is packed into sixteen bits:
//
//     bits  0.. 5   origin square
//     bits  6..11   destination square
//     bits 12..15   MoveKind
//
// The search stores a separate score alongside, so the packed form stays
// narrow enough that a whole move list fits in a couple of cache lines.
let MOVE_FROM_SHIFT: int = 0;
let MOVE_TO_SHIFT: int = 6;
let MOVE_KIND_SHIFT: int = 12;
let MOVE_KIND_MASK: int = 15;          // 0xF
let MOVE_NULL: int = 0;

// The bit inside the kind field that marks a capture, and the one that marks
// a promotion. Both are set for the promotion-captures.
let KIND_CAPTURE_BIT: int = 4;         // 0x4
let KIND_PROMOTION_BIT: int = 8;       // 0x8

fn encode_move(from: int, to: int, kind: int) -> int {
    return ((from & SQUARE_MASK) << MOVE_FROM_SHIFT)
        | ((to & SQUARE_MASK) << MOVE_TO_SHIFT)
        | ((kind & MOVE_KIND_MASK) << MOVE_KIND_SHIFT);
}

fn move_from(m: int) -> int {
    return (m >> MOVE_FROM_SHIFT) & SQUARE_MASK;
}

fn move_to(m: int) -> int {
    return (m >> MOVE_TO_SHIFT) & SQUARE_MASK;
}

fn move_kind(m: int) -> int {
    return (m >> MOVE_KIND_SHIFT) & MOVE_KIND_MASK;
}

fn move_is_null(m: int) -> bool {
    return m == MOVE_NULL;
}

fn move_is_capture(m: int) -> bool {
    return (move_kind(m) & KIND_CAPTURE_BIT) != 0;
}

fn move_is_promotion(m: int) -> bool {
    return (move_kind(m) & KIND_PROMOTION_BIT) != 0;
}

fn move_is_en_passant(m: int) -> bool {
    return move_kind(m) == 5;
}

fn move_is_double_push(m: int) -> bool {
    return move_kind(m) == 1;
}

fn move_is_castle(m: int) -> bool {
    let kind = move_kind(m);
    return kind == 2 or kind == 3;
}

fn move_is_king_castle(m: int) -> bool {
    return move_kind(m) == 2;
}

fn move_is_quiet(m: int) -> bool {
    let kind = move_kind(m);
    return (kind & KIND_CAPTURE_BIT) == 0 and (kind & KIND_PROMOTION_BIT) == 0;
}

// The piece a promotion produces. The low two bits of the kind field count
// knight, bishop, rook, queen in that order, which is why this is a shift
// and an add rather than a table.
fn move_promotion_kind(m: int) -> PieceKind {
    if (move_is_promotion(m) == false) {
        return PieceKind.KindNone;
    }
    let selector = move_kind(m) & 3;
    return kind_from_index(selector + 1);
}

// Build the four promotion kinds for a given selector, capture or not.
fn promotion_move_kind(selector: int, capture: bool) -> int {
    var kind = KIND_PROMOTION_BIT | (selector & 3);
    if (capture) {
        kind |= KIND_CAPTURE_BIT;
    }
    return kind;
}

// A move plus the ordering score the search assigns it.
class ScoredMove {

    fn from_square(self) -> int {
        return move_from(self.packed);
    }

    fn to_square(self) -> int {
        return move_to(self.packed);
    }

    fn kind(self) -> int {
        return move_kind(self.packed);
    }

    fn is_capture(self) -> bool {
        return move_is_capture(self.packed);
    }

    var packed: int = 0;
    var score: int = 0;
}

// A fixed-capacity move list. Generation appends; the search sorts in place
// with a selection pass rather than a full sort, because it usually only
// looks at the first few moves.
class MoveList {

    fn size(self) -> int {
        return self.count;
    }

    fn empty(self) -> bool {
        return self.count == 0;
    }

    fn clear(ref self) {
        self.count = 0;
        self.moves.clear();
    }

    fn add(ref self, m: int) {
        Assert(self.count < MAX_MOVES);
        self.moves.push_back(ScoredMove({.packed = m, .score = 0}));
        self.count += 1;
        generated_move_count += 1;
    }

    fn add_scored(ref self, m: int, score: int) {
        Assert(self.count < MAX_MOVES);
        self.moves.push_back(ScoredMove({.packed = m, .score = score}));
        self.count += 1;
        generated_move_count += 1;
    }

    fn at(ref self, index: int) -> ref ScoredMove {
        Assert(index >= 0);
        Assert(index < self.count);
        return self.moves.at(index);
    }

    fn packed_at(self, index: int) -> int {
        return self.moves.at(index).packed;
    }

    fn contains(self, m: int) -> bool {
        var i: int = 0;
        while (i < self.count) {
            if (self.moves.at(i).packed == m) {
                return true;
            }
            i += 1;
        }
        return false;
    }

    // Move the best remaining entry to `start` and return its score. This is
    // the selection step of an incremental sort.
    fn select_best(ref self, start: int) -> int {
        var best = start;
        var i = start + 1;
        while (i < self.count) {
            if (self.moves.at(i).score > self.moves.at(best).score) {
                best = i;
            }
            i += 1;
        }
        if (best != start) {
            swap(self.moves.at(start), self.moves.at(best));
        }
        return self.moves.at(start).score;
    }

    // Drop every entry for which the predicate flag array says false. The
    // legality filter uses it after a pseudo-legal pass.
    fn compact(ref self, keep: vector(bool)) {
        var write: int = 0;
        var read: int = 0;
        while (read < self.count) {
            if (keep.at(read)) {
                self.moves.at(write) = self.moves.at(read);
                write += 1;
            }
            read += 1;
        }
        self.count = write;
    }

    var moves: vector(ScoredMove);
    var count: int = 0;
}

// --- Irreversible state ---------------------------------------------------

// Everything make_move destroys and unmake_move must restore. The mutable
// half of the position (the bitboards, the square array) is undone by
// replaying the move backwards; only these five scalars need saving.
class Undo {
    var move_packed: int = 0;
    var captured: int = 12;
    var castling: int = 0;
    var ep_square: int = -1;
    var halfmove: int = 0;
    var key: int = 0;
}

// CASTLE_MASK[s] is the set of rights that survive any move touching square
// s. A rook leaving a1 clears white's queenside right; the king leaving e1
// clears both of white's. Every other square keeps all four.
var CASTLE_MASK: vector(int) = vector(int)(SQUARE_COUNT);

fn init_castle_masks() {
    var square: int = 0;
    while (square < SQUARE_COUNT) {
        CASTLE_MASK.at(square) = CASTLE_ALL;
        square += 1;
    }
    CASTLE_MASK.at(SQ_A1) = CASTLE_ALL ^ CASTLE_WHITE_QUEEN;
    CASTLE_MASK.at(SQ_H1) = CASTLE_ALL ^ CASTLE_WHITE_KING;
    CASTLE_MASK.at(SQ_E1) = CASTLE_ALL ^ CASTLE_WHITE_ANY;
    CASTLE_MASK.at(SQ_A8) = CASTLE_ALL ^ CASTLE_BLACK_QUEEN;
    CASTLE_MASK.at(SQ_H8) = CASTLE_ALL ^ CASTLE_BLACK_KING;
    CASTLE_MASK.at(SQ_E8) = CASTLE_ALL ^ CASTLE_BLACK_ANY;
}

// The squares that must be empty, and the squares that must not be attacked,
// for each of the four castles. Indexed by right bit position 0..3.
var CASTLE_EMPTY_MASK: vector(int) = vector(int)(4);
var CASTLE_SAFE_MASK: vector(int) = vector(int)(4);
var CASTLE_KING_FROM: vector(int) = vector(int)(4);
var CASTLE_KING_TO: vector(int) = vector(int)(4);
var CASTLE_ROOK_FROM: vector(int) = vector(int)(4);
var CASTLE_ROOK_TO: vector(int) = vector(int)(4);

fn init_castle_paths() {
    // White kingside: f1 and g1 empty, e1 f1 g1 unattacked.
    CASTLE_EMPTY_MASK.at(0) = (1 << SQ_F1) | (1 << SQ_G1);
    CASTLE_SAFE_MASK.at(0) = (1 << SQ_E1) | (1 << SQ_F1) | (1 << SQ_G1);
    CASTLE_KING_FROM.at(0) = SQ_E1;
    CASTLE_KING_TO.at(0) = SQ_G1;
    CASTLE_ROOK_FROM.at(0) = SQ_H1;
    CASTLE_ROOK_TO.at(0) = SQ_F1;

    // White queenside: b1 c1 d1 empty, e1 d1 c1 unattacked. b1 may be
    // attacked, which is the classic asymmetry.
    CASTLE_EMPTY_MASK.at(1) = (1 << SQ_B1) | (1 << SQ_C1) | (1 << SQ_D1);
    CASTLE_SAFE_MASK.at(1) = (1 << SQ_E1) | (1 << SQ_D1) | (1 << SQ_C1);
    CASTLE_KING_FROM.at(1) = SQ_E1;
    CASTLE_KING_TO.at(1) = SQ_C1;
    CASTLE_ROOK_FROM.at(1) = SQ_A1;
    CASTLE_ROOK_TO.at(1) = SQ_D1;

    // Black kingside.
    CASTLE_EMPTY_MASK.at(2) = (1 << SQ_F8) | (1 << SQ_G8);
    CASTLE_SAFE_MASK.at(2) = (1 << SQ_E8) | (1 << SQ_F8) | (1 << SQ_G8);
    CASTLE_KING_FROM.at(2) = SQ_E8;
    CASTLE_KING_TO.at(2) = SQ_G8;
    CASTLE_ROOK_FROM.at(2) = SQ_H8;
    CASTLE_ROOK_TO.at(2) = SQ_F8;

    // Black queenside.
    CASTLE_EMPTY_MASK.at(3) = (1 << SQ_B8) | (1 << SQ_C8) | (1 << SQ_D8);
    CASTLE_SAFE_MASK.at(3) = (1 << SQ_E8) | (1 << SQ_D8) | (1 << SQ_C8);
    CASTLE_KING_FROM.at(3) = SQ_E8;
    CASTLE_KING_TO.at(3) = SQ_C8;
    CASTLE_ROOK_FROM.at(3) = SQ_A8;
    CASTLE_ROOK_TO.at(3) = SQ_D8;
}

// --- Zobrist hashing ------------------------------------------------------

// A xorshift generator seeded with a fixed constant, so that two runs of the
// program agree on every key. The multiplier is the usual 64 bit mixer.
let ZOBRIST_SEED: int = 1070372;
let XORSHIFT_MULTIPLIER: int = 2685821657736338717;

var ZOBRIST_PIECES: vector(vector(int)) = vector(vector(int))(PIECE_CODE_COUNT);
var ZOBRIST_CASTLING: vector(int) = vector(int)(16);
var ZOBRIST_EP_FILE: vector(int) = vector(int)(BOARD_FILES);
var ZOBRIST_SIDE: int = 0;
var zobrist_state: int = 0;

// One step of xorshift64*, written out so the shifts are visible.
fn next_random() -> int {
    var x = zobrist_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    zobrist_state = x;
    return x * XORSHIFT_MULTIPLIER;
}

fn init_zobrist() {
    zobrist_state = ZOBRIST_SEED;
    var piece: int = 0;
    while (piece < PIECE_CODE_COUNT) {
        ZOBRIST_PIECES.at(piece) = vector(int)(SQUARE_COUNT);
        var square: int = 0;
        while (square < SQUARE_COUNT) {
            ZOBRIST_PIECES.at(piece).at(square) = next_random();
            square += 1;
        }
        piece += 1;
    }
    var rights: int = 0;
    while (rights < 16) {
        ZOBRIST_CASTLING.at(rights) = next_random();
        rights += 1;
    }
    var file: int = 0;
    while (file < BOARD_FILES) {
        ZOBRIST_EP_FILE.at(file) = next_random();
        file += 1;
    }
    ZOBRIST_SIDE = next_random();
}

// --- Position -------------------------------------------------------------

// The position. `squares` is the redundant mailbox that make_move consults to
// find out what it is capturing; the twelve bitboards are the primary
// representation and everything else is derived from them.
class Position {

    // --- basic accessors ---------------------------------------------------

    fn piece_at(self, square: int) -> int {
        Assert(square >= 0);
        Assert(square < SQUARE_COUNT);
        return self.squares.at(square);
    }

    fn is_empty_square(self, square: int) -> bool {
        return self.squares.at(square) >= PIECE_CODE_COUNT;
    }

    fn side_to_move(self) -> Color {
        return self.side;
    }

    fn opponent(self) -> Color {
        return opposite_color(self.side);
    }

    fn occupied(self) -> int {
        return self.occupancy;
    }

    fn empty_squares(self) -> int {
        return FULL_BOARD ^ self.occupancy;
    }

    fn pieces_of_color(self, color: Color) -> int {
        return self.by_color.at(color_index(color));
    }

    fn pieces_of_kind(self, kind: PieceKind) -> int {
        return self.by_kind.at(kind_index(kind));
    }

    // The intersection that almost every generation loop starts from.
    fn pieces(self, color: Color, kind: PieceKind) -> int {
        return self.by_color.at(color_index(color)) & self.by_kind.at(kind_index(kind));
    }

    fn diagonal_sliders(self, color: Color) -> int {
        return self.by_color.at(color_index(color))
            & (self.by_kind.at(kind_index(PieceKind.Bishop)) | self.by_kind.at(kind_index(PieceKind.Queen)));
    }

    fn straight_sliders(self, color: Color) -> int {
        return self.by_color.at(color_index(color))
            & (self.by_kind.at(kind_index(PieceKind.Rook)) | self.by_kind.at(kind_index(PieceKind.Queen)));
    }

    fn king_square(self, color: Color) -> int {
        return self.kings.at(color_index(color));
    }

    fn castling_rights(self) -> int {
        return self.castling;
    }

    fn has_castling_right(self, right: int) -> bool {
        return (self.castling & right) != 0;
    }

    fn en_passant_square(self) -> int {
        return self.ep_square;
    }

    fn halfmove_clock(self) -> int {
        return self.halfmove;
    }

    fn fullmove_number(self) -> int {
        return self.fullmove;
    }

    fn hash_key(self) -> int {
        return self.key;
    }

    fn piece_count(self, color: Color, kind: PieceKind) -> int {
        return popcount(self.pieces(color, kind));
    }

    fn total_piece_count(self) -> int {
        return popcount(self.occupancy);
    }

    // --- board mutation ----------------------------------------------------

    // Clear every board and reset the scalars. Called before FEN parsing.
    fn reset(ref self) {
        self.squares = vector(int)(SQUARE_COUNT);
        var square: int = 0;
        while (square < SQUARE_COUNT) {
            self.squares.at(square) = PIECE_CODE_COUNT;
            square += 1;
        }
        self.by_color = vector(int)(COLOR_COUNT);
        self.by_color.at(0) = 0;
        self.by_color.at(1) = 0;
        self.by_kind = vector(int)(PIECE_KIND_COUNT);
        var kind: int = 0;
        while (kind < PIECE_KIND_COUNT) {
            self.by_kind.at(kind) = 0;
            kind += 1;
        }
        self.kings = vector(int)(COLOR_COUNT);
        self.kings.at(0) = NO_SQUARE;
        self.kings.at(1) = NO_SQUARE;
        self.occupancy = 0;
        self.side = Color.White;
        self.castling = CASTLE_NONE;
        self.ep_square = NO_SQUARE;
        self.halfmove = 0;
        self.fullmove = 1;
        self.key = 0;
        self.ply = 0;
    }

    // Put `piece` on an empty square, updating all four views plus the hash.
    fn put_piece(ref self, piece: int, square: int) {
        Assert(self.is_empty_square(square));
        let color = color_index(piece_color(piece));
        let kind = kind_index(piece_kind(piece));
        let bit = 1 << square;
        self.squares.at(square) = piece;
        self.by_color.at(color) |= bit;
        self.by_kind.at(kind) |= bit;
        self.occupancy |= bit;
        self.key ^= ZOBRIST_PIECES.at(piece).at(square);
        if (kind == kind_index(PieceKind.King)) {
            self.kings.at(color) = square;
        }
    }

    // Take a piece off an occupied square.
    fn remove_piece(ref self, square: int) {
        let piece = self.squares.at(square);
        Assert(piece < PIECE_CODE_COUNT);
        let color = color_index(piece_color(piece));
        let kind = kind_index(piece_kind(piece));
        let clear = FULL_BOARD ^ (1 << square);
        self.squares.at(square) = PIECE_CODE_COUNT;
        self.by_color.at(color) &= clear;
        self.by_kind.at(kind) &= clear;
        self.occupancy &= clear;
        self.key ^= ZOBRIST_PIECES.at(piece).at(square);
        if (kind == kind_index(PieceKind.King)) {
            self.kings.at(color) = NO_SQUARE;
        }
    }

    // Slide a piece from one square to another. The two xors touch each
    // board once, which is why this is not a remove followed by a put.
    fn move_piece(ref self, from: int, to: int) {
        let piece = self.squares.at(from);
        Assert(piece < PIECE_CODE_COUNT);
        let color = color_index(piece_color(piece));
        let kind = kind_index(piece_kind(piece));
        let delta = (1 << from) | (1 << to);
        self.squares.at(from) = PIECE_CODE_COUNT;
        self.squares.at(to) = piece;
        self.by_color.at(color) ^= delta;
        self.by_kind.at(kind) ^= delta;
        self.occupancy ^= delta;
        self.key ^= ZOBRIST_PIECES.at(piece).at(from);
        self.key ^= ZOBRIST_PIECES.at(piece).at(to);
        if (kind == kind_index(PieceKind.King)) {
            self.kings.at(color) = to;
        }
    }

    // Recompute the hash from scratch. Used to assert the incremental
    // updates in make_move and unmake_move have not drifted.
    fn compute_key(self) -> int {
        var key: int = 0;
        var square: int = 0;
        while (square < SQUARE_COUNT) {
            let piece = self.squares.at(square);
            if (piece < PIECE_CODE_COUNT) {
                key ^= ZOBRIST_PIECES.at(piece).at(square);
            }
            square += 1;
        }
        key ^= ZOBRIST_CASTLING.at(self.castling & CASTLE_ALL);
        if (self.ep_square != NO_SQUARE) {
            key ^= ZOBRIST_EP_FILE.at(file_of(self.ep_square));
        }
        if (self.side == Color.Black) {
            key ^= ZOBRIST_SIDE;
        }
        return key;
    }

    // --- attack detection --------------------------------------------------

    // Every piece of either colour that attacks `square` given `occupancy`.
    // The pawn term reads the *opposite* colour's table because "which black
    // pawn attacks e4" is the same question as "which squares would a white
    // pawn on e4 attack".
    fn attackers_to(self, square: int, occupancy: int) -> int {
        var attackers: int = 0;
        attackers |= PAWN_ATTACKS.at(0).at(square) & self.pieces(Color.Black, PieceKind.Pawn);
        attackers |= PAWN_ATTACKS.at(1).at(square) & self.pieces(Color.White, PieceKind.Pawn);
        attackers |= KNIGHT_ATTACKS.at(square) & self.by_kind.at(kind_index(PieceKind.Knight));
        attackers |= KING_ATTACKS.at(square) & self.by_kind.at(kind_index(PieceKind.King));
        attackers |= bishop_attacks(square, occupancy)
            & (self.by_kind.at(kind_index(PieceKind.Bishop)) | self.by_kind.at(kind_index(PieceKind.Queen)));
        attackers |= rook_attacks(square, occupancy)
            & (self.by_kind.at(kind_index(PieceKind.Rook)) | self.by_kind.at(kind_index(PieceKind.Queen)));
        return attackers;
    }

    // The same restricted to one colour.
    fn attackers_of_color(self, square: int, color: Color, occupancy: int) -> int {
        return self.attackers_to(square, occupancy) & self.by_color.at(color_index(color));
    }

    // Is `square` attacked by `color`? Written as an early-out cascade
    // instead of reusing attackers_to, because the cheap leaper tests reject
    // the overwhelming majority of calls before any ray is walked.
    fn is_attacked(self, square: int, color: Color) -> bool {
        attack_query_count += 1;
        let them = color_index(color);
        let us = 1 - them;
        if ((PAWN_ATTACKS.at(us).at(square) & self.pieces(color, PieceKind.Pawn)) != 0) {
            return true;
        }
        if ((KNIGHT_ATTACKS.at(square) & self.pieces(color, PieceKind.Knight)) != 0) {
            return true;
        }
        if ((KING_ATTACKS.at(square) & self.pieces(color, PieceKind.King)) != 0) {
            return true;
        }
        let diagonal = self.diagonal_sliders(color);
        if (diagonal != 0) {
            if ((bishop_attacks(square, self.occupancy) & diagonal) != 0) {
                return true;
            }
        }
        let straight = self.straight_sliders(color);
        if (straight != 0) {
            if ((rook_attacks(square, self.occupancy) & straight) != 0) {
                return true;
            }
        }
        return false;
    }

    // The same question with a hypothetical occupancy, which is what the
    // en passant legality test needs: removing the captured pawn can expose
    // a rook along the fifth rank.
    fn is_attacked_with(self, square: int, color: Color, occupancy: int) -> bool {
        let them = color_index(color);
        let us = 1 - them;
        var hits: int = 0;
        hits |= PAWN_ATTACKS.at(us).at(square) & self.pieces(color, PieceKind.Pawn);
        hits |= KNIGHT_ATTACKS.at(square) & self.pieces(color, PieceKind.Knight);
        hits |= KING_ATTACKS.at(square) & self.pieces(color, PieceKind.King);
        hits |= bishop_attacks(square, occupancy) & self.diagonal_sliders(color);
        hits |= rook_attacks(square, occupancy) & self.straight_sliders(color);
        return (hits & occupancy) != 0;
    }

    // Every square the given colour attacks, as one union. Evaluation uses
    // it for mobility and king safety.
    fn attack_map(self, color: Color) -> int {
        var map: int = 0;
        map |= pawn_attack_span(color, self.pieces(color, PieceKind.Pawn));

        var knights = self.pieces(color, PieceKind.Knight);
        while (knights != 0) {
            let square = bit_scan_forward(knights);
            map |= KNIGHT_ATTACKS.at(square);
            knights = clear_lowest_bit(knights);
        }

        var bishops = self.pieces(color, PieceKind.Bishop);
        while (bishops != 0) {
            let square = bit_scan_forward(bishops);
            map |= bishop_attacks(square, self.occupancy);
            bishops = clear_lowest_bit(bishops);
        }

        var rooks = self.pieces(color, PieceKind.Rook);
        while (rooks != 0) {
            let square = bit_scan_forward(rooks);
            map |= rook_attacks(square, self.occupancy);
            rooks = clear_lowest_bit(rooks);
        }

        var queens = self.pieces(color, PieceKind.Queen);
        while (queens != 0) {
            let square = bit_scan_forward(queens);
            map |= queen_attacks(square, self.occupancy);
            queens = clear_lowest_bit(queens);
        }

        let king = self.kings.at(color_index(color));
        if (king != NO_SQUARE) {
            map |= KING_ATTACKS.at(king);
        }
        return map;
    }

    // --- checks and pins ---------------------------------------------------

    // The pieces giving check to `color`'s king right now.
    fn checkers_of(self, color: Color) -> int {
        let king = self.kings.at(color_index(color));
        if (king == NO_SQUARE) {
            return 0;
        }
        return self.attackers_to(king, self.occupancy) & self.by_color.at(color_index(opposite_color(color)));
    }

    fn checkers(self) -> int {
        return self.checkers_of(self.side);
    }

    fn in_check(self) -> bool {
        let king = self.kings.at(color_index(self.side));
        if (king == NO_SQUARE) {
            return false;
        }
        return self.is_attacked(king, opposite_color(self.side));
    }

    fn in_double_check(self) -> bool {
        let checking = self.checkers();
        return checking != 0 and has_single_bit(checking) == false;
    }

    // The pieces of `color` that are pinned against their own king.
    //
    // For each enemy slider that would hit the king on an empty board, look
    // at the segment between it and the king. Exactly one occupant means
    // that occupant is pinned; two or more and nothing is.
    fn pinned_pieces(self, color: Color) -> int {
        let king = self.kings.at(color_index(color));
        if (king == NO_SQUARE) {
            return 0;
        }
        let them = opposite_color(color);
        let ours = self.by_color.at(color_index(color));
        var pinned: int = 0;

        var snipers = (BISHOP_PSEUDO.at(king) & self.diagonal_sliders(them))
            | (ROOK_PSEUDO.at(king) & self.straight_sliders(them));
        while (snipers != 0) {
            let sniper = bit_scan_forward(snipers);
            snipers = clear_lowest_bit(snipers);
            let blockers = BETWEEN.at(king).at(sniper) & self.occupancy;
            if (blockers != 0 and has_single_bit(blockers)) {
                pinned |= blockers & ours;
            }
        }
        return pinned;
    }

    // The full line a pinned piece may still travel along: the shared line
    // of the pinner and the king, including both endpoints.
    fn pin_line(self, color: Color, square: int) -> int {
        let king = self.kings.at(color_index(color));
        if (king == NO_SQUARE) {
            return FULL_BOARD;
        }
        return LINE_THROUGH.at(king).at(square);
    }

    // Squares of `color`'s pieces that, if they moved, would discover check
    // on the *enemy* king. Used by the move ordering to bonus discoveries.
    fn discovery_candidates(self, color: Color) -> int {
        let them = opposite_color(color);
        let enemy_king = self.kings.at(color_index(them));
        if (enemy_king == NO_SQUARE) {
            return 0;
        }
        let ours = self.by_color.at(color_index(color));
        var candidates: int = 0;
        var snipers = (BISHOP_PSEUDO.at(enemy_king) & self.diagonal_sliders(color))
            | (ROOK_PSEUDO.at(enemy_king) & self.straight_sliders(color));
        while (snipers != 0) {
            let sniper = bit_scan_forward(snipers);
            snipers = clear_lowest_bit(snipers);
            let between = BETWEEN.at(enemy_king).at(sniper) & self.occupancy;
            if (between != 0 and has_single_bit(between)) {
                candidates |= between & ours;
            }
        }
        return candidates;
    }

    // --- pseudo-legal move generation --------------------------------------

    // Pawn moves: single pushes, double pushes, both capture directions, the
    // four promotions of each and en passant. The pushes are generated set
    // wise -- shift the whole pawn board and mask -- while the captures walk
    // pawn by pawn so that the promotion split stays readable.
    fn generate_pawn_moves(self, ref list: MoveList, kind: GenKind) {
        let us = self.side;
        let them = opposite_color(us);
        let pawns = self.pieces(us, PieceKind.Pawn);
        if (pawns == 0) {
            return;
        }
        let empty = FULL_BOARD ^ self.occupancy;
        let enemies = self.by_color.at(color_index(them));
        let promo_rank = pawn_promotion_rank(us);
        let delta = pawn_push_delta(us);

        if (kind != GenKind.GenCaptures) {
            var pushes = pawn_single_pushes(us, pawns, empty);
            while (pushes != 0) {
                let to = bit_scan_forward(pushes);
                pushes = clear_lowest_bit(pushes);
                let from = to - delta;
                if (rank_of(to) == promo_rank) {
                    var selector: int = 0;
                    while (selector < 4) {
                        list.add(encode_move(from, to, promotion_move_kind(selector, false)));
                        selector += 1;
                    }
                } else {
                    list.add(encode_move(from, to, 0));
                }
            }

            var doubles = pawn_double_pushes(us, pawns, empty);
            while (doubles != 0) {
                let to = bit_scan_forward(doubles);
                doubles = clear_lowest_bit(doubles);
                list.add(encode_move(to - delta - delta, to, 1));
            }
        }

        if (kind == GenKind.GenQuiets) {
            return;
        }

        // Captures, one pawn at a time so the promotion branch is shared.
        var movers = pawns;
        while (movers != 0) {
            let from = bit_scan_forward(movers);
            movers = clear_lowest_bit(movers);
            var targets = PAWN_ATTACKS.at(color_index(us)).at(from) & enemies;
            while (targets != 0) {
                let to = bit_scan_forward(targets);
                targets = clear_lowest_bit(targets);
                if (rank_of(to) == promo_rank) {
                    var selector: int = 0;
                    while (selector < 4) {
                        list.add(encode_move(from, to, promotion_move_kind(selector, true)));
                        selector += 1;
                    }
                } else {
                    list.add(encode_move(from, to, 4));
                }
            }
        }

        // En passant. At most two pawns can ever take, and the legality of
        // the capture is settled later by the discovered-check test.
        if (self.ep_square != NO_SQUARE) {
            var takers = PAWN_ATTACKS.at(color_index(them)).at(self.ep_square) & pawns;
            while (takers != 0) {
                let from = bit_scan_forward(takers);
                takers = clear_lowest_bit(takers);
                list.add(encode_move(from, self.ep_square, 5));
            }
        }
    }

    // Knight moves. The outer loop walks the knights, the inner loop the
    // target squares -- the shape every leaper generator has.
    fn generate_knight_moves(self, ref list: MoveList, mask: int) {
        let us = self.side;
        var knights = self.pieces(us, PieceKind.Knight);
        while (knights != 0) {
            let from = bit_scan_forward(knights);
            knights = clear_lowest_bit(knights);
            var targets = KNIGHT_ATTACKS.at(from) & mask;
            while (targets != 0) {
                let to = bit_scan_forward(targets);
                targets = clear_lowest_bit(targets);
                if (self.is_empty_square(to)) {
                    list.add(encode_move(from, to, 0));
                } else {
                    list.add(encode_move(from, to, 4));
                }
            }
        }
    }

    fn generate_bishop_moves(self, ref list: MoveList, mask: int) {
        let us = self.side;
        var bishops = self.pieces(us, PieceKind.Bishop);
        while (bishops != 0) {
            let from = bit_scan_forward(bishops);
            bishops = clear_lowest_bit(bishops);
            var targets = bishop_attacks(from, self.occupancy) & mask;
            while (targets != 0) {
                let to = bit_scan_forward(targets);
                targets = clear_lowest_bit(targets);
                if (self.is_empty_square(to)) {
                    list.add(encode_move(from, to, 0));
                } else {
                    list.add(encode_move(from, to, 4));
                }
            }
        }
    }

    fn generate_rook_moves(self, ref list: MoveList, mask: int) {
        let us = self.side;
        var rooks = self.pieces(us, PieceKind.Rook);
        while (rooks != 0) {
            let from = bit_scan_forward(rooks);
            rooks = clear_lowest_bit(rooks);
            var targets = rook_attacks(from, self.occupancy) & mask;
            while (targets != 0) {
                let to = bit_scan_forward(targets);
                targets = clear_lowest_bit(targets);
                if (self.is_empty_square(to)) {
                    list.add(encode_move(from, to, 0));
                } else {
                    list.add(encode_move(from, to, 4));
                }
            }
        }
    }

    fn generate_queen_moves(self, ref list: MoveList, mask: int) {
        let us = self.side;
        var queens = self.pieces(us, PieceKind.Queen);
        while (queens != 0) {
            let from = bit_scan_forward(queens);
            queens = clear_lowest_bit(queens);
            var targets = queen_attacks(from, self.occupancy) & mask;
            while (targets != 0) {
                let to = bit_scan_forward(targets);
                targets = clear_lowest_bit(targets);
                if (self.is_empty_square(to)) {
                    list.add(encode_move(from, to, 0));
                } else {
                    list.add(encode_move(from, to, 4));
                }
            }
        }
    }

    fn generate_king_moves(self, ref list: MoveList, mask: int) {
        let from = self.kings.at(color_index(self.side));
        if (from == NO_SQUARE) {
            return;
        }
        var targets = KING_ATTACKS.at(from) & mask;
        while (targets != 0) {
            let to = bit_scan_forward(targets);
            targets = clear_lowest_bit(targets);
            if (self.is_empty_square(to)) {
                list.add(encode_move(from, to, 0));
            } else {
                list.add(encode_move(from, to, 4));
            }
        }
    }

    // Castling. The four rights are checked in one loop over the right bits,
    // with the path tables doing the work that would otherwise be four
    // near-identical blocks.
    fn generate_castling_moves(self, ref list: MoveList) {
        if (self.in_check()) {
            return;
        }
        let us = self.side;
        let them = opposite_color(us);
        var first: int = 0;
        var last: int = 2;
        if (us == Color.Black) {
            first = 2;
            last = 4;
        }
        var index = first;
        while (index < last) {
            let right = 1 << index;
            if ((self.castling & right) == 0) {
                index += 1;
                continue;
            }
            if ((CASTLE_EMPTY_MASK.at(index) & self.occupancy) != 0) {
                index += 1;
                continue;
            }
            // Every square the king passes over, including the one it starts
            // on, must be free of enemy attack.
            var path = CASTLE_SAFE_MASK.at(index);
            var safe: bool = true;
            while (path != 0) {
                let square = bit_scan_forward(path);
                path = clear_lowest_bit(path);
                if (self.is_attacked(square, them)) {
                    safe = false;
                }
            }
            if (safe) {
                var move_kind_bits: int = 2;
                if (index == 1 or index == 3) {
                    move_kind_bits = 3;
                }
                list.add(encode_move(CASTLE_KING_FROM.at(index), CASTLE_KING_TO.at(index), move_kind_bits));
            }
            index += 1;
        }
    }

    // Everything the side to move could play if legality were not checked.
    fn generate_pseudo_legal(self, ref list: MoveList, kind: GenKind) {
        let us = self.side;
        let them = opposite_color(us);
        let ours = self.by_color.at(color_index(us));
        let enemies = self.by_color.at(color_index(them));
        var mask = FULL_BOARD ^ ours;
        if (kind == GenKind.GenCaptures) {
            mask = enemies;
        }
        if (kind == GenKind.GenQuiets) {
            mask = FULL_BOARD ^ self.occupancy;
        }

        self.generate_pawn_moves(list, kind);
        self.generate_knight_moves(list, mask);
        self.generate_bishop_moves(list, mask);
        self.generate_rook_moves(list, mask);
        self.generate_queen_moves(list, mask);
        self.generate_king_moves(list, mask);
        if (kind != GenKind.GenCaptures) {
            self.generate_castling_moves(list);
        }
    }

    // Evasions: when in check, only three things can help. Capture the
    // checker, block the ray (single check only), or move the king.
    fn generate_evasions(self, ref list: MoveList) {
        let us = self.side;
        let king = self.kings.at(color_index(us));
        let checking = self.checkers_of(us);
        let ours = self.by_color.at(color_index(us));

        // The king may always try to run, whatever the number of checkers.
        self.generate_king_moves(list, FULL_BOARD ^ ours);
        if (checking == 0 or has_single_bit(checking) == false) {
            return;
        }

        let checker = bit_scan_forward(checking);
        let target = checking | BETWEEN.at(king).at(checker);
        self.generate_pawn_moves(list, GenKind.GenAll);
        self.generate_knight_moves(list, target);
        self.generate_bishop_moves(list, target);
        self.generate_rook_moves(list, target);
        self.generate_queen_moves(list, target);
    }

    // --- legality ----------------------------------------------------------

    // Is a pseudo-legal move legal? Three cases, in order of frequency:
    //
    //  * a king move must land on a square the enemy does not attack, with
    //    the king itself removed from the occupancy so that it cannot hide
    //    behind its own shadow along a checking ray;
    //  * an en passant capture removes two pieces from one rank at once and
    //    needs the full hypothetical-occupancy test;
    //  * anything else is legal exactly when the mover is not pinned, or is
    //    pinned but stays on its pin line.
    fn is_legal_move(self, m: int) -> bool {
        legality_test_count += 1;
        let us = self.side;
        let them = opposite_color(us);
        let from = move_from(m);
        let to = move_to(m);
        let king = self.kings.at(color_index(us));

        if (from == king) {
            if (move_is_castle(m)) {
                return true;
            }
            let without_king = self.occupancy ^ (1 << from);
            let hypothetical = (without_king | (1 << to)) & (FULL_BOARD ^ (1 << to));
            return self.is_attacked_with(to, them, hypothetical | (1 << to)) == false;
        }

        if (move_is_en_passant(m)) {
            let captured = to - pawn_push_delta(us);
            let hypothetical = (self.occupancy ^ (1 << from) ^ (1 << captured)) | (1 << to);
            var exposed: int = 0;
            exposed |= bishop_attacks(king, hypothetical) & self.diagonal_sliders(them);
            exposed |= rook_attacks(king, hypothetical) & self.straight_sliders(them);
            return exposed == 0;
        }

        let pinned = self.pinned_pieces(us);
        if ((pinned & (1 << from)) == 0) {
            return true;
        }
        return (LINE_THROUGH.at(king).at(from) & (1 << to)) != 0;
    }

    // Full legal generation: generate pseudo-legal moves, then filter. When
    // in check the evasion generator narrows the candidate set first, which
    // is what makes the filter cheap.
    fn generate_legal(self, ref list: MoveList) {
        var pseudo: MoveList;
        if (self.in_check()) {
            self.generate_evasions(pseudo);
        } else {
            self.generate_pseudo_legal(pseudo, GenKind.GenAll);
        }
        var index: int = 0;
        while (index < pseudo.size()) {
            let m = pseudo.packed_at(index);
            if (self.is_legal_move(m)) {
                list.add(m);
            }
            index += 1;
        }
        Destroy(pseudo);
    }

    fn generate_legal_captures(self, ref list: MoveList) {
        var pseudo: MoveList;
        self.generate_pseudo_legal(pseudo, GenKind.GenCaptures);
        var index: int = 0;
        while (index < pseudo.size()) {
            let m = pseudo.packed_at(index);
            if (self.is_legal_move(m)) {
                list.add(m);
            }
            index += 1;
        }
        Destroy(pseudo);
    }

    fn count_legal_moves(self) -> int {
        var list: MoveList;
        self.generate_legal(list);
        let total = list.size();
        Destroy(list);
        return total;
    }

    fn has_legal_move(self) -> bool {
        return self.count_legal_moves() > 0;
    }

    // --- make and unmake ---------------------------------------------------

    // Apply a move, pushing the irreversible state onto the history stack.
    // The order matters: the captured piece comes off before the mover
    // arrives, and the hash is updated as a side effect of each board edit.
    fn make_move(ref self, m: int) {
        make_move_count += 1;
        let us = self.side;
        let them = opposite_color(us);
        let from = move_from(m);
        let to = move_to(m);
        let kind = move_kind(m);
        let mover = self.squares.at(from);

        var record: Undo;
        record.move_packed = m;
        record.castling = self.castling;
        record.ep_square = self.ep_square;
        record.halfmove = self.halfmove;
        record.key = self.key;
        record.captured = PIECE_CODE_COUNT;

        // Retire the old en passant file from the hash before it changes.
        if (self.ep_square != NO_SQUARE) {
            self.key ^= ZOBRIST_EP_FILE.at(file_of(self.ep_square));
        }
        self.key ^= ZOBRIST_CASTLING.at(self.castling & CASTLE_ALL);

        self.ep_square = NO_SQUARE;
        self.halfmove += 1;

        if (kind == 5) {
            // En passant: the captured pawn is not on the target square.
            let victim = to - pawn_push_delta(us);
            record.captured = self.squares.at(victim);
            self.remove_piece(victim);
            self.move_piece(from, to);
            self.halfmove = 0;
        } else {
            if (move_is_capture(m)) {
                record.captured = self.squares.at(to);
                self.remove_piece(to);
                self.halfmove = 0;
            }
            self.move_piece(from, to);
        }

        if (piece_kind(mover) == PieceKind.Pawn) {
            self.halfmove = 0;
        }

        // A double push arms the en passant square for exactly one ply.
        if (kind == 1) {
            self.ep_square = to - pawn_push_delta(us);
        }

        // Promotions swap the pawn for the chosen piece on the target.
        if (move_is_promotion(m)) {
            self.remove_piece(to);
            self.put_piece(make_piece(us, move_promotion_kind(m)), to);
        }

        // Castling moves the rook as a second, silent half-move.
        if (kind == 2 or kind == 3) {
            var index: int = 0;
            if (us == Color.White) {
                if (kind == 3) {
                    index = 1;
                }
            } else {
                index = 2;
                if (kind == 3) {
                    index = 3;
                }
            }
            self.move_piece(CASTLE_ROOK_FROM.at(index), CASTLE_ROOK_TO.at(index));
        }

        self.castling &= CASTLE_MASK.at(from);
        self.castling &= CASTLE_MASK.at(to);
        self.key ^= ZOBRIST_CASTLING.at(self.castling & CASTLE_ALL);
        if (self.ep_square != NO_SQUARE) {
            self.key ^= ZOBRIST_EP_FILE.at(file_of(self.ep_square));
        }
        self.key ^= ZOBRIST_SIDE;

        if (us == Color.Black) {
            self.fullmove += 1;
        }
        self.side = them;
        self.ply += 1;
        self.history.push_back(record);
    }

    // Undo the most recent make_move. Every board edit above is its own
    // inverse, so this is the same sequence run backwards.
    fn unmake_move(ref self) {
        Assert(self.history.size() > 0);
        let record = self.history.back();
        let m = record.move_packed;
        let from = move_from(m);
        let to = move_to(m);
        let kind = move_kind(m);
        let us = opposite_color(self.side);

        self.side = us;
        self.ply -= 1;
        if (us == Color.Black) {
            self.fullmove -= 1;
        }

        // Undo the rook half of a castle first, so the king move below sees
        // the board it expects.
        if (kind == 2 or kind == 3) {
            var index: int = 0;
            if (us == Color.White) {
                if (kind == 3) {
                    index = 1;
                }
            } else {
                index = 2;
                if (kind == 3) {
                    index = 3;
                }
            }
            self.move_piece(CASTLE_ROOK_TO.at(index), CASTLE_ROOK_FROM.at(index));
        }

        // A promoted piece becomes a pawn again before it travels back.
        if (move_is_promotion(m)) {
            self.remove_piece(to);
            self.put_piece(make_piece(us, PieceKind.Pawn), to);
        }

        self.move_piece(to, from);

        if (kind == 5) {
            let victim = to - pawn_push_delta(us);
            self.put_piece(record.captured, victim);
        } else {
            if (record.captured < PIECE_CODE_COUNT) {
                self.put_piece(record.captured, to);
            }
        }

        self.castling = record.castling;
        self.ep_square = record.ep_square;
        self.halfmove = record.halfmove;
        self.key = record.key;
        self.history.pop_back();
    }

    // A null move: pass the turn without touching a piece. The search uses
    // it for null-move pruning; it is illegal in real chess, so the caller
    // must check that the side to move is not in check first.
    fn make_null_move(ref self) {
        var record: Undo;
        record.move_packed = MOVE_NULL;
        record.captured = PIECE_CODE_COUNT;
        record.castling = self.castling;
        record.ep_square = self.ep_square;
        record.halfmove = self.halfmove;
        record.key = self.key;
        if (self.ep_square != NO_SQUARE) {
            self.key ^= ZOBRIST_EP_FILE.at(file_of(self.ep_square));
        }
        self.ep_square = NO_SQUARE;
        self.key ^= ZOBRIST_SIDE;
        self.side = opposite_color(self.side);
        self.halfmove += 1;
        self.ply += 1;
        self.history.push_back(record);
    }

    fn unmake_null_move(ref self) {
        Assert(self.history.size() > 0);
        let record = self.history.back();
        self.side = opposite_color(self.side);
        self.castling = record.castling;
        self.ep_square = record.ep_square;
        self.halfmove = record.halfmove;
        self.key = record.key;
        self.ply -= 1;
        self.history.pop_back();
    }

    // --- terminal states ---------------------------------------------------

    fn is_checkmate(self) -> bool {
        return self.in_check() and self.count_legal_moves() == 0;
    }

    fn is_stalemate(self) -> bool {
        return self.in_check() == false and self.count_legal_moves() == 0;
    }

    fn is_fifty_move_draw(self) -> bool {
        return self.halfmove >= FIFTY_MOVE_PLIES;
    }

    // King versus king, king and minor versus king, and king and bishop
    // versus king and same-coloured bishop are the drawn material configs.
    fn is_insufficient_material(self) -> bool {
        let pawns = self.by_kind.at(kind_index(PieceKind.Pawn));
        let rooks = self.by_kind.at(kind_index(PieceKind.Rook));
        let queens = self.by_kind.at(kind_index(PieceKind.Queen));
        if ((pawns | rooks | queens) != 0) {
            return false;
        }
        let knights = self.by_kind.at(kind_index(PieceKind.Knight));
        let bishops = self.by_kind.at(kind_index(PieceKind.Bishop));
        let minors = knights | bishops;
        if (popcount(minors) <= 1) {
            return true;
        }
        if (knights == 0 and popcount(bishops) == 2) {
            if ((bishops & LIGHT_SQUARES) == bishops) {
                return true;
            }
            if ((bishops & DARK_SQUARES) == bishops) {
                return true;
            }
        }
        return false;
    }

    // Has the current key appeared `limit` times in the history? The scan
    // only needs to look at every second entry, since the side to move must
    // match, and can stop at the last irreversible move.
    fn repetition_count(self) -> int {
        var seen: int = 0;
        var index = self.history.size() - 2;
        var span: int = 0;
        while (index >= 0 and span < self.halfmove) {
            if (self.history.at(index).key == self.key) {
                seen += 1;
            }
            index -= 2;
            span += 2;
        }
        return seen;
    }

    fn result(self) -> GameResult {
        if (self.count_legal_moves() == 0) {
            if (self.in_check()) {
                if (self.side == Color.White) {
                    return GameResult.BlackWins;
                }
                return GameResult.WhiteWins;
            }
            return GameResult.DrawStalemate;
        }
        if (self.is_fifty_move_draw()) {
            return GameResult.DrawFiftyMove;
        }
        if (self.is_insufficient_material()) {
            return GameResult.DrawInsufficient;
        }
        if (self.repetition_count() >= 2) {
            return GameResult.DrawRepetition;
        }
        return GameResult.Ongoing;
    }

    // Does `m` give check, without playing it? The direct case is a table
    // lookup against the enemy king; the discovered case asks whether the
    // origin square was a blocker on some ray to that king.
    fn gives_check(self, m: int) -> bool {
        let us = self.side;
        let them = opposite_color(us);
        let enemy_king = self.kings.at(color_index(them));
        if (enemy_king == NO_SQUARE) {
            return false;
        }
        let from = move_from(m);
        let to = move_to(m);
        var kind = piece_kind(self.squares.at(from));
        if (move_is_promotion(m)) {
            kind = move_promotion_kind(m);
        }
        let after = (self.occupancy ^ (1 << from)) | (1 << to);
        if ((attacks_of_kind(kind, us, to, after) & (1 << enemy_king)) != 0) {
            return true;
        }
        let candidates = self.discovery_candidates(us);
        if ((candidates & (1 << from)) == 0) {
            return false;
        }
        return (LINE_THROUGH.at(enemy_king).at(from) & (1 << to)) == 0;
    }

    // The cheapest attacker of `square` belonging to `color`, as a bit. The
    // static exchange loop pops these one at a time.
    fn least_valuable_attacker(self, attackers: int, color: Color) -> int {
        var kind: int = 0;
        while (kind < PIECE_KIND_COUNT) {
            let subset = attackers
                & self.by_color.at(color_index(color))
                & self.by_kind.at(kind);
            if (subset != 0) {
                return lowest_bit(subset);
            }
            kind += 1;
        }
        return 0;
    }

    // --- fields ------------------------------------------------------------

    // The mailbox: one piece code per square, PIECE_CODE_COUNT for empty.
    var squares: vector(int);
    // Two colour unions and six kind unions; a piece board is their and.
    var by_color: vector(int);
    var by_kind: vector(int);
    // The union of both colours, kept in step with every board edit.
    var occupancy: int = 0;
    // Cached king squares, because is_attacked wants them on every node.
    var kings: vector(int);
    var side: Color = Color.White;
    var castling: int = 0;
    var ep_square: int = -1;
    var halfmove: int = 0;
    var fullmove: int = 1;
    var key: int = 0;
    var ply: int = 0;
    var history: vector(Undo);
}

// --- Notation -------------------------------------------------------------

// 'a'..'h' for a file index, '1'..'8' for a rank index.
fn file_char(file: int) -> char {
    return CHAR_LOWER_A + file;
}

fn rank_char(rank: int) -> char {
    return CHAR_ZERO + 1 + rank;
}

fn char_to_file(c: char) -> int {
    return c - CHAR_LOWER_A;
}

fn char_to_rank(c: char) -> int {
    return c - CHAR_ZERO - 1;
}

// Coordinate name of a square: "e4", "a1", or "-" for the null square.
fn square_name(square: int) -> string {
    var out: string;
    if (square == NO_SQUARE) {
        out.push_back(CHAR_DASH);
        return out;
    }
    out.push_back(file_char(file_of(square)));
    out.push_back(rank_char(rank_of(square)));
    return out;
}

fn square_from_name(name: string) -> int {
    if (name.size() < 2) {
        return NO_SQUARE;
    }
    let file = char_to_file(name.at(0));
    let rank = char_to_rank(name.at(1));
    if (file < 0 or file >= BOARD_FILES or rank < 0 or rank >= BOARD_RANKS) {
        return NO_SQUARE;
    }
    return make_square(file, rank);
}

// The lowercase letter for a piece kind, as used by FEN and by SAN.
fn kind_char(kind: PieceKind) -> char {
    if (kind == PieceKind.Pawn) {
        return 'p';
    } else { if (kind == PieceKind.Knight) {
        return 'n';
    } else { if (kind == PieceKind.Bishop) {
        return 'b';
    } else { if (kind == PieceKind.Rook) {
        return 'r';
    } else { if (kind == PieceKind.Queen) {
        return 'q';
    } else { if (kind == PieceKind.King) {
        return 'k';
    } else {
        return '?';
    } } } } } }
}

fn char_to_kind(c: char) -> PieceKind {
    var lower = c;
    if (lower >= CHAR_UPPER_A and lower < CHAR_UPPER_A + 26) {
        lower = lower | CASE_BIT;
    }
    if (lower == 'p') {
        return PieceKind.Pawn;
    } else { if (lower == 'n') {
        return PieceKind.Knight;
    } else { if (lower == 'b') {
        return PieceKind.Bishop;
    } else { if (lower == 'r') {
        return PieceKind.Rook;
    } else { if (lower == 'q') {
        return PieceKind.Queen;
    } else { if (lower == 'k') {
        return PieceKind.King;
    } else {
        return PieceKind.KindNone;
    } } } } } }
}

// The FEN letter for a piece code: uppercase for white, lowercase for black.
fn piece_char(piece: int) -> char {
    if (piece_is_empty(piece)) {
        return '.';
    }
    let letter = kind_char(piece_kind(piece));
    if (piece_color(piece) == Color.White) {
        return letter & (FULL_BOARD ^ CASE_BIT);
    }
    return letter;
}

// Long algebraic (UCI) form: "e2e4", "e7e8q", "e1g1" for a castle.
fn move_to_uci(m: int) -> string {
    var out: string;
    if (move_is_null(m)) {
        out.append("0000");
        return out;
    }
    let from = move_from(m);
    let to = move_to(m);
    out.push_back(file_char(file_of(from)));
    out.push_back(rank_char(rank_of(from)));
    out.push_back(file_char(file_of(to)));
    out.push_back(rank_char(rank_of(to)));
    if (move_is_promotion(m)) {
        out.push_back(kind_char(move_promotion_kind(m)));
    }
    return out;
}

// Parse a UCI move against a position, so that the promotion suffix and the
// capture and castle flags can be reconstructed. Returns MOVE_NULL when the
// text does not name a legal move.
fn move_from_uci(board: Position, text: string) -> int {
    if (text.size() < 4) {
        return MOVE_NULL;
    }
    let from = make_square(char_to_file(text.at(0)), char_to_rank(text.at(1)));
    let to = make_square(char_to_file(text.at(2)), char_to_rank(text.at(3)));
    var promotion = PieceKind.KindNone;
    if (text.size() >= 5) {
        promotion = char_to_kind(text.at(4));
    }

    var list: MoveList;
    board.generate_legal(list);
    var index: int = 0;
    var found: int = MOVE_NULL;
    while (index < list.size()) {
        let m = list.packed_at(index);
        if (move_from(m) == from and move_to(m) == to) {
            if (move_is_promotion(m) == false and promotion == PieceKind.KindNone) {
                found = m;
            }
            if (move_is_promotion(m) and move_promotion_kind(m) == promotion) {
                found = m;
            }
        }
        index += 1;
    }
    Destroy(list);
    return found;
}

// Standard algebraic notation. The disambiguation rules are the fiddly part:
// when two identical pieces can reach the same square, the file is added,
// then the rank, then both.
fn move_to_san(board: Position, m: int) -> string {
    var out: string;
    if (move_is_king_castle(m)) {
        out.append("O-O");
        return out;
    }
    if (move_is_castle(m)) {
        out.append("O-O-O");
        return out;
    }

    let from = move_from(m);
    let to = move_to(m);
    let mover = board.piece_at(from);
    let kind = piece_kind(mover);

    if (kind == PieceKind.Pawn) {
        if (move_is_capture(m)) {
            out.push_back(file_char(file_of(from)));
            out.push_back('x');
        }
        out.push_back(file_char(file_of(to)));
        out.push_back(rank_char(rank_of(to)));
        if (move_is_promotion(m)) {
            out.push_back('=');
            out.push_back(kind_char(move_promotion_kind(m)) & (FULL_BOARD ^ CASE_BIT));
        }
        return out;
    }

    out.push_back(kind_char(kind) & (FULL_BOARD ^ CASE_BIT));

    // Find every other piece of the same kind that could also play to `to`.
    var same_file: bool = false;
    var same_rank: bool = false;
    var ambiguous: bool = false;
    var list: MoveList;
    board.generate_legal(list);
    var index: int = 0;
    while (index < list.size()) {
        let other = list.packed_at(index);
        let other_from = move_from(other);
        if (other_from != from and move_to(other) == to) {
            if (piece_kind(board.piece_at(other_from)) == kind) {
                ambiguous = true;
                if (file_of(other_from) == file_of(from)) {
                    same_file = true;
                }
                if (rank_of(other_from) == rank_of(from)) {
                    same_rank = true;
                }
            }
        }
        index += 1;
    }
    Destroy(list);

    if (ambiguous) {
        if (same_file == false) {
            out.push_back(file_char(file_of(from)));
        } else {
            if (same_rank == false) {
                out.push_back(rank_char(rank_of(from)));
            } else {
                out.push_back(file_char(file_of(from)));
                out.push_back(rank_char(rank_of(from)));
            }
        }
    }

    if (move_is_capture(m)) {
        out.push_back('x');
    }
    out.push_back(file_char(file_of(to)));
    out.push_back(rank_char(rank_of(to)));

    if (board.gives_check(m)) {
        out.push_back('+');
    }
    return out;
}

// --- FEN ------------------------------------------------------------------

// The standard start position, and a couple of the positions whose perft
// numbers everyone checks a new generator against.
let START_FEN: string = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
let KIWIPETE_FEN: string = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
let ENDGAME_FEN: string = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
let PROMOTION_FEN: string = "n1n5/PPPk4/8/8/8/8/4Kppp/5N1N b - - 0 1";

// Parse a FEN string into `board`. The six fields are walked with a single
// cursor rather than being split first, because the language has no string
// splitting in its standard surface.
fn parse_fen(ref board: Position, fen: string) -> bool {
    board.reset();

    var cursor: int = 0;
    let length = fen.size();

    // Field one: the piece placement, rank eight first, files a to h.
    var rank: int = BOARD_RANKS - 1;
    var file: int = 0;
    while (cursor < length) {
        let c = fen.at(cursor);
        if (c == CHAR_SPACE) {
            cursor += 1;
            break;
        }
        if (c == CHAR_SLASH) {
            rank -= 1;
            file = 0;
            cursor += 1;
            continue;
        }
        if (c >= CHAR_ZERO and c <= CHAR_NINE) {
            file += c - CHAR_ZERO;
            cursor += 1;
            continue;
        }
        let kind = char_to_kind(c);
        if (kind == PieceKind.KindNone) {
            return false;
        }
        var color = Color.Black;
        if (c < CHAR_LOWER_A) {
            color = Color.White;
        }
        if (rank < 0 or rank >= BOARD_RANKS or file < 0 or file >= BOARD_FILES) {
            return false;
        }
        board.put_piece(make_piece(color, kind), make_square(file, rank));
        file += 1;
        cursor += 1;
    }

    // Field two: the side to move.
    if (cursor < length) {
        if (fen.at(cursor) == 'b') {
            board.side = Color.Black;
        } else {
            board.side = Color.White;
        }
        cursor += 1;
    }
    while (cursor < length and fen.at(cursor) == CHAR_SPACE) {
        cursor += 1;
    }

    // Field three: the castling rights, in KQkq order or a bare dash.
    board.castling = CASTLE_NONE;
    while (cursor < length) {
        let c = fen.at(cursor);
        if (c == CHAR_SPACE) {
            cursor += 1;
            break;
        }
        if (c == 'K') {
            board.castling |= CASTLE_WHITE_KING;
        }
        if (c == 'Q') {
            board.castling |= CASTLE_WHITE_QUEEN;
        }
        if (c == 'k') {
            board.castling |= CASTLE_BLACK_KING;
        }
        if (c == 'q') {
            board.castling |= CASTLE_BLACK_QUEEN;
        }
        cursor += 1;
    }

    // Field four: the en passant target square.
    board.ep_square = NO_SQUARE;
    if (cursor < length) {
        let c = fen.at(cursor);
        if (c == CHAR_DASH) {
            cursor += 2;
        } else {
            if (cursor + 1 < length) {
                board.ep_square = make_square(char_to_file(c), char_to_rank(fen.at(cursor + 1)));
            }
            cursor += 3;
        }
    }

    // Fields five and six: the halfmove clock and the fullmove number.
    var halfmove: int = 0;
    while (cursor < length and fen.at(cursor) >= CHAR_ZERO and fen.at(cursor) <= CHAR_NINE) {
        halfmove = halfmove * 10 + (fen.at(cursor) - CHAR_ZERO);
        cursor += 1;
    }
    board.halfmove = halfmove;
    while (cursor < length and fen.at(cursor) == CHAR_SPACE) {
        cursor += 1;
    }

    var fullmove: int = 0;
    while (cursor < length and fen.at(cursor) >= CHAR_ZERO and fen.at(cursor) <= CHAR_NINE) {
        fullmove = fullmove * 10 + (fen.at(cursor) - CHAR_ZERO);
        cursor += 1;
    }
    if (fullmove == 0) {
        fullmove = 1;
    }
    board.fullmove = fullmove;
    board.key = board.compute_key();
    return true;
}

// Serialise a position back to FEN. The run-length encoding of empty squares
// is the only interesting part.
fn to_fen(board: Position) -> string {
    var out: string;
    var rank: int = BOARD_RANKS - 1;
    while (rank >= 0) {
        var empty_run: int = 0;
        var file: int = 0;
        while (file < BOARD_FILES) {
            let piece = board.piece_at(make_square(file, rank));
            if (piece_is_empty(piece)) {
                empty_run += 1;
            } else {
                if (empty_run > 0) {
                    out.push_back(CHAR_ZERO + empty_run);
                    empty_run = 0;
                }
                out.push_back(piece_char(piece));
            }
            file += 1;
        }
        if (empty_run > 0) {
            out.push_back(CHAR_ZERO + empty_run);
        }
        if (rank > 0) {
            out.push_back(CHAR_SLASH);
        }
        rank -= 1;
    }

    out.push_back(CHAR_SPACE);
    if (board.side_to_move() == Color.White) {
        out.push_back('w');
    } else {
        out.push_back('b');
    }

    out.push_back(CHAR_SPACE);
    let rights = board.castling_rights();
    if (rights == CASTLE_NONE) {
        out.push_back(CHAR_DASH);
    } else {
        if ((rights & CASTLE_WHITE_KING) != 0) {
            out.push_back('K');
        }
        if ((rights & CASTLE_WHITE_QUEEN) != 0) {
            out.push_back('Q');
        }
        if ((rights & CASTLE_BLACK_KING) != 0) {
            out.push_back('k');
        }
        if ((rights & CASTLE_BLACK_QUEEN) != 0) {
            out.push_back('q');
        }
    }

    out.push_back(CHAR_SPACE);
    out.append(square_name(board.en_passant_square()));
    out.push_back(CHAR_SPACE);
    out.append(to_string(board.halfmove_clock()));
    out.push_back(CHAR_SPACE);
    out.append(to_string(board.fullmove_number()));
    return out;
}

// An ASCII diagram of the board, rank eight at the top.
fn board_to_text(board: Position) -> string {
    var out: string;
    var rank: int = BOARD_RANKS - 1;
    while (rank >= 0) {
        out.push_back(rank_char(rank));
        out.push_back(CHAR_SPACE);
        var file: int = 0;
        while (file < BOARD_FILES) {
            out.push_back(piece_char(board.piece_at(make_square(file, rank))));
            out.push_back(CHAR_SPACE);
            file += 1;
        }
        out.push_back('\n');
        rank -= 1;
    }
    out.append("  a b c d e f g h\n");
    return out;
}

// --- Perft ----------------------------------------------------------------

// Node counts per depth, so the driver can print a table.
var perft_node_counts: vector(int) = vector(int)(16);
var perft_capture_counts: vector(int) = vector(int)(16);
var perft_ep_counts: vector(int) = vector(int)(16);
var perft_castle_counts: vector(int) = vector(int)(16);
var perft_promotion_counts: vector(int) = vector(int)(16);
var perft_check_counts: vector(int) = vector(int)(16);

fn reset_perft_counters() {
    var depth: int = 0;
    while (depth < 16) {
        perft_node_counts.at(depth) = 0;
        perft_capture_counts.at(depth) = 0;
        perft_ep_counts.at(depth) = 0;
        perft_castle_counts.at(depth) = 0;
        perft_promotion_counts.at(depth) = 0;
        perft_check_counts.at(depth) = 0;
        depth += 1;
    }
}

// The plain leaf counter. At depth one the number of legal moves *is* the
// answer, which saves a whole ply of make/unmake.
fn perft(ref board: Position, depth: int) -> int {
    if (depth == 0) {
        return 1;
    }
    var list: MoveList;
    board.generate_legal(list);
    if (depth == 1) {
        let leaves = list.size();
        Destroy(list);
        return leaves;
    }
    var nodes: int = 0;
    var index: int = 0;
    while (index < list.size()) {
        let m = list.packed_at(index);
        board.make_move(m);
        nodes += perft(board, depth - 1);
        board.unmake_move();
        index += 1;
    }
    Destroy(list);
    return nodes;
}

// The instrumented variant, which also fills the per-depth breakdown that a
// generator bug shows up in long before the node total does.
fn perft_detailed(ref board: Position, depth: int, ply: int) -> int {
    if (depth == 0) {
        perft_node_counts.at(ply) += 1;
        return 1;
    }
    var list: MoveList;
    board.generate_legal(list);
    var nodes: int = 0;
    var index: int = 0;
    while (index < list.size()) {
        let m = list.packed_at(index);
        if (move_is_capture(m)) {
            perft_capture_counts.at(ply) += 1;
        }
        if (move_is_en_passant(m)) {
            perft_ep_counts.at(ply) += 1;
        }
        if (move_is_castle(m)) {
            perft_castle_counts.at(ply) += 1;
        }
        if (move_is_promotion(m)) {
            perft_promotion_counts.at(ply) += 1;
        }
        if (board.gives_check(m)) {
            perft_check_counts.at(ply) += 1;
        }
        board.make_move(m);
        nodes += perft_detailed(board, depth - 1, ply + 1);
        board.unmake_move();
        index += 1;
    }
    Destroy(list);
    perft_node_counts.at(ply) += nodes;
    return nodes;
}

// Per-root-move node counts, the standard way to bisect a generator bug
// against a reference implementation.
fn perft_divide(ref board: Position, depth: int) {
    var list: MoveList;
    board.generate_legal(list);
    var total: int = 0;
    var index: int = 0;
    while (index < list.size()) {
        let m = list.packed_at(index);
        board.make_move(m);
        let nodes = perft(board, depth - 1);
        board.unmake_move();
        total += nodes;

        var line: string;
        line.append(move_to_uci(m));
        line.append(": ");
        line.append(to_string(nodes));
        println(line);
        Destroy(line);
        index += 1;
    }
    Destroy(list);

    var summary: string;
    summary.append("total: ");
    summary.append(to_string(total));
    println(summary);
    Destroy(summary);
}

// Make and unmake every legal move, asserting the position is byte for byte
// what it was. This catches an unmake bug immediately instead of at the next
// perft mismatch twelve plies deeper.
fn verify_make_unmake(ref board: Position) -> bool {
    let key_before = board.hash_key();
    let occupancy_before = board.occupied();
    let castling_before = board.castling_rights();

    var list: MoveList;
    board.generate_legal(list);
    var index: int = 0;
    var ok: bool = true;
    while (index < list.size()) {
        let m = list.packed_at(index);
        board.make_move(m);
        board.unmake_move();
        if (board.hash_key() != key_before) {
            ok = false;
        }
        if (board.occupied() != occupancy_before) {
            ok = false;
        }
        if (board.castling_rights() != castling_before) {
            ok = false;
        }
        index += 1;
    }
    Destroy(list);
    Assert(board.hash_key() == key_before);
    Prove(board.occupied() == occupancy_before);
    return ok;
}

// --- Evaluation tables ----------------------------------------------------

// Centipawn material values, middlegame and endgame. A king is scored as
// zero: both sides always have exactly one, so any value cancels.
var MATERIAL_MG: vector(int) = vector(int)(PIECE_KIND_COUNT);
var MATERIAL_EG: vector(int) = vector(int)(PIECE_KIND_COUNT);

// Phase weights: the game is "endgame" once the weighted sum of the
// remaining pieces falls to zero. A full board sums to PHASE_TOTAL.
var PHASE_WEIGHT: vector(int) = vector(int)(PIECE_KIND_COUNT);
let PHASE_TOTAL: int = 24;

// Piece-square tables, one per kind per phase, indexed from white's point of
// view. Black reads them through flip_rank.
var PST_MG: vector(vector(int)) = vector(vector(int))(PIECE_KIND_COUNT);
var PST_EG: vector(vector(int)) = vector(vector(int))(PIECE_KIND_COUNT);

// PASSED_MASK[c][s] is every square that could hold an enemy pawn stopping
// the pawn on s: the three files around s, from the next rank forward.
var PASSED_MASK: vector(vector(int)) = vector(vector(int))(COLOR_COUNT);

// The king zone is the king's square plus its eight neighbours plus the
// three squares two ranks ahead, which is where an attack builds up.
var KING_ZONE: vector(vector(int)) = vector(vector(int))(COLOR_COUNT);

// Bonus per attacker weight, saturating at the end of the table.
var KING_ATTACK_SCALE: vector(int) = vector(int)(64);

// Structural bonuses and penalties, all in centipawns.
let BONUS_BISHOP_PAIR: int = 45;
let BONUS_ROOK_OPEN_FILE: int = 28;
let BONUS_ROOK_SEMI_OPEN: int = 14;
let BONUS_PASSED_BASE: int = 12;
let BONUS_PASSED_RANK: int = 14;
let BONUS_CONNECTED_PAWN: int = 8;
let PENALTY_DOUBLED_PAWN: int = -18;
let PENALTY_ISOLATED_PAWN: int = -14;
let PENALTY_BACKWARD_PAWN: int = -9;
let PENALTY_KING_EXPOSED: int = -22;
let MOBILITY_WEIGHT_MG: int = 4;
let MOBILITY_WEIGHT_EG: int = 2;
let TEMPO_BONUS: int = 12;

// Write one rank of a piece-square table. The eight values are listed file a
// to file h, and the rank argument is the white-side rank, so the tables can
// be typed in the order a human draws a board.
fn set_pst_rank(ref table: vector(int), rank: int, v0: int, v1: int, v2: int, v3: int, v4: int, v5: int, v6: int, v7: int) {
    table.at(make_square(0, rank)) = v0;
    table.at(make_square(1, rank)) = v1;
    table.at(make_square(2, rank)) = v2;
    table.at(make_square(3, rank)) = v3;
    table.at(make_square(4, rank)) = v4;
    table.at(make_square(5, rank)) = v5;
    table.at(make_square(6, rank)) = v6;
    table.at(make_square(7, rank)) = v7;
}

fn init_material() {
    MATERIAL_MG.at(0) = 82;
    MATERIAL_MG.at(1) = 337;
    MATERIAL_MG.at(2) = 365;
    MATERIAL_MG.at(3) = 477;
    MATERIAL_MG.at(4) = 1025;
    MATERIAL_MG.at(5) = 0;

    MATERIAL_EG.at(0) = 94;
    MATERIAL_EG.at(1) = 281;
    MATERIAL_EG.at(2) = 297;
    MATERIAL_EG.at(3) = 512;
    MATERIAL_EG.at(4) = 936;
    MATERIAL_EG.at(5) = 0;

    PHASE_WEIGHT.at(0) = 0;
    PHASE_WEIGHT.at(1) = 1;
    PHASE_WEIGHT.at(2) = 1;
    PHASE_WEIGHT.at(3) = 2;
    PHASE_WEIGHT.at(4) = 4;
    PHASE_WEIGHT.at(5) = 0;
}

// The middlegame piece-square tables. Rank 0 is white's first rank.
fn init_pst_middlegame() {
    var kind: int = 0;
    while (kind < PIECE_KIND_COUNT) {
        PST_MG.at(kind) = vector(int)(SQUARE_COUNT);
        kind += 1;
    }

    // Pawns: reward the centre, punish the f and g pawns for wandering.
    set_pst_rank(PST_MG.at(0), 0, 0, 0, 0, 0, 0, 0, 0, 0);
    set_pst_rank(PST_MG.at(0), 1, -35, -1, -20, -23, -15, 24, 38, -22);
    set_pst_rank(PST_MG.at(0), 2, -26, -4, -4, -10, 3, 3, 33, -12);
    set_pst_rank(PST_MG.at(0), 3, -27, -2, -5, 12, 17, 6, 10, -25);
    set_pst_rank(PST_MG.at(0), 4, -14, 13, 6, 21, 23, 12, 17, -23);
    set_pst_rank(PST_MG.at(0), 5, -6, 7, 26, 31, 65, 56, 25, -20);
    set_pst_rank(PST_MG.at(0), 6, 98, 134, 61, 95, 68, 126, 34, -11);
    set_pst_rank(PST_MG.at(0), 7, 0, 0, 0, 0, 0, 0, 0, 0);

    // Knights: strongly centralised, miserable on the rim.
    set_pst_rank(PST_MG.at(1), 0, -105, -21, -58, -33, -17, -28, -19, -23);
    set_pst_rank(PST_MG.at(1), 1, -29, -53, -12, -3, -1, 18, -14, -19);
    set_pst_rank(PST_MG.at(1), 2, -23, -9, 12, 10, 19, 17, 25, -16);
    set_pst_rank(PST_MG.at(1), 3, -13, 4, 16, 13, 28, 19, 21, -8);
    set_pst_rank(PST_MG.at(1), 4, -9, 17, 19, 53, 37, 69, 18, 22);
    set_pst_rank(PST_MG.at(1), 5, -47, 60, 37, 65, 84, 129, 73, 44);
    set_pst_rank(PST_MG.at(1), 6, -73, -41, 72, 36, 23, 62, 7, -17);
    set_pst_rank(PST_MG.at(1), 7, -167, -89, -34, -49, 61, -97, -15, -107);

    // Bishops: long diagonals and the two development squares.
    set_pst_rank(PST_MG.at(2), 0, -33, -3, -14, -21, -13, -12, -39, -21);
    set_pst_rank(PST_MG.at(2), 1, 4, 15, 16, 0, 7, 21, 33, 1);
    set_pst_rank(PST_MG.at(2), 2, 0, 15, 15, 15, 14, 27, 18, 10);
    set_pst_rank(PST_MG.at(2), 3, -6, 13, 13, 26, 34, 12, 10, 4);
    set_pst_rank(PST_MG.at(2), 4, -4, 5, 19, 50, 37, 37, 7, -2);
    set_pst_rank(PST_MG.at(2), 5, -16, 37, 43, 40, 35, 50, 37, -2);
    set_pst_rank(PST_MG.at(2), 6, -26, 16, -18, -13, 30, 59, 18, -47);
    set_pst_rank(PST_MG.at(2), 7, -29, 4, -82, -37, -25, -42, 7, -8);

    // Rooks: the seventh rank and the central files.
    set_pst_rank(PST_MG.at(3), 0, -19, -13, 1, 17, 16, 7, -37, -26);
    set_pst_rank(PST_MG.at(3), 1, -44, -16, -20, -9, -1, 11, -6, -71);
    set_pst_rank(PST_MG.at(3), 2, -45, -25, -16, -17, 3, 0, -5, -33);
    set_pst_rank(PST_MG.at(3), 3, -36, -26, -12, -1, 9, -7, 6, -23);
    set_pst_rank(PST_MG.at(3), 4, -24, -11, 7, 26, 24, 35, -8, -20);
    set_pst_rank(PST_MG.at(3), 5, -5, 19, 26, 36, 17, 45, 61, 16);
    set_pst_rank(PST_MG.at(3), 6, 27, 32, 58, 62, 80, 67, 26, 44);
    set_pst_rank(PST_MG.at(3), 7, 32, 42, 32, 51, 63, 9, 31, 43);

    // Queens: mild centralisation, a penalty for coming out early.
    set_pst_rank(PST_MG.at(4), 0, -1, -18, -9, 10, -15, -25, -31, -50);
    set_pst_rank(PST_MG.at(4), 1, -35, -8, 11, 2, 8, 15, -3, 1);
    set_pst_rank(PST_MG.at(4), 2, -14, 2, -11, -2, -5, 2, 14, 5);
    set_pst_rank(PST_MG.at(4), 3, -9, -26, -9, -10, -2, -4, 3, -3);
    set_pst_rank(PST_MG.at(4), 4, -27, -27, -16, -16, -1, 17, -2, 1);
    set_pst_rank(PST_MG.at(4), 5, -13, -17, 7, 8, 29, 56, 47, 57);
    set_pst_rank(PST_MG.at(4), 6, -24, -39, -5, 1, -16, 57, 28, 54);
    set_pst_rank(PST_MG.at(4), 7, -28, 0, 29, 12, 59, 44, 43, 45);

    // Kings: stay home and castle.
    set_pst_rank(PST_MG.at(5), 0, -15, 36, 12, -54, 8, -28, 24, 14);
    set_pst_rank(PST_MG.at(5), 1, 1, 7, -8, -64, -43, -16, 9, 8);
    set_pst_rank(PST_MG.at(5), 2, -14, -14, -22, -46, -44, -30, -15, -27);
    set_pst_rank(PST_MG.at(5), 3, -49, -1, -27, -39, -46, -44, -33, -51);
    set_pst_rank(PST_MG.at(5), 4, -17, -20, -12, -27, -30, -25, -14, -36);
    set_pst_rank(PST_MG.at(5), 5, -9, 24, 2, -16, -20, 6, 22, -22);
    set_pst_rank(PST_MG.at(5), 6, 29, -1, -20, -7, -8, -4, -38, -29);
    set_pst_rank(PST_MG.at(5), 7, -65, 23, 16, -15, -56, -34, 2, 13);
}

// The endgame tables. Pawns matter more, kings march to the centre.
fn init_pst_endgame() {
    var kind: int = 0;
    while (kind < PIECE_KIND_COUNT) {
        PST_EG.at(kind) = vector(int)(SQUARE_COUNT);
        kind += 1;
    }

    set_pst_rank(PST_EG.at(0), 0, 0, 0, 0, 0, 0, 0, 0, 0);
    set_pst_rank(PST_EG.at(0), 1, 13, 8, 8, 10, 13, 0, 2, -7);
    set_pst_rank(PST_EG.at(0), 2, 4, 7, -6, 1, 0, -5, -1, -8);
    set_pst_rank(PST_EG.at(0), 3, 13, 9, -3, -7, -7, -8, 3, -1);
    set_pst_rank(PST_EG.at(0), 4, 32, 24, 13, 5, -2, 4, 17, 17);
    set_pst_rank(PST_EG.at(0), 5, 94, 100, 85, 67, 56, 53, 82, 84);
    set_pst_rank(PST_EG.at(0), 6, 178, 173, 158, 134, 147, 132, 165, 187);
    set_pst_rank(PST_EG.at(0), 7, 0, 0, 0, 0, 0, 0, 0, 0);

    set_pst_rank(PST_EG.at(1), 0, -29, -51, -23, -15, -22, -18, -50, -64);
    set_pst_rank(PST_EG.at(1), 1, -42, -20, -10, -5, -2, -20, -23, -44);
    set_pst_rank(PST_EG.at(1), 2, -23, -3, -1, 15, 10, -3, -20, -22);
    set_pst_rank(PST_EG.at(1), 3, -18, -6, 16, 25, 16, 17, 4, -18);
    set_pst_rank(PST_EG.at(1), 4, -17, 3, 22, 22, 22, 11, 8, -18);
    set_pst_rank(PST_EG.at(1), 5, -24, -20, 10, 9, -1, -9, -19, -41);
    set_pst_rank(PST_EG.at(1), 6, -25, -8, -25, -2, -9, -25, -24, -52);
    set_pst_rank(PST_EG.at(1), 7, -58, -38, -13, -28, -31, -27, -63, -99);

    set_pst_rank(PST_EG.at(2), 0, -23, -9, -23, -5, -9, -16, -5, -17);
    set_pst_rank(PST_EG.at(2), 1, -14, -18, -7, -1, 4, -9, -15, -27);
    set_pst_rank(PST_EG.at(2), 2, -12, -3, 8, 10, 13, 3, -7, -15);
    set_pst_rank(PST_EG.at(2), 3, -6, 3, 13, 19, 7, 10, -3, -9);
    set_pst_rank(PST_EG.at(2), 4, -3, 9, 12, 9, 14, 10, 3, 2);
    set_pst_rank(PST_EG.at(2), 5, 2, -8, 0, -1, -2, 6, 0, 4);
    set_pst_rank(PST_EG.at(2), 6, -8, -4, 7, -12, -3, -13, -4, -14);
    set_pst_rank(PST_EG.at(2), 7, -14, -21, -11, -8, -7, -9, -17, -24);

    set_pst_rank(PST_EG.at(3), 0, -9, 2, 3, -1, -5, -13, 4, -20);
    set_pst_rank(PST_EG.at(3), 1, -6, -6, 0, 2, -9, -9, -11, -3);
    set_pst_rank(PST_EG.at(3), 2, -4, 0, -5, -1, -7, -12, -8, -16);
    set_pst_rank(PST_EG.at(3), 3, 3, 5, 8, 4, -5, -6, -8, -11);
    set_pst_rank(PST_EG.at(3), 4, 4, 3, 13, 1, 2, 1, -1, 2);
    set_pst_rank(PST_EG.at(3), 5, 7, 7, 7, 5, 4, -3, -5, -3);
    set_pst_rank(PST_EG.at(3), 6, 11, 13, 13, 11, -3, 3, 8, 3);
    set_pst_rank(PST_EG.at(3), 7, 13, 10, 18, 15, 12, 12, 8, 5);

    set_pst_rank(PST_EG.at(4), 0, -33, -28, -22, -43, -5, -32, -20, -41);
    set_pst_rank(PST_EG.at(4), 1, -22, -23, -30, -16, -16, -23, -36, -32);
    set_pst_rank(PST_EG.at(4), 2, -16, -27, 15, 6, 9, 17, 10, 5);
    set_pst_rank(PST_EG.at(4), 3, -18, 28, 19, 47, 31, 34, 39, 23);
    set_pst_rank(PST_EG.at(4), 4, 3, 22, 24, 45, 57, 40, 57, 36);
    set_pst_rank(PST_EG.at(4), 5, -20, 6, 9, 49, 47, 35, 19, 9);
    set_pst_rank(PST_EG.at(4), 6, -17, 20, 32, 41, 58, 25, 30, 0);
    set_pst_rank(PST_EG.at(4), 7, -9, 22, 22, 27, 27, 19, 10, 20);

    set_pst_rank(PST_EG.at(5), 0, -53, -34, -21, -11, -28, -14, -24, -43);
    set_pst_rank(PST_EG.at(5), 1, -27, -11, 4, 13, 14, 4, -5, -17);
    set_pst_rank(PST_EG.at(5), 2, -19, -3, 11, 21, 23, 16, 7, -9);
    set_pst_rank(PST_EG.at(5), 3, -18, -4, 21, 24, 27, 23, 9, -11);
    set_pst_rank(PST_EG.at(5), 4, -8, 22, 24, 27, 26, 33, 26, 3);
    set_pst_rank(PST_EG.at(5), 5, 10, 17, 23, 15, 20, 45, 44, 13);
    set_pst_rank(PST_EG.at(5), 6, -12, 17, 14, 17, 17, 38, 23, 11);
    set_pst_rank(PST_EG.at(5), 7, -74, -35, -18, -18, -11, 15, 4, -17);
}

// Passed pawn and king zone masks, both built by walking files and ranks.
fn init_evaluation_masks() {
    var color: int = 0;
    while (color < COLOR_COUNT) {
        PASSED_MASK.at(color) = vector(int)(SQUARE_COUNT);
        KING_ZONE.at(color) = vector(int)(SQUARE_COUNT);
        color += 1;
    }

    var side: int = 0;
    while (side < COLOR_COUNT) {
        var forward: int = 1;
        if (side == 1) {
            forward = -1;
        }
        var rank: int = 0;
        while (rank < BOARD_RANKS) {
            var file: int = 0;
            while (file < BOARD_FILES) {
                let square = make_square(file, rank);

                // Every square on the three files ahead of the pawn.
                var passed: int = 0;
                var scan = rank + forward;
                while (scan >= 0 and scan < BOARD_RANKS) {
                    var neighbour = file - 1;
                    while (neighbour <= file + 1) {
                        if (neighbour >= 0 and neighbour < BOARD_FILES) {
                            passed |= 1 << make_square(neighbour, scan);
                        }
                        neighbour += 1;
                    }
                    scan += forward;
                }
                PASSED_MASK.at(side).at(square) = passed;

                // The king's neighbourhood plus the shelter in front of it.
                var zone = KING_ATTACKS.at(square) | (1 << square);
                let ahead = rank + forward + forward;
                if (ahead >= 0 and ahead < BOARD_RANKS) {
                    var neighbour = file - 1;
                    while (neighbour <= file + 1) {
                        if (neighbour >= 0 and neighbour < BOARD_FILES) {
                            zone |= 1 << make_square(neighbour, ahead);
                        }
                        neighbour += 1;
                    }
                }
                KING_ZONE.at(side).at(square) = zone;

                file += 1;
            }
            rank += 1;
        }
        side += 1;
    }

    // A quadratic ramp that saturates, so that a fourth attacker adds less
    // than the second did.
    var index: int = 0;
    while (index < 64) {
        var value = index * index * 3;
        if (value > 500) {
            value = 500;
        }
        KING_ATTACK_SCALE.at(index) = value;
        index += 1;
    }
}

// --- Evaluation -----------------------------------------------------------

// How far into the endgame the position is, from PHASE_TOTAL (all pieces on
// the board) down to zero (bare kings).
fn game_phase(board: Position) -> int {
    var phase: int = 0;
    var kind: int = 1;
    while (kind < 5) {
        let white = popcount(board.pieces(Color.White, kind_from_index(kind)));
        let black = popcount(board.pieces(Color.Black, kind_from_index(kind)));
        phase += (white + black) * PHASE_WEIGHT.at(kind);
        kind += 1;
    }
    if (phase > PHASE_TOTAL) {
        phase = PHASE_TOTAL;
    }
    return phase;
}

// Blend a middlegame and an endgame score by the phase. Integer division at
// the end keeps everything in centipawns.
fn taper(mg: int, eg: int, phase: int) -> int {
    return (mg * phase + eg * (PHASE_TOTAL - phase)) / PHASE_TOTAL;
}

// Material plus piece-square, for one colour, in both phases at once. The
// pair is returned through two out-parameters because the language has no
// tuples in its standard surface.
fn material_and_placement(board: Position, color: Color, ref mg: int, ref eg: int) {
    var mg_total: int = 0;
    var eg_total: int = 0;
    var kind: int = 0;
    while (kind < PIECE_KIND_COUNT) {
        var pieces = board.pieces(color, kind_from_index(kind));
        while (pieces != 0) {
            let square = bit_scan_forward(pieces);
            pieces = clear_lowest_bit(pieces);
            let indexed = relative_square(color, square);
            mg_total += MATERIAL_MG.at(kind) + PST_MG.at(kind).at(indexed);
            eg_total += MATERIAL_EG.at(kind) + PST_EG.at(kind).at(indexed);
        }
        kind += 1;
    }
    *mg = mg_total;
    *eg = eg_total;
}

// Mobility: the number of squares each piece attacks that are not occupied
// by a friendly piece and not attacked by an enemy pawn.
fn mobility_score(board: Position, color: Color) -> int {
    let them = opposite_color(color);
    let ours = board.pieces_of_color(color);
    let occupancy = board.occupied();
    let enemy_pawn_attacks = pawn_attack_span(them, board.pieces(them, PieceKind.Pawn));
    let allowed = FULL_BOARD ^ (ours | enemy_pawn_attacks);
    var total: int = 0;

    var knights = board.pieces(color, PieceKind.Knight);
    while (knights != 0) {
        let square = bit_scan_forward(knights);
        knights = clear_lowest_bit(knights);
        total += popcount(KNIGHT_ATTACKS.at(square) & allowed) * 4;
    }

    var bishops = board.pieces(color, PieceKind.Bishop);
    while (bishops != 0) {
        let square = bit_scan_forward(bishops);
        bishops = clear_lowest_bit(bishops);
        total += popcount(bishop_attacks(square, occupancy) & allowed) * 3;
    }

    var rooks = board.pieces(color, PieceKind.Rook);
    while (rooks != 0) {
        let square = bit_scan_forward(rooks);
        rooks = clear_lowest_bit(rooks);
        total += popcount(rook_attacks(square, occupancy) & allowed) * 2;
    }

    var queens = board.pieces(color, PieceKind.Queen);
    while (queens != 0) {
        let square = bit_scan_forward(queens);
        queens = clear_lowest_bit(queens);
        total += popcount(queen_attacks(square, occupancy) & allowed);
    }

    return total;
}

// Doubled, isolated, backward and passed pawns, plus the connected bonus.
fn pawn_structure_score(board: Position, color: Color) -> int {
    let them = opposite_color(color);
    let ours = board.pieces(color, PieceKind.Pawn);
    let theirs = board.pieces(them, PieceKind.Pawn);
    var score: int = 0;

    // Doubled pawns: count the extra pawns on every file.
    var file: int = 0;
    while (file < BOARD_FILES) {
        let on_file = popcount(ours & FILE_MASKS.at(file));
        if (on_file > 1) {
            score += PENALTY_DOUBLED_PAWN * (on_file - 1);
        }
        file += 1;
    }

    var pawns = ours;
    while (pawns != 0) {
        let square = bit_scan_forward(pawns);
        pawns = clear_lowest_bit(pawns);
        let f = file_of(square);

        // Isolated: no friendly pawn on either neighbouring file.
        if ((ours & ADJACENT_FILES.at(f)) == 0) {
            score += PENALTY_ISOLATED_PAWN;
        } else {
            // Connected: a friendly pawn defends it or stands beside it.
            let support = pawn_attack_span(them, 1 << square) & ours;
            if (support != 0) {
                score += BONUS_CONNECTED_PAWN;
            } else {
                // Backward: cannot advance without being met by a pawn.
                let stop = 1 << (square + pawn_push_delta(color));
                if ((pawn_attack_span(color, stop) & theirs) != 0) {
                    score += PENALTY_BACKWARD_PAWN;
                }
            }
        }

        // Passed: nothing in the three files ahead can stop it.
        if ((PASSED_MASK.at(color_index(color)).at(square) & theirs) == 0) {
            let advanced = relative_rank(color, square);
            score += BONUS_PASSED_BASE + BONUS_PASSED_RANK * advanced * advanced / 4;
        }
    }
    return score;
}

// Rooks like open and half-open files; the bishop pair is worth a pawn's
// worth of long-term pressure.
fn piece_bonus_score(board: Position, color: Color) -> int {
    let them = opposite_color(color);
    let ours = board.pieces(color, PieceKind.Pawn);
    let theirs = board.pieces(them, PieceKind.Pawn);
    var score: int = 0;

    var rooks = board.pieces(color, PieceKind.Rook);
    while (rooks != 0) {
        let square = bit_scan_forward(rooks);
        rooks = clear_lowest_bit(rooks);
        let mask = FILE_MASKS.at(file_of(square));
        if ((mask & ours) == 0) {
            if ((mask & theirs) == 0) {
                score += BONUS_ROOK_OPEN_FILE;
            } else {
                score += BONUS_ROOK_SEMI_OPEN;
            }
        }
        if (relative_rank(color, square) == 6) {
            score += 20;
        }
    }

    if (popcount(board.pieces(color, PieceKind.Bishop)) >= 2) {
        score += BONUS_BISHOP_PAIR;
    }
    return score;
}

// King safety: count the attackers into the king zone, weight them by piece
// value, and look up the ramp.
fn king_safety_score(board: Position, color: Color) -> int {
    let king = board.king_square(color);
    if (king == NO_SQUARE) {
        return 0;
    }
    let them = opposite_color(color);
    let zone = KING_ZONE.at(color_index(color)).at(king);
    let occupancy = board.occupied();
    var weight: int = 0;

    var knights = board.pieces(them, PieceKind.Knight);
    while (knights != 0) {
        let square = bit_scan_forward(knights);
        knights = clear_lowest_bit(knights);
        weight += popcount(KNIGHT_ATTACKS.at(square) & zone) * 2;
    }

    var bishops = board.pieces(them, PieceKind.Bishop);
    while (bishops != 0) {
        let square = bit_scan_forward(bishops);
        bishops = clear_lowest_bit(bishops);
        weight += popcount(bishop_attacks(square, occupancy) & zone) * 2;
    }

    var rooks = board.pieces(them, PieceKind.Rook);
    while (rooks != 0) {
        let square = bit_scan_forward(rooks);
        rooks = clear_lowest_bit(rooks);
        weight += popcount(rook_attacks(square, occupancy) & zone) * 3;
    }

    var queens = board.pieces(them, PieceKind.Queen);
    while (queens != 0) {
        let square = bit_scan_forward(queens);
        queens = clear_lowest_bit(queens);
        weight += popcount(queen_attacks(square, occupancy) & zone) * 5;
    }

    if (weight >= 64) {
        weight = 63;
    }
    var score = -KING_ATTACK_SCALE.at(weight);

    // A king on a file with no pawn of its own is exposed.
    let shelter = FILE_MASKS.at(file_of(king)) & board.pieces(color, PieceKind.Pawn);
    if (shelter == 0) {
        score += PENALTY_KING_EXPOSED;
    }
    return score;
}

// The whole evaluation, from the side to move's point of view.
fn evaluate(board: Position) -> int {
    let phase = game_phase(board);

    var white_mg: int = 0;
    var white_eg: int = 0;
    material_and_placement(board, Color.White, white_mg, white_eg);

    var black_mg: int = 0;
    var black_eg: int = 0;
    material_and_placement(board, Color.Black, black_mg, black_eg);

    var mg = white_mg - black_mg;
    var eg = white_eg - black_eg;

    let mobility = mobility_score(board, Color.White) - mobility_score(board, Color.Black);
    mg += mobility * MOBILITY_WEIGHT_MG / 4;
    eg += mobility * MOBILITY_WEIGHT_EG / 4;

    let structure = pawn_structure_score(board, Color.White) - pawn_structure_score(board, Color.Black);
    mg += structure;
    eg += structure * 3 / 2;

    let bonuses = piece_bonus_score(board, Color.White) - piece_bonus_score(board, Color.Black);
    mg += bonuses;
    eg += bonuses;

    let safety = king_safety_score(board, Color.White) - king_safety_score(board, Color.Black);
    mg += safety;
    eg += safety / 4;

    var score = taper(mg, eg, phase);
    if (board.side_to_move() == Color.Black) {
        score = -score;
    }
    return score + TEMPO_BONUS;
}

// A quick material-only score, used by the move ordering to sort captures
// most-valuable-victim first.
fn move_order_score(board: Position, m: int) -> int {
    if (move_is_promotion(m)) {
        return 900 + kind_index(move_promotion_kind(m)) * 10;
    }
    if (move_is_capture(m) == false) {
        return 0;
    }
    let victim = board.piece_at(move_to(m));
    let attacker = board.piece_at(move_from(m));
    var victim_value: int = 100;
    if (piece_is_empty(victim) == false) {
        victim_value = MATERIAL_MG.at(kind_index(piece_kind(victim)));
    }
    var attacker_value: int = 100;
    if (piece_is_empty(attacker) == false) {
        attacker_value = MATERIAL_MG.at(kind_index(piece_kind(attacker)));
    }
    return victim_value * 16 - attacker_value;
}

fn score_move_list(board: Position, ref list: MoveList) {
    var index: int = 0;
    while (index < list.size()) {
        let ref entry: auto = list.at(index);
        entry.score = move_order_score(board, entry.packed);
        index += 1;
    }
}

// --- Driver ---------------------------------------------------------------

// Build every table. Nothing in this file works before this has run, so the
// self-test below calls it first and the assert catches the mistake if a
// future caller forgets.
fn initialise() {
    init_tables();
    init_castle_masks();
    init_castle_paths();
    init_zobrist();
    init_material();
    init_pst_middlegame();
    init_pst_endgame();
    init_evaluation_masks();
    reset_perft_counters();
}

// Sanity checks on the tables themselves. A knight on a corner has two
// moves, one in the middle has eight, and the rays out of a corner cover
// exactly twenty-one squares.
fn verify_tables() -> bool {
    var ok: bool = true;

    if (popcount(KNIGHT_ATTACKS.at(SQ_A1)) != 2) {
        ok = false;
    }
    if (popcount(KNIGHT_ATTACKS.at(make_square(3, 3))) != 8) {
        ok = false;
    }
    if (popcount(KING_ATTACKS.at(SQ_A1)) != 3) {
        ok = false;
    }
    if (popcount(KING_ATTACKS.at(make_square(4, 4))) != 8) {
        ok = false;
    }
    if (popcount(rook_attacks(SQ_A1, 0)) != 14) {
        ok = false;
    }
    if (popcount(bishop_attacks(SQ_A1, 0)) != 7) {
        ok = false;
    }
    if (popcount(queen_attacks(SQ_A1, 0)) != 21) {
        ok = false;
    }
    if (popcount(FILE_MASKS.at(0)) != 8) {
        ok = false;
    }
    if (popcount(RANK_MASKS.at(7)) != 8) {
        ok = false;
    }
    if (popcount(LIGHT_SQUARES) != 32 or popcount(DARK_SQUARES) != 32) {
        ok = false;
    }
    if ((LIGHT_SQUARES & DARK_SQUARES) != 0) {
        ok = false;
    }

    // Every square must round-trip through the bit scans.
    var square: int = 0;
    while (square < SQUARE_COUNT) {
        let bit = 1 << square;
        if (bit_scan_forward(bit) != square) {
            ok = false;
        }
        if (bit_scan_reverse(bit) != square) {
            ok = false;
        }
        if (popcount(bit) != 1) {
            ok = false;
        }
        if (flip_rank(flip_rank(square)) != square) {
            ok = false;
        }
        square += 1;
    }

    // The between table must be symmetric and must exclude its endpoints.
    var from: int = 0;
    while (from < SQUARE_COUNT) {
        var to: int = 0;
        while (to < SQUARE_COUNT) {
            let segment = BETWEEN.at(from).at(to);
            if ((segment & ((1 << from) | (1 << to))) != 0) {
                ok = false;
            }
            if (segment != BETWEEN.at(to).at(from)) {
                ok = false;
            }
            to += 1;
        }
        from += 1;
    }

    Assert(ok);
    return ok;
}

// Perft from the start position. The expected counts are the ones every
// engine is checked against; a mismatch at depth n means the bug is in
// whatever was first exercised at depth n.
fn run_start_position_perft(depth: int) -> int {
    var board: Position;
    let parsed = parse_fen(board, START_FEN);
    Assert(parsed);

    var expected: vector(int) = vector(int)(7);
    expected.at(0) = 1;
    expected.at(1) = 20;
    expected.at(2) = 400;
    expected.at(3) = 8902;
    expected.at(4) = 197281;
    expected.at(5) = 4865609;
    expected.at(6) = 119060324;

    var total: int = 0;
    var current: int = 1;
    while (current <= depth) {
        let nodes = perft(board, current);
        total = nodes;

        var line: string;
        line.append("perft(");
        line.append(to_string(current));
        line.append(") = ");
        line.append(to_string(nodes));
        if (current < expected.size()) {
            if (nodes == expected.at(current)) {
                line.append(" ok");
            } else {
                line.append(" MISMATCH, expected ");
                line.append(to_string(expected.at(current)));
            }
        }
        println(line);
        Destroy(line);
        current += 1;
    }

    Destroy(expected);
    Destroy(board);
    return total;
}

// Walk a short game move by move, printing the SAN and the evaluation after
// each ply. This exercises make, unmake, notation and evaluation together.
fn run_notation_demo() {
    var board: Position;
    Discard(parse_fen(board, START_FEN));

    var script: vector(string);
    script.push_back("e2e4");
    script.push_back("e7e5");
    script.push_back("g1f3");
    script.push_back("b8c6");
    script.push_back("f1b5");
    script.push_back("a7a6");
    script.push_back("b5a4");
    script.push_back("g8f6");
    script.push_back("e1g1");

    var index: int = 0;
    while (index < script.size()) {
        let m = move_from_uci(board, script.at(index));
        if (move_is_null(m)) {
            println("illegal move in script");
            break;
        }

        var line: string;
        line.append(to_string(board.fullmove_number()));
        line.append(". ");
        line.append(move_to_san(board, m));
        line.append("  eval ");
        board.make_move(m);
        line.append(to_string(evaluate(board)));
        println(line);
        Destroy(line);
        index += 1;
    }

    println(board_to_text(board));
    println(to_fen(board));
    Destroy(script);
    Destroy(board);
}

// Generate, order and count the moves of a tactical position, then report
// how much work the generator did.
fn run_generation_report(fen: string, label: string) {
    var board: Position;
    if (parse_fen(board, fen) == false) {
        println("bad fen");
        Destroy(board);
        return;
    }

    var list: MoveList;
    board.generate_legal(list);
    score_move_list(board, list);

    var captures: int = 0;
    var promotions: int = 0;
    var castles: int = 0;
    var checks: int = 0;
    var index: int = 0;
    while (index < list.size()) {
        let m = list.packed_at(index);
        if (move_is_capture(m)) {
            captures += 1;
        }
        if (move_is_promotion(m)) {
            promotions += 1;
        }
        if (move_is_castle(m)) {
            castles += 1;
        }
        if (board.gives_check(m)) {
            checks += 1;
        }
        index += 1;
    }

    var line: string;
    line.append(label);
    line.append(": ");
    line.append(to_string(list.size()));
    line.append(" legal, ");
    line.append(to_string(captures));
    line.append(" captures, ");
    line.append(to_string(promotions));
    line.append(" promotions, ");
    line.append(to_string(castles));
    line.append(" castles, ");
    line.append(to_string(checks));
    line.append(" checks");
    println(line);
    Destroy(line);

    // Print the three best-ordered moves, which should be the fattest
    // captures.
    var shown: int = 0;
    while (shown < 3 and shown < list.size()) {
        Discard(list.select_best(shown));
        var entry: string;
        entry.append("  ");
        entry.append(move_to_uci(list.packed_at(shown)));
        entry.append(" score ");
        entry.append(to_string(list.at(shown).score));
        println(entry);
        Destroy(entry);
        shown += 1;
    }

    Destroy(list);
    Destroy(board);
}

// The whole self-test, in the order a new build should be checked.
fn self_test() -> bool {
    initialise();
    var ok = verify_tables();

    var board: Position;
    if (parse_fen(board, START_FEN) == false) {
        Destroy(board);
        return false;
    }

    // The start position has thirty-two pieces, sixteen a side, and twenty
    // legal first moves.
    if (board.total_piece_count() != 32) {
        ok = false;
    }
    if (popcount(board.pieces_of_color(Color.White)) != 16) {
        ok = false;
    }
    if (board.count_legal_moves() != 20) {
        ok = false;
    }
    if (board.in_check()) {
        ok = false;
    }
    if (board.castling_rights() != CASTLE_ALL) {
        ok = false;
    }
    if (board.hash_key() != board.compute_key()) {
        ok = false;
    }
    if (verify_make_unmake(board) == false) {
        ok = false;
    }

    // FEN must round-trip.
    let regenerated = to_fen(board);
    if (regenerated.compare(START_FEN) != 0) {
        ok = false;
    }

    run_generation_report(KIWIPETE_FEN, "kiwipete");
    run_generation_report(PROMOTION_FEN, "promotions");
    run_generation_report(ENDGAME_FEN, "endgame");
    Discard(run_start_position_perft({.depth = 4}));
    run_notation_demo();

    var stats: string;
    stats.append("moves generated: ");
    stats.append(to_string(generated_move_count));
    stats.append(", legality tests: ");
    stats.append(to_string(legality_test_count));
    stats.append(", attack queries: ");
    stats.append(to_string(attack_query_count));
    stats.append(", makes: ");
    stats.append(to_string(make_move_count));
    println(stats);
    Destroy(stats);

    Assert(ok);
    Destroy(board);
    return ok;
}


