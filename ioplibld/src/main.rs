//! Minimal IOP import-stub generator. Reads ELF objects directly; no host tools.
use std::collections::BTreeSet;
use std::{env, fs};

type Result<T> = std::result::Result<T, String>;
const USAGE: &str =
    "Usage: ioplibld [--source-name filename] -s imports.s object.o... : imports.ilb...";
const RESERVED: &[&str] = &[
    "_ftext", "etext", "_etext", "_fdata", "edata", "_edata", "_fbss", "end", "_end", "_gp",
];

fn bytes(data: &[u8], offset: usize, size: usize) -> Result<&[u8]> {
    let end = offset.checked_add(size).ok_or("ELF offset overflow")?;
    data.get(offset..end)
        .ok_or_else(|| "Truncated ELF data".into())
}

fn u16_at(data: &[u8], offset: usize) -> Result<u16> {
    Ok(u16::from_le_bytes(
        bytes(data, offset, 2)?.try_into().unwrap(),
    ))
}

fn u32_at(data: &[u8], offset: usize) -> Result<usize> {
    Ok(u32::from_le_bytes(bytes(data, offset, 4)?.try_into().unwrap()) as usize)
}

#[derive(Default)]
struct Symbols {
    defined: BTreeSet<String>,
    undefined: BTreeSet<String>,
}

impl Symbols {
    fn read_object(&mut self, data: &[u8]) -> Result<()> {
        if bytes(data, 0, 7)? != b"\x7fELF\x01\x01\x01"
            || u16_at(data, 16)? != 1 // ET_REL
            || u16_at(data, 18)? != 8
        // EM_MIPS
        {
            return Err("Expected a little-endian ELF32 MIPS relocatable object".into());
        }
        let offset = u32_at(data, 32)?;
        let size = usize::from(u16_at(data, 46)?);
        let count = usize::from(u16_at(data, 48)?);
        if size != 40 || count == 0 {
            return Err("Unsupported ELF section table".into());
        }
        let sections = bytes(data, offset, size * count)?;
        let section_data = |header: &[u8]| bytes(data, u32_at(header, 16)?, u32_at(header, 20)?);
        let mut found_symbols = false;
        for header in sections.chunks_exact(size) {
            if u32_at(header, 4)? != 2 {
                // SHT_SYMTAB
                continue;
            }
            found_symbols = true;
            let link = u32_at(header, 24)?;
            if link >= count || u32_at(header, 36)? != 16 {
                return Err("Invalid ELF symbol table".into());
            }
            let string_header = &sections[link * size..(link + 1) * size];
            if u32_at(string_header, 4)? != 3 {
                // SHT_STRTAB
                return Err("Symbol table does not reference a string table".into());
            }
            let strings = section_data(string_header)?;
            let symbols = section_data(header)?;
            let (records, remainder) = symbols.as_chunks::<16>();
            if !remainder.is_empty() {
                return Err("Invalid ELF symbol-table size".into());
            }
            for symbol in records {
                let binding = symbol[12] >> 4;
                if binding != 1 && binding != 2 {
                    // STB_GLOBAL, STB_WEAK
                    continue;
                }
                let start = u32_at(symbol, 0)?;
                let tail = strings
                    .get(start..)
                    .ok_or("Invalid ELF symbol name offset")?;
                let end = tail
                    .iter()
                    .position(|&b| b == 0)
                    .ok_or("Unterminated ELF symbol name")?;
                let name = std::str::from_utf8(&tail[..end]).map_err(|_| "Non-UTF-8 ELF symbol")?;
                if name.is_empty() {
                    continue;
                }
                if u16_at(symbol, 14)? != 0 {
                    // Includes common and absolute definitions, not just section symbols.
                    self.defined.insert(name.into());
                } else if binding == 1 {
                    // Undefined weak symbols need not be resolved.
                    self.undefined.insert(name.into());
                }
            }
        }
        if !found_symbols {
            return Err("Object has no symbol table".into());
        }
        Ok(())
    }

    fn needed(&self) -> BTreeSet<String> {
        self.undefined
            .difference(&self.defined)
            .filter(|s| !RESERVED.contains(&s.as_str()))
            .cloned()
            .collect()
    }
}

struct Library {
    name: String,
    version: u16,
    flags: u16,
    entries: Vec<(u16, String)>,
}

fn number(value: &str) -> Result<u16> {
    let parsed = if let Some(hex) = value
        .strip_prefix("0x")
        .or_else(|| value.strip_prefix("0X"))
    {
        u16::from_str_radix(hex, 16)
    } else {
        value.parse() // Leading zeros in E ordinals are decimal, not octal.
    };
    parsed.map_err(|_| format!("Invalid 16-bit number: {value}"))
}

fn identifier(name: &str) -> bool {
    let mut chars = name.bytes();
    chars
        .next()
        .is_some_and(|c| c.is_ascii_alphabetic() || b"_.$".contains(&c))
        && chars.all(|c| c.is_ascii_alphanumeric() || b"_.$".contains(&c))
}

