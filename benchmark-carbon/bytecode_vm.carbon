// A stack-based bytecode virtual machine for a small dynamic language.
//
// The design follows the classic single-pass compiler / register-less VM
// layout: a `Chunk` holds a flat instruction array plus a constant pool, a
// `CallFrame` records the return address and the base of the local window,
// and `VM::run` walks the instruction stream with an explicit dispatch
// cascade. The language has no `switch`, so every dispatch site below is a
// hand-rolled `if op == OpCode::X { } else { ... }` chain. The nested
// `else` blocks are written on the same line as the enclosing brace so the
// chain stays readable instead of drifting a hundred columns to the right.
//
// Values are dynamically typed and reference-counted objects are tracked by
// a mark-sweep collector rooted at the value stack, the frame array and the
// globals table. Fractional numbers are represented as fixed-point integers
// scaled by FIXED_SCALE; the VM never sees a floating point value.
namespace vm;

// --- Tunables ------------------------------------------------------------

let STACK_MAX: int = 4096;
let FRAMES_MAX: int = 256;
let UPVALUE_MAX: int = 128;
let CONSTANTS_MAX: int = 65536;
let LOCALS_MAX: int = 256;

// Fixed-point scale: a "number" value of 1500 means 1.5.
let FIXED_SCALE: int = 1000;

// Bit masks used when packing a 24 bit operand next to an 8 bit opcode.
let OPCODE_MASK: int = 255;            // 0xFF
let OPERAND_MASK: int = 16777215;      // 0xFFFFFF
let OPERAND_SHIFT: int = 8;
let SHORT_MASK: int = 65535;           // 0xFFFF
let BYTE_BITS: int = 8;

// Collector heuristics.
let GC_INITIAL_THRESHOLD: int = 1048576;
let GC_GROWTH_PERCENT: int = 200;
let GC_MIN_THRESHOLD: int = 8192;
let GC_GRAY_INITIAL: int = 64;

// --- Value model ---------------------------------------------------------

choice ValueKind {
    Nil,
    Bool,
    Number,
    Object,
}

choice ObjectKind {
    String,
    Array,
    Map,
    Function,
    Closure,
    Upvalue,
    Native,
    BoundMethod,
    Instance,
    Class,
}

// The garbage collected header shared by every heap object. Real objects
// embed one of these as their first field so the collector can walk them
// without knowing the concrete type.
class Obj {
    var kind: ObjectKind = ObjectKind.String;
    var marked: bool = false;
    // Intrusive linked list of every object the collector knows about.
    var next: int = -1;
    // Index into VM::objects; -1 means "not yet registered".
    var handle: int = -1;
}

// A dynamically typed slot. Only one payload field is meaningful at a time
// and `kind` says which; the VM asserts on every access.
class Value {

    fn is_nil(self) -> bool {
        return self.kind == ValueKind.Nil;
    }

    fn is_bool(self) -> bool {
        return self.kind == ValueKind.Bool;
    }

    fn is_number(self) -> bool {
        return self.kind == ValueKind.Number;
    }

    fn is_object(self) -> bool {
        return self.kind == ValueKind.Object;
    }

    // Everything except `nil` and `false` is truthy, matching the source
    // language's semantics.
    fn is_truthy(self) -> bool {
        if (self.kind == ValueKind.Nil) {
            return false;
        } else {
            if (self.kind == ValueKind.Bool) {
                return self.boolean;
            } else {
                return true;
            }
        }
    }

    fn as_number(self) -> int {
        Assert(self.kind == ValueKind.Number);
        return self.number;
    }

    fn as_object(self) -> int {
        Assert(self.kind == ValueKind.Object);
        return self.object;
    }

    var kind: ValueKind = ValueKind.Nil;
    var boolean: bool = false;
    var number: int = 0;
    // Handle into VM::objects rather than a raw pointer, so the collector
    // may relocate the object table without patching the stack.
    var object: int = -1;
}

fn make_nil() -> Value {
    var v: Value;
    v.kind = ValueKind.Nil;
    return v;
}

fn make_bool(b: bool) -> Value {
    var v: Value;
    v.kind = ValueKind.Bool;
    v.boolean = b;
    return v;
}

fn make_number(n: int) -> Value {
    var v: Value;
    v.kind = ValueKind.Number;
    v.number = n;
    return v;
}

fn make_object(handle: int) -> Value {
    var v: Value;
    v.kind = ValueKind.Object;
    v.object = handle;
    return v;
}

fn values_equal(a: Value, b: Value) -> bool {
    if (a.kind != b.kind) {
        return false;
    }
    if (a.kind == ValueKind.Nil) {
        return true;
    } else {
        if (a.kind == ValueKind.Bool) {
            return a.boolean == b.boolean;
        } else {
            if (a.kind == ValueKind.Number) {
                return a.number == b.number;
            } else {
                return a.object == b.object;
            }
        }
    }
}

// --- Instruction set -----------------------------------------------------

// The complete instruction set. Values are fixed because serialized
// chunks are written to disk; never renumber an existing opcode, only
// append. The trailing comment on each line is the byte as it appears
// in a hex dump of the code section.
choice OpCode {
    // Constants, literals and raw stack shuffling.
    Constant,                         // 0x00
    ConstantLong,                     // 0x01
    Nil,                              // 0x02
    True,                             // 0x03
    False,                            // 0x04
    Pop,                              // 0x05
    PopN,                             // 0x06
    Dup,                              // 0x07
    Swap,                             // 0x08
    Rotate,                           // 0x09

    // Locals, globals, upvalues, properties and subscripts.
    GetLocal,                        // 0x0A
    SetLocal,                        // 0x0B
    GetGlobal,                       // 0x0C
    SetGlobal,                       // 0x0D
    DefineGlobal,                    // 0x0E
    GetUpvalue,                      // 0x0F
    SetUpvalue,                      // 0x10
    GetProperty,                     // 0x11
    SetProperty,                     // 0x12
    GetIndex,                        // 0x13
    SetIndex,                        // 0x14

    // Comparison; every one of these pops two and pushes a bool.
    Equal,                           // 0x15
    NotEqual,                        // 0x16
    Greater,                         // 0x17
    GreaterEqual,                    // 0x18
    Less,                            // 0x19
    LessEqual,                       // 0x1A

    // Binary arithmetic on fixed-point numbers.
    Add,                             // 0x1B
    Subtract,                        // 0x1C
    Multiply,                        // 0x1D
    Divide,                          // 0x1E
    Modulo,                          // 0x1F
    Power,                           // 0x20

    // Unary operators; they rewrite the slot in place.
    Negate,                          // 0x21
    Increment,                       // 0x22
    Decrement,                       // 0x23
    Not,                             // 0x24

    // Bitwise operators. Operands are truncated to integers first.
    BitAnd,                          // 0x25
    BitOr,                           // 0x26
    BitXor,                          // 0x27
    BitNot,                          // 0x28
    ShiftLeft,                       // 0x29
    ShiftRight,                      // 0x2A
    ShiftRightLogical,               // 0x2B

    // Control flow. The operand is an absolute instruction index.
    Jump,                            // 0x2C
    JumpIfFalse,                     // 0x2D
    JumpIfTrue,                      // 0x2E
    JumpIfNil,                       // 0x2F
    JumpIfNotNil,                    // 0x30
    Loop,                            // 0x31

    // Call forms. The operand is the argument count.
    Call,                            // 0x32
    Invoke,                          // 0x33
    TailCall,                        // 0x34
    CallNative,                      // 0x35

    // Closure construction and function exit.
    Closure,                         // 0x36
    CloseUpvalue,                    // 0x37
    Return,                          // 0x38
    ReturnNil,                       // 0x39

    // Array intrinsics.
    NewArray,                        // 0x3A
    ArrayPush,                       // 0x3B
    ArrayLength,                     // 0x3C
    ArraySlice,                      // 0x3D
    ArrayConcat,                     // 0x3E

    // Map intrinsics.
    NewMap,                          // 0x3F
    MapInsert,                       // 0x40
    MapLookup,                       // 0x41
    MapErase,                        // 0x42
    MapLength,                       // 0x43

    // Builtins, diagnostics and terminators.
    Concat,                          // 0x44
    Length,                          // 0x45
    TypeOf,                          // 0x46
    Print,                           // 0x47
    Assert,                          // 0x48
    Import,                          // 0x49
    Nop,                             // 0x4A
    Halt,                            // 0x4B
}

// A decoded instruction. The on-disk form packs `op` into the low byte and
// `operand` into the upper three bytes of a single word; `extra` is only
// materialised for the two-operand forms such as OpCode::Invoke.
class Instruction {

    fn packed(self) -> int {
        return (self.operand << OPERAND_SHIFT) | (self.opcode_byte() & OPCODE_MASK);
    }

    fn opcode_byte(self) -> int {
        return self.raw & OPCODE_MASK;
    }

