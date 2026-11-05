// Convert IOP ELF to IRX.
use std::{env, fs};

type Result<T> = std::result::Result<T, String>;
const USAGE: &str = "Usage: iopfixup [-s | --strip=minimal-symbols | --strip=empty-sections] -r output.irx -e entry input.o";

#[derive(Clone, Copy, PartialEq, Eq)]
enum StripMode {
    None,
    MinimalSymbols, // normal stripping
    EmptySections,  // empty debug sections, not sure how this was done originally.
}
const NOBITS: u32 = 8;
const REL: u32 = 9;
const MDEBUG: u32 = 0x70000005;
const REGINFO: u32 = 0x70000006;
const IOPMOD: u32 = 0x70000080;

// Sony's section order. Flags: memory group (1/2/4), start/end group
// (0x10/0x20), GP origin (0x40), place after section headers (0x100).
const ORDER: &[(&str, u32)] = &[
    (".reginfo", 0),
    (".iopmod", 0),
    (".init", 0x11),
    (".rel.init", 0x100),
    (".text", 1),
    (".rel.text", 0x100),
    (".fini", 0x21),
    (".rel.fini", 0x100),
    (".rodata", 0x12),
    (".rel.rodata", 0x100),
    (".rodata1", 2),
    (".rel.rodata1", 0x100),
    (".data", 2),
    (".rel.data", 0x100),
    (".data1", 2),
    (".rel.data1", 0x100),
    (".sdata", 0x42),
    (".rel.sdata", 0x100),
    (".lit8", 2),
    (".lit4", 0x22),
    (".sbss", 0x14),
    (".bss", 0x24),
    (".mdebug", 0),
    (".shstrtab", 0),
    (".symtab", 0x100),
    (".strtab", 0x100),
];

fn bytes(data: &[u8], at: usize, size: usize) -> Result<&[u8]> {
    data.get(at..at.checked_add(size).ok_or("ELF offset overflow")?)
        .ok_or_else(|| "Truncated ELF data".into())
}
fn u16_at(data: &[u8], at: usize) -> Result<u16> {
    Ok(u16::from_le_bytes(bytes(data, at, 2)?.try_into().unwrap()))
}
fn u32_at(data: &[u8], at: usize) -> Result<u32> {
    Ok(u32::from_le_bytes(bytes(data, at, 4)?.try_into().unwrap()))
}
fn put16(data: &mut [u8], at: usize, value: u16) {
    data[at..at + 2].copy_from_slice(&value.to_le_bytes());
}
fn put32(data: &mut [u8], at: usize, value: u32) {
    data[at..at + 4].copy_from_slice(&value.to_le_bytes());
}
fn string(data: &[u8], at: u32) -> Result<String> {
    let tail = data.get(at as usize..).ok_or("Invalid string offset")?;
    let len = tail
        .iter()
        .position(|&b| b == 0)
        .ok_or("Unterminated ELF string")?;
    String::from_utf8(tail[..len].to_vec()).map_err(|_| "Non-UTF-8 ELF name".into())
}
fn align(value: u32, alignment: u32) -> Result<u32> {
    let alignment = alignment.max(1);
    if !alignment.is_power_of_two() {
        return Err("Invalid ELF alignment".into());
    }
    Ok(value
        .checked_add(alignment - 1)
        .ok_or("ELF size overflow")?
        & !(alignment - 1))
}
fn add_string(table: &mut Vec<u8>, name: &str) -> u32 {
    if name.is_empty() {
        return 0;
    }
    let mut at = 1;
    for old in table[1..].split_inclusive(|&b| b == 0) {
        if old.len() == name.len() + 1 && &old[..old.len() - 1] == name.as_bytes() {
            return at as u32;
        }
        at += old.len();
    }
    table.extend_from_slice(name.as_bytes());
    table.push(0);
    at as u32
}

