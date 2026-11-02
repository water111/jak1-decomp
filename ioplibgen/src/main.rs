use std::collections::BTreeSet;
use std::{env, fs};

type Result<T> = std::result::Result<T, String>;
const USAGE: &str = "Usage: ioplibgen -d library.ilb library.tbl";

fn identifier(name: &str) -> bool {
    let mut chars = name.bytes();
    chars
        .next()
        .is_some_and(|c| c.is_ascii_alphabetic() || b"_.$".contains(&c))
        && chars.all(|c| c.is_ascii_alphanumeric() || b"_.$".contains(&c))
}

fn generate(source: &str) -> Result<String> {
    let mut name = None;
    let mut version = None;
    let mut flags = None;
    let mut entries = Vec::new();
    let mut symbols = BTreeSet::new();

    for (index, line) in source.lines().enumerate() {
        let fields: Vec<_> = line.split('#').next().unwrap().split_whitespace().collect();
        let error = |message: &str| format!("line {}: {message}", index + 1);
        match fields.as_slice() {
            [] => {}
            ["Libname", value] => {
                if name.is_some() || !identifier(value) || value.len() > 8 {
                    return Err(error(
                        "Expected one library name, an ASCII identifier of 1–8 bytes",
                    ));
                }
                name = Some(*value);
            }
            ["Version", value] => {
                if version.is_some() {
                    return Err(error("Duplicate Version"));
                }
                let (major, minor) = value
                    .split_once('.')
                    .ok_or_else(|| error("Expected Version major.minor"))?;
                let major: u8 = major
                    .parse()
                    .map_err(|_| error("Invalid major version (0–255)"))?;
                let minor: u8 = minor
                    .parse()
                    .map_err(|_| error("Invalid minor version (0–255)"))?;
                version = Some((u16::from(major) << 8) | u16::from(minor));
            }
            ["Flags", value] => {
                if flags.is_some() {
                    return Err(error("Duplicate Flags"));
                }
                let parsed = if let Some(hex) = value
                    .strip_prefix("0x")
                    .or_else(|| value.strip_prefix("0X"))
                {
                    u16::from_str_radix(hex, 16)
                } else {
                    value.parse::<u16>()
                };
                flags = Some(parsed.map_err(|_| error("Invalid 16-bit Flags value"))?);
            }
            ["Entry", symbol] => {
                if entries.len() == 65536 {
                    return Err(error("Entry ordinal exceeds 65535"));
                }
                if *symbol != "-" && (!identifier(symbol) || !symbols.insert(*symbol)) {
                    return Err(error("Invalid or duplicate entry name"));
                }
                entries.push(*symbol);
            }
            _ => {
                return Err(error(
                    "Expected Libname, Version, Flags, or Entry; unsupported table syntax",
                ));
            }
        }
    }
    let name = name.ok_or("Missing Libname")?;
    let version = version.ok_or("Missing Version")?;
    if entries.len() < 4 {
        return Err("At least four Entry slots are required (ordinals 0–3)".into());
    }
    let mut output = format!(
        "#IOP-ILB# $Id$\nL {name}\nV 0x{version:04x}\nF 0x{:04x}\n",
        flags.unwrap_or(0)
    );
    for (ordinal, symbol) in entries.iter().enumerate() {
        if *symbol != "-" {
            output.push_str(&format!("E {ordinal:03} {symbol}\n"));
        }
    }
    Ok(output)
}

fn run(args: &[String]) -> Result<()> {
    if args == ["--help"] {
        println!("{USAGE}");
        return Ok(());
    }
    let [option, output, input] = args else {
        return Err(USAGE.into());
    };
    if option != "-d" {
        return Err(USAGE.into());
    }
    let source = fs::read_to_string(input).map_err(|e| format!("{input}: {e}"))?;
    let ilb = generate(&source).map_err(|e| format!("{input}: {e}"))?;
    fs::write(output, ilb).map_err(|e| format!("{output}: {e}"))
}

fn main() {
    if let Err(error) = run(&env::args().skip(1).collect::<Vec<_>>()) {
        eprintln!("ioplibgen: {error}");
        std::process::exit(1);
    }
}