    fn is_jump(self) -> bool {
        if (self.op == OpCode.Jump) {
            return true;
        }
        if (self.op == OpCode.JumpIfFalse) {
            return true;
        }
        if (self.op == OpCode.JumpIfTrue) {
            return true;
        }
        if (self.op == OpCode.JumpIfNil) {
            return true;
        }
        if (self.op == OpCode.JumpIfNotNil) {
            return true;
        }
        return self.op == OpCode.Loop;
    }

    var op: OpCode = OpCode.Nop;
    var operand: int = 0;
    var extra: int = 0;
    var raw: int = 0;
    var line: int = 0;
}

fn decode(raw: int) -> Instruction {
    var ins: Instruction;
    ins.raw = raw;
    ins.operand = (raw >> OPERAND_SHIFT) & OPERAND_MASK;
    return ins;
}

// Run-length encoded line table: `count` consecutive instructions all
// originate from source line `line`.
class LineRun {
    var line: int = 0;
    var count: int = 0;
}

// --- Chunks --------------------------------------------------------------

// A compiled unit: code, the constant pool it indexes into, and enough
// debug information to reconstruct a stack trace.
class Chunk {

    fn size(self) -> int {
        return self.code.size();
    }

    fn empty(self) -> bool {
        return self.code.empty();
    }

    fn write(ref self, op: OpCode, operand: int, line: int) -> int {
        var ins: Instruction;
        ins.op = op;
        ins.operand = operand;
        ins.line = line;
        ins.raw = ins.packed();
        self.code.push_back(ins);
        self.note_line(line);
        return self.code.size() - 1;
    }

    fn write_simple(ref self, op: OpCode, line: int) -> int {
        return self.write(op, 0, line);
    }

    fn write_invoke(ref self, name_slot: int, argc: int, line: int) -> int {
        let index = self.write(OpCode.Invoke, name_slot, line);
        let ref slot: auto = self.code.at(index);
        slot.extra = argc;
        return index;
    }

    // Append to the run-length encoded line table.
    fn note_line(ref self, line: int) {
        if (self.lines.empty()) {
            self.lines.push_back(LineRun({.line = line, .count = 1}));
            return;
        }
        let ref last: auto = self.lines.back();
        if (last.line == line) {
            last.count += 1;
        } else {
            self.lines.push_back(LineRun({.line = line, .count = 1}));
        }
    }

    fn line_at(self, offset: int) -> int {
        var seen: int = 0;
        for (run in self.lines) {
            seen += run.count;
            if (offset < seen) {
                return run.line;
            }
        }
        return 0;
    }

    // Interning: identical constants share a slot so the pool stays small.
    fn add_constant(ref self, value: Value) -> int {
        var i: int = 0;
        while (i < self.constants.size()) {
            if (values_equal(self.constants.at(i), value)) {
                return i;
            }
            i += 1;
        }
        Assert(self.constants.size() < CONSTANTS_MAX);
        self.constants.push_back(value);
        return self.constants.size() - 1;
    }

    fn constant_at(self, index: int) -> ref const Value {
        Assert(index >= 0);
        Assert(index < self.constants.size());
        return self.constants.at(index);
    }

    fn patch_jump(ref self, offset: int, target: int) {
        let ref slot: auto = self.code.at(offset);
        Assert(slot.is_jump());
        slot.operand = target;
        slot.raw = slot.packed();
    }

    fn emit_loop(ref self, start: int, line: int) -> int {
        return self.write(OpCode.Loop, start, line);
    }

    fn clear(ref self) {
        self.code.clear();
        self.constants.clear();
        self.lines.clear();
    }

    var code: vector(Instruction);
    var constants: vector(Value);
    var lines: vector(LineRun);
    var name: string;
    var arity: int = 0;
    var upvalue_count: int = 0;
}

// --- Disassembly ---------------------------------------------------------

// Canonical mnemonic for every opcode, used by the disassembler and by
// the error reporter. There is no `switch` in the language, so this is a
// straight cascade; the nested `else` blocks are all closed in one run at
// the bottom of the function.
fn opcode_name(op: OpCode) -> string {
    // constants and stack shuffling
    if (op == OpCode.Constant) {
        return "OP_CONSTANT";
    } else { if (op == OpCode.ConstantLong) {
        return "OP_CONSTANT_LONG";
    } else { if (op == OpCode.Nil) {
        return "OP_NIL";
    } else { if (op == OpCode.True) {
        return "OP_TRUE";
    } else { if (op == OpCode.False) {
        return "OP_FALSE";
    } else { if (op == OpCode.Pop) {
        return "OP_POP";
    } else { if (op == OpCode.PopN) {
        return "OP_POP_N";
    } else { if (op == OpCode.Dup) {
        return "OP_DUP";
    } else { if (op == OpCode.Swap) {
        return "OP_SWAP";
    } else { if (op == OpCode.Rotate) {
        return "OP_ROTATE";
    // variable access
    } else { if (op == OpCode.GetLocal) {
        return "OP_GET_LOCAL";
    } else { if (op == OpCode.SetLocal) {
        return "OP_SET_LOCAL";
    } else { if (op == OpCode.GetGlobal) {
        return "OP_GET_GLOBAL";
    } else { if (op == OpCode.SetGlobal) {
        return "OP_SET_GLOBAL";
    } else { if (op == OpCode.DefineGlobal) {
        return "OP_DEFINE_GLOBAL";
    } else { if (op == OpCode.GetUpvalue) {
        return "OP_GET_UPVALUE";
    } else { if (op == OpCode.SetUpvalue) {
        return "OP_SET_UPVALUE";
    } else { if (op == OpCode.GetProperty) {
        return "OP_GET_PROPERTY";
    } else { if (op == OpCode.SetProperty) {
        return "OP_SET_PROPERTY";
    } else { if (op == OpCode.GetIndex) {
        return "OP_GET_INDEX";
    } else { if (op == OpCode.SetIndex) {
        return "OP_SET_INDEX";
    // comparison
    } else { if (op == OpCode.Equal) {
        return "OP_EQUAL";
    } else { if (op == OpCode.NotEqual) {
        return "OP_NOT_EQUAL";
    } else { if (op == OpCode.Greater) {
        return "OP_GREATER";
    } else { if (op == OpCode.GreaterEqual) {
        return "OP_GREATER_EQUAL";
    } else { if (op == OpCode.Less) {
        return "OP_LESS";
    } else { if (op == OpCode.LessEqual) {
        return "OP_LESS_EQUAL";
    // binary arithmetic
    } else { if (op == OpCode.Add) {
        return "OP_ADD";
    } else { if (op == OpCode.Subtract) {
        return "OP_SUBTRACT";
    } else { if (op == OpCode.Multiply) {
        return "OP_MULTIPLY";
    } else { if (op == OpCode.Divide) {
        return "OP_DIVIDE";
    } else { if (op == OpCode.Modulo) {
        return "OP_MODULO";
    } else { if (op == OpCode.Power) {
        return "OP_POWER";
    // unary operators
    } else { if (op == OpCode.Negate) {
        return "OP_NEGATE";
    } else { if (op == OpCode.Increment) {
        return "OP_INCREMENT";
    } else { if (op == OpCode.Decrement) {
        return "OP_DECREMENT";
    } else { if (op == OpCode.Not) {
        return "OP_NOT";
    // bitwise
    } else { if (op == OpCode.BitAnd) {
        return "OP_BIT_AND";
    } else { if (op == OpCode.BitOr) {
        return "OP_BIT_OR";
    } else { if (op == OpCode.BitXor) {
        return "OP_BIT_XOR";
    } else { if (op == OpCode.BitNot) {
        return "OP_BIT_NOT";
    } else { if (op == OpCode.ShiftLeft) {
        return "OP_SHIFT_LEFT";
    } else { if (op == OpCode.ShiftRight) {
        return "OP_SHIFT_RIGHT";
    } else { if (op == OpCode.ShiftRightLogical) {
        return "OP_SHIFT_RIGHT_LOGICAL";
    // control flow
    } else { if (op == OpCode.Jump) {
        return "OP_JUMP";
    } else { if (op == OpCode.JumpIfFalse) {
        return "OP_JUMP_IF_FALSE";
    } else { if (op == OpCode.JumpIfTrue) {
        return "OP_JUMP_IF_TRUE";
    } else { if (op == OpCode.JumpIfNil) {
        return "OP_JUMP_IF_NIL";
    } else { if (op == OpCode.JumpIfNotNil) {
        return "OP_JUMP_IF_NOT_NIL";
    } else { if (op == OpCode.Loop) {
        return "OP_LOOP";
    // calls
    } else { if (op == OpCode.Call) {
        return "OP_CALL";
    } else { if (op == OpCode.Invoke) {
        return "OP_INVOKE";
    } else { if (op == OpCode.TailCall) {
        return "OP_TAIL_CALL";
    } else { if (op == OpCode.CallNative) {
        return "OP_CALL_NATIVE";
    // closures and returns
    } else { if (op == OpCode.Closure) {
        return "OP_CLOSURE";
    } else { if (op == OpCode.CloseUpvalue) {
        return "OP_CLOSE_UPVALUE";
    } else { if (op == OpCode.Return) {
        return "OP_RETURN";
    } else { if (op == OpCode.ReturnNil) {
        return "OP_RETURN_NIL";
    // arrays
    } else { if (op == OpCode.NewArray) {
        return "OP_NEW_ARRAY";
    } else { if (op == OpCode.ArrayPush) {
        return "OP_ARRAY_PUSH";
    } else { if (op == OpCode.ArrayLength) {
        return "OP_ARRAY_LENGTH";
    } else { if (op == OpCode.ArraySlice) {
        return "OP_ARRAY_SLICE";
    } else { if (op == OpCode.ArrayConcat) {
        return "OP_ARRAY_CONCAT";
    // maps
    } else { if (op == OpCode.NewMap) {
        return "OP_NEW_MAP";
    } else { if (op == OpCode.MapInsert) {
        return "OP_MAP_INSERT";
    } else { if (op == OpCode.MapLookup) {
        return "OP_MAP_LOOKUP";
    } else { if (op == OpCode.MapErase) {
        return "OP_MAP_ERASE";
    } else { if (op == OpCode.MapLength) {
        return "OP_MAP_LENGTH";
    // builtins and terminators
    } else { if (op == OpCode.Concat) {
        return "OP_CONCAT";
    } else { if (op == OpCode.Length) {
        return "OP_LENGTH";
    } else { if (op == OpCode.TypeOf) {
        return "OP_TYPE_OF";
    } else { if (op == OpCode.Print) {
        return "OP_PRINT";
    } else { if (op == OpCode.Assert) {
        return "OP_ASSERT";
    } else { if (op == OpCode.Import) {
        return "OP_IMPORT";
    } else { if (op == OpCode.Nop) {
        return "OP_NOP";
    } else { if (op == OpCode.Halt) {
        return "OP_HALT";
    } else {
        // Unreachable for a well-formed chunk, but the chunk verifier
        // calls this on raw bytes read straight off disk.
        return "OP_UNKNOWN";
    }
    } } } } } } } } } } } } } } } } } } }
    } } } } } } } } } } } } } } } } } } }
    } } } } } } } } } } } } } } } } } } }
    } } } } } } } } } } } } } } } } } }
}

