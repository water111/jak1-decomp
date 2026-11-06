use std::{env, fs, io};

fn word(data: &[u8], offset: usize) -> usize {
    u32::from_le_bytes(data[offset..offset + 4].try_into().unwrap()) as usize
}

fn put(data: &mut [u8], offset: usize, value: usize) {
    data[offset..offset + 4].copy_from_slice(&(value as u32).to_le_bytes());
}

fn half(data: &[u8], offset: usize) -> usize {
    u16::from_le_bytes(data[offset..offset + 2].try_into().unwrap()) as usize
}

fn string(data: &[u8], offset: usize) -> &[u8] {
    let data = &data[offset..];
    &data[..data.iter().position(|&b| b == 0).unwrap()]
}

fn align(data: &mut Vec<u8>, alignment: usize) {
    data.resize(data.len().next_multiple_of(alignment.max(1)), 0);
}

// Restore the FILE symbol associated with an assembly include in ELF and ECOFF metadata.
fn add_file_symbol(raw: &[u8], source: &str, include: &str) -> Vec<u8> {
    assert_eq!(&raw[..7], b"\x7fELF\x01\x01\x01");
    assert_eq!((half(raw, 16), half(raw, 18)), (1, 8));
    let section_offset = word(raw, 32);
    let mut sections: Vec<Vec<u8>> = (0..half(raw, 48))
        .map(|i| raw[section_offset + i * 40..section_offset + (i + 1) * 40].to_vec())
        .collect();
    let mut payloads: Vec<Vec<u8>> = sections
        .iter()
        .map(|s| {
            if word(s, 4) == 8 {
                Vec::new()
            } else {
                raw[word(s, 16)..word(s, 16) + word(s, 20)].to_vec()
            }
        })
        .collect();
    let section_names = &payloads[half(raw, 50)];
    let index = |name: &[u8]| {
        sections
            .iter()
            .position(|s| string(section_names, word(s, 0)) == name)
            .unwrap()
    };
    let debug_index = index(b".mdebug");
    let symbol_index = index(b".symtab");
    let debug_offset = word(&sections[debug_index], 16);
    let debug = &payloads[debug_index];
    let mut header = debug[..96].to_vec();
    // Each ECOFF table has a count, an absolute file offset, and fixed-size entries.
    let specs = [
        (8, 12, 1),
        (16, 20, 8),
        (24, 28, 52),
        (32, 36, 12),
        (40, 44, 12),
        (48, 52, 4),
        (56, 60, 1),
        (64, 68, 1),
        (72, 76, 72),
        (80, 84, 4),
        (88, 92, 16),
    ];
    let mut tables: Vec<Vec<u8>> = specs
        .iter()
        .map(|&(count, offset, width)| {
            let count = word(&header, count);
            if count == 0 {
                Vec::new()
            } else {
                let start = word(&header, offset) - debug_offset;
                debug[start..start + count * width].to_vec()
            }
        })
        .collect();
    let fd_offset = (word(&header, 72) - 1) * 72;
    let mut fd = tables[8][fd_offset..fd_offset + 72].to_vec();
    assert_eq!(
        string(&tables[6], word(&fd, 8) + word(&fd, 4)),
        include.as_bytes()
    );
    assert_eq!(word(&fd, 20), 2);
    let name_index = word(&fd, 12);
    tables[6].truncate(word(&fd, 8) + name_index);
    tables[6].extend_from_slice(source.as_bytes());
    tables[6].push(0);
    put(&mut fd, 12, name_index + source.len() + 1);
    align(&mut tables[6], 4);
    put(&mut header, 56, tables[6].len());
    let first = word(&fd, 16) * 12;
    let bits = word(&tables[3], first + 8);
    put(&mut tables[3], first + 8, bits + (1 << 12));
    let mut symbol = vec![0; 12];
    put(&mut symbol, 0, name_index);
    put(&mut symbol, 8, 0xfffff142); // stStatic, scAbs, indexNil.
    tables[3].splice(first + 12..first + 12, symbol);
    put(&mut fd, 20, 3);
    let count = word(&header, 32);
    put(&mut header, 32, count + 1);
    tables[8][fd_offset..fd_offset + 72].copy_from_slice(&fd);
    let mut debug = vec![0; 96];
    for ((_, offset, _), table) in specs.iter().zip(&tables) {
        put(
            &mut header,
            *offset,
            if table.is_empty() { 0 } else { debug.len() },
        );
        debug.extend_from_slice(table);
    }
    payloads[debug_index] = debug;

    let strings = &payloads[word(&sections[symbol_index], 24)];
    let symbols = &payloads[symbol_index];
    let source_index = symbols
        .chunks_exact(16)
        .position(|s| s[12] == 4 && string(strings, word(s, 0)) == source.as_bytes())
        .unwrap();
    let insertion = source_index + 1;
    let duplicate = symbols[source_index * 16..insertion * 16].to_vec();
    payloads[symbol_index].splice(insertion * 16..insertion * 16, duplicate);
    let local_count = word(&sections[symbol_index], 28);
    if local_count > insertion {
        put(&mut sections[symbol_index], 28, local_count + 1);
    }
    for (section, data) in sections.iter().zip(&mut payloads) {
        if word(section, 4) == 9 && word(section, 24) == symbol_index {
            for relocation in data.chunks_exact_mut(8) {
                let info = word(relocation, 4);
                if info >> 8 >= insertion {
                    put(relocation, 4, info + 256);
                }
            }
        }
    }

    let mut result = raw[..half(raw, 40)].to_vec();
    for (i, section) in sections.iter_mut().enumerate().skip(1) {
        align(&mut result, word(section, 32));
        put(section, 16, result.len());
        if word(section, 4) == 8 {
            continue;
        }
        if i == debug_index {
            for (_, offset, _) in specs {
                let relative = word(&header, offset);
                if relative != 0 {
                    put(&mut header, offset, relative + result.len());
                }
            }
            payloads[i][..96].copy_from_slice(&header);
        }
        put(section, 20, payloads[i].len());
        result.extend_from_slice(&payloads[i]);
    }
    align(&mut result, 4);
    let section_offset = result.len();
    put(&mut result, 32, section_offset);
    for section in sections {
        result.extend_from_slice(&section);
    }
    result
}

fn main() -> io::Result<()> {
    let args: Vec<_> = env::args().skip(1).collect();
    if args.len() != 4 {
        eprintln!("Usage: eefixup <input.o> <output.o> <source-name> <include-name>");
        std::process::exit(1);
    }
    let raw = fs::read(&args[0])?;
    fs::write(&args[1], add_file_symbol(&raw, &args[2], &args[3]))
}
