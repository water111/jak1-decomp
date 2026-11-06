use crate::ee::{self, ImmediateOpcode as I, RegisterOpcode as R};
use crate::frontend::{FileContext, FunctionData, FunctionIdentity};
use crate::ir::{
    BranchCondition, BranchDelay, FunctionId, Gpr, IrInstruction, PhysicalRegister, Register,
};
use crate::object::SymbolId;
use crate::register_allocator::{RegisterAllocation, allocate_registers};
use crate::static_data::Segment;
use crate::type_system::RegisterClass;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SymbolLink {
    pub offset: u32,
    pub symbol: SymbolId,
    pub addend: i32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct OpcodeFunction {
    pub id: FunctionId,
    pub identity: FunctionIdentity,
    pub segment: Segment,
    pub words: Vec<u32>,
    pub symbol_links: Vec<SymbolLink>,
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
    let frame = Frame::new(&allocation)?;
    let mut words = Vec::new();
    frame.emit_prologue(&mut words);

    let mut instruction_offsets = Vec::with_capacity(allocation.instructions.len() + 1);
    for (index, instruction) in allocation.instructions.iter().enumerate() {
        instruction_offsets.push(words.len());
        match instruction {
            IrInstruction::LoadConstant { destination, value } => {
                let destination = expect_gpr(&allocation, *destination)?;
                match value {
                    0 => words.push(ee::encode_register(R::Or, destination, Gpr::R0, Gpr::R0)),
                    _ => {
                        return Err(format!(
                            "IR instruction {index} loads an integer constant that codegen does not support"
                        ));
                    }
                }
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
                destination,
                source,
                condition,
            } => {
                let destination = expect_gpr(&allocation, *destination)?;
                let source = expect_gpr(&allocation, *source)?;
                let condition = expect_gpr(&allocation, *condition)?;
                words.push(ee::encode_register(R::Movn, destination, source, condition));
            }
            IrInstruction::Call { .. } => {
                words.push(ee::encode_register(R::Jalr, Gpr::Ra, Gpr::T9, Gpr::R0))
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
            IrInstruction::Nop => words.push(0),
            IrInstruction::Operation { .. } | IrInstruction::Unary { .. } => {
                return Err(format!(
                    "IR instruction {index} has no machine opcode selected"
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
        symbol_links: Vec::new(),
    })
}

#[derive(Debug)]
struct Frame {
    size: u16,
    save_ra: bool,
    saves: Vec<(PhysicalRegister, u16)>,
}

impl Frame {
    fn new(allocation: &RegisterAllocation) -> Result<Self, String> {
        let save_ra = allocation
            .instructions
            .iter()
            .any(|instruction| matches!(instruction, IrInstruction::Call { .. }));
        let mut offset = if save_ra { 8_u32 } else { 0 };
        let mut saves = Vec::new();
        for register in &allocation.used_callee_saved {
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
        for (register, offset) in &self.saves {
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
        _ => Err("cannot move directly between GPR and FPR registers".to_owned()),
    }
}

fn encode_branch(
    allocation: &RegisterAllocation,
    condition: BranchCondition,
    delay: BranchDelay,
    displacement: i16,
) -> Result<u32, String> {
    let likely = delay == BranchDelay::Likely;
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
    fn return_ir_precolors_a_zero_constant_to_v0() {
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
            IrInstruction::LoadConstant {
                destination: result,
                value: 0,
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