// How many operands follow the opcode byte in the serialized form. Grouped
// rather than fully expanded because the answer only takes three values.
fn opcode_operand_count(op: OpCode) -> int {
    if (op == OpCode.Invoke) {
        // Constant slot for the method name plus the argument count.
        return 2;
    }
    if (op == OpCode.Constant or op == OpCode.ConstantLong or op == OpCode.PopN) {
        return 1;
    }
    if (op == OpCode.Rotate or op == OpCode.GetLocal or op == OpCode.SetLocal) {
        return 1;
    }
    if (op == OpCode.GetGlobal or op == OpCode.SetGlobal or op == OpCode.DefineGlobal) {
        return 1;
    }
    if (op == OpCode.GetUpvalue or op == OpCode.SetUpvalue) {
        return 1;
    }
    if (op == OpCode.GetProperty or op == OpCode.SetProperty) {
        return 1;
    }
    if (op == OpCode.Jump or op == OpCode.JumpIfFalse or op == OpCode.JumpIfTrue) {
        return 1;
    }
    if (op == OpCode.JumpIfNil or op == OpCode.JumpIfNotNil or op == OpCode.Loop) {
        return 1;
    }
    if (op == OpCode.Call or op == OpCode.TailCall or op == OpCode.CallNative) {
        return 1;
    }
    if (op == OpCode.Closure or op == OpCode.NewArray or op == OpCode.NewMap) {
        return 1;
    }
    if (op == OpCode.Assert or op == OpCode.Import) {
        return 1;
    }
    return 0;
}

// What the operand means, for the annotated disassembly listing.
choice OperandKind {
    None,
    ConstantSlot,
    LocalSlot,
    UpvalueSlot,
    GlobalSlot,
    JumpTarget,
    ArgumentCount,
    Count,
}

fn opcode_operand_kind(op: OpCode) -> OperandKind {
    if (op == OpCode.Constant or op == OpCode.ConstantLong) {
        return OperandKind.ConstantSlot;
    }
    if (op == OpCode.GetProperty or op == OpCode.SetProperty or op == OpCode.Invoke) {
        return OperandKind.ConstantSlot;
    }
    if (op == OpCode.GetLocal or op == OpCode.SetLocal) {
        return OperandKind.LocalSlot;
    }
    if (op == OpCode.GetUpvalue or op == OpCode.SetUpvalue) {
        return OperandKind.UpvalueSlot;
    }
    if (op == OpCode.GetGlobal or op == OpCode.SetGlobal or op == OpCode.DefineGlobal) {
        return OperandKind.GlobalSlot;
    }
    if (op == OpCode.Jump or op == OpCode.JumpIfFalse or op == OpCode.JumpIfTrue) {
        return OperandKind.JumpTarget;
    }
    if (op == OpCode.JumpIfNil or op == OpCode.JumpIfNotNil or op == OpCode.Loop) {
        return OperandKind.JumpTarget;
    }
    if (op == OpCode.Call or op == OpCode.TailCall or op == OpCode.CallNative) {
        return OperandKind.ArgumentCount;
    }
    if (op == OpCode.PopN or op == OpCode.Rotate) {
        return OperandKind.Count;
    }
    if (op == OpCode.NewArray or op == OpCode.NewMap) {
        return OperandKind.Count;
    }
    return OperandKind.None;
}

fn operand_kind_name(kind: OperandKind) -> string {
    if (kind == OperandKind.ConstantSlot) {
        return "const";
    } else { if (kind == OperandKind.LocalSlot) {
        return "local";
    } else { if (kind == OperandKind.UpvalueSlot) {
        return "upval";
    } else { if (kind == OperandKind.GlobalSlot) {
        return "global";
    } else { if (kind == OperandKind.JumpTarget) {
        return "->";
    } else { if (kind == OperandKind.ArgumentCount) {
        return "argc";
    } else { if (kind == OperandKind.Count) {
        return "n";
    } else {
        return "";
    }
    } } } } } }
}

// Net effect of an instruction on the value stack depth. The verifier walks
// the chunk with this to prove the stack never underflows.
fn stack_effect(op: OpCode, operand: int) -> int {
    if (op == OpCode.PopN) {
        return -operand;
    }
    if (op == OpCode.Call or op == OpCode.TailCall or op == OpCode.CallNative) {
        return -operand;
    }
    if (op == OpCode.Invoke) {
        return -operand;
    }
    if (op == OpCode.NewArray) {
        return 1 - operand;
    }
    if (op == OpCode.NewMap) {
        return 1 - operand * 2;
    }
    if (op == OpCode.SetIndex or op == OpCode.MapInsert) {
        return -2;
    }
    if (op == OpCode.ArraySlice) {
        return -2;
    }
    if (is_binary_op(op)) {
        return -1;
    }
    if (is_unary_op(op)) {
        return 0;
    }
    if (pushes_value(op)) {
        return 1;
    }
    if (pops_value(op)) {
        return -1;
    }
    return 0;
}

fn is_binary_op(op: OpCode) -> bool {
    if (op == OpCode.Add or op == OpCode.Subtract or op == OpCode.Multiply) {
        return true;
    }
    if (op == OpCode.Divide or op == OpCode.Modulo or op == OpCode.Power) {
        return true;
    }
    if (op == OpCode.Equal or op == OpCode.NotEqual) {
        return true;
    }
    if (op == OpCode.Greater or op == OpCode.GreaterEqual) {
        return true;
    }
    if (op == OpCode.Less or op == OpCode.LessEqual) {
        return true;
    }
    if (op == OpCode.BitAnd or op == OpCode.BitOr or op == OpCode.BitXor) {
        return true;
    }
    if (op == OpCode.ShiftLeft or op == OpCode.ShiftRight) {
        return true;
    }
    if (op == OpCode.ShiftRightLogical or op == OpCode.Concat) {
        return true;
    }
    return op == OpCode.GetIndex or op == OpCode.ArrayConcat;
}

fn is_unary_op(op: OpCode) -> bool {
    if (op == OpCode.Negate or op == OpCode.Not or op == OpCode.BitNot) {
        return true;
    }
    if (op == OpCode.Increment or op == OpCode.Decrement) {
        return true;
    }
    if (op == OpCode.Length or op == OpCode.TypeOf or op == OpCode.ArrayLength) {
        return true;
    }
    return op == OpCode.MapLength;
}

fn pushes_value(op: OpCode) -> bool {
    if (op == OpCode.Constant or op == OpCode.ConstantLong) {
        return true;
    }
    if (op == OpCode.Nil or op == OpCode.True or op == OpCode.False) {
        return true;
    }
    if (op == OpCode.Dup or op == OpCode.Closure or op == OpCode.Import) {
        return true;
    }
    return op == OpCode.GetLocal or op == OpCode.GetGlobal or op == OpCode.GetUpvalue;
}