#[derive(Clone)]
struct Section {
    name: String,
    // ELF32 section header, in its on-disk field order.
    h: [u32; 10],
    data: Vec<u8>,
    original: usize,
}
impl Section {
    fn replace_data(&mut self, data: Vec<u8>) {
        self.h[5] = data.len() as u32;
        self.data = data;
    }
}
#[derive(Clone)]
struct Symbol {
    name: String,
    value: u32,
    size: u32,
    info: u8,
    other: u8,
    section: u16,
}
#[derive(Default, Clone, Copy)]
struct Group {
    start: u32,
    size: u32,
    first: usize,
}

fn relocate(data: &mut [u8], rels: &[u8], symbols: &[Symbol], address: u32) -> Result<Vec<u8>> {
    if !rels.len().is_multiple_of(8) {
        return Err("Invalid relocation table size".into());
    }
    let mut output = Vec::new();
    let mut at = 0;
    while at < rels.len() {
        let offset = u32_at(rels, at)?;
        let info = u32_at(rels, at + 4)?;
        let kind = info & 255;
        let sym = symbols
            .get((info >> 8) as usize)
            .ok_or("Invalid relocation symbol")?;
        if kind == 0 {
            at += 8;
            continue;
        }
        if sym.section == u16::MAX {
            return Err("Relocation refers to a removed metadata section".into());
        }
        if offset & 3 != 0 {
            return Err("Unaligned relocation".into());
        }
        let word = u32_at(data, offset as usize)?;
        let value = match kind {
            2 => word.wrapping_add(sym.value), // R_MIPS_32
            4 => {
                // R_MIPS_26: global jump addends are signed.
                let addend = if sym.info >> 4 == 0 {
                    (word & 0x03ffffff) << 2
                } else {
                    ((word << 6) as i32 >> 4) as u32
                };
                (word & 0xfc000000) | (addend.wrapping_add(sym.value) >> 2 & 0x03ffffff)
            }
            5 => {
                // Sony expects adjacent HI16/LO16 relocations for the same symbol.
                if u32_at(rels, at + 12)? != (info & !255) | 6 {
                    return Err("R_MIPS_HI16 without matching R_MIPS_LO16".into());
                }
                let low_offset = u32_at(rels, at + 8)?;
                if low_offset & 3 != 0 {
                    return Err("Unaligned LO16 relocation".into());
                }
                let low = u32_at(data, low_offset as usize)?;
                let full = (word << 16)
                    .wrapping_add(low as i16 as i32 as u32)
                    .wrapping_add(sym.value);
                put32(
                    data,
                    low_offset as usize,
                    (low & 0xffff0000) | (full & 0xffff),
                );
                (word & 0xffff0000) | (full.wrapping_add(0x8000) >> 16)
            }
            _ => return Err(format!("Unsupported MIPS relocation type {kind}")),
        };
        put32(data, offset as usize, value);
        output.extend_from_slice(&offset.wrapping_add(address).to_le_bytes());
        output.extend_from_slice(&kind.to_le_bytes()); // IRX relocations use symbol zero.
        if kind == 5 {
            output.extend_from_slice(&u32_at(rels, at + 8)?.wrapping_add(address).to_le_bytes());
            output.extend_from_slice(&6u32.to_le_bytes());
            at += 8;
        }
        at += 8;
    }
    Ok(output)
}

