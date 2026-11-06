use std::cmp::Reverse;
use std::collections::{BTreeMap, BTreeSet, HashMap};

use crate::ee::{self, OperandFormat};
use crate::goal_object::{Function, GoalObject, Link, Location};
use crate::ir::Gpr;
use crate::static_data::Segment;

pub fn disassemble_object(bytes: &[u8], file_name: &str) -> Result<String, String> {
    let object = GoalObject::read(bytes).map_err(|error| format!("{file_name}: {error}"))?;
    let functions = object
        .functions()
        .map_err(|error| format!("{file_name}: {error}"))?;
    let labels = collect_labels(&object, &functions, file_name)?;
    let mut output = String::new();
    output.push_str(&format!("; object: {}\n", object.name));
    for segment in Segment::DISPLAY_ORDER {
        output.push_str("\n;------------------------------------------\n;  ");
        output.push_str(segment.name());
        output.push_str(" segment\n;------------------------------------------\n\n");
        for function in functions
            .iter()
            .filter(|function| function.segment == segment)
        {
            write_function(&mut output, &object, function, &labels, file_name)?;
        }
        let data_start = functions
            .iter()
            .filter(|function| function.segment == segment)
            .map(|function| function.end)
            .max()
            .unwrap_or(0);
        write_data(&mut output, &object, segment, data_start, &labels)?;
    }
    Ok(output)
}

fn collect_labels(
    object: &GoalObject<'_>,
    functions: &[Function],
    file_name: &str,
) -> Result<BTreeMap<Location, String>, String> {
    let mut locations = BTreeSet::new();
    for relocation in &object.relocations {
        match relocation.link {
            Link::Pointer { target } | Link::SplitPointer { target, .. } => {
                locations.insert(target);
            }
            _ => {}
        }
    }
    for function in functions {
        locations.insert(Location {
            segment: function.segment,
            offset: function.start,
        });
        for offset in (function.start..function.end).step_by(4) {
            let word = object.word(Location {
                segment: function.segment,
                offset,
            })?;
            let instruction = ee::decode(word).map_err(|error| {
                format!(
                    "{file_name}: {} segment offset {offset:#x}: {error}",
                    function.segment.name()
                )
            })?;
            if let Some(target) = instruction.branch_target(offset) {
                locations.insert(Location {
                    segment: function.segment,
                    offset: target,
                });
            }
            if uses_immediate(instruction.opcode.operands)
                && ee::gpr_rs(word) == Gpr::Fp
                && object
                    .relocation(Location {
                        segment: function.segment,
                        offset,
                    })
                    .is_none()
            {
                let target = i64::from(function.start) + i64::from(word as i16);
                if let Ok(target) = u32::try_from(target)
                    && target as usize <= object.segments[function.segment.index()].len()
                {
                    locations.insert(Location {
                        segment: function.segment,
                        offset: target,
                    });
                }
            }
        }
    }
    let mut locations: Vec<_> = locations.into_iter().collect();
    locations.sort_unstable_by_key(|location| (Reverse(location.segment), location.offset));
    Ok(locations
        .into_iter()
        .enumerate()
        .map(|(index, location)| (location, format!("L{index}")))
        .collect())
}

fn write_function(
    output: &mut String,
    object: &GoalObject<'_>,
    function: &Function,
    labels: &BTreeMap<Location, String>,
    file_name: &str,
) -> Result<(), String> {
    output.push_str(";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n");
    output.push_str(&format!("; .function {}\n", function.name));
    output.push_str(";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n");
    output.push_str(
        &labels[&Location {
            segment: function.segment,
            offset: function.start,
        }],
    );
    output.push_str(":\n");
    let low_relocations: HashMap<_, _> = object
        .relocations
        .iter()
        .filter_map(|relocation| match relocation.link {
            Link::SplitPointer { low_offset, target }
                if relocation.at.segment == function.segment =>
            {
                Some((low_offset, target))
            }
            _ => None,
        })
        .collect();
    let mut previous_had_delay = false;
    for offset in (function.start..function.end).step_by(4) {
        let location = Location {
            segment: function.segment,
            offset,
        };
        if offset != function.start
            && let Some(label) = labels.get(&location)
        {
            output.push_str(label);
            output.push_str(":\n");
        }
        let word = object.word(location)?;
        let instruction = ee::decode(word).map_err(|error| {
            format!(
                "{file_name}: {} `{}` offset {offset:#x}, word {word:#010x}: {error}",
                function.segment.name(),
                function.name
            )
        })?;
        let relocation_target = match object
            .relocation(location)
            .map(|relocation| relocation.link)
        {
            Some(Link::SplitPointer { target, .. }) => Some(target),
            _ => low_relocations.get(&offset).copied(),
        };
        let relocation_text = match object
            .relocation(location)
            .map(|relocation| relocation.link)
        {
            Some(Link::SymbolOffset { name, .. }) => Some(name.to_owned()),
            Some(Link::SymbolPointer(name)) => Some(name.to_owned()),
            Some(Link::EmptyList) => Some("'()".to_owned()),
            Some(Link::SplitPointer { target, .. }) => labels.get(&target).cloned(),
            Some(Link::Pointer { target }) => labels.get(&target).cloned(),
            Some(Link::TypePointer { name, .. }) => Some(name.to_owned()),
            None => relocation_target.and_then(|target| labels.get(&target).cloned()),
        };
        let fp_text = if relocation_text.is_none()
            && uses_immediate(instruction.opcode.operands)
            && ee::gpr_rs(word) == Gpr::Fp
        {
            let target = i64::from(function.start) + i64::from(word as i16);
            u32::try_from(target).ok().and_then(|offset| {
                labels
                    .get(&Location {
                        segment: function.segment,
                        offset,
                    })
                    .cloned()
            })
        } else {
            None
        };
        let branch = instruction.branch_target(offset).and_then(|target| {
            labels
                .get(&Location {
                    segment: function.segment,
                    offset: target,
                })
                .map(String::as_str)
        });
        output.push_str("    ");
        output.push_str(
            &instruction.format(relocation_text.as_deref().or(fp_text.as_deref()), branch),
        );
        output.push('\n');
        if previous_had_delay {
            output.push('\n');
        }
        previous_had_delay = instruction.opcode.delay_slot;
    }
    output.push('\n');
    Ok(())
}