fn pops_value(op: OpCode) -> bool {
    if (op == OpCode.Pop or op == OpCode.Print or op == OpCode.Assert) {
        return true;
    }
    if (op == OpCode.DefineGlobal or op == OpCode.SetProperty) {
        return true;
    }
    if (op == OpCode.JumpIfFalse or op == OpCode.JumpIfTrue) {
        return true;
    }
    if (op == OpCode.ArrayPush or op == OpCode.MapLookup or op == OpCode.MapErase) {
        return true;
    }
    return op == OpCode.CloseUpvalue or op == OpCode.Return;
}

// Render a single instruction as `0042  12 OP_GET_LOCAL   local 3`.
fn disassemble_instruction(chunk: Chunk, offset: int) -> string {
    let ref ins: auto = chunk.code.at(offset);
    var text = to_string(offset);
    text.append("  ");
    text.append(to_string(chunk.line_at(offset)));
    text.append("  ");
    text.append(opcode_name(ins.op));
    let count = opcode_operand_count(ins.op);
    if (count > 0) {
        text.append(" ");
        text.append(operand_kind_name(opcode_operand_kind(ins.op)));
        text.append(" ");
        text.append(to_string(ins.operand));
    }
    if (count > 1) {
        text.append(" argc ");
        text.append(to_string(ins.extra));
    }
    return text;
}

fn disassemble_chunk(chunk: Chunk) {
    println(chunk.name);
    var depth: int = 0;
    for (i in range(0, chunk.size())) {
        println(disassemble_instruction(chunk, i));
        let ref ins: auto = chunk.code.at(i);
        depth += stack_effect(ins.op, ins.operand);
        Assert(depth >= 0);
    }
    println("-- end of chunk --");
}

// --- Heap objects --------------------------------------------------------

// Interned string object. `hash` is cached because the globals table and the
// property lookup path both key on it.
class ObjString {

    fn size(self) -> int {
        return self.text.size();
    }

    fn rehash(ref self) {
        self.hash = hash_of(self.text);
    }

    var header: Obj;
    var text: string;
    var hash: int = 0;
}

// A growable value array. `items` is the only field the collector has to
// trace, but it may be arbitrarily deep so tracing is iterative.
class ObjArray {

    fn size(self) -> int {
        return self.items.size();
    }

    fn in_bounds(self, index: int) -> bool {
        return index >= 0 and index < self.items.size();
    }

    fn at(ref self, index: int) -> ref Value {
        Assert(self.in_bounds(index));
        return self.items.at(index);
    }

    var header: Obj;
    var items: vector(Value);
}

// Key/value pair storage for OpCode::NewMap. Keys are restricted to strings
// and numbers by the compiler, so a linear scan is good enough in practice.
class ObjMap {

    fn size(self) -> int {
        return self.keys.size();
    }

    fn find(self, key: Value) -> int {
        for (i in range(0, self.keys.size())) {
            if (values_equal(self.keys.at(i), key)) {
                return i;
            }
        }
        return -1;
    }

    var header: Obj;
    var keys: vector(Value);
    var values: vector(Value);
}

// A compiled function before it is captured into a closure.
class ObjFunction {

    fn name(self) -> ref const string {
        return self.chunk.name;
    }

    var header: Obj;
    var chunk: Chunk;
    var arity: int = 0;
    var upvalue_count: int = 0;
    var is_variadic: bool = false;
}

// An upvalue is either open (pointing at a live stack slot) or closed
// (owning its value). `location` is -1 once the upvalue has been closed.
class ObjUpvalue {

    fn is_open(self) -> bool {
        return self.location >= 0;
    }

    fn read(ref self) -> ref Value {
        return self.closed;
    }

    var header: Obj;
    var location: int = -1;
    var closed: Value;
    var next_open: int = -1;
}

// A function plus the upvalues it captured at the point of creation.
class ObjClosure {

    fn arity(self) -> int {
        return self.function.arity;
    }

    var header: Obj;
    var function: ObjFunction;
    var upvalues: vector(int);
}

// A builtin implemented outside the VM. `slot` indexes the native table.
class ObjNative {
    var header: Obj;
    var slot: int = 0;
    var arity: int = 0;
    var name: string;
}

// --- Call frames ---------------------------------------------------------

// One activation record. `slot_base` is the index in VM::stack of local 0;
// arguments were pushed by the caller and become the first locals.
class CallFrame {

    fn local(ref self, index: int) -> int {
        Assert(index >= 0);
        Assert(index < LOCALS_MAX);
        return self.slot_base + index;
    }

    fn advance(ref self) -> int {
        let here = self.ip;
        self.ip += 1;
        return here;
    }

    fn jump_to(ref self, target: int) {
        Assert(target >= 0);
        self.ip = target;
    }

    // Index into VM::closures rather than a borrow, so a frame may outlive
    // the borrow checker's view of the closure table during a collection.
    var closure: int = -1;
    var ip: int = 0;
    var slot_base: int = 0;
    var argument_count: int = 0;
    var is_tail: bool = false;
}

choice InterpretResult {
    Ok,
    CompileError,
    RuntimeError,
    Halted,
}

// --- The virtual machine -------------------------------------------------

// The interpreter. One instance owns the value stack, the frame array, the
// globals table and every heap object reachable from them.
class VM {

    // --- Stack primitives ------------------------------------------------

    fn push(ref self, value: Value) {
        Assert(self.stack_top < STACK_MAX);
        self.stack.push_back(value);
        self.stack_top += 1;
    }

    fn pop(ref self) -> Value {
        Assert(self.stack_top > 0);
        self.stack_top -= 1;
        let value = self.stack.back();
        self.stack.pop_back();
        return value;
    }

    fn pop_n(ref self, count: int) {
        var i: int = 0;
        while (i < count) {
            Discard(self.pop());
            i += 1;
        }
    }

    // `distance` counts from the top: peek(0) is the topmost slot.
    fn peek(ref self, distance: int) -> ref Value {
        Assert(distance < self.stack_top);
        return self.stack.at(self.stack_top - 1 - distance);
    }

    fn store_slot(ref self, index: int, value: Value) {
        Assert(index >= 0);
        Assert(index < self.stack_top);
        let ref slot: auto = self.stack.at(index);
        slot.kind = value.kind;
        slot.boolean = value.boolean;
        slot.number = value.number;
        slot.object = value.object;
    }

    fn swap_top(ref self) {
        let a = self.pop();
        let b = self.pop();
        self.push(a);
        self.push(b);
    }

    // Move the slot `depth` below the top up to the top.
    fn rotate(ref self, depth: int) {
        if (depth <= 0) {
            return;
        }
        let lifted = self.stack.at(self.stack_top - 1 - depth);
        var i = self.stack_top - 1 - depth;
        while (i < self.stack_top - 1) {
            self.store_slot(i, self.stack.at(i + 1));
            i += 1;
        }
        self.store_slot(self.stack_top - 1, lifted);
    }

    fn reset_stack(ref self) {
        self.stack.clear();
        self.stack_top = 0;
        self.frame_count = 0;
        self.open_upvalues.clear();
    }

    // --- Diagnostics -----------------------------------------------------

    fn runtime_error(ref self, message: string, line: int) {
        var text = string();
        text.append("runtime error: ");
        text.append(message);
        text.append(" (line ");
        text.append(to_string(line));
        text.append(")");
        println(text);
        self.print_backtrace();
        self.had_error = true;
    }

    fn print_backtrace(self) {
        var i = self.frame_count - 1;
        while (i >= 0) {
            let ref frame: auto = self.frames.at(i);
            var line = string();
            line.append("  at frame ");
            line.append(to_string(i));
            line.append(" ip ");
            line.append(to_string(frame.ip));
            println(line);
            i -= 1;
        }
    }

    fn print_stack(self) {
        for (value in self.stack) {
            print(self.to_display(value));
            print(" ");
        }
        println("");
    }

    // --- Globals ---------------------------------------------------------

    fn load_global(ref self, name: Value) {
        let key = self.string_of(name);
        if (not self.globals.contains(key)) {
            self.runtime_error("undefined variable", 0);
            return;
        }
        self.push(self.globals.at(key));
    }

    fn assign_global(ref self, name: Value) {
        let key = self.string_of(name);
        if (not self.globals.contains(key)) {
            self.runtime_error("assignment to undefined variable", 0);
            return;
        }
        Discard(self.globals.insert(key, self.peek(0)));
    }

    fn define_global(ref self, name: Value) {
        Discard(self.globals.insert(self.string_of(name), self.pop()));
    }

    fn string_of(self, value: Value) -> string {
        Assert(value.is_object());
        return self.strings_by_handle.at(value.object);
    }

    // --- Properties and subscripts ---------------------------------------