fn convert(input: &[u8], entry_name: &str, strip: StripMode) -> Result<Vec<u8>> {
    if bytes(input, 0, 7)? != b"\x7fELF\x01\x01\x01"
        || u16_at(input, 16)? != 1
        || u16_at(input, 18)? != 8
    {
        return Err("Expected a little-endian ELF32 MIPS relocatable object".into());
    }
    if u16_at(input, 40)? != 52 || u16_at(input, 44)? != 0 || u16_at(input, 46)? != 40 {
        return Err("Unsupported ELF header layout".into());
    }
    let count = u16_at(input, 48)? as usize;
    let shstr = u16_at(input, 50)? as usize;
    let headers = bytes(input, u32_at(input, 32)? as usize, count * 40)?;
    let mut sections = Vec::new();
    for (i, header) in headers.as_chunks::<40>().0.iter().enumerate() {
        let mut h = [0; 10];
        for (j, field) in h.iter_mut().enumerate() {
            *field = u32_at(header, j * 4)?;
        }
        let data = if h[1] == NOBITS {
            Vec::new()
        } else {
            bytes(input, h[4] as usize, h[5] as usize)?.to_vec()
        };
        sections.push(Section {
            name: String::new(),
            h,
            data,
            original: i,
        });
    }
    let names = sections
        .get(shstr)
        .ok_or("Invalid section name table")?
        .data
        .clone();
    for section in &mut sections {
        section.name = string(&names, section.h[0])?;
        if section.h[2] & 2 != 0 && !ORDER.iter().any(|&(name, _)| name == section.name) {
            return Err(format!("Unsupported allocated section {}", section.name));
        }
        if !matches!(
            section.h[1],
            0 | 1 | 2 | 3 | NOBITS | REL | MDEBUG | REGINFO
        ) {
            return Err(format!("Unsupported section type in {}", section.name));
        }
    }
    if sections.first().is_none_or(|s| s.h != [0; 10]) {
        return Err("Invalid null section".into());
    }
    let symtab = sections
        .iter()
        .position(|s| s.h[1] == 2)
        .ok_or("Missing symbol table")?;
    if sections.iter().filter(|s| s.h[1] == 2).count() != 1
        || sections[symtab].h[9] != 16
        || !sections[symtab].data.len().is_multiple_of(16)
    {
        return Err("Unsupported symbol table".into());
    }
    let strings = sections
        .get(sections[symtab].h[6] as usize)
        .ok_or("Invalid symbol string table")?;
    let mut symbols = Vec::new();
    for s in sections[symtab].data.as_chunks::<16>().0 {
        symbols.push(Symbol {
            name: string(&strings.data, u32_at(s, 0)?)?,
            value: u32_at(s, 4)?,
            size: u32_at(s, 8)?,
            info: s[12],
            other: s[13],
            section: u16_at(s, 14)?,
        });
    }
    if symbols.is_empty() || symbols[0].info != 0 || symbols[0].section != 0 {
        return Err("Missing null symbol".into());
    }
    let removed: Vec<_> = sections
        .iter()
        .filter(|s| s.h[1] == REGINFO || (strip == StripMode::MinimalSymbols && s.h[1] == MDEBUG))
        .map(|s| s.original)
        .collect();
    sections.retain(|s| !removed.contains(&s.original));
    sections.push(Section {
        name: ".iopmod".into(),
        h: [0, IOPMOD, 0, 0, 0, 27, 0, 0, 4, 0],
        data: vec![0; 27],
        original: count,
    });
    sections[1..].sort_by_key(|s| {
        ORDER
            .iter()
            .position(|&(name, _)| name == s.name)
            .unwrap_or(ORDER.len())
    });
    let mut mapping = vec![0; count + 1];
    for (i, s) in sections.iter().enumerate() {
        mapping[s.original] = i;
    }
    for s in &mut sections {
        if s.h[6] != 0 {
            s.h[6] = *mapping.get(s.h[6] as usize).ok_or("Invalid section link")? as u32;
        }
        if s.h[1] == REL {
            s.h[7] = *mapping
                .get(s.h[7] as usize)
                .ok_or("Invalid relocation target")? as u32;
        }
        if s.h[1] == MDEBUG {
            s.h[3] = 0;
        }
    }
    let mut groups = [Group::default(); 3];
    let mut address = 0u32;
    let mut gp = 0;
    for &(name, flags) in ORDER {
        if flags & 7 == 0 {
            continue;
        }
        let group = &mut groups[(flags & 7).trailing_zeros() as usize];
        if flags & 0x10 != 0 {
            address = align(address, 16)?;
            group.start = address;
        }
        if flags & 0x40 != 0 {
            gp = address.checked_add(0x7ff0).ok_or("GP overflow")?;
        }
        if let Some((i, s)) = sections
            .iter_mut()
            .enumerate()
            .find(|(_, s)| s.name == name && s.h[2] & 2 != 0)
        {
            address = align(address, s.h[8])?;
            s.h[3] = address;
            address = address.checked_add(s.h[5]).ok_or("Module size overflow")?;
            if group.first == 0 {
                group.first = i;
            }
        }
        if flags & 0x20 != 0 {
            group.size = address - group.start;
        }
    }
    let [text, data, bss] = groups;
    let reserved = [
        ("_ftext", text.start, text.first as u16),
        ("etext", text.start + text.size, 0xff1f),
        ("_etext", text.start + text.size, 0xff1f),
        ("_fdata", data.start, data.first as u16),
        ("edata", data.start + data.size, 0xff1f),
        ("_edata", data.start + data.size, 0xff1f),
        ("_fbss", bss.start, 0xff1f),
        ("end", bss.start + bss.size, 0xff1f),
        ("_end", bss.start + bss.size, 0xff1f),
        ("_gp", gp, 0xff1f),
    ];
    for s in symbols.iter_mut().skip(1) {
        if removed.contains(&(s.section as usize)) {
            s.section = u16::MAX;
            continue;
        }
        if s.section > 0 && s.section < 0xff00 {
            s.section = *mapping
                .get(s.section as usize)
                .ok_or("Invalid symbol section")? as u16;
            s.value = s.value.wrapping_add(sections[s.section as usize].h[3]);
        } else if s.section == 0 {
            if !reserved.iter().any(|&(name, _, _)| name == s.name) {
                return Err(format!("Undefined symbol: {}", s.name));
            }
        } else if s.section != 0xfff1 {
            return Err(format!(
                "Unsupported symbol section for {} (link with -r -d)",
                s.name
            ));
        }
    }
    for (name, value, section) in reserved {
        if section == 0 {
            return Err("Module needs code and data sections".into());
        }
        let new = Symbol {
            name: name.into(),
            value,
            size: 0,
            info: 0x11,
            other: 0,
            section,
        };
        if let Some(s) = symbols
            .iter_mut()
            .find(|s| s.name == name && s.info >> 4 != 0)
        {
            if s.section != 0 {
                return Err(format!("Reserved symbol already defined: {name}"));
            }
            *s = new;
        } else {
            symbols.push(new);
        }
    }
    for i in 1..sections.len() {
        if sections[i].h[1] != REL {
            continue;
        }
        let target = sections[i].h[7] as usize;
        if sections[i].h[9] != 8 || sections[i].h[6] as usize != mapping[symtab] {
            return Err("Invalid relocation section".into());
        }
        let rels = sections[i].data.clone();
        let s = sections
            .get_mut(target)
            .ok_or("Invalid relocation target")?;
        if s.h[2] & 2 == 0 {
            return Err("Relocations in non-allocated section are unsupported".into());
        }
        let rels = relocate(&mut s.data, &rels, &symbols, s.h[3])?;
        sections[i].replace_data(rels);
    }
    let entry = symbols
        .iter()
        .find(|s| s.name == entry_name && s.info >> 4 != 0 && s.section != 0)
        .ok_or_else(|| format!("Missing entry symbol: {entry_name}"))?
        .value;
    // Resolve the optional Module descriptor after applying its name relocation.
    let module = sections.iter().position(|s| s.h[1] == IOPMOD).unwrap();
    let mut module_address = u32::MAX;
    if let Some(descriptor) = symbols
        .iter()
        .find(|s| s.name == "Module" && s.info >> 4 != 0 && s.section != 0)
    {
        let section = sections
            .get(descriptor.section as usize)
            .ok_or("Invalid Module descriptor section")?;
        let offset = descriptor
            .value
            .checked_sub(section.h[3])
            .ok_or("Invalid Module descriptor address")? as usize;
        let name_address = u32_at(&section.data, offset)?;
        let version = u16_at(&section.data, offset + 4)?;
        let name_section = sections
            .iter()
            .find(|s| {
                s.h[2] & 2 != 0
                    && name_address >= s.h[3]
                    && (name_address - s.h[3]) < s.data.len() as u32
            })
            .ok_or("Invalid Module name address")?;
        let name = string(&name_section.data, name_address - name_section.h[3])?;
        let mut data = vec![0; 26];
        put16(&mut data, 24, version);
        data.extend_from_slice(name.as_bytes());
        data.push(0);
        sections[module].replace_data(data);
        module_address = descriptor.value;
    }
    for (i, value) in [module_address, entry, gp, text.size, data.size, bss.size]
        .into_iter()
        .enumerate()
    {
        put32(&mut sections[module].data, i * 4, value);
    }
    // Drop metadata symbols. Relocations have already been resolved.
    // Section symbols sort first, then other locals, then globals, preserving order.
    symbols.retain(|s| s.section != u16::MAX);
    if strip != StripMode::None {
        symbols.truncate(1);
    }
    symbols[1..].sort_by_key(|s| {
        if s.info >> 4 == 0 {
            if s.info & 15 == 3 {
                (0, s.section)
            } else {
                (1, 0)
            }
        } else {
            (2, 0)
        }
    });
    let mut strtab = vec![0];
    let mut symdata = Vec::new();
    for s in &symbols {
        symdata.extend_from_slice(&add_string(&mut strtab, &s.name).to_le_bytes());
        symdata.extend_from_slice(&s.value.to_le_bytes());
        symdata.extend_from_slice(&s.size.to_le_bytes());
        symdata.extend_from_slice(&[s.info, s.other]);
        symdata.extend_from_slice(&s.section.to_le_bytes());
    }
    let symtab = mapping[symtab];
    let stridx = sections[symtab].h[6] as usize;
    sections[stridx].replace_data(strtab);
    sections[symtab].replace_data(symdata);
    sections[symtab].h[7] = symbols.iter().take_while(|s| s.info >> 4 == 0).count() as u32;
    // 989SND has empty debug/symbol section slots and retains the input
    // section-name table size after shortening its contents. The SDK tools
    // tested do not reproduce this layout; its original producer is unknown.
    let mut shnames = vec![0];
    for s in &mut sections {
        if strip == StripMode::EmptySections && s.h[1] == MDEBUG {
            s.h = [0; 10];
            s.data.clear();
            s.h[0] = shnames.len() as u32;
            shnames.push(0);
        } else {
            s.h[0] = add_string(&mut shnames, &s.name);
        }
    }
    if strip == StripMode::EmptySections {
        sections[symtab].replace_data(Vec::new());
        sections[stridx].replace_data(Vec::new());
    }
    sections[mapping[shstr]].replace_data(shnames);

    // Sony places loadable/debug data before the section headers, then relocations
    // and symbols after them. NOBITS occupies memory but no file bytes.
    let mut offset = 52 + 2 * 32;
    let mut placed = vec![false; sections.len()];
    placed[0] = true;
    for &(name, flags) in ORDER {
        if flags & 0x100 != 0 {
            continue;
        }
        if flags & 0x10 != 0 {
            offset = align(offset, 16)?;
        }
        if let Some((i, s)) = sections
            .iter_mut()
            .enumerate()
            .find(|(_, s)| s.name == name)
        {
            place(s, &mut offset)?;
            placed[i] = true;
        }
    }
    let shoff = align(offset, 4)?;
    offset = shoff
        .checked_add(sections.len() as u32 * 40)
        .ok_or("ELF size overflow")?;
    for (i, s) in sections.iter_mut().enumerate() {
        if !placed[i] {
            place(s, &mut offset)?;
        }
    }
    if strip == StripMode::EmptySections {
        for s in &mut sections {
            if s.name == ".mdebug" {
                let name_offset = s.h[0];
                s.h = [0; 10];
                s.h[0] = name_offset;
            }
        }
        sections[symtab].h = [0; 10];
        sections[stridx].h = [0; 10];
        sections[mapping[shstr]].h[5] = names.len() as u32;
    }
    let mut output = vec![0; offset as usize];
    output[..52].copy_from_slice(bytes(input, 0, 52)?);
    put16(&mut output, 16, 0xff80);
    put32(&mut output, 24, entry);
    put32(&mut output, 28, 52);
    put32(&mut output, 32, shoff);
    put16(&mut output, 42, 32);
    put16(&mut output, 44, 2);
    put16(&mut output, 48, sections.len() as u16);
    put16(&mut output, 50, mapping[shstr] as u16);
    for (i, words) in [
        [
            IOPMOD,
            sections[module].h[4],
            0,
            0,
            if module_address == u32::MAX {
                28
            } else {
                sections[module].h[5]
            },
            0,
            4,
            4,
        ],
        [
            1,
            sections[text.first].h[4],
            text.start,
            0,
            data.start + data.size - text.start,
            bss.start + bss.size - text.start,
            7,
            16,
        ],
    ]
    .iter()
    .enumerate()
    {
        for (j, &word) in words.iter().enumerate() {
            put32(&mut output, 52 + i * 32 + j * 4, word);
        }
    }
    for (i, s) in sections.iter().enumerate() {
        for (j, &word) in s.h.iter().enumerate() {
            put32(&mut output, shoff as usize + i * 40 + j * 4, word);
        }
        if s.h[1] != NOBITS {
            output[s.h[4] as usize..s.h[4] as usize + s.data.len()].copy_from_slice(&s.data);
        }
    }
    Ok(output)
}

