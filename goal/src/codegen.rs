use crate::ee::{
    self, FloatOpcode, ImmediateOpcode as I, ImmediateShiftOpcode as S, RegimmOpcode,
    RegisterOpcode as R,
};
use crate::frontend::{FileContext, FunctionData, FunctionIdentity};
use crate::ir::{
    ArithmeticOperation, BranchCondition, BranchDelay, ConditionalMoveOperation, DoublewordPart,
    FunctionId, Gpr, IntegerBitwiseOperation, IntegerComparison, IrInstruction, MemoryLoad,
    MemoryStore, PhysicalRegister, Register, ShiftOperation,
};
use crate::object::SymbolId;
use crate::register_allocator::{RegisterAllocation, allocate_registers};
use crate::static_data::{Segment, StaticLabelId};
use crate::type_system::RegisterClass;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SymbolLink {
    pub offset: u32,
    pub symbol: SymbolId,
    pub addend: i32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FunctionLink {
    pub high_offset: u32,
    pub low_offset: u32,
    pub target: FunctionId,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct StaticLink {
    pub offset: u32,
    pub label: StaticLabelId,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct OpcodeFunction {
    pub id: FunctionId,
    pub identity: FunctionIdentity,
    pub segment: Segment,
    pub words: Vec<u32>,
    pub symbol_links: Vec<SymbolLink>,
    pub function_links: Vec<FunctionLink>,
    pub static_links: Vec<StaticLink>,
}

pub fn generate_file(file: &FileContext) -> Result<Vec<OpcodeFunction>, String> {
    file.functions
        .iter()
        .enumerate()
        .map(|(index, function)| {
            generate_function(
                FunctionId(
                    u32::try_from(index).map_err(|_| "exhausted function IDs during codegen")?,
                ),
                function,
            )
        })
        .collect()
}

pub fn generate_function(
    id: FunctionId,
    function: &FunctionData,
) -> Result<OpcodeFunction, String> {
    let allocation = allocate_registers(function)?;
    let frame = Frame::new(&allocation, function.uses_frame_pointer())?;
    let mut words = Vec::new();
    let mut symbol_links = Vec::new();
    let mut function_links = Vec::new();
    let mut static_links = Vec::new();
    frame.emit_prologue(&mut words);

    let mut instruction_offsets = Vec::with_capacity(allocation.instructions.len() + 1);
    for (index, instruction) in allocation.instructions.iter().enumerate() {
        instruction_offsets.push(words.len());
        match instruction {
            IrInstruction::LoadConstant { destination, value } => {
                let destination = expect_gpr(&allocation, *destination)?;
                match value {
                    value if i16::try_from(*value).is_ok() => words.push(ee::encode_immediate(
                        I::Addiu,
                        destination.number(),
                        Gpr::R0,
                        *value as i16 as u16,
                    )),
                    value if u16::try_from(*value).is_ok() => words.push(ee::encode_immediate(
                        I::Ori,
                        destination.number(),
                        Gpr::R0,
                        *value as u16,
                    )),
                    value if u32::try_from(*value).is_ok() && value & 0xffff == 0 => {
                        words.push(ee::encode_immediate(
                            I::Lui,
                            destination.number(),
                            Gpr::R0,
                            (*value >> 16) as u16,
                        ));
                    }
                    _ => {
                        return Err(format!(
                            "IR instruction {index} loads an integer constant that codegen does not support"
                        ));
                    }
                }
            }
            IrInstruction::LoadFunctionHigh { destination } => {
                let destination = expect_gpr(&allocation, *destination)?;
                words.push(ee::encode_immediate(
                    I::Lui,
                    destination.number(),
                    Gpr::R0,
                    0,
                ));
            }
            IrInstruction::LoadFunctionLow {
                destination,
                source,
                function,
            } => {
                let source = expect_gpr(&allocation, *source)?;
                let destination = expect_gpr(&allocation, *destination)?;
                let low_offset = word_offset(&words)?;
                let high_offset = low_offset - 4;
                words.push(ee::encode_immediate(
                    I::Ori,
                    destination.number(),
                    source,
                    0,
                ));
                function_links.push(FunctionLink {
                    high_offset,
                    low_offset,
                    target: *function,
                });
            }
            IrInstruction::GetSymbolValue {
                destination,
                symbol,
            } => {
                let destination = expect_gpr(&allocation, *destination)?;
                symbol_links.push(SymbolLink {
                    offset: word_offset(&words)?,
                    symbol: *symbol,
                    addend: 0,
                });
                words.push(ee::encode_immediate(
                    I::Lw,
                    destination.number(),
                    Gpr::S7,
                    0,
                ));
            }
            IrInstruction::GetSymbol {
                destination,
                symbol,
            } => {
                let destination = expect_gpr(&allocation, *destination)?;
                symbol_links.push(SymbolLink {
                    offset: word_offset(&words)?,
                    symbol: *symbol,
                    addend: 0,
                });
                words.push(ee::encode_immediate(
                    I::Daddiu,
                    destination.number(),
                    Gpr::S7,
                    0,
                ));
            }
            IrInstruction::LoadFloatConstant { destination, label } => {
                let destination = expect_fpr(&allocation, *destination)?;
                static_links.push(StaticLink {
                    offset: word_offset(&words)?,
                    label: *label,
                });
                words.push(ee::encode_immediate(I::Lwc1, destination, Gpr::Fp, 0));
            }
            IrInstruction::LoadConstant64 { destination, label } => {
                let destination = expect_gpr(&allocation, *destination)?;
                static_links.push(StaticLink {
                    offset: word_offset(&words)?,
                    label: *label,
                });
                words.push(ee::encode_immediate(
                    I::Ld,
                    destination.number(),
                    Gpr::Fp,
                    0,
                ));
            }
            IrInstruction::LoadStaticAddress { destination, label } => {
                let destination = expect_gpr(&allocation, *destination)?;
                static_links.push(StaticLink {
                    offset: word_offset(&words)?,
                    label: *label,
                });
                words.push(ee::encode_immediate(
                    I::Daddiu,
                    destination.number(),
                    Gpr::Fp,
                    0,
                ));
            }
            IrInstruction::LoadMemory {
                operation,
                destination,
                base,
                offset,
            } => {
                let base = expect_gpr(&allocation, *base)?;
                let (destination, opcode) = match operation {
                    MemoryLoad::SignedByte => {
                        (expect_gpr(&allocation, *destination)?.number(), I::Lb)
                    }
                    MemoryLoad::UnsignedByte => {
                        (expect_gpr(&allocation, *destination)?.number(), I::Lbu)
                    }
                    MemoryLoad::SignedHalfword => {
                        (expect_gpr(&allocation, *destination)?.number(), I::Lh)
                    }
                    MemoryLoad::UnsignedHalfword => {
                        (expect_gpr(&allocation, *destination)?.number(), I::Lhu)
                    }
                    MemoryLoad::SignedWord => {
                        (expect_gpr(&allocation, *destination)?.number(), I::Lw)
                    }
                    MemoryLoad::UnsignedWord => {
                        (expect_gpr(&allocation, *destination)?.number(), I::Lwu)
                    }
                    MemoryLoad::Doubleword => {
                        (expect_gpr(&allocation, *destination)?.number(), I::Ld)
                    }
                    MemoryLoad::Quadword => {
                        (expect_gpr(&allocation, *destination)?.number(), I::Lq)
                    }
                    MemoryLoad::FloatWord => (expect_fpr(&allocation, *destination)?, I::Lwc1),
                };
                words.push(ee::encode_immediate(
                    opcode,
                    destination,
                    base,
                    *offset as u16,
                ));
            }
            IrInstruction::StoreMemory {
                operation,
                source,
                base,
                offset,
            } => {
                let (source, opcode) = match operation {
                    MemoryStore::Byte => (expect_gpr(&allocation, *source)?.number(), I::Sb),
                    MemoryStore::Halfword => (expect_gpr(&allocation, *source)?.number(), I::Sh),
                    MemoryStore::Word => (expect_gpr(&allocation, *source)?.number(), I::Sw),
                    MemoryStore::Doubleword => (expect_gpr(&allocation, *source)?.number(), I::Sd),
                    MemoryStore::Quadword => (expect_gpr(&allocation, *source)?.number(), I::Sq),
                    MemoryStore::FloatWord => (expect_fpr(&allocation, *source)?, I::Swc1),
                };
                words.push(ee::encode_immediate(
                    opcode,
                    source,
                    expect_gpr(&allocation, *base)?,
                    *offset as u16,
                ));
            }
            IrInstruction::StoreSymbol { source, symbol } => {
                let source = expect_gpr(&allocation, *source)?;
                symbol_links.push(SymbolLink {
                    offset: word_offset(&words)?,
                    symbol: *symbol,
                    addend: 0,
                });
                words.push(ee::encode_immediate(I::Sw, source.number(), Gpr::S7, 0));
            }
            IrInstruction::MoveToCop0 { source, register } => {
                let source = expect_gpr(&allocation, *source)?;
                words.push(ee::encode_move_to_cop0(source, *register));
            }
            IrInstruction::Move {
                destination,
                source,
                ..
            } => {
                if allocation.should_emit_copy(index)? {
                    words.push(encode_move(
                        &allocation,
                        *destination,
                        *source,
                        register_is_gpr128(&allocation, *destination)
                            || register_is_gpr128(&allocation, *source),
                    )?);
                }
            }
            IrInstruction::ConditionalMove {
                operation,
                destination,
                source,
                condition,
            } => {
                let destination = expect_gpr(&allocation, *destination)?;
                let source = expect_gpr(&allocation, *source)?;
                let condition = expect_gpr(&allocation, *condition)?;
                words.push(ee::encode_register(
                    match operation {
                        ConditionalMoveOperation::Zero => R::Movz,
                        ConditionalMoveOperation::NonZero => R::Movn,
                    },
                    destination,
                    source,
                    condition,
                ));
            }
            IrInstruction::IntegerArithmetic {
                operation,
                signed,
                destination,
                left,
                right,
            } => {
                let destination = expect_gpr(&allocation, *destination)?;
                let left = expect_gpr(&allocation, *left)?;
                let right = expect_gpr(&allocation, *right)?;
                match operation {
                    ArithmeticOperation::Add => {
                        words.push(ee::encode_register(R::Daddu, destination, left, right))
                    }
                    ArithmeticOperation::Subtract => {
                        words.push(ee::encode_register(R::Dsubu, destination, left, right))
                    }
                    ArithmeticOperation::WordSubtract => {
                        words.push(ee::encode_register(R::Subu, destination, left, right))
                    }
                    ArithmeticOperation::Multiply => words.push(ee::encode_register(
                        if *signed { R::Mult } else { R::Multu },
                        destination,
                        left,
                        right,
                    )),
                    ArithmeticOperation::Divide => {
                        words.push(ee::encode_register(
                            if *signed { R::Div } else { R::Divu },
                            Gpr::R0,
                            left,
                            right,
                        ));
                        words.push(ee::encode_register(R::Mflo, destination, Gpr::R0, Gpr::R0));
                    }
                    ArithmeticOperation::Remainder => {
                        words.push(ee::encode_register(
                            if *signed { R::Div } else { R::Divu },
                            Gpr::R0,
                            left,
                            right,
                        ));
                        words.push(ee::encode_register(R::Mfhi, destination, Gpr::R0, Gpr::R0));
                    }
                }
            }
            IrInstruction::IntegerAddImmediate {
                destination,
                source,
                value,
            } => words.push(ee::encode_immediate(
                I::Daddiu,
                expect_gpr(&allocation, *destination)?.number(),
                expect_gpr(&allocation, *source)?,
                *value as u16,
            )),
            IrInstruction::FloatArithmetic {
                operation,
                destination,
                left,
                right,
            } => {
                let operation = match operation {
                    ArithmeticOperation::Add => FloatOpcode::Add,
                    ArithmeticOperation::Subtract => FloatOpcode::Subtract,
                    ArithmeticOperation::WordSubtract => {
                        return Err("word subtraction cannot operate on floats".to_owned());
                    }
                    ArithmeticOperation::Multiply => FloatOpcode::Multiply,
                    ArithmeticOperation::Divide => FloatOpcode::Divide,
                    ArithmeticOperation::Remainder => {
                        return Err("floating-point remainder has no EE opcode".to_owned());
                    }
                };
                words.push(ee::encode_float_arithmetic(
                    operation,
                    expect_fpr(&allocation, *destination)?,
                    expect_fpr(&allocation, *left)?,
                    expect_fpr(&allocation, *right)?,
                ));
            }
            IrInstruction::VariableShift {
                operation,
                destination,
                value,
                amount,
            } => {
                let opcode = match operation {
                    ShiftOperation::Left => R::Dsllv,
                    ShiftOperation::LogicalRight => R::Dsrlv,
                    ShiftOperation::ArithmeticRight => R::Dsrav,
                };
                words.push(ee::encode_variable_shift(
                    opcode,
                    expect_gpr(&allocation, *destination)?,
                    expect_gpr(&allocation, *value)?,
                    expect_gpr(&allocation, *amount)?,
                ));
            }
            IrInstruction::IntegerShiftImmediate {
                operation,
                destination,
                value,
                amount,
            } => {
                let (opcode, amount) = match (operation, amount >= &32) {
                    (ShiftOperation::Left, false) => (S::Dsll, *amount),
                    (ShiftOperation::Left, true) => (S::Dsll32, *amount - 32),
                    (ShiftOperation::LogicalRight, false) => (S::Dsrl, *amount),
                    (ShiftOperation::LogicalRight, true) => (S::Dsrl32, *amount - 32),
                    (ShiftOperation::ArithmeticRight, false) => (S::Dsra, *amount),
                    (ShiftOperation::ArithmeticRight, true) => (S::Dsra32, *amount - 32),
                };
                words.push(ee::encode_immediate_shift(
                    opcode,
                    expect_gpr(&allocation, *destination)?,
                    expect_gpr(&allocation, *value)?,
                    amount,
                ));
            }
            IrInstruction::IntegerCompare {
                comparison,
                signed,
                destination,
                left,
                right,
            } => words.push(ee::encode_register(
                match (comparison, signed) {
                    (IntegerComparison::LessThan, true) => R::Slt,
                    (IntegerComparison::LessThan, false) => R::Sltu,
                },
                expect_gpr(&allocation, *destination)?,
                expect_gpr(&allocation, *left)?,
                expect_gpr(&allocation, *right)?,
            )),
            IrInstruction::IntegerBitwise {
                operation,
                destination,
                left,
                right,
            } => words.push(ee::encode_register(
                match operation {
                    IntegerBitwiseOperation::Or => R::Or,
                    IntegerBitwiseOperation::And => R::And,
                    IntegerBitwiseOperation::Nor => R::Nor,
                    IntegerBitwiseOperation::Xor => R::Xor,
                },
                expect_gpr(&allocation, *destination)?,
                expect_gpr(&allocation, *left)?,
                expect_gpr(&allocation, *right)?,
            )),
            IrInstruction::Gpr128Upper64 {
                destination,
                source,
            } => words.push(ee::encode_pcpyud(
                expect_gpr(&allocation, *destination)?,
                expect_gpr(&allocation, *source)?,
                Gpr::R0,
            )),
            IrInstruction::ExtractWord {
                part,
                destination,
                source,
            } => {
                let destination = expect_gpr(&allocation, *destination)?;
                let source = expect_gpr(&allocation, *source)?;
                words.push(match part {
                    DoublewordPart::Low => {
                        ee::encode_register(R::Sllv, destination, Gpr::R0, source)
                    }
                    DoublewordPart::High => {
                        ee::encode_immediate_shift(S::Dsra32, destination, source, 0)
                    }
                });
            }
            IrInstruction::Call { .. } => {
                words.push(ee::encode_register(R::Jalr, Gpr::Ra, Gpr::T9, Gpr::R0))
            }
            IrInstruction::CallDelay => {
                words.push(ee::encode_immediate_shift(S::Sll, Gpr::V0, Gpr::Ra, 0));
            }
            IrInstruction::Return { value, register } => {
                if allocation.should_emit_copy(index)? {
                    words.push(encode_move(
                        &allocation,
                        Register::Physical(*register),
                        *value,
                        register_is_gpr128(&allocation, *value),
                    )?);
                }
            }
            // Forward targets are not known until every IR instruction has a
            // machine-code offset. This slot is patched in the pass below.
            IrInstruction::Branch { .. } => words.push(0),
            IrInstruction::Align { bits } => {
                let bytes = usize::from(*bits / 8);
                while (words.len() * 4) % bytes != 0 {
                    words.push(0);
                }
            }
            IrInstruction::Nop => words.push(0),
            IrInstruction::Operation { .. } | IrInstruction::Unary { .. } => {
                return Err(format!(
                    "IR instruction {index} has no machine opcode selected"
                ));
            }
            IrInstruction::IntegerBitwiseImmediate {
                operation,
                destination,
                source,
                value,
            } => {
                let opcode = match operation {
                    IntegerBitwiseOperation::And => I::Andi,
                    IntegerBitwiseOperation::Or => I::Ori,
                    IntegerBitwiseOperation::Xor => I::Xori,
                    IntegerBitwiseOperation::Nor => {
                        return Err("NOR has no immediate opcode".to_owned());
                    }
                };
                words.push(ee::encode_immediate(
                    opcode,
                    expect_gpr(&allocation, *destination)?.number(),
                    expect_gpr(&allocation, *source)?,
                    *value,
                ));
            }
        }
    }
    instruction_offsets.push(words.len());

    for (index, instruction) in allocation.instructions.iter().enumerate() {
        if instruction.has_delay_slot() {
            let Some(after_delay) = instruction_offsets.get(index + 2) else {
                return Err(format!("instruction {index} has no delay slot"));
            };
            if *after_delay != instruction_offsets[index + 1] + 1 {
                return Err(format!(
                    "instruction {} needs a delay slot that emits exactly one opcode",
                    index + 1
                ));
            }
        }
        let IrInstruction::Branch {
            condition,
            target,
            delay,
        } = instruction
        else {
            continue;
        };
        let target = function
            .labels
            .location(*target)?
            .ok_or_else(|| format!("branch at instruction {index} targets an unbound label"))?
            as usize;
        let target = *instruction_offsets
            .get(target)
            .ok_or_else(|| format!("branch at instruction {index} has an invalid target"))?;
        let branch_index = instruction_offsets[index];
        let target = i64::try_from(target)
            .map_err(|_| format!("branch at instruction {index} target exceeds i64"))?;
        let branch = i64::try_from(branch_index + 1)
            .map_err(|_| format!("branch at instruction {index} offset exceeds i64"))?;
        let displacement = target - branch;
        let displacement = i16::try_from(displacement)
            .map_err(|_| format!("branch at instruction {index} exceeds the 16-bit range"))?;
        words[branch_index] = encode_branch(&allocation, *condition, *delay, displacement)?;
    }

    frame.emit_epilogue(&mut words);

    Ok(OpcodeFunction {
        id,
        identity: function.identity,
        segment: function.segment,
        words,
        symbol_links,
        function_links,
        static_links,
    })
}

fn word_offset(words: &[u32]) -> Result<u32, String> {
    u32::try_from(words.len())
        .ok()
        .and_then(|words| words.checked_mul(4))
        .ok_or_else(|| "generated function exceeds u32".to_owned())
}

#[derive(Debug)]
struct Frame {
    size: u16,
    save_ra: bool,
    use_fp: bool,
    saves: Vec<(PhysicalRegister, u16)>,
}

impl Frame {
    fn new(allocation: &RegisterAllocation, uses_frame_pointer: bool) -> Result<Self, String> {
        let save_ra = allocation
            .instructions
            .iter()
            .any(|instruction| matches!(instruction, IrInstruction::Call { .. }));
        let use_fp = uses_frame_pointer;
        let mut offset = if save_ra { 8_u32 } else { 0 };
        if use_fp {
            offset = offset.max(16);
        }
        let mut saved_registers = allocation.used_callee_saved.clone();
        saved_registers.sort_by_key(|register| match register {
            PhysicalRegister::Gpr(Gpr::S0) => 0,
            PhysicalRegister::Gpr(Gpr::S1) => 1,
            PhysicalRegister::Gpr(Gpr::S2) => 2,
            PhysicalRegister::Gpr(Gpr::S3) => 3,
            PhysicalRegister::Gpr(Gpr::S4) => 4,
            PhysicalRegister::Gpr(Gpr::S5) => 5,
            PhysicalRegister::Gpr(Gpr::Gp) => 6,
            PhysicalRegister::Fpr(register) => 7 + usize::from(*register),
            _ => unreachable!("only callee-saved registers are placed in the frame"),
        });
        let mut saves = Vec::new();
        for register in &saved_registers {
            let alignment = match register {
                PhysicalRegister::Gpr(_) => 16,
                PhysicalRegister::Fpr(_) => 4,
            };
            offset = align(offset, alignment);
            saves.push((
                *register,
                u16::try_from(offset).map_err(|_| "stack frame exceeds 64 KiB")?,
            ));
            offset += alignment;
        }
        let size = if offset == 0 { 0 } else { align(offset, 16) };
        Ok(Self {
            size: u16::try_from(size).map_err(|_| "stack frame exceeds 64 KiB")?,
            save_ra,
            use_fp,
            saves,
        })
    }

    fn emit_prologue(&self, words: &mut Vec<u32>) {
        if self.size != 0 {
            words.push(ee::encode_immediate(
                I::Daddiu,
                Gpr::Sp.number(),
                Gpr::Sp,
                self.size.wrapping_neg(),
            ));
        }
        if self.save_ra {
            words.push(ee::encode_immediate(I::Sd, Gpr::Ra.number(), Gpr::Sp, 0));
        }
        if self.use_fp {
            words.push(ee::encode_immediate(I::Sd, Gpr::Fp.number(), Gpr::Sp, 8));
            words.push(ee::encode_register(R::Or, Gpr::Fp, Gpr::T9, Gpr::R0));
        }
        for (register, offset) in &self.saves {
            words.push(match register {
                PhysicalRegister::Gpr(register) => {
                    ee::encode_immediate(I::Sq, register.number(), Gpr::Sp, *offset)
                }
                PhysicalRegister::Fpr(register) => {
                    ee::encode_immediate(I::Swc1, *register, Gpr::Sp, *offset)
                }
            });
        }
    }

    fn emit_epilogue(&self, words: &mut Vec<u32>) {
        if self.save_ra {
            words.push(ee::encode_immediate(I::Ld, Gpr::Ra.number(), Gpr::Sp, 0));
        }
        if self.use_fp {
            words.push(ee::encode_immediate(I::Ld, Gpr::Fp.number(), Gpr::Sp, 8));
        }
        for (register, offset) in self.saves.iter().rev() {
            words.push(match register {
                PhysicalRegister::Gpr(register) => {
                    ee::encode_immediate(I::Lq, register.number(), Gpr::Sp, *offset)
                }
                PhysicalRegister::Fpr(register) => {
                    ee::encode_immediate(I::Lwc1, *register, Gpr::Sp, *offset)
                }
            });
        }
        words.push(ee::encode_register(R::Jr, Gpr::R0, Gpr::Ra, Gpr::R0));
        words.push(if self.size == 0 {
            ee::encode_register(R::Daddu, Gpr::Sp, Gpr::Sp, Gpr::R0)
        } else {
            ee::encode_immediate(I::Daddiu, Gpr::Sp.number(), Gpr::Sp, self.size)
        });
    }
}

fn encode_move(
    allocation: &RegisterAllocation,
    destination: Register,
    source: Register,
    gpr128: bool,
) -> Result<u32, String> {
    match (
        allocation.physical_register(destination)?,
        allocation.physical_register(source)?,
    ) {
        (PhysicalRegister::Gpr(destination), PhysicalRegister::Gpr(source)) => {
            Ok(ee::encode_register(
                if gpr128 { R::Por } else { R::Or },
                destination,
                source,
                Gpr::R0,
            ))
        }
        (PhysicalRegister::Fpr(destination), PhysicalRegister::Fpr(source)) => {
            Ok(ee::encode_float_move(destination, source))
        }
        (PhysicalRegister::Gpr(destination), PhysicalRegister::Fpr(source)) => {
            Ok(ee::encode_mfc1(destination, source))
        }
        (PhysicalRegister::Fpr(destination), PhysicalRegister::Gpr(source)) => {
            Ok(ee::encode_mtc1(destination, source))
        }
    }
}

fn encode_branch(
    allocation: &RegisterAllocation,
    condition: BranchCondition,
    delay: BranchDelay,
    displacement: i16,
) -> Result<u32, String> {
    let likely = delay == BranchDelay::Likely;
    if let BranchCondition::Negative(register) | BranchCondition::NonNegative(register) = condition
    {
        let opcode = match (condition, likely) {
            (BranchCondition::Negative(_), false) => RegimmOpcode::Bltz,
            (BranchCondition::Negative(_), true) => RegimmOpcode::Bltzl,
            (BranchCondition::NonNegative(_), false) => RegimmOpcode::Bgez,
            (BranchCondition::NonNegative(_), true) => RegimmOpcode::Bgezl,
            _ => unreachable!(),
        };
        return Ok(ee::encode_regimm(
            opcode,
            expect_gpr(allocation, register)?,
            displacement as u16,
        ));
    }
    let (opcode, left, right) = match condition {
        BranchCondition::Always if likely => {
            return Err("an unconditional branch cannot be branch-likely".to_owned());
        }
        BranchCondition::Always => (I::Beq, Gpr::R0, Gpr::R0),
        BranchCondition::Zero(register) => (
            if likely { I::Beql } else { I::Beq },
            expect_gpr(allocation, register)?,
            Gpr::R0,
        ),
        BranchCondition::NonZero(register) => (
            if likely { I::Bnel } else { I::Bne },
            expect_gpr(allocation, register)?,
            Gpr::R0,
        ),
        BranchCondition::Positive(register) => (
            if likely { I::Bgtzl } else { I::Bgtz },
            expect_gpr(allocation, register)?,
            Gpr::R0,
        ),
        BranchCondition::NonPositive(register) => (
            if likely { I::Blezl } else { I::Blez },
            expect_gpr(allocation, register)?,
            Gpr::R0,
        ),
        BranchCondition::Negative(_) => unreachable!(),
        BranchCondition::NonNegative(_) => unreachable!(),
        BranchCondition::Equal(left, right) => (
            if likely { I::Beql } else { I::Beq },
            expect_gpr(allocation, left)?,
            expect_gpr(allocation, right)?,
        ),
        BranchCondition::NotEqual(left, right) => (
            if likely { I::Bnel } else { I::Bne },
            expect_gpr(allocation, left)?,
            expect_gpr(allocation, right)?,
        ),
    };
    Ok(ee::encode_immediate(
        opcode,
        right.number(),
        left,
        displacement as u16,
    ))
}

fn expect_gpr(allocation: &RegisterAllocation, register: Register) -> Result<Gpr, String> {
    match allocation.physical_register(register)? {
        PhysicalRegister::Gpr(register) => Ok(register),
        PhysicalRegister::Fpr(register) => Err(format!("register f{register} is not a GPR")),
    }
}

fn expect_fpr(allocation: &RegisterAllocation, register: Register) -> Result<u8, String> {
    match allocation.physical_register(register)? {
        PhysicalRegister::Fpr(register) => Ok(register),
        PhysicalRegister::Gpr(register) => {
            Err(format!("register {} is not an FPR", register.name()))
        }
    }
}

fn register_is_gpr128(allocation: &RegisterAllocation, register: Register) -> bool {
    match register {
        Register::Virtual(register) => {
            allocation
                .registers
                .get(register.0 as usize)
                .unwrap_or_else(|| {
                    panic!(
                        "allocated virtual register {} has no register description",
                        register.0
                    )
                })
                .class
                == RegisterClass::Gpr128
        }
        Register::Physical(_) => false,
    }
}

fn align(value: u32, alignment: u32) -> u32 {
    (value + alignment - 1) & !(alignment - 1)
}

#[cfg(test)]
mod tests {
    use crate::frontend::FileContext;
    use crate::ir::{BranchCondition, BranchDelay, IrInstruction};
    use crate::object::SymbolId;
    use crate::parse::Parser;
    use crate::type_system::{TypeSystem, Visibility};

    use super::*;

    #[test]
    fn top_level_identity_does_not_add_a_return_value() {
        let source = Parser::default().parse("empty.gc", "").unwrap().source_id;
        let mut file = FileContext::new(0, source, Visibility::new(0, []));
        let mut types = TypeSystem::new();
        let none = types.intern_type_spec("none");
        file.functions.push(FunctionData::new(
            FunctionIdentity::TopLevel,
            None,
            Segment::TopLevel,
            none,
        ));
        let code = generate_file(&file).unwrap();
        assert_eq!(code.len(), 1);
        assert_eq!(code[0].words, [0x03e0_0008, 0x03a0_e82d]);
    }

    #[test]
    fn return_ir_precolors_a_zero_register_copy_to_v0() {
        let mut types = TypeSystem::new();
        let none = types.intern_type_spec("none");
        let mut function = FunctionData::new(
            FunctionIdentity::Global(SymbolId(1)),
            None,
            Segment::Main,
            none,
        );
        let result = Register::Virtual(
            function
                .allocate_register(none, RegisterClass::Gpr)
                .unwrap(),
        );
        function.ir.extend([
            IrInstruction::Move {
                destination: result,
                source: Register::Physical(PhysicalRegister::ZERO),
                must_emit: false,
            },
            IrInstruction::Return {
                value: result,
                register: PhysicalRegister::RET,
            },
        ]);

        let allocation = allocate_registers(&function).unwrap();
        assert_eq!(
            allocation.physical_register(result).unwrap(),
            PhysicalRegister::RET
        );
        let code = generate_function(FunctionId(0), &function).unwrap();
        assert_eq!(code.words, [0x0000_1025, 0x03e0_0008, 0x03a0_e82d]);
    }

    #[test]
    fn patches_branches_after_instruction_selection() {
        let mut types = TypeSystem::new();
        let integer = types.intern_type_spec("integer");
        let mut function = FunctionData::new(
            FunctionIdentity::Global(SymbolId(1)),
            None,
            Segment::Main,
            integer,
        );
        let target = function.labels.allocate().unwrap();
        function.ir.extend([
            IrInstruction::Branch {
                condition: BranchCondition::Always,
                target,
                delay: BranchDelay::Normal,
            },
            IrInstruction::Nop,
            IrInstruction::Nop,
        ]);
        function.labels.bind(target, 2).unwrap();
        let code = generate_function(FunctionId(0), &function).unwrap();
        assert_eq!(code.words, [0x1000_0001, 0, 0, 0x03e0_0008, 0x03a0_e82d]);
    }

    #[test]
    fn calls_get_a_return_address_save_and_delay_slot() {
        let mut types = TypeSystem::new();
        let integer = types.intern_type_spec("integer");
        let mut function = FunctionData::new(
            FunctionIdentity::Global(SymbolId(1)),
            None,
            Segment::Main,
            integer,
        );
        function.ir.extend([
            IrInstruction::Call { argument_count: 0 },
            IrInstruction::Nop,
        ]);
        let code = generate_function(FunctionId(0), &function).unwrap();
        assert_eq!(
            code.words,
            [
                0x67bd_fff0,
                0xffbf_0000,
                0x0320_f809,
                0,
                0xdfbf_0000,
                0x03e0_0008,
                0x67bd_0010,
            ]
        );
    }
}