    fn get_property(ref self, name: Value) {
        let target = self.pop();
        if (not target.is_object()) {
            self.runtime_error("only objects have properties", 0);
            return;
        }
        self.push(self.lookup_field(target.object, self.string_of(name)));
    }

    fn set_property(ref self, name: Value) {
        let value = self.pop();
        let target = self.peek(0);
        if (not target.is_object()) {
            self.runtime_error("only objects have properties", 0);
            return;
        }
        self.store_field(target.object, self.string_of(name), value);
    }

    fn lookup_field(ref self, handle: int, name: string) -> Value {
        Discard(handle);
        Discard(name);
        return make_nil();
    }

    fn store_field(ref self, handle: int, name: string, value: Value) {
        Discard(handle);
        Discard(name);
        Discard(value);
    }

    fn index_get(ref self) {
        let key = self.pop();
        let target = self.pop();
        if (not target.is_object()) {
            self.runtime_error("value is not indexable", 0);
            return;
        }
        let ref container: auto = self.arrays.at(target.object);
        let index = key.as_number() / FIXED_SCALE;
        if (not container.in_bounds(index)) {
            self.runtime_error("index out of range", 0);
            return;
        }
        self.push(container.at(index));
    }

    fn index_set(ref self) {
        let value = self.pop();
        let key = self.pop();
        let target = self.pop();
        let ref container: auto = self.arrays.at(target.object);
        let index = key.as_number() / FIXED_SCALE;
        if (not container.in_bounds(index)) {
            self.runtime_error("index out of range", 0);
            return;
        }
        let ref slot: auto = container.at(index);
        slot.kind = value.kind;
        slot.number = value.number;
        slot.object = value.object;
        self.push(value);
    }

    // --- Comparison ------------------------------------------------------

    fn compare_equal(ref self, invert: bool) {
        let b = self.pop();
        let a = self.pop();
        let same = values_equal(a, b);
        if (invert) {
            self.push(make_bool(not same));
        } else {
            self.push(make_bool(same));
        }
    }

    fn compare_order(ref self, op: OpCode) {
        let b = self.pop();
        let a = self.pop();
        if (not a.is_number() or not b.is_number()) {
            self.runtime_error("operands must be numbers", 0);
            return;
        }
        let l = a.as_number();
        let r = b.as_number();
        if (op == OpCode.Greater) {
            self.push(make_bool(l > r));
        } else { if (op == OpCode.GreaterEqual) {
            self.push(make_bool(l >= r));
        } else { if (op == OpCode.Less) {
            self.push(make_bool(l < r));
        } else {
            self.push(make_bool(l <= r));
        }
        } }
    }

    // --- Arithmetic ------------------------------------------------------

    // All numbers are fixed-point with FIXED_SCALE fractional units, so
    // multiplication and division need a rescale afterwards.
    fn arith(ref self, op: OpCode) {
        let b = self.pop();
        let a = self.pop();
        if (not a.is_number() or not b.is_number()) {
            self.runtime_error("operands must be numbers", 0);
            return;
        }
        let l = a.as_number();
        let r = b.as_number();
        if (op == OpCode.Add) {
            self.push(make_number(l + r));
        } else { if (op == OpCode.Subtract) {
            self.push(make_number(l - r));
        } else { if (op == OpCode.Multiply) {
            self.push(make_number(l * r / FIXED_SCALE));
        } else { if (op == OpCode.Divide) {
            self.push(make_number(l * FIXED_SCALE / r));
        } else { if (op == OpCode.Modulo) {
            self.push(make_number(l % r));
        } else {
            self.push(make_number(self.power(l, r / FIXED_SCALE)));
        }
        } } } }
    }

    fn power(self, base_: int, exponent: int) -> int {
        var result = FIXED_SCALE;
        var i: int = 0;
        while (i < exponent) {
            result = result * base_ / FIXED_SCALE;
            i += 1;
        }
        return result;
    }

    fn negate(ref self) {
        let value = self.pop();
        if (not value.is_number()) {
            self.runtime_error("operand must be a number", 0);
            return;
        }
        self.push(make_number(-value.as_number()));
    }

    fn adjust(ref self, delta: int) {
        let value = self.pop();
        self.push(make_number(value.as_number() + delta));
    }

    // --- Bitwise ---------------------------------------------------------

    fn bitwise(ref self, op: OpCode) {
        let b = self.pop();
        let a = self.pop();
        let l = a.as_number() / FIXED_SCALE;
        let r = b.as_number() / FIXED_SCALE;
        if (op == OpCode.BitAnd) {
            self.push(make_number((l & r) * FIXED_SCALE));
        } else { if (op == OpCode.BitOr) {
            self.push(make_number((l | r) * FIXED_SCALE));
        } else { if (op == OpCode.BitXor) {
            self.push(make_number((l ^ r) * FIXED_SCALE));
        } else { if (op == OpCode.ShiftLeft) {
            self.push(make_number((l << r) * FIXED_SCALE));
        } else { if (op == OpCode.ShiftRight) {
            self.push(make_number((l >> r) * FIXED_SCALE));
        } else {
            self.push(make_number(((l >> r) & OPERAND_MASK) * FIXED_SCALE));
        }
        } } } }
    }

    fn bit_not(ref self) {
        let value = self.pop();
        let raw = value.as_number() / FIXED_SCALE;
        self.push(make_number(^raw * FIXED_SCALE));
    }

    // --- Strings, arrays and maps ----------------------------------------

    fn concat_strings(ref self) {
        let b = self.pop();
        let a = self.pop();
        var joined = self.string_of(a);
        joined.append(self.string_of(b));
        self.push(self.intern(joined));
    }

    fn intern(ref self, text: string) -> Value {
        if (self.interned.contains(text)) {
            return make_object(self.interned.at(text));
        }
        let handle = self.allocate(ObjectKind.String, text.size());
        Discard(self.interned.insert(text, handle));
        Discard(self.strings_by_handle.insert(handle, text));
        return make_object(handle);
    }

    fn new_array(ref self, count: int) {
        let handle = self.allocate(ObjectKind.Array, count);
        let ref target: auto = self.arrays.at(handle);
        var i: int = 0;
        while (i < count) {
            target.items.push_back(self.peek(count - 1 - i));
            i += 1;
        }
        self.pop_n(count);
        self.push(make_object(handle));
    }

    fn array_push(ref self) {
        let value = self.pop();
        let ref target: auto = self.arrays.at(self.peek(0).as_object());
        target.items.push_back(value);
    }

    fn array_length(ref self) {
        let target = self.pop();
        self.push(make_number(self.arrays.at(target.as_object()).size() * FIXED_SCALE));
    }

    fn array_slice(ref self) {
        let length = self.pop().as_number() / FIXED_SCALE;
        let begin = self.pop().as_number() / FIXED_SCALE;
        let source = self.pop();
        let handle = self.allocate(ObjectKind.Array, length);
        let ref target: auto = self.arrays.at(handle);
        let ref original: auto = self.arrays.at(source.as_object());
        for (i in range(begin, begin + length)) {
            if (original.in_bounds(i)) {
                target.items.push_back(original.at(i));
            }
        }
        self.push(make_object(handle));
    }

    fn array_concat(ref self) {
        let b = self.pop();
        let a = self.pop();
        let handle = self.allocate(ObjectKind.Array, 0);
        let ref target: auto = self.arrays.at(handle);
        for (value in self.arrays.at(a.as_object()).items) {
            target.items.push_back(value);
        }
        for (value in self.arrays.at(b.as_object()).items) {
            target.items.push_back(value);
        }
        self.push(make_object(handle));
    }

    fn new_map(ref self, pair_count: int) {
        let handle = self.allocate(ObjectKind.Map, pair_count);
        let ref target: auto = self.maps.at(handle);
        var i: int = 0;
        while (i < pair_count) {
            target.values.push_back(self.pop());
            target.keys.push_back(self.pop());
            i += 1;
        }
        self.push(make_object(handle));
    }

    fn map_insert(ref self) {
        let value = self.pop();
        let key = self.pop();
        let ref target: auto = self.maps.at(self.peek(0).as_object());
        let existing = target.find(key);
        if (existing >= 0) {
            self.store_slot_in(target.values, existing, value);
        } else {
            target.keys.push_back(key);
            target.values.push_back(value);
        }
    }

    fn store_slot_in(ref self, ref slots: vector(Value), index: int, value: Value) {
        let ref slot: auto = slots.at(index);
        slot.kind = value.kind;
        slot.number = value.number;
        slot.object = value.object;
        slot.boolean = value.boolean;
    }

    fn map_lookup(ref self) {
        let key = self.pop();
        let ref target: auto = self.maps.at(self.pop().as_object());
        let index = target.find(key);
        if (index < 0) {
            self.push(make_nil());
        } else {
            self.push(target.values.at(index));
        }
    }