fn place(s: &mut Section, offset: &mut u32) -> Result<()> {
    *offset = align(*offset, s.h[8])?;
    if s.h[1] == MDEBUG {
        // ECOFF's symbolic header contains absolute file offsets, unlike ELF sections.
        for at in (12..96).step_by(8) {
            let old = u32_at(&s.data, at)?;
            if old != 0 {
                let relative = old.checked_sub(s.h[4]).ok_or("Invalid .mdebug offset")?;
                if relative as usize > s.data.len() {
                    return Err("Invalid .mdebug offset".into());
                }
                put32(
                    &mut s.data,
                    at,
                    offset
                        .checked_add(relative)
                        .ok_or("Debug offset overflow")?,
                );
            }
        }
    }
    s.h[4] = *offset;
    if s.h[1] != NOBITS {
        *offset = offset.checked_add(s.h[5]).ok_or("ELF size overflow")?;
    }
    Ok(())
}

fn run() -> Result<()> {
    let mut args = env::args().skip(1);
    let (mut output, mut entry, mut input) = (None, None, None);
    let mut strip = StripMode::None;
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "-s" | "--strip=minimal-symbols" if strip == StripMode::None => {
                strip = StripMode::MinimalSymbols;
            }
            "--strip=empty-sections" if strip == StripMode::None => {
                strip = StripMode::EmptySections;
            }
            "-r" if output.is_none() => output = Some(args.next().ok_or(USAGE)?),
            "-e" if entry.is_none() => entry = Some(args.next().ok_or(USAGE)?),
            _ if !arg.starts_with('-') && input.is_none() => input = Some(arg),
            _ => return Err(USAGE.into()),
        }
    }
    let (Some(output), Some(entry), Some(input)) = (output, entry, input) else {
        return Err(USAGE.into());
    };
    let data = fs::read(&input).map_err(|e| format!("{input}: {e}"))?;
    let data = convert(&data, &entry, strip)?;
    fs::write(&output, data).map_err(|e| format!("{output}: {e}"))
}

fn main() {
    if let Err(error) = run() {
        eprintln!("iopfixup: {error}");
        std::process::exit(1);
    }
}