fn write_data(
    output: &mut String,
    object: &GoalObject<'_>,
    segment: Segment,
    start: u32,
    labels: &BTreeMap<Location, String>,
) -> Result<(), String> {
    let bytes = object.segments[segment.index()];
    if start as usize >= bytes.len() {
        return Ok(());
    }
    output.push_str("; data\n");
    let low_relocations: HashMap<_, _> = object
        .relocations
        .iter()
        .filter_map(|relocation| match relocation.link {
            Link::SplitPointer { low_offset, target } if relocation.at.segment == segment => {
                Some((low_offset, target))
            }
            _ => None,
        })
        .collect();
    let mut offset = start;
    while offset < bytes.len() as u32 {
        let location = Location { segment, offset };
        if let Some(label) = labels.get(&location) {
            output.push_str(label);
            output.push_str(":\n");
        }

        if offset >= 4
            && object
                .relocation(Location {
                    segment,
                    offset: offset - 4,
                })
                .is_some_and(|relocation| {
                    matches!(relocation.link, Link::TypePointer { name: "string", .. })
                })
        {
            let (string, size) = read_string(bytes, offset)?;
            output.push_str("    .string \"");
            output.push_str(&string);
            output.push_str("\"\n");
            offset += size;
            continue;
        }

        let line = match object
            .relocation(location)
            .map(|relocation| relocation.link)
        {
            Some(Link::Pointer { target }) => format!("    .word {}\n", labels[&target]),
            Some(Link::SplitPointer { target, .. }) => {
                format!("    .word.hi {}\n", labels[&target])
            }
            Some(Link::SymbolPointer(name)) => format!("    .symbol {name}\n"),
            Some(Link::EmptyList) => "    .empty-list\n".to_owned(),
            Some(Link::TypePointer { name, methods }) => {
                format!("    .type {name}, {methods}\n")
            }
            Some(Link::SymbolOffset { name, signed }) => format!(
                "    .symbol-offset {}, {name}\n",
                if signed { -1 } else { 0 }
            ),
            None => match low_relocations.get(&offset) {
                Some(target) => format!("    .word.lo {}\n", labels[target]),
                None => format!("    .word #x{:08x}\n", object.word(location)?),
            },
        };
        output.push_str(&line);
        offset += 4;
    }
    output.push('\n');
    Ok(())
}

fn read_string(bytes: &[u8], offset: u32) -> Result<(String, u32), String> {
    let offset = offset as usize;
    let length_bytes = bytes
        .get(offset..offset + 4)
        .ok_or_else(|| "truncated GOAL string length".to_owned())?;
    let length = u32::from_le_bytes(length_bytes.try_into().unwrap()) as usize;
    let text_start = offset + 4;
    let text_end = text_start
        .checked_add(length)
        .ok_or_else(|| "GOAL string length overflow".to_owned())?;
    if bytes.get(text_end) != Some(&0) {
        return Err("GOAL string is truncated or lacks a NUL terminator".to_owned());
    }
    let padded_end = text_end
        .checked_add(1)
        .and_then(|end| end.checked_add(3))
        .map(|end| end & !3)
        .ok_or_else(|| "GOAL string size overflow".to_owned())?;
    let padding = bytes
        .get(text_end + 1..padded_end)
        .ok_or_else(|| "GOAL string padding extends past the segment".to_owned())?;
    if padding.iter().any(|byte| *byte != 0) {
        return Err("GOAL string has nonzero word padding".to_owned());
    }

    let mut escaped = String::with_capacity(length);
    for byte in &bytes[text_start..text_end] {
        match *byte {
            b'"' => escaped.push_str("\\\""),
            b'\\' => escaped.push_str("\\\\"),
            b'\n' => escaped.push_str("\\n"),
            b'\r' => escaped.push_str("\\r"),
            b'\t' => escaped.push_str("\\t"),
            0x20..=0x7e => escaped.push(*byte as char),
            _ => escaped.push_str(&format!("\\x{byte:02x}")),
        }
    }
    Ok((escaped, (padded_end - offset) as u32))
}

fn uses_immediate(format: OperandFormat) -> bool {
    matches!(
        format,
        OperandFormat::RtRsSigned
            | OperandFormat::RtRsUnsigned
            | OperandFormat::RtUnsigned
            | OperandFormat::RtMemory
            | OperandFormat::FtMemory
            | OperandFormat::VfMemory
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_and_escapes_goal_strings() {
        let bytes = [6, 0, 0, 0, b'a', b'"', b'\\', b'\n', 0x80, b'z', 0, 0];
        assert_eq!(
            read_string(&bytes, 0).unwrap(),
            ("a\\\"\\\\\\n\\x80z".to_owned(), 12)
        );
    }

    #[test]
    fn rejects_invalid_goal_strings() {
        assert!(read_string(&[1, 0, 0, 0, b'a'], 0).is_err());
        assert!(read_string(&[1, 0, 0, 0, b'a', 0, 1, 0], 0).is_err());
    }
}