    fn map_erase(ref self) {
        let key = self.pop();
        let ref target: auto = self.maps.at(self.peek(0).as_object());
        let index = target.find(key);
        if (index >= 0) {
            target.keys.erase(index);
            target.values.erase(index);
        }
    }

    fn map_length(ref self) {
        let target = self.pop();
        self.push(make_number(self.maps.at(target.as_object()).size() * FIXED_SCALE));
    }

    fn length_of(ref self) {
        let value = self.peek(0);
        if (not value.is_object()) {
            self.runtime_error("value has no length", 0);
            return;
        }
        let kind = self.objects.at(value.as_object()).kind;
        if (kind == ObjectKind.Array) {
            self.array_length();
        } else { if (kind == ObjectKind.Map) {
            self.map_length();
        } else {
            let text = self.string_of(self.pop());
            self.push(make_number(text.size() * FIXED_SCALE));
        }
        }
    }

    fn type_of(ref self) {
        let value = self.pop();
        self.push(self.intern(self.type_name(value)));
    }

    fn type_name(self, value: Value) -> string {
        if (value.is_nil()) {
            return "nil";
        } else { if (value.is_bool()) {
            return "bool";
        } else { if (value.is_number()) {
            return "number";
        } else {
            return object_kind_name(self.objects.at(value.object).kind);
        }
        } }
    }

    fn to_display(self, value: Value) -> string {
        if (value.is_nil()) {
            return "nil";
        }
        if (value.is_bool()) {
            if (value.boolean) {
                return "true";
            } else {
                return "false";
            }
        }
        if (value.is_number()) {
            return format_fixed(value.number);
        }
        return self.string_of(value);
    }

    fn import_module(ref self, name: Value) -> Value {
        let key = self.string_of(name);
        if (self.modules.contains(key)) {
            return self.modules.at(key);
        }
        self.runtime_error("no such module", 0);
        return make_nil();
    }

    // A cooperative yield point so long-running loops stay interruptible.
    fn check_interrupt(ref self) {
        self.steps += 1;
        if (self.steps % 4096 == 0) {
            if (self.interrupted) {
                self.runtime_error("execution interrupted", 0);
            }
        }
    }

    // --- Calls -----------------------------------------------------------

    fn call_value(ref self, callee: Value, argument_count: int) {
        if (not callee.is_object()) {
            self.runtime_error("can only call functions", 0);
            return;
        }
        let kind = self.objects.at(callee.as_object()).kind;
        if (kind == ObjectKind.Closure) {
            self.call_closure(callee.as_object(), argument_count, false);
        } else { if (kind == ObjectKind.Native) {
            self.call_native(argument_count);
        } else { if (kind == ObjectKind.BoundMethod) {
            self.call_closure(self.bound_target(callee.as_object()), argument_count, false);
        } else {
            self.runtime_error("value is not callable", 0);
        }
        } }
    }

    fn call_closure(ref self, handle: int, argument_count: int, tail: bool) {
        let ref closure: auto = self.closures.at(handle);
        if (argument_count != closure.arity()) {
            self.runtime_error("wrong number of arguments", 0);
            return;
        }
        if (self.frame_count >= FRAMES_MAX) {
            self.runtime_error("stack overflow", 0);
            return;
        }
        var frame: CallFrame;
        frame.closure = handle;
        frame.ip = 0;
        frame.slot_base = self.stack_top - argument_count - 1;
        frame.argument_count = argument_count;
        frame.is_tail = tail;
        self.frames.push_back(frame);
        self.frame_count += 1;
    }

    fn tail_call(ref self, argument_count: int) {
        let callee = self.peek(argument_count);
        let ref frame: auto = self.frames.at(self.frame_count - 1);
        let base_ = frame.slot_base;
        self.frame_count -= 1;
        self.frames.pop_back();
        self.shift_arguments(base_, argument_count);
        self.call_closure(callee.as_object(), argument_count, true);
    }

    // Slide the pending arguments down over the frame being replaced.
    fn shift_arguments(ref self, base_: int, argument_count: int) {
        var i: int = 0;
        while (i <= argument_count) {
            self.store_slot(base_ + i, self.stack.at(self.stack_top - argument_count - 1 + i));
            i += 1;
        }
        self.stack_top = base_ + argument_count + 1;
    }

    fn invoke(ref self, name: Value, argument_count: int) {
        let receiver = self.peek(argument_count);
        if (not receiver.is_object()) {
            self.runtime_error("only objects have methods", 0);
            return;
        }
        let method = self.lookup_field(receiver.as_object(), self.string_of(name));
        self.call_value(method, argument_count);
    }

    fn call_native(ref self, argument_count: int) {
        let callee = self.peek(argument_count);
        let slot = self.natives.at(callee.as_object()).slot;
        let result = self.dispatch_native(slot, argument_count);
        self.pop_n(argument_count + 1);
        self.push(result);
    }

    fn dispatch_native(ref self, slot: int, argument_count: int) -> Value {
        Discard(slot);
        Discard(argument_count);
        return make_nil();
    }

    fn bound_target(self, handle: int) -> int {
        return self.bound_methods.at(handle);
    }

    fn return_from(ref self, result: Value) {
        let ref frame: auto = self.frames.at(self.frame_count - 1);
        let base_ = frame.slot_base;
        self.close_upvalues(base_);
        self.frame_count -= 1;
        self.frames.pop_back();
        while (self.stack_top > base_) {
            Discard(self.pop());
        }
        if (self.frame_count > 0) {
            self.push(result);
        } else {
            self.last_result = result;
        }
    }

    // --- Upvalues --------------------------------------------------------

    // Capture the stack slot at `index`, reusing an existing open upvalue if
    // one already points there. The open list is kept sorted by slot so the
    // scan stops early.
    fn capture_upvalue(ref self, index: int) -> int {
        for (handle in self.open_upvalues) {
            if (self.upvalues.at(handle).location == index) {
                return handle;
            }
        }
        let created = self.allocate(ObjectKind.Upvalue, 0);
        let ref target: auto = self.upvalues.at(created);
        target.location = index;
        self.open_upvalues.push_back(created);
        return created;
    }

    // Close every open upvalue at or above `floor`, copying the live stack
    // slot into the upvalue itself.
    fn close_upvalues(ref self, floor: int) {
        var i: int = 0;
        while (i < self.open_upvalues.size()) {
            let handle = self.open_upvalues.at(i);
            let ref target: auto = self.upvalues.at(handle);
            if (target.location >= floor) {
                target.closed = self.stack.at(target.location);
                target.location = -1;
                self.open_upvalues.erase(i);
            } else {
                i += 1;
            }
        }
    }

    fn read_upvalue(ref self, closure: int, index: int) -> Value {
        let handle = self.closures.at(closure).upvalues.at(index);
        let ref target: auto = self.upvalues.at(handle);
        if (target.is_open()) {
            return self.stack.at(target.location);
        }
        return target.closed;
    }

    fn write_upvalue(ref self, closure: int, index: int, value: Value) {
        let handle = self.closures.at(closure).upvalues.at(index);
        let ref target: auto = self.upvalues.at(handle);
        if (target.is_open()) {
            self.store_slot(target.location, value);
        } else {
            target.closed = value;
        }
    }

    fn make_closure(ref self, function: Value, frame: CallFrame) {
        let handle = self.allocate(ObjectKind.Closure, 0);
        let ref created: auto = self.closures.at(handle);
        created.function = self.functions.at(function.as_object());
        for (i in range(0, created.function.upvalue_count)) {
            let ref descriptor: auto = self.upvalue_plan.at(i);
            if (descriptor.first) {
                created.upvalues.push_back(self.capture_upvalue(frame.slot_base + descriptor.second));
            } else {
                created.upvalues.push_back(self.closures.at(frame.closure).upvalues.at(descriptor.second));
            }
        }
        self.push(make_object(handle));
    }

    // --- The interpreter loop --------------------------------------------

