use crate::ir::{Cop0Register, Gpr};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum OperandFormat {
    None,
    RdRsRt,
    RdRtRs,
    RdRtShift,
    Rd,
    Rs,
    RsRt,
    RtRsSigned,
    RtRsUnsigned,
    RtUnsigned,
    RtMemory,
    FtMemory,
    VfMemory,
    BranchRsRt,
    BranchRs,
    Branch,
    Jalr,
    Syscall,
    RtFs,
    FsRt,
    FtFsFd,
    FdFsFt,
    FdFs,
    FdFt,
    FsFt,
    Cop0,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Opcode {
    pub name: &'static str,
    pub mask: u32,
    pub value: u32,
    pub operands: OperandFormat,
    pub delay_slot: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum RegisterOpcode {
    Sllv = 0x04,
    Jr = 0x08,
    Jalr = 0x09,
    Movn = 0x0b,
    Movz = 0x0a,
    Mfhi = 0x10,
    Mflo = 0x12,
    Dsllv = 0x14,
    Dsrlv = 0x16,
    Dsrav = 0x17,
    Mult = 0x18,
    Multu = 0x19,
    Div = 0x1a,
    Divu = 0x1b,
    And = 0x24,
    Or = 0x25,
    Xor = 0x26,
    Nor = 0x27,
    Daddu = 0x2d,
    Dsubu = 0x2f,
    Slt = 0x2a,
    Sltu = 0x2b,
    Por = 0x7000_04a9,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ImmediateShiftOpcode {
    Sll = 0x00,
    Srl = 0x02,
    Sra = 0x03,
    Dsll = 0x38,
    Dsrl = 0x3a,
    Dsra = 0x3b,
    Dsll32 = 0x3c,
    Dsrl32 = 0x3e,
    Dsra32 = 0x3f,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum RegimmOpcode {
    Bltz = 0,
    Bgez = 1,
    Bltzl = 2,
    Bgezl = 3,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ImmediateOpcode {
    Beq = 4,
    Bne = 5,
    Blez = 6,
    Bgtz = 7,
    Addiu = 9,
    Andi = 12,
    Ori = 13,
    Xori = 14,
    Lui = 15,
    Beql = 20,
    Bnel = 21,
    Blezl = 22,
    Bgtzl = 23,
    Daddiu = 25,
    Lq = 30,
    Sq = 31,
    Lb = 32,
    Lh = 33,
    Lw = 35,
    Lbu = 36,
    Lhu = 37,
    Lwu = 39,
    Sb = 40,
    Sh = 41,
    Sw = 43,
    Lwc1 = 49,
    Ld = 55,
    Swc1 = 57,
    Sd = 63,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum FloatOpcode {
    Add = 0,
    Subtract = 1,
    Multiply = 2,
    Divide = 3,
}

pub const fn encode_register(
    opcode: RegisterOpcode,
    destination: Gpr,
    left: Gpr,
    right: Gpr,
) -> u32 {
    opcode as u32 | ((left as u32) << 21) | ((right as u32) << 16) | ((destination as u32) << 11)
}

pub const fn encode_variable_shift(
    opcode: RegisterOpcode,
    destination: Gpr,
    value: Gpr,
    amount: Gpr,
) -> u32 {
    debug_assert!(matches!(
        opcode,
        RegisterOpcode::Dsllv | RegisterOpcode::Dsrlv | RegisterOpcode::Dsrav
    ));
    encode_register(opcode, destination, amount, value)
}

pub const fn encode_immediate_shift(
    opcode: ImmediateShiftOpcode,
    destination: Gpr,
    value: Gpr,
    amount: u8,
) -> u32 {
    debug_assert!(amount < 32);
    opcode as u32 | ((value as u32) << 16) | ((destination as u32) << 11) | ((amount as u32) << 6)
}

pub const fn encode_pcpyud(destination: Gpr, left: Gpr, right: Gpr) -> u32 {
    (0x1c << 26)
        | ((left as u32) << 21)
        | ((right as u32) << 16)
        | ((destination as u32) << 11)
        | (0x0e << 6)
        | 0x29
}

pub const fn encode_immediate(
    opcode: ImmediateOpcode,
    register: u8,
    base: Gpr,
    immediate: u16,
) -> u32 {
    debug_assert!(register < 32);
    ((opcode as u32) << 26) | ((base as u32) << 21) | ((register as u32) << 16) | immediate as u32
}

pub const fn encode_move_to_cop0(source: Gpr, register: Cop0Register) -> u32 {
    0x4080_0000
        | ((source as u32) << 16)
        | ((register.number() as u32) << 11)
        | register.select() as u32
}

pub const fn encode_regimm(opcode: RegimmOpcode, source: Gpr, immediate: u16) -> u32 {
    (1 << 26) | ((source as u32) << 21) | ((opcode as u32) << 16) | immediate as u32
}

pub const fn encode_float_move(destination: u8, source: u8) -> u32 {
    debug_assert!(destination < 32 && source < 32);
    0x4600_0006 | ((source as u32) << 11) | ((destination as u32) << 6)
}

pub const fn encode_float_arithmetic(
    opcode: FloatOpcode,
    destination: u8,
    left: u8,
    right: u8,
) -> u32 {
    debug_assert!(destination < 32 && left < 32 && right < 32);
    0x4600_0000
        | ((right as u32) << 16)
        | ((left as u32) << 11)
        | ((destination as u32) << 6)
        | opcode as u32
}

pub const fn encode_mtc1(destination: u8, source: Gpr) -> u32 {
    debug_assert!(destination < 32);
    0x4480_0000 | ((source as u32) << 16) | ((destination as u32) << 11)
}

pub const fn encode_mfc1(destination: Gpr, source: u8) -> u32 {
    debug_assert!(source < 32);
    0x4400_0000 | ((destination as u32) << 16) | ((source as u32) << 11)
}

pub const fn has_immediate_opcode(word: u32, opcode: ImmediateOpcode) -> bool {
    word >> 26 == opcode as u32
}

pub fn gpr_rs(word: u32) -> Gpr {
    Gpr::ALL[((word >> 21) & 0x1f) as usize]
}

pub fn gpr_rt(word: u32) -> Gpr {
    Gpr::ALL[((word >> 16) & 0x1f) as usize]
}

pub fn gpr_rd(word: u32) -> Gpr {
    Gpr::ALL[((word >> 11) & 0x1f) as usize]
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DecodedInstruction {
    pub word: u32,
    pub opcode: &'static Opcode,
}

impl DecodedInstruction {
    pub fn format(self, immediate: Option<&str>, branch: Option<&str>) -> String {
        let word = self.word;
        let rs = gpr_rs(word).name();
        let rt = gpr_rt(word).name();
        let rd = gpr_rd(word).name();
        let fs = (word >> 11) & 31;
        let ft = (word >> 16) & 31;
        let fd = (word >> 6) & 31;
        let signed = i16::from_le_bytes((word as u16).to_le_bytes());
        let signed_text = signed.to_string();
        let unsigned_text = format!("#x{:x}", word & 0xffff);
        let immediate = immediate.unwrap_or(match self.opcode.operands {
            OperandFormat::RtRsUnsigned | OperandFormat::RtUnsigned => &unsigned_text,
            _ => &signed_text,
        });
        let branch = branch.unwrap_or("<unresolved-branch>");
        let operands = match self.opcode.operands {
            OperandFormat::None => String::new(),
            OperandFormat::RdRsRt => format!(" {rd}, {rs}, {rt}"),
            OperandFormat::RdRtRs => format!(" {rd}, {rt}, {rs}"),
            OperandFormat::RdRtShift => format!(" {rd}, {rt}, {}", (word >> 6) & 31),
            OperandFormat::Rd => format!(" {rd}"),
            OperandFormat::Rs => format!(" {rs}"),
            OperandFormat::RsRt => format!(" {rs}, {rt}"),
            OperandFormat::RtRsSigned | OperandFormat::RtRsUnsigned => {
                format!(" {rt}, {rs}, {immediate}")
            }
            OperandFormat::RtUnsigned => format!(" {rt}, {immediate}"),
            OperandFormat::RtMemory => format!(" {rt}, {immediate}({rs})"),
            OperandFormat::FtMemory => format!(" f{ft}, {immediate}({rs})"),
            OperandFormat::VfMemory => format!(" vf{ft}, {immediate}({rs})"),
            OperandFormat::BranchRsRt => format!(" {rs}, {rt}, {branch}"),
            OperandFormat::BranchRs => format!(" {rs}, {branch}"),
            OperandFormat::Branch => format!(" {branch}"),
            OperandFormat::Jalr => format!(" {rd}, {rs}"),
            OperandFormat::Syscall => format!(" {}", (word >> 6) & 0xfffff),
            OperandFormat::RtFs => format!(" {rt}, f{fs}"),
            OperandFormat::FsRt => format!(" f{fs}, {rt}"),
            OperandFormat::FtFsFd => format!(" f{ft}, f{fs}, f{fd}"),
            OperandFormat::FdFsFt => format!(" f{fd}, f{fs}, f{ft}"),
            OperandFormat::FdFs => format!(" f{fd}, f{fs}"),
            OperandFormat::FdFt => format!(" f{fd}, f{ft}"),
            OperandFormat::FsFt => format!(" f{fs}, f{ft}"),
            OperandFormat::Cop0 => format!(" {rt}, c{rd}, {}", word & 7),
        };
        format!("{}{operands}", self.opcode.name)
    }

    pub fn branch_target(self, address: u32) -> Option<u32> {
        matches!(
            self.opcode.operands,
            OperandFormat::BranchRsRt | OperandFormat::BranchRs | OperandFormat::Branch
        )
        .then(|| {
            let displacement = i32::from(self.word as i16) * 4;
            address.wrapping_add(4).wrapping_add_signed(displacement)
        })
    }
}

pub fn decode(word: u32) -> Result<DecodedInstruction, String> {
    OPCODES
        .iter()
        .find(|opcode| word & opcode.mask == opcode.value)
        .map(|opcode| DecodedInstruction { word, opcode })
        .ok_or_else(|| format!("unknown EE instruction {word:#010x}"))
}

const OP: u32 = 0xfc00_0000;
const SPECIAL: u32 = 0xfc00_003f;
const REGIMM: u32 = 0xfc1f_0000;
const COP_RS: u32 = 0xffe0_0000;
const COP_FMT_FUNCT: u32 = 0xffe0_003f;
const MMI: u32 = 0xfc00_003f;
const MMI_SUB: u32 = 0xfc00_07ff;

const fn op(name: &'static str, value: u32, operands: OperandFormat) -> Opcode {
    Opcode {
        name,
        mask: OP,
        value: value << 26,
        operands,
        delay_slot: false,
    }
}

const fn special(name: &'static str, function: u32, operands: OperandFormat) -> Opcode {
    Opcode {
        name,
        mask: SPECIAL,
        value: function,
        operands,
        delay_slot: false,
    }
}

const fn branch(name: &'static str, value: u32, operands: OperandFormat) -> Opcode {
    Opcode {
        name,
        mask: OP,
        value: value << 26,
        operands,
        delay_slot: true,
    }
}

const fn regimm(name: &'static str, rt: u32) -> Opcode {
    Opcode {
        name,
        mask: REGIMM,
        value: (1 << 26) | (rt << 16),
        operands: OperandFormat::BranchRs,
        delay_slot: true,
    }
}

const fn cop1(name: &'static str, format: u32, function: u32, operands: OperandFormat) -> Opcode {
    Opcode {
        name,
        mask: COP_FMT_FUNCT,
        value: (0x11 << 26) | (format << 21) | function,
        operands,
        delay_slot: false,
    }
}

const fn mmi(name: &'static str, function: u32, operands: OperandFormat) -> Opcode {
    Opcode {
        name,
        mask: MMI,
        value: (0x1c << 26) | function,
        operands,
        delay_slot: false,
    }
}

const fn mmi_sub(name: &'static str, group: u32, function: u32, operands: OperandFormat) -> Opcode {
    Opcode {
        name,
        mask: MMI_SUB,
        value: (0x1c << 26) | (function << 6) | group,
        operands,
        delay_slot: false,
    }
}

pub static OPCODES: &[Opcode] = &[
    Opcode {
        name: "nop",
        mask: u32::MAX,
        value: 0,
        operands: OperandFormat::None,
        delay_slot: false,
    },
    Opcode {
        name: "eret",
        mask: u32::MAX,
        value: 0x4200_0018,
        operands: OperandFormat::None,
        delay_slot: false,
    },
    Opcode {
        name: "sync.l",
        mask: u32::MAX,
        value: 0x0000_000f,
        operands: OperandFormat::None,
        delay_slot: false,
    },
    Opcode {
        name: "sync.p",
        mask: u32::MAX,
        value: 0x0000_040f,
        operands: OperandFormat::None,
        delay_slot: false,
    },
    Opcode {
        name: "jr",
        mask: SPECIAL,
        value: RegisterOpcode::Jr as u32,
        operands: OperandFormat::Rs,
        delay_slot: true,
    },
    Opcode {
        name: "jalr",
        mask: SPECIAL,
        value: RegisterOpcode::Jalr as u32,
        operands: OperandFormat::Jalr,
        delay_slot: true,
    },
    special("syscall", 0x0c, OperandFormat::Syscall),
    special("sll", 0x00, OperandFormat::RdRtShift),
    special("srl", 0x02, OperandFormat::RdRtShift),
    special("sra", 0x03, OperandFormat::RdRtShift),
    special("sllv", 0x04, OperandFormat::RdRtRs),
    special("srlv", 0x06, OperandFormat::RdRtRs),
    special("srav", 0x07, OperandFormat::RdRtRs),
    special("movz", RegisterOpcode::Movz as u32, OperandFormat::RdRsRt),
    special("movn", RegisterOpcode::Movn as u32, OperandFormat::RdRsRt),
    special("mfhi", RegisterOpcode::Mfhi as u32, OperandFormat::Rd),
    special("mflo", RegisterOpcode::Mflo as u32, OperandFormat::Rd),
    special("dsllv", RegisterOpcode::Dsllv as u32, OperandFormat::RdRtRs),
    special("dsrlv", RegisterOpcode::Dsrlv as u32, OperandFormat::RdRtRs),
    special("dsrav", RegisterOpcode::Dsrav as u32, OperandFormat::RdRtRs),
    special("mult", RegisterOpcode::Mult as u32, OperandFormat::RsRt),
    special("multu", RegisterOpcode::Multu as u32, OperandFormat::RsRt),
    special("div", RegisterOpcode::Div as u32, OperandFormat::RsRt),
    special("divu", RegisterOpcode::Divu as u32, OperandFormat::RsRt),
    special("addu", 0x21, OperandFormat::RdRsRt),
    special("subu", 0x23, OperandFormat::RdRsRt),
    special("and", RegisterOpcode::And as u32, OperandFormat::RdRsRt),
    special("or", RegisterOpcode::Or as u32, OperandFormat::RdRsRt),
    special("xor", RegisterOpcode::Xor as u32, OperandFormat::RdRsRt),
    special("nor", RegisterOpcode::Nor as u32, OperandFormat::RdRsRt),
    special("slt", RegisterOpcode::Slt as u32, OperandFormat::RdRsRt),
    special("sltu", RegisterOpcode::Sltu as u32, OperandFormat::RdRsRt),
    special("daddu", RegisterOpcode::Daddu as u32, OperandFormat::RdRsRt),
    special("dsubu", RegisterOpcode::Dsubu as u32, OperandFormat::RdRsRt),
    special("dsll", 0x38, OperandFormat::RdRtShift),
    special("dsrl", 0x3a, OperandFormat::RdRtShift),
    special("dsra", 0x3b, OperandFormat::RdRtShift),
    special("dsll32", 0x3c, OperandFormat::RdRtShift),
    special("dsrl32", 0x3e, OperandFormat::RdRtShift),
    special("dsra32", 0x3f, OperandFormat::RdRtShift),
    regimm("bltz", RegimmOpcode::Bltz as u32),
    regimm("bgez", RegimmOpcode::Bgez as u32),
    regimm("bltzl", RegimmOpcode::Bltzl as u32),
    regimm("bgezl", RegimmOpcode::Bgezl as u32),
    regimm("bgezal", 17),
    branch(
        "beq",
        ImmediateOpcode::Beq as u32,
        OperandFormat::BranchRsRt,
    ),
    branch(
        "bne",
        ImmediateOpcode::Bne as u32,
        OperandFormat::BranchRsRt,
    ),
    branch("blez", 6, OperandFormat::BranchRs),
    branch("bgtz", 7, OperandFormat::BranchRs),
    op(
        "addiu",
        ImmediateOpcode::Addiu as u32,
        OperandFormat::RtRsSigned,
    ),
    op("slti", 10, OperandFormat::RtRsSigned),
    op("sltiu", 11, OperandFormat::RtRsSigned),
    op("andi", 12, OperandFormat::RtRsUnsigned),
    op(
        "ori",
        ImmediateOpcode::Ori as u32,
        OperandFormat::RtRsUnsigned,
    ),
    op("xori", 14, OperandFormat::RtRsUnsigned),
    op(
        "lui",
        ImmediateOpcode::Lui as u32,
        OperandFormat::RtUnsigned,
    ),
    branch(
        "beql",
        ImmediateOpcode::Beql as u32,
        OperandFormat::BranchRsRt,
    ),
    branch(
        "bnel",
        ImmediateOpcode::Bnel as u32,
        OperandFormat::BranchRsRt,
    ),
    branch("bgtzl", 23, OperandFormat::BranchRs),
    op(
        "daddiu",
        ImmediateOpcode::Daddiu as u32,
        OperandFormat::RtRsSigned,
    ),
    op("ldl", 26, OperandFormat::RtMemory),
    op("ldr", 27, OperandFormat::RtMemory),
    op("lq", ImmediateOpcode::Lq as u32, OperandFormat::RtMemory),
    op("sq", ImmediateOpcode::Sq as u32, OperandFormat::RtMemory),
    op("lb", 32, OperandFormat::RtMemory),
    op("lh", 33, OperandFormat::RtMemory),
    op("lwl", 34, OperandFormat::RtMemory),
    op("lw", ImmediateOpcode::Lw as u32, OperandFormat::RtMemory),
    op("lbu", 36, OperandFormat::RtMemory),
    op("lhu", 37, OperandFormat::RtMemory),
    op("lwr", 38, OperandFormat::RtMemory),
    op("lwu", 39, OperandFormat::RtMemory),
    op("sb", 40, OperandFormat::RtMemory),
    op("sh", 41, OperandFormat::RtMemory),
    op("sw", ImmediateOpcode::Sw as u32, OperandFormat::RtMemory),
    op(
        "lwc1",
        ImmediateOpcode::Lwc1 as u32,
        OperandFormat::FtMemory,
    ),
    op("pref", 51, OperandFormat::RtMemory),
    op("lqc2", 54, OperandFormat::VfMemory),
    op("ld", ImmediateOpcode::Ld as u32, OperandFormat::RtMemory),
    op(
        "swc1",
        ImmediateOpcode::Swc1 as u32,
        OperandFormat::FtMemory,
    ),
    op("sqc2", 62, OperandFormat::VfMemory),
    op("sd", ImmediateOpcode::Sd as u32, OperandFormat::RtMemory),
    Opcode {
        name: "mfc0",
        mask: COP_RS,
        value: 0x4000_0000,
        operands: OperandFormat::Cop0,
        delay_slot: false,
    },
    Opcode {
        name: "mtc0",
        mask: COP_RS,
        value: 0x4080_0000,
        operands: OperandFormat::Cop0,
        delay_slot: false,
    },
    Opcode {
        name: "mfc1",
        mask: COP_RS,
        value: 0x4400_0000,
        operands: OperandFormat::RtFs,
        delay_slot: false,
    },
    Opcode {
        name: "mtc1",
        mask: COP_RS,
        value: 0x4480_0000,
        operands: OperandFormat::FsRt,
        delay_slot: false,
    },
    Opcode {
        name: "bc1f",
        mask: 0xffff_0000,
        value: 0x4500_0000,
        operands: OperandFormat::Branch,
        delay_slot: true,
    },
    Opcode {
        name: "bc1t",
        mask: 0xffff_0000,
        value: 0x4501_0000,
        operands: OperandFormat::Branch,
        delay_slot: true,
    },
    Opcode {
        name: "bc1fl",
        mask: 0xffff_0000,
        value: 0x4502_0000,
        operands: OperandFormat::Branch,
        delay_slot: true,
    },
    Opcode {
        name: "bc1tl",
        mask: 0xffff_0000,
        value: 0x4503_0000,
        operands: OperandFormat::Branch,
        delay_slot: true,
    },
    cop1("add.s", 16, FloatOpcode::Add as u32, OperandFormat::FdFsFt),
    cop1(
        "sub.s",
        16,
        FloatOpcode::Subtract as u32,
        OperandFormat::FdFsFt,
    ),
    cop1(
        "mul.s",
        16,
        FloatOpcode::Multiply as u32,
        OperandFormat::FdFsFt,
    ),
    cop1(
        "div.s",
        16,
        FloatOpcode::Divide as u32,
        OperandFormat::FdFsFt,
    ),
    cop1("sqrt.s", 16, 4, OperandFormat::FdFt),
    cop1("abs.s", 16, 5, OperandFormat::FdFs),
    Opcode {
        name: "mov.s",
        mask: COP_FMT_FUNCT,
        value: encode_float_move(0, 0),
        operands: OperandFormat::FdFs,
        delay_slot: false,
    },
    cop1("neg.s", 16, 7, OperandFormat::FdFs),
    cop1("madd.s", 16, 0x18, OperandFormat::FdFsFt),
    cop1("msub.s", 16, 0x19, OperandFormat::FdFsFt),
    cop1("max.s", 16, 0x28, OperandFormat::FdFsFt),
    cop1("min.s", 16, 0x29, OperandFormat::FdFsFt),
    cop1("c.eq.s", 16, 0x32, OperandFormat::FsFt),
    cop1("c.lt.s", 16, 0x34, OperandFormat::FsFt),
    cop1("c.le.s", 16, 0x36, OperandFormat::FsFt),
    cop1("cvt.w.s", 16, 0x24, OperandFormat::FdFs),
    cop1("cvt.s.w", 20, 0x20, OperandFormat::FdFs),
    mmi("plzcw", 0x04, OperandFormat::RdRsRt),
    mmi("mtlo1", 0x13, OperandFormat::Rs),
    mmi("mflo1", 0x12, OperandFormat::Rd),
    mmi_sub("paddw", 0x08, 0x00, OperandFormat::RdRsRt),
    mmi_sub("psubw", 0x08, 0x01, OperandFormat::RdRsRt),
    mmi_sub("pcgtw", 0x08, 0x02, OperandFormat::RdRsRt),
    mmi_sub("pmaxw", 0x08, 0x03, OperandFormat::RdRsRt),
    mmi_sub("paddh", 0x08, 0x04, OperandFormat::RdRsRt),
    mmi_sub("psubh", 0x08, 0x05, OperandFormat::RdRsRt),
    mmi_sub("pmaxh", 0x08, 0x07, OperandFormat::RdRsRt),
    mmi_sub("paddb", 0x08, 0x08, OperandFormat::RdRsRt),
    mmi_sub("pcgtb", 0x08, 0x0a, OperandFormat::RdRsRt),
    mmi_sub("pextlw", 0x08, 0x12, OperandFormat::RdRsRt),
    mmi_sub("ppacw", 0x08, 0x13, OperandFormat::RdRsRt),
    mmi_sub("pextlh", 0x08, 0x16, OperandFormat::RdRsRt),
    mmi_sub("ppach", 0x08, 0x17, OperandFormat::RdRsRt),
    mmi_sub("pextlb", 0x08, 0x1a, OperandFormat::RdRsRt),
    mmi_sub("ppacb", 0x08, 0x1b, OperandFormat::RdRsRt),
    mmi_sub("pceqw", 0x28, 0x02, OperandFormat::RdRsRt),
    mmi_sub("pminw", 0x28, 0x03, OperandFormat::RdRsRt),
    mmi_sub("pminh", 0x28, 0x07, OperandFormat::RdRsRt),
    mmi_sub("pceqb", 0x28, 0x0a, OperandFormat::RdRsRt),
    mmi_sub("pextuw", 0x28, 0x12, OperandFormat::RdRsRt),
    mmi_sub("pextuh", 0x28, 0x16, OperandFormat::RdRsRt),
    mmi_sub("pextub", 0x28, 0x1a, OperandFormat::RdRsRt),
    mmi_sub("qfsrv", 0x28, 0x1b, OperandFormat::RdRsRt),
    mmi_sub("pcpyld", 0x09, 0x0e, OperandFormat::RdRsRt),
    mmi_sub("pmaddh", 0x09, 0x10, OperandFormat::RdRsRt),
    mmi_sub("pand", 0x09, 0x12, OperandFormat::RdRsRt),
    mmi_sub("pxor", 0x09, 0x13, OperandFormat::RdRsRt),
    mmi_sub("pmulth", 0x09, 0x1c, OperandFormat::RdRsRt),
    mmi_sub("pinteh", 0x29, 0x0a, OperandFormat::RdRsRt),
    mmi_sub("pcpyud", 0x29, 0x0e, OperandFormat::RdRsRt),
    Opcode {
        name: "por",
        mask: MMI_SUB,
        value: RegisterOpcode::Por as u32,
        operands: OperandFormat::RdRsRt,
        delay_slot: false,
    },
    mmi_sub("pnor", 0x29, 0x13, OperandFormat::RdRsRt),
];

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decodes_common_goal_instructions() {
        assert_eq!(decode(0).unwrap().format(None, None), "nop");
        assert_eq!(
            decode(0x67bd_fff0).unwrap().format(None, None),
            "daddiu sp, sp, -16"
        );
        assert_eq!(
            decode(0x0320_f809).unwrap().format(None, None),
            "jalr ra, t9"
        );
        assert!(decode(0x4c00_0000).is_err());
    }
}
