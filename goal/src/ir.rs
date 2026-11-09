use crate::object::SymbolId;
use crate::static_data::StaticLabelId;
use crate::type_system::{RegisterClass, TypeSpec};

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct FunctionId(pub u32);

/// Index into a function's virtual-register table.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct VirtualRegisterId(pub u32);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct LabelId(pub u32);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct StackSlot(pub u32);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct VirtualRegister {
    pub ty: TypeSpec,
    pub class: RegisterClass,
    /// Creation-order number within this register class.
    pub number: u32,
}

impl VirtualRegister {
    pub fn width(self) -> u16 {
        if self.class == RegisterClass::Gpr128 {
            128
        } else {
            64
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
#[repr(u8)]
pub enum Gpr {
    R0 = 0,
    At = 1,
    V0 = 2,
    V1 = 3,
    A0 = 4,
    A1 = 5,
    A2 = 6,
    A3 = 7,
    T0 = 8,
    T1 = 9,
    T2 = 10,
    T3 = 11,
    T4 = 12,
    T5 = 13,
    T6 = 14,
    T7 = 15,
    S0 = 16,
    S1 = 17,
    S2 = 18,
    S3 = 19,
    S4 = 20,
    S5 = 21,
    S6 = 22,
    S7 = 23,
    T8 = 24,
    T9 = 25,
    K0 = 26,
    K1 = 27,
    Gp = 28,
    Sp = 29,
    Fp = 30,
    Ra = 31,
}

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum Cop0Register {
    Debug,
    DataAddressBreakpoint,
    DataAddressBreakpointMask,
}

impl Cop0Register {
    pub const fn number(self) -> u8 {
        24
    }

    pub const fn select(self) -> u8 {
        match self {
            Self::Debug => 0,
            Self::DataAddressBreakpoint => 4,
            Self::DataAddressBreakpointMask => 5,
        }
    }
}

impl Gpr {
    pub const ALL: [Self; 32] = [
        Self::R0,
        Self::At,
        Self::V0,
        Self::V1,
        Self::A0,
        Self::A1,
        Self::A2,
        Self::A3,
        Self::T0,
        Self::T1,
        Self::T2,
        Self::T3,
        Self::T4,
        Self::T5,
        Self::T6,
        Self::T7,
        Self::S0,
        Self::S1,
        Self::S2,
        Self::S3,
        Self::S4,
        Self::S5,
        Self::S6,
        Self::S7,
        Self::T8,
        Self::T9,
        Self::K0,
        Self::K1,
        Self::Gp,
        Self::Sp,
        Self::Fp,
        Self::Ra,
    ];

    pub const fn number(self) -> u8 {
        self as u8
    }

    pub const fn name(self) -> &'static str {
        match self {
            Self::R0 => "r0",
            Self::At => "at",
            Self::V0 => "v0",
            Self::V1 => "v1",
            Self::A0 => "a0",
            Self::A1 => "a1",
            Self::A2 => "a2",
            Self::A3 => "a3",
            Self::T0 => "t0",
            Self::T1 => "t1",
            Self::T2 => "t2",
            Self::T3 => "t3",
            Self::T4 => "t4",
            Self::T5 => "t5",
            Self::T6 => "t6",
            Self::T7 => "t7",
            Self::S0 => "s0",
            Self::S1 => "s1",
            Self::S2 => "s2",
            Self::S3 => "s3",
            Self::S4 => "s4",
            Self::S5 => "s5",
            Self::S6 => "s6",
            Self::S7 => "s7",
            Self::T8 => "t8",
            Self::T9 => "t9",
            Self::K0 => "k0",
            Self::K1 => "k1",
            Self::Gp => "gp",
            Self::Sp => "sp",
            Self::Fp => "fp",
            Self::Ra => "ra",
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum PhysicalRegister {
    Gpr(Gpr),
    Fpr(u8),
}

impl PhysicalRegister {
    pub const ZERO: Self = Self::Gpr(Gpr::R0);
    pub const RET: Self = Self::Gpr(Gpr::V0);
    pub const CALL: Self = Self::Gpr(Gpr::T9);

    pub fn class(self) -> RegisterClass {
        match self {
            Self::Gpr(_) => RegisterClass::Gpr,
            Self::Fpr(_) => RegisterClass::Fpr,
        }
    }

    pub fn is_argument(self) -> bool {
        ARGUMENT_REGISTERS.contains(&self)
    }

    pub fn is_callee_saved(self) -> bool {
        matches!(
            self,
            Self::Gpr(Gpr::Gp | Gpr::S0 | Gpr::S1 | Gpr::S2 | Gpr::S3 | Gpr::S4 | Gpr::S5)
                | Self::Fpr(20 | 22 | 24 | 26 | 28 | 30)
        )
    }
}

pub const ARGUMENT_REGISTERS: [PhysicalRegister; 8] = [
    PhysicalRegister::Gpr(Gpr::A0),
    PhysicalRegister::Gpr(Gpr::A1),
    PhysicalRegister::Gpr(Gpr::A2),
    PhysicalRegister::Gpr(Gpr::A3),
    PhysicalRegister::Gpr(Gpr::T0),
    PhysicalRegister::Gpr(Gpr::T1),
    PhysicalRegister::Gpr(Gpr::T2),
    PhysicalRegister::Gpr(Gpr::T3),
];

pub const GPR_ALLOCATION_ORDER: [PhysicalRegister; 23] = [
    PhysicalRegister::Gpr(Gpr::V1),
    PhysicalRegister::Gpr(Gpr::A0),
    PhysicalRegister::Gpr(Gpr::A1),
    PhysicalRegister::Gpr(Gpr::A2),
    PhysicalRegister::Gpr(Gpr::A3),
    PhysicalRegister::Gpr(Gpr::T0),
    PhysicalRegister::Gpr(Gpr::T1),
    PhysicalRegister::Gpr(Gpr::T2),
    PhysicalRegister::Gpr(Gpr::T3),
    PhysicalRegister::Gpr(Gpr::T4),
    PhysicalRegister::Gpr(Gpr::T5),
    PhysicalRegister::Gpr(Gpr::T6),
    PhysicalRegister::Gpr(Gpr::T7),
    PhysicalRegister::Gpr(Gpr::T8),
    PhysicalRegister::Gpr(Gpr::T9),
    PhysicalRegister::Gpr(Gpr::V0),
    PhysicalRegister::Gpr(Gpr::Gp),
    PhysicalRegister::Gpr(Gpr::S5),
    PhysicalRegister::Gpr(Gpr::S4),
    PhysicalRegister::Gpr(Gpr::S3),
    PhysicalRegister::Gpr(Gpr::S2),
    PhysicalRegister::Gpr(Gpr::S1),
    PhysicalRegister::Gpr(Gpr::S0),
];

pub const FPR_ALLOCATION_ORDER: [PhysicalRegister; 26] = [
    PhysicalRegister::Fpr(0),
    PhysicalRegister::Fpr(1),
    PhysicalRegister::Fpr(2),
    PhysicalRegister::Fpr(3),
    PhysicalRegister::Fpr(4),
    PhysicalRegister::Fpr(5),
    PhysicalRegister::Fpr(6),
    PhysicalRegister::Fpr(7),
    PhysicalRegister::Fpr(8),
    PhysicalRegister::Fpr(9),
    PhysicalRegister::Fpr(10),
    PhysicalRegister::Fpr(11),
    PhysicalRegister::Fpr(12),
    PhysicalRegister::Fpr(13),
    PhysicalRegister::Fpr(14),
    PhysicalRegister::Fpr(15),
    PhysicalRegister::Fpr(16),
    PhysicalRegister::Fpr(17),
    PhysicalRegister::Fpr(18),
    PhysicalRegister::Fpr(19),
    PhysicalRegister::Fpr(30),
    PhysicalRegister::Fpr(28),
    PhysicalRegister::Fpr(26),
    PhysicalRegister::Fpr(24),
    PhysicalRegister::Fpr(22),
    PhysicalRegister::Fpr(20),
];

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum Register {
    Virtual(VirtualRegisterId),
    Physical(PhysicalRegister),
}

impl From<VirtualRegisterId> for Register {
    fn from(value: VirtualRegisterId) -> Self {
        Self::Virtual(value)
    }
}

impl From<PhysicalRegister> for Register {
    fn from(value: PhysicalRegister) -> Self {
        Self::Physical(value)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BranchCondition {
    Always,
    Zero(Register),
    NonZero(Register),
    Negative(Register),
    NonNegative(Register),
    Positive(Register),
    NonPositive(Register),
    Equal(Register, Register),
    NotEqual(Register, Register),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BranchDelay {
    /// The following instruction executes on both paths.
    Normal,
    /// The following instruction executes only when the branch is taken.
    Likely,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ArithmeticOperation {
    Add,
    Subtract,
    WordSubtract,
    Multiply,
    Divide,
    Remainder,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ShiftOperation {
    Left,
    LogicalRight,
    ArithmeticRight,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ConditionalMoveOperation {
    Zero,
    NonZero,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum IntegerComparison {
    LessThan,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum IntegerBitwiseOperation {
    Or,
    And,
    Nor,
    Xor,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MemoryLoad {
    SignedByte,
    UnsignedByte,
    SignedHalfword,
    UnsignedHalfword,
    SignedWord,
    UnsignedWord,
    Doubleword,
    Quadword,
    FloatWord,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MemoryStore {
    Byte,
    Halfword,
    Word,
    Doubleword,
    Quadword,
    FloatWord,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DoublewordPart {
    Low,
    High,
}

/// Register metadata computed once per instruction by the allocator.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct RegisterEffects {
    pub uses: Vec<Register>,
    pub definitions: Vec<Register>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum IrInstruction {
    LoadConstant {
        destination: Register,
        value: i64,
    },
    LoadFunctionHigh {
        destination: Register,
    },
    LoadFunctionLow {
        destination: Register,
        source: Register,
        function: FunctionId,
    },
    GetSymbolValue {
        destination: Register,
        symbol: SymbolId,
    },
    GetSymbol {
        destination: Register,
        symbol: SymbolId,
    },
    LoadFloatConstant {
        destination: Register,
        label: StaticLabelId,
    },
    LoadConstant64 {
        destination: Register,
        label: StaticLabelId,
    },
    LoadStaticAddress {
        destination: Register,
        label: StaticLabelId,
    },
    LoadMemory {
        operation: MemoryLoad,
        destination: Register,
        base: Register,
        offset: i16,
    },
    StoreMemory {
        operation: MemoryStore,
        source: Register,
        base: Register,
        offset: i16,
    },
    StoreSymbol {
        source: Register,
        symbol: SymbolId,
    },
    MoveToCop0 {
        source: Register,
        register: Cop0Register,
    },
    /// `must_emit` excludes early-return and explicit `rlet` copies from
    /// coalescing. Delay-slot moves are excluded automatically.
    Move {
        destination: Register,
        source: Register,
        must_emit: bool,
    },
    /// A general instruction for machine operations without allocator-specific
    /// behavior.
    Operation {
        uses: Vec<Register>,
        definitions: Vec<Register>,
    },
    Unary {
        destination: Register,
        source: Register,
    },
    ConditionalMove {
        operation: ConditionalMoveOperation,
        destination: Register,
        source: Register,
        condition: Register,
    },
    IntegerArithmetic {
        operation: ArithmeticOperation,
        signed: bool,
        destination: Register,
        left: Register,
        right: Register,
    },
    IntegerAddImmediate {
        destination: Register,
        source: Register,
        value: i16,
    },
    FloatArithmetic {
        operation: ArithmeticOperation,
        destination: Register,
        left: Register,
        right: Register,
    },
    VariableShift {
        operation: ShiftOperation,
        destination: Register,
        value: Register,
        amount: Register,
    },
    IntegerShiftImmediate {
        operation: ShiftOperation,
        destination: Register,
        value: Register,
        amount: u8,
    },
    IntegerCompare {
        comparison: IntegerComparison,
        signed: bool,
        destination: Register,
        left: Register,
        right: Register,
    },
    IntegerBitwise {
        operation: IntegerBitwiseOperation,
        destination: Register,
        left: Register,
        right: Register,
    },
    IntegerBitwiseImmediate {
        operation: IntegerBitwiseOperation,
        destination: Register,
        source: Register,
        value: u16,
    },
    Gpr128Upper64 {
        destination: Register,
        source: Register,
    },
    ExtractWord {
        part: DoublewordPart,
        destination: Register,
        source: Register,
    },
    /// Uses CALL and the first `argument_count` argument registers and defines
    /// all caller-saved registers. The following instruction is its delay slot.
    Call {
        argument_count: u8,
    },
    CallDelay,
    /// Copies a value to the ABI return register. Register allocation normally
    /// coalesces the value directly into that register.
    Return {
        value: Register,
        register: PhysicalRegister,
    },
    /// A branch's delay slot is the next instruction in the IR vector.
    Branch {
        condition: BranchCondition,
        target: LabelId,
        delay: BranchDelay,
    },
    /// Pad with EE nops until the following instruction has the requested
    /// power-of-two bit alignment.
    Align {
        bits: u16,
    },
    Nop,
}

impl IrInstruction {
    pub fn move_register(destination: impl Into<Register>, source: impl Into<Register>) -> Self {
        Self::Move {
            destination: destination.into(),
            source: source.into(),
            must_emit: false,
        }
    }

    pub fn branch_likely(condition: BranchCondition, target: LabelId) -> Self {
        Self::Branch {
            condition,
            target,
            delay: BranchDelay::Likely,
        }
    }

    pub fn variable_shift(
        operation: ShiftOperation,
        destination: impl Into<Register>,
        value: impl Into<Register>,
        amount: impl Into<Register>,
    ) -> Self {
        Self::VariableShift {
            operation,
            destination: destination.into(),
            value: value.into(),
            amount: amount.into(),
        }
    }

    pub fn integer_subtract(
        destination: impl Into<Register>,
        left: impl Into<Register>,
        right: impl Into<Register>,
    ) -> Self {
        Self::IntegerArithmetic {
            operation: ArithmeticOperation::Subtract,
            signed: true,
            destination: destination.into(),
            left: left.into(),
            right: right.into(),
        }
    }

    pub fn integer_compare(
        comparison: IntegerComparison,
        signed: bool,
        destination: impl Into<Register>,
        left: impl Into<Register>,
        right: impl Into<Register>,
    ) -> Self {
        Self::IntegerCompare {
            comparison,
            signed,
            destination: destination.into(),
            left: left.into(),
            right: right.into(),
        }
    }

    pub fn integer_bitwise(
        operation: IntegerBitwiseOperation,
        destination: impl Into<Register>,
        left: impl Into<Register>,
        right: impl Into<Register>,
    ) -> Self {
        Self::IntegerBitwise {
            operation,
            destination: destination.into(),
            left: left.into(),
            right: right.into(),
        }
    }

    pub fn conditional_move(
        operation: ConditionalMoveOperation,
        destination: impl Into<Register>,
        source: impl Into<Register>,
        condition: impl Into<Register>,
    ) -> Self {
        Self::ConditionalMove {
            operation,
            destination: destination.into(),
            source: source.into(),
            condition: condition.into(),
        }
    }

    pub const fn requires_frame_pointer(&self) -> bool {
        matches!(
            self,
            Self::LoadFloatConstant { .. }
                | Self::LoadConstant64 { .. }
                | Self::LoadStaticAddress { .. }
        )
    }

    pub fn register_effects(&self) -> RegisterEffects {
        let mut effects = RegisterEffects::default();
        match self {
            Self::LoadConstant { destination, .. } => {
                effects.definitions.push(*destination);
            }
            Self::LoadFunctionHigh { destination } => {
                effects.definitions.push(*destination);
            }
            Self::LoadFunctionLow {
                destination,
                source,
                ..
            } => {
                effects.uses.push(*source);
                effects.definitions.push(*destination);
            }
            Self::GetSymbolValue { destination, .. } | Self::GetSymbol { destination, .. } => {
                effects.definitions.push(*destination);
            }
            Self::LoadFloatConstant { destination, .. } => {
                effects.definitions.push(*destination);
            }
            Self::LoadConstant64 { destination, .. } => {
                effects.definitions.push(*destination);
            }
            Self::LoadStaticAddress { destination, .. } => {
                effects.definitions.push(*destination);
            }
            Self::LoadMemory {
                destination, base, ..
            } => {
                effects.uses.push(*base);
                effects.definitions.push(*destination);
            }
            Self::StoreMemory { source, base, .. } => {
                effects.uses.push(*source);
                effects.uses.push(*base);
            }
            Self::StoreSymbol { source, .. } => effects.uses.push(*source),
            Self::MoveToCop0 { source, .. } => effects.uses.push(*source),
            Self::Move {
                destination,
                source,
                ..
            } => {
                effects.uses.push(*source);
                effects.definitions.push(*destination);
            }
            Self::Operation { uses, definitions } => {
                effects.uses.extend_from_slice(uses);
                effects.definitions.extend_from_slice(definitions);
            }
            Self::Unary {
                destination,
                source,
            } => {
                effects.uses.push(*source);
                effects.definitions.push(*destination);
            }
            Self::ConditionalMove {
                destination,
                source,
                condition,
                ..
            } => {
                effects.uses.extend([*destination, *source, *condition]);
                effects.definitions.push(*destination);
            }
            Self::IntegerArithmetic {
                destination,
                left,
                right,
                ..
            }
            | Self::FloatArithmetic {
                destination,
                left,
                right,
                ..
            } => {
                effects.uses.extend([*left, *right]);
                effects.definitions.push(*destination);
            }
            Self::IntegerAddImmediate {
                destination,
                source,
                ..
            } => {
                effects.uses.push(*source);
                effects.definitions.push(*destination);
            }
            Self::VariableShift {
                destination,
                value,
                amount,
                ..
            } => {
                effects.uses.extend([*value, *amount]);
                effects.definitions.push(*destination);
            }
            Self::IntegerShiftImmediate {
                destination, value, ..
            } => {
                effects.uses.push(*value);
                effects.definitions.push(*destination);
            }
            Self::IntegerCompare {
                destination,
                left,
                right,
                ..
            } => {
                effects.uses.extend([*left, *right]);
                effects.definitions.push(*destination);
            }
            Self::IntegerBitwise {
                destination,
                left,
                right,
                ..
            } => {
                effects.uses.extend([*left, *right]);
                effects.definitions.push(*destination);
            }
            Self::IntegerBitwiseImmediate {
                destination,
                source,
                ..
            } => {
                effects.uses.push(*source);
                effects.definitions.push(*destination);
            }
            Self::Gpr128Upper64 {
                destination,
                source,
            }
            | Self::ExtractWord {
                destination,
                source,
                ..
            } => {
                effects.uses.push(*source);
                effects.definitions.push(*destination);
            }
            Self::Call { argument_count } => {
                effects
                    .uses
                    .push(Register::Physical(PhysicalRegister::CALL));
                effects.uses.extend(
                    ARGUMENT_REGISTERS
                        [..usize::from(*argument_count).min(ARGUMENT_REGISTERS.len())]
                        .iter()
                        .copied()
                        .map(Register::Physical),
                );
                effects
                    .definitions
                    .extend(call_clobbers().map(Register::Physical));
            }
            Self::CallDelay => {}
            Self::Return { value, register } => {
                effects.uses.push(*value);
                effects.definitions.push(Register::Physical(*register));
            }
            Self::Branch { condition, .. } => match condition {
                BranchCondition::Always => {}
                BranchCondition::Zero(register)
                | BranchCondition::NonZero(register)
                | BranchCondition::Negative(register)
                | BranchCondition::NonNegative(register)
                | BranchCondition::Positive(register)
                | BranchCondition::NonPositive(register) => {
                    effects.uses.push(*register);
                }
                BranchCondition::Equal(left, right) | BranchCondition::NotEqual(left, right) => {
                    effects.uses.extend([*left, *right]);
                }
            },
            Self::Align { .. } | Self::Nop => {}
        }
        effects
    }

    pub fn is_branch(&self) -> bool {
        matches!(self, Self::Branch { .. })
    }

    pub fn has_delay_slot(&self) -> bool {
        matches!(self, Self::Branch { .. } | Self::Call { .. })
    }

    pub fn map_registers(
        &self,
        mut map_use: impl FnMut(Register) -> Register,
        mut map_definition: impl FnMut(Register) -> Register,
    ) -> Self {
        match self {
            Self::LoadConstant { destination, value } => Self::LoadConstant {
                destination: map_definition(*destination),
                value: *value,
            },
            Self::LoadFunctionHigh { destination } => Self::LoadFunctionHigh {
                destination: map_definition(*destination),
            },
            Self::LoadFunctionLow {
                destination,
                source,
                function,
            } => Self::LoadFunctionLow {
                destination: map_definition(*destination),
                source: map_use(*source),
                function: *function,
            },
            Self::GetSymbolValue {
                destination,
                symbol,
            } => Self::GetSymbolValue {
                destination: map_definition(*destination),
                symbol: *symbol,
            },
            Self::GetSymbol {
                destination,
                symbol,
            } => Self::GetSymbol {
                destination: map_definition(*destination),
                symbol: *symbol,
            },
            Self::LoadFloatConstant { destination, label } => Self::LoadFloatConstant {
                destination: map_definition(*destination),
                label: *label,
            },
            Self::LoadConstant64 { destination, label } => Self::LoadConstant64 {
                destination: map_definition(*destination),
                label: *label,
            },
            Self::LoadStaticAddress { destination, label } => Self::LoadStaticAddress {
                destination: map_definition(*destination),
                label: *label,
            },
            Self::LoadMemory {
                operation,
                destination,
                base,
                offset,
            } => Self::LoadMemory {
                operation: *operation,
                destination: map_definition(*destination),
                base: map_use(*base),
                offset: *offset,
            },
            Self::StoreMemory {
                operation,
                source,
                base,
                offset,
            } => Self::StoreMemory {
                operation: *operation,
                source: map_use(*source),
                base: map_use(*base),
                offset: *offset,
            },
            Self::StoreSymbol { source, symbol } => Self::StoreSymbol {
                source: map_use(*source),
                symbol: *symbol,
            },
            Self::MoveToCop0 { source, register } => Self::MoveToCop0 {
                source: map_use(*source),
                register: *register,
            },
            Self::Move {
                destination,
                source,
                must_emit,
            } => Self::Move {
                destination: map_definition(*destination),
                source: map_use(*source),
                must_emit: *must_emit,
            },
            Self::Operation { uses, definitions } => Self::Operation {
                uses: uses.iter().copied().map(&mut map_use).collect(),
                definitions: definitions
                    .iter()
                    .copied()
                    .map(&mut map_definition)
                    .collect(),
            },
            Self::Unary {
                destination,
                source,
            } => Self::Unary {
                destination: map_definition(*destination),
                source: map_use(*source),
            },
            Self::ConditionalMove {
                operation,
                destination,
                source,
                condition,
            } => Self::ConditionalMove {
                operation: *operation,
                destination: map_definition(*destination),
                source: map_use(*source),
                condition: map_use(*condition),
            },
            Self::IntegerArithmetic {
                operation,
                signed,
                destination,
                left,
                right,
            } => Self::IntegerArithmetic {
                operation: *operation,
                signed: *signed,
                destination: map_definition(*destination),
                left: map_use(*left),
                right: map_use(*right),
            },
            Self::IntegerAddImmediate {
                destination,
                source,
                value,
            } => Self::IntegerAddImmediate {
                destination: map_definition(*destination),
                source: map_use(*source),
                value: *value,
            },
            Self::FloatArithmetic {
                operation,
                destination,
                left,
                right,
            } => Self::FloatArithmetic {
                operation: *operation,
                destination: map_definition(*destination),
                left: map_use(*left),
                right: map_use(*right),
            },
            Self::VariableShift {
                operation,
                destination,
                value,
                amount,
            } => Self::VariableShift {
                operation: *operation,
                destination: map_definition(*destination),
                value: map_use(*value),
                amount: map_use(*amount),
            },
            Self::IntegerShiftImmediate {
                operation,
                destination,
                value,
                amount,
            } => Self::IntegerShiftImmediate {
                operation: *operation,
                destination: map_definition(*destination),
                value: map_use(*value),
                amount: *amount,
            },
            Self::IntegerCompare {
                comparison,
                signed,
                destination,
                left,
                right,
            } => Self::IntegerCompare {
                comparison: *comparison,
                signed: *signed,
                destination: map_definition(*destination),
                left: map_use(*left),
                right: map_use(*right),
            },
            Self::IntegerBitwise {
                operation,
                destination,
                left,
                right,
            } => Self::IntegerBitwise {
                operation: *operation,
                destination: map_definition(*destination),
                left: map_use(*left),
                right: map_use(*right),
            },
            Self::IntegerBitwiseImmediate {
                operation,
                destination,
                source,
                value,
            } => Self::IntegerBitwiseImmediate {
                operation: *operation,
                destination: map_definition(*destination),
                source: map_use(*source),
                value: *value,
            },
            Self::Gpr128Upper64 {
                destination,
                source,
            } => Self::Gpr128Upper64 {
                destination: map_definition(*destination),
                source: map_use(*source),
            },
            Self::ExtractWord {
                part,
                destination,
                source,
            } => Self::ExtractWord {
                part: *part,
                destination: map_definition(*destination),
                source: map_use(*source),
            },
            Self::Call { argument_count } => Self::Call {
                argument_count: *argument_count,
            },
            Self::CallDelay => Self::CallDelay,
            Self::Return { value, register } => Self::Return {
                value: map_use(*value),
                register: *register,
            },
            Self::Branch {
                condition,
                target,
                delay,
            } => Self::Branch {
                condition: match condition {
                    BranchCondition::Always => BranchCondition::Always,
                    BranchCondition::Zero(register) => BranchCondition::Zero(map_use(*register)),
                    BranchCondition::NonZero(register) => {
                        BranchCondition::NonZero(map_use(*register))
                    }
                    BranchCondition::Negative(register) => {
                        BranchCondition::Negative(map_use(*register))
                    }
                    BranchCondition::NonNegative(register) => {
                        BranchCondition::NonNegative(map_use(*register))
                    }
                    BranchCondition::Positive(register) => {
                        BranchCondition::Positive(map_use(*register))
                    }
                    BranchCondition::NonPositive(register) => {
                        BranchCondition::NonPositive(map_use(*register))
                    }
                    BranchCondition::Equal(left, right) => {
                        BranchCondition::Equal(map_use(*left), map_use(*right))
                    }
                    BranchCondition::NotEqual(left, right) => {
                        BranchCondition::NotEqual(map_use(*left), map_use(*right))
                    }
                },
                target: *target,
                delay: *delay,
            },
            Self::Align { bits } => Self::Align { bits: *bits },
            Self::Nop => Self::Nop,
        }
    }

    pub fn copy_operands(&self) -> Option<(Register, Register, bool)> {
        match self {
            Self::Move {
                destination,
                source,
                must_emit,
            } => Some((*destination, *source, *must_emit)),
            Self::Return { value, register } => {
                Some((Register::Physical(*register), *value, false))
            }
            _ => None,
        }
    }
}

fn call_clobbers() -> impl Iterator<Item = PhysicalRegister> {
    const GPRS: [PhysicalRegister; 17] = [
        PhysicalRegister::Gpr(Gpr::V0),
        PhysicalRegister::Gpr(Gpr::V1),
        PhysicalRegister::Gpr(Gpr::T9),
        PhysicalRegister::Gpr(Gpr::Ra),
        PhysicalRegister::Gpr(Gpr::A0),
        PhysicalRegister::Gpr(Gpr::A1),
        PhysicalRegister::Gpr(Gpr::A2),
        PhysicalRegister::Gpr(Gpr::A3),
        PhysicalRegister::Gpr(Gpr::T0),
        PhysicalRegister::Gpr(Gpr::T1),
        PhysicalRegister::Gpr(Gpr::T2),
        PhysicalRegister::Gpr(Gpr::T3),
        PhysicalRegister::Gpr(Gpr::T4),
        PhysicalRegister::Gpr(Gpr::T5),
        PhysicalRegister::Gpr(Gpr::T6),
        PhysicalRegister::Gpr(Gpr::T7),
        PhysicalRegister::Gpr(Gpr::T8),
    ];
    GPRS.into_iter().chain(
        (0..32)
            .filter(|index| !(20..=30).contains(index) || index % 2 != 0)
            .map(PhysicalRegister::Fpr),
    )
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct LabelTable {
    locations: Vec<Option<u32>>,
}

impl LabelTable {
    pub fn allocate(&mut self) -> Result<LabelId, String> {
        let id = LabelId(u32::try_from(self.locations.len()).map_err(|_| "exhausted label IDs")?);
        self.locations.push(None);
        Ok(id)
    }

    pub fn bind(&mut self, label: LabelId, instruction: usize) -> Result<(), String> {
        let instruction =
            u32::try_from(instruction).map_err(|_| "IR instruction index exceeds u32")?;
        let location = self
            .locations
            .get_mut(label.0 as usize)
            .ok_or_else(|| format!("invalid label ID {}", label.0))?;
        if location.is_some() {
            return Err(format!("label {} is already bound", label.0));
        }
        *location = Some(instruction);
        Ok(())
    }

    pub fn location(&self, label: LabelId) -> Result<Option<u32>, String> {
        self.locations
            .get(label.0 as usize)
            .copied()
            .ok_or_else(|| format!("invalid label ID {}", label.0))
    }

    pub fn len(&self) -> usize {
        self.locations.len()
    }

    pub fn is_empty(&self) -> bool {
        self.locations.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn labels_are_separate_from_the_instruction_stream() {
        let mut labels = LabelTable::default();
        let label = labels.allocate().unwrap();
        assert_eq!(labels.location(label).unwrap(), None);
        labels.bind(label, 12).unwrap();
        assert_eq!(labels.location(label).unwrap(), Some(12));
        assert!(labels.bind(label, 13).is_err());
    }

    #[test]
    fn call_metadata_models_the_calling_convention() {
        let effects = IrInstruction::Call { argument_count: 2 }.register_effects();
        assert_eq!(
            effects.uses,
            vec![
                Register::Physical(PhysicalRegister::CALL),
                Register::Physical(ARGUMENT_REGISTERS[0]),
                Register::Physical(ARGUMENT_REGISTERS[1]),
            ]
        );
        assert!(
            effects
                .definitions
                .contains(&Register::Physical(PhysicalRegister::RET))
        );
        assert!(
            effects
                .definitions
                .contains(&Register::Physical(PhysicalRegister::Fpr(31)))
        );
        assert!(
            !effects
                .definitions
                .contains(&Register::Physical(PhysicalRegister::Fpr(20)))
        );
    }

    #[test]
    fn conditional_move_reads_its_destination() {
        let destination = Register::Virtual(VirtualRegisterId(0));
        let effects = IrInstruction::ConditionalMove {
            operation: ConditionalMoveOperation::NonZero,
            destination,
            source: Register::Virtual(VirtualRegisterId(1)),
            condition: Register::Virtual(VirtualRegisterId(2)),
        }
        .register_effects();
        assert!(effects.uses.contains(&destination));
        assert_eq!(effects.definitions, vec![destination]);
    }
}