    // Ordering in the cascade below matters for throughput: the hottest
    // opcodes (constants, locals, arithmetic) are tested first and the cold
    // ones (import, halt) last. There is no `switch` in the language, so
    // this is a plain chain of equality tests whose nested `else` blocks are
    // all closed in one run of braces at the bottom.
    fn run(ref self) -> InterpretResult {
        self.steps = 0;
        self.had_error = false;
        while (true) {
            if (self.frame_count == 0) {
                break;
            }
            let ref frame: auto = self.frames.at(self.frame_count - 1);
            let ref chunk: auto = self.closures.at(frame.closure).function.chunk;
            if (frame.ip >= chunk.size()) {
                // Falling off the end of a chunk is an implicit `return nil`.
                self.return_from(make_nil());
                continue;
            }
            if (self.trace_execution) {
                self.print_stack();
                println(disassemble_instruction(chunk, frame.ip));
            }
            let ref ins: auto = chunk.code.at(frame.ip);
            let op = ins.op;
            frame.ip += 1;
            if (self.bytes_allocated > self.next_gc) {
                self.collect_garbage();
            }

            // --- constants, literals and stack shuffling ---
            if (op == OpCode.Constant) {
                self.push(chunk.constant_at(ins.operand));
            } else { if (op == OpCode.ConstantLong) {
                self.push(chunk.constant_at(ins.operand & OPERAND_MASK));
            } else { if (op == OpCode.Nil) {
                self.push(make_nil());
            } else { if (op == OpCode.True) {
                self.push(make_bool(true));
            } else { if (op == OpCode.False) {
                self.push(make_bool(false));
            } else { if (op == OpCode.Pop) {
                Discard(self.pop());
            } else { if (op == OpCode.PopN) {
                self.pop_n(ins.operand);
            } else { if (op == OpCode.Dup) {
                self.push(self.peek(0));
            } else { if (op == OpCode.Swap) {
                self.swap_top();
            } else { if (op == OpCode.Rotate) {
                // Bring the n-th slot to the top, sliding the rest down.
                self.rotate(ins.operand);
            // --- variable access: locals, globals, upvalues, members ---
            } else { if (op == OpCode.GetLocal) {
                self.push(self.stack.at(frame.local(ins.operand)));
            } else { if (op == OpCode.SetLocal) {
                self.store_slot(frame.local(ins.operand), self.peek(0));
            } else { if (op == OpCode.GetGlobal) {
                self.load_global(chunk.constant_at(ins.operand));
            } else { if (op == OpCode.SetGlobal) {
                self.assign_global(chunk.constant_at(ins.operand));
            } else { if (op == OpCode.DefineGlobal) {
                self.define_global(chunk.constant_at(ins.operand));
            } else { if (op == OpCode.GetUpvalue) {
                self.push(self.read_upvalue(frame.closure, ins.operand));
            } else { if (op == OpCode.SetUpvalue) {
                self.write_upvalue(frame.closure, ins.operand, self.peek(0));
            } else { if (op == OpCode.GetProperty) {
                self.get_property(chunk.constant_at(ins.operand));
            } else { if (op == OpCode.SetProperty) {
                self.set_property(chunk.constant_at(ins.operand));
            } else { if (op == OpCode.GetIndex) {
                self.index_get();
            } else { if (op == OpCode.SetIndex) {
                self.index_set();
            // --- comparison ---
            } else { if (op == OpCode.Equal) {
                self.compare_equal(false);
            } else { if (op == OpCode.NotEqual) {
                self.compare_equal(true);
            } else { if (op == OpCode.Greater) {
                self.compare_order(OpCode.Greater);
            } else { if (op == OpCode.GreaterEqual) {
                self.compare_order(OpCode.GreaterEqual);
            } else { if (op == OpCode.Less) {
                self.compare_order(OpCode.Less);
            } else { if (op == OpCode.LessEqual) {
                self.compare_order(OpCode.LessEqual);
            // --- arithmetic; operands are fixed-point, scaled by FIXED_SCALE ---
            } else { if (op == OpCode.Add) {
                // Add is overloaded: strings concatenate, numbers add.
                if (self.peek(0).is_object() and self.peek(1).is_object()) {
                    self.concat_strings();
                } else {
                    self.arith(OpCode.Add);
                }
            } else { if (op == OpCode.Subtract) {
                self.arith(OpCode.Subtract);
            } else { if (op == OpCode.Multiply) {
                self.arith(OpCode.Multiply);
            } else { if (op == OpCode.Divide) {
                if (self.peek(0).as_number() == 0) {
                    self.runtime_error("division by zero", chunk.line_at(frame.ip));
                } else {
                    self.arith(OpCode.Divide);
                }
            } else { if (op == OpCode.Modulo) {
                if (self.peek(0).as_number() == 0) {
                    self.runtime_error("modulo by zero", chunk.line_at(frame.ip));
                } else {
                    self.arith(OpCode.Modulo);
                }
            } else { if (op == OpCode.Power) {
                self.arith(OpCode.Power);
            // --- unary operators ---
            } else { if (op == OpCode.Negate) {
                self.negate();
            } else { if (op == OpCode.Increment) {
                self.adjust(FIXED_SCALE);
            } else { if (op == OpCode.Decrement) {
                self.adjust(-FIXED_SCALE);
            } else { if (op == OpCode.Not) {
                let value = self.pop();
                self.push(make_bool(not value.is_truthy()));
            // --- bitwise; operands are truncated to whole integers first ---
            } else { if (op == OpCode.BitAnd) {
                self.bitwise(OpCode.BitAnd);
            } else { if (op == OpCode.BitOr) {
                self.bitwise(OpCode.BitOr);
            } else { if (op == OpCode.BitXor) {
                self.bitwise(OpCode.BitXor);
            } else { if (op == OpCode.BitNot) {
                self.bit_not();
            } else { if (op == OpCode.ShiftLeft) {
                self.bitwise(OpCode.ShiftLeft);
            } else { if (op == OpCode.ShiftRight) {
                self.bitwise(OpCode.ShiftRight);
            } else { if (op == OpCode.ShiftRightLogical) {
                self.bitwise(OpCode.ShiftRightLogical);
            // --- control flow ---
            } else { if (op == OpCode.Jump) {
                frame.jump_to(ins.operand);
            } else { if (op == OpCode.JumpIfFalse) {
                let cond = self.pop();
                if (not cond.is_truthy()) {
                    frame.jump_to(ins.operand);
                }
            } else { if (op == OpCode.JumpIfTrue) {
                let cond = self.pop();
                if (cond.is_truthy()) {
                    frame.jump_to(ins.operand);
                }
            } else { if (op == OpCode.JumpIfNil) {
                // Peeks rather than pops: this backs the nil-coalescing lowering.
                if (self.peek(0).is_nil()) {
                    frame.jump_to(ins.operand);
                }
            } else { if (op == OpCode.JumpIfNotNil) {
                if (not self.peek(0).is_nil()) {
                    frame.jump_to(ins.operand);
                }
            } else { if (op == OpCode.Loop) {
                // Backwards edge; the operand is an absolute index, not a delta.
                frame.jump_to(ins.operand);
                self.check_interrupt();
            // --- calls ---
            } else { if (op == OpCode.Call) {
                self.call_value(self.peek(ins.operand), ins.operand);
            } else { if (op == OpCode.Invoke) {
                self.invoke(chunk.constant_at(ins.operand), ins.extra);
            } else { if (op == OpCode.TailCall) {
                // Reuses the current frame instead of pushing a new one.
                self.tail_call(ins.operand);
            } else { if (op == OpCode.CallNative) {
                self.call_native(ins.operand);
            // --- closures and returns ---
            } else { if (op == OpCode.Closure) {
                self.make_closure(chunk.constant_at(ins.operand), frame);
            } else { if (op == OpCode.CloseUpvalue) {
                self.close_upvalues(self.stack_top - 1);
                Discard(self.pop());
            } else { if (op == OpCode.Return) {
                self.return_from(self.pop());
            } else { if (op == OpCode.ReturnNil) {
                self.return_from(make_nil());
            // --- arrays ---
            } else { if (op == OpCode.NewArray) {
                self.new_array(ins.operand);
            } else { if (op == OpCode.ArrayPush) {
                self.array_push();
            } else { if (op == OpCode.ArrayLength) {
                self.array_length();
            } else { if (op == OpCode.ArraySlice) {
                self.array_slice();
            } else { if (op == OpCode.ArrayConcat) {
                self.array_concat();
            // --- maps ---
            } else { if (op == OpCode.NewMap) {
                self.new_map(ins.operand);
            } else { if (op == OpCode.MapInsert) {
                self.map_insert();
            } else { if (op == OpCode.MapLookup) {
                self.map_lookup();
            } else { if (op == OpCode.MapErase) {
                self.map_erase();
            } else { if (op == OpCode.MapLength) {
                self.map_length();
            // --- builtins, diagnostics and terminators ---
            } else { if (op == OpCode.Concat) {
                self.concat_strings();
            } else { if (op == OpCode.Length) {
                self.length_of();
            } else { if (op == OpCode.TypeOf) {
                self.type_of();
            } else { if (op == OpCode.Print) {
                let value = self.pop();
                println(self.to_display(value));
            } else { if (op == OpCode.Assert) {
                let cond = self.pop();
                if (not cond.is_truthy()) {
                    self.runtime_error("assertion failed", ins.operand);
                }
            } else { if (op == OpCode.Import) {
                self.push(self.import_module(chunk.constant_at(ins.operand)));
            } else { if (op == OpCode.Nop) {
                // Alignment padding left behind by the peephole pass.
                Discard(ins.operand);
            } else { if (op == OpCode.Halt) {
                self.frame_count = 0;
                break;
            } else {
                // An opcode this build does not know about. Serialized chunks
                // carry a version stamp, so this only fires on corruption.
                self.runtime_error("unknown opcode", chunk.line_at(frame.ip));
            }
            } } } } } } } } } } } } } } } } } } }
            } } } } } } } } } } } } } } } } } } }
            } } } } } } } } } } } } } } } } } } }
            } } } } } } } } } } } } } } } } } }

            if (self.had_error) {
                self.reset_stack();
                return InterpretResult.RuntimeError;
            }
        }
        return InterpretResult.Ok;
    }