fn parse_ilb(text: &str) -> Result<Vec<Library>> {
    let mut libraries: Vec<Library> = Vec::new();
    for (index, line) in text.lines().enumerate() {
        let fields: Vec<_> = line.split_whitespace().collect();
        if fields.is_empty() || fields[0].starts_with('#') {
            continue;
        }
        let error = |message: &str| format!("line {}: {message}", index + 1);
        if let ["L", name] = fields.as_slice() {
            if !identifier(name) || name.len() > 8 {
                return Err(error(
                    "Library name must be an ASCII identifier of 1–8 bytes",
                ));
            }
            libraries.push(Library {
                name: (*name).into(),
                version: 0,
                flags: 0,
                entries: Vec::new(),
            });
            continue;
        }
        let lib = libraries
            .last_mut()
            .ok_or_else(|| error("Expected L before library fields"))?;
        match fields.as_slice() {
            ["V", value] => lib.version = number(value).map_err(|e| error(&e))?,
            ["F", value] => lib.flags = number(value).map_err(|e| error(&e))?,
            ["E", ordinal, symbol] if identifier(symbol) => {
                lib.entries
                    .push((number(ordinal).map_err(|e| error(&e))?, (*symbol).into()));
            }
            _ => {
                return Err(error(
                    "Expected L name, V version, F flags, or E ordinal symbol",
                ));
            }
        }
    }
    Ok(libraries)
}

fn generate(libraries: &[Library], needed: &BTreeSet<String>) -> Result<String> {
    let mut names = BTreeSet::new();
    let mut available = BTreeSet::new();
    for lib in libraries {
        if !names.insert(&lib.name) {
            return Err(format!("Duplicate library: {}", lib.name));
        }
        let mut ordinals = BTreeSet::new();
        for (ordinal, symbol) in &lib.entries {
            if !ordinals.insert(ordinal) || !available.insert(symbol.clone()) {
                return Err(format!(
                    "Duplicate import ordinal or symbol: {} / {symbol}",
                    lib.name
                ));
            }
        }
    }
    let missing: Vec<_> = needed.difference(&available).cloned().collect();
    if !missing.is_empty() {
        return Err(format!(
            "Unresolved symbols absent from ILB: {}",
            missing.join(", ")
        ));
    }
    // Match Sony's output spelling and order, including library-name padding.
    let mut out = String::from("\t.text\n\t.set noreorder\n\n");
    for lib in libraries {
        let entries: Vec<_> = lib
            .entries
            .iter()
            .filter(|(_, s)| needed.contains(s))
            .collect();
        if entries.is_empty() {
            continue;
        }
        let name = &lib.name;
        out.push_str(&format!(
            "/* ------------ {name} ------------ */\n\t.globl\t{name}_stub\n{name}_stub:\n\t.word\t0x41e00000\t/* stub magic */\n\t.word\t0\n\t.short\t0x{:04x}\t\t/* version */\n\t.short\t0x{:04x}\t\t/* flags */\n\t.ascii\t\"{name}{}\"\n\t.align\t2\n\n",
            lib.version, lib.flags, "\\0".repeat(8 - name.len())
        ));
        for (ordinal, symbol) in entries {
            out.push_str(&format!(
                "\t.globl\t\t{symbol}\n{symbol}:\n\tj\t$31\n\taddiu\t$0, $0, {ordinal}\n\n"
            ));
        }
        out.push_str("\t.word\t0\n\t.word\t0\n\n");
    }
    Ok(out)
}

fn source_directive(name: &str) -> Result<String> {
    if name.chars().any(char::is_control) {
        return Err("Source filename must not contain control characters".into());
    }
    let name = name.replace('\\', "\\\\").replace('"', "\\\"");
    Ok(format!(".file 1 \"{name}\"\n"))
}

fn run(args: &[String]) -> Result<()> {
    if args == ["--help"] {
        println!("{USAGE}");
        return Ok(());
    }
    let (source_name, args) = if args.first().map(String::as_str) == Some("--source-name") {
        let name = args.get(1).ok_or(USAGE)?;
        (Some(name.as_str()), &args[2..])
    } else {
        (None, args)
    };
    let separator = args.iter().position(|a| a == ":").ok_or(USAGE)?;
    if args.first().map(String::as_str) != Some("-s")
        || separator < 3
        || separator + 1 == args.len()
    {
        return Err(USAGE.into());
    }
    let mut symbols = Symbols::default();
    for path in &args[2..separator] {
        let data = fs::read(path).map_err(|e| format!("{path}: {e}"))?;
        symbols
            .read_object(&data)
            .map_err(|e| format!("{path}: {e}"))?;
    }
    let mut libraries = Vec::new();
    for path in &args[separator + 1..] {
        let text = fs::read_to_string(path).map_err(|e| format!("{path}: {e}"))?;
        libraries.extend(parse_ilb(&text).map_err(|e| format!("{path}: {e}"))?);
    }
    let mut output = String::new();
    if let Some(name) = source_name {
        output.push_str(&source_directive(name)?);
    }
    output.push_str(&generate(&libraries, &symbols.needed())?);
    fs::write(&args[1], output).map_err(|e| format!("{}: {e}", args[1]))
}

fn main() {
    if let Err(error) = run(&env::args().skip(1).collect::<Vec<_>>()) {
        eprintln!("ioplibld: {error}");
        std::process::exit(1);
    }
}