    // --- Allocation and the mark-sweep collector -------------------------

    // Every heap object is registered here so the sweep phase can walk them
    // all. Allocation is also the only place a collection can start, which
    // keeps the set of live roots easy to reason about.
    fn allocate(ref self, kind: ObjectKind, hint: int) -> int {
        self.bytes_allocated += 32 + hint * 16;
        if (self.bytes_allocated > self.next_gc) {
            self.collect_garbage();
        }
        var header: Obj;
        header.kind = kind;
        header.marked = false;
        header.handle = self.objects.size();
        self.objects.push_back(header);
        return header.handle;
    }

    fn collect_garbage(ref self) {
        let before = self.bytes_allocated;
        self.gray_stack.clear();
        self.mark_roots();
        self.trace_references();
        self.sweep();
        self.next_gc = self.bytes_allocated * GC_GROWTH_PERCENT / 100;
        if (self.next_gc < GC_MIN_THRESHOLD) {
            self.next_gc = GC_MIN_THRESHOLD;
        }
        if (self.trace_gc) {
            var line = string();
            line.append("gc: reclaimed ");
            line.append(to_string(before - self.bytes_allocated));
            line.append(" bytes");
            println(line);
        }
    }

    // Roots are the value stack, every live call frame, the open upvalue
    // list, the globals table and the module cache.
    fn mark_roots(ref self) {
        for (value in self.stack) {
            self.mark_value(value);
        }
        for (frame in self.frames) {
            if (frame.closure >= 0) {
                self.mark_object(frame.closure);
            }
        }
        for (handle in self.open_upvalues) {
            self.mark_object(handle);
        }
        for (entry in self.globals) {
            self.mark_value(entry.second);
        }
        for (entry in self.modules) {
            self.mark_value(entry.second);
        }
        self.mark_value(self.last_result);
    }

    fn mark_value(ref self, value: Value) {
        if (not value.is_object()) {
            return;
        }
        self.mark_object(value.object);
    }

    fn mark_object(ref self, handle: int) {
        if (handle < 0) {
            return;
        }
        let ref header: auto = self.objects.at(handle);
        if (header.marked) {
            return;
        }
        header.marked = true;
        self.gray_stack.push_back(handle);
    }

    // Tri-colour marking: gray objects still have unvisited children.
    fn trace_references(ref self) {
        while (not self.gray_stack.empty()) {
            let handle = self.gray_stack.back();
            self.gray_stack.pop_back();
            self.blacken_object(handle);
        }
    }

    fn blacken_object(ref self, handle: int) {
        let kind = self.objects.at(handle).kind;
        if (kind == ObjectKind.Array) {
            for (value in self.arrays.at(handle).items) {
                self.mark_value(value);
            }
        } else { if (kind == ObjectKind.Map) {
            for (value in self.maps.at(handle).keys) {
                self.mark_value(value);
            }
            for (value in self.maps.at(handle).values) {
                self.mark_value(value);
            }
        } else { if (kind == ObjectKind.Closure) {
            for (upvalue in self.closures.at(handle).upvalues) {
                self.mark_object(upvalue);
            }
            self.mark_chunk(self.closures.at(handle).function.chunk);
        } else { if (kind == ObjectKind.Upvalue) {
            self.mark_value(self.upvalues.at(handle).closed);
        } else { if (kind == ObjectKind.Function) {
            self.mark_chunk(self.functions.at(handle).chunk);
        } else { if (kind == ObjectKind.BoundMethod) {
            self.mark_object(self.bound_methods.at(handle));
        } else {
            // Strings and natives have no outgoing references.
            Discard(handle);
        }
        } } } } }
    }

    fn mark_chunk(ref self, chunk: Chunk) {
        for (value in chunk.constants) {
            self.mark_value(value);
        }
    }

    // Unmarked objects are released; survivors are cleared for the next
    // cycle. The object table itself is compacted lazily.
    fn sweep(ref self) {
        var i: int = 0;
        while (i < self.objects.size()) {
            let ref header: auto = self.objects.at(i);
            if (header.marked) {
                header.marked = false;
                i += 1;
            } else {
                self.free_object(i);
                i += 1;
            }
        }
    }

    fn free_object(ref self, handle: int) {
        let ref header: auto = self.objects.at(handle);
        self.bytes_allocated -= 32;
        if (header.kind == ObjectKind.Array) {
            self.arrays.at(handle).items.clear();
        } else { if (header.kind == ObjectKind.Map) {
            self.maps.at(handle).keys.clear();
            self.maps.at(handle).values.clear();
        } else { if (header.kind == ObjectKind.Closure) {
            self.closures.at(handle).upvalues.clear();
        } else {
            Discard(handle);
        }
        } }
        header.handle = -1;
    }

    // --- State -----------------------------------------------------------

    var stack: vector(Value);
    var stack_top: int = 0;
    var frames: vector(CallFrame);
    var frame_count: int = 0;

    var globals: map(string, Value);
    var modules: map(string, Value);
    var interned: map(string, int);
    var strings_by_handle: map(int, string);

    // Parallel object tables, indexed by the handle stored in a Value.
    var objects: vector(Obj);
    var arrays: vector(ObjArray);
    var maps: vector(ObjMap);
    var closures: vector(ObjClosure);
    var functions: vector(ObjFunction);
    var upvalues: vector(ObjUpvalue);
    var natives: vector(ObjNative);
    var bound_methods: vector(int);

    // Capture plan for the closure currently being built: `first` is true
    // when the upvalue comes from the enclosing frame's locals.
    var upvalue_plan: vector(pair(bool, int));
    var open_upvalues: vector(int);
    var gray_stack: vector(int);

    var bytes_allocated: int = 0;
    var next_gc: int = 1048576;
    var steps: int = 0;
    var last_result: Value;

    var had_error: bool = false;
    var interrupted: bool = false;
    var trace_execution: bool = false;
    var trace_gc: bool = false;
}

// --- Free helpers --------------------------------------------------------

fn object_kind_name(kind: ObjectKind) -> string {
    if (kind == ObjectKind.String) {
        return "string";
    } else { if (kind == ObjectKind.Array) {
        return "array";
    } else { if (kind == ObjectKind.Map) {
        return "map";
    } else { if (kind == ObjectKind.Function) {
        return "function";
    } else { if (kind == ObjectKind.Closure) {
        return "closure";
    } else { if (kind == ObjectKind.Upvalue) {
        return "upvalue";
    } else { if (kind == ObjectKind.Native) {
        return "native";
    } else { if (kind == ObjectKind.BoundMethod) {
        return "method";
    } else { if (kind == ObjectKind.Instance) {
        return "instance";
    } else {
        return "class";
    }
    } } } } } } } }
}

// Render a fixed-point number: 1500 becomes "1.500".
fn format_fixed(value: int) -> string {
    var text = string();
    if (value < 0) {
        text.append("-");
    }
    let magnitude = abs(value);
    text.append(to_string(magnitude / FIXED_SCALE));
    let fraction = magnitude % FIXED_SCALE;
    if (fraction != 0) {
        text.append(".");
        var divisor = FIXED_SCALE / 10;
        while (divisor > 0) {
            text.append(to_string((fraction / divisor) % 10));
            divisor /= 10;
        }
    }
    return text;
}

// --- Entry point ---------------------------------------------------------

// Load a top-level chunk into a fresh frame and run it to completion.
fn interpret(ref machine: VM, chunk: Chunk) -> InterpretResult {
    machine.reset_stack();
    let handle = machine.allocate(ObjectKind.Closure, 0);
    let ref entry: auto = machine.closures.at(handle);
    entry.function.chunk = chunk;
    entry.function.arity = 0;
    machine.push(make_object(handle));
    machine.call_closure(handle, 0, false);
    let result = machine.run();
    if (result == InterpretResult.RuntimeError) {
        println("execution aborted");
    }
    return result;
}

// Verify a chunk before running it: the stack depth must never go negative
// and every jump target must land inside the chunk.
fn verify_chunk(chunk: Chunk) -> bool {
    var depth: int = 0;
    for (i in range(0, chunk.size())) {
        let ref ins: auto = chunk.code.at(i);
        depth += stack_effect(ins.op, ins.operand);
        if (depth < 0) {
            return false;
        }
        if (ins.is_jump()) {
            if (ins.operand < 0 or ins.operand >= chunk.size()) {
                return false;
            }
        }
        if (opcode_operand_count(ins.op) > 0) {
            if (opcode_operand_kind(ins.op) == OperandKind.ConstantSlot) {
                if (ins.operand >= chunk.constants.size()) {
                    return false;
                }
            }
        }
    }
    Prove(depth >= 0);
    return true;
}


