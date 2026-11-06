use std::collections::HashMap;

use crate::ee::{self, ImmediateOpcode as I, RegisterOpcode as R};
use crate::ir::Gpr;
use crate::static_data::Segment;

pub const V3_HEADER_SIZE: usize = 128;
pub const V3_NAME_OFFSET: usize = 16;
pub const V3_NAME_SIZE: usize = 64;
pub const V3_SEGMENT_INFO_OFFSET: usize = 80;
pub const V3_SEGMENT_INFO_SIZE: usize = 16;

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct Location {
    pub segment: Segment,
    pub offset: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Link<'a> {
    Pointer { target: Location },
    SplitPointer { low_offset: u32, target: Location },
    SymbolPointer(&'a str),
    EmptyList,
    TypePointer { name: &'a str, methods: u8 },
    SymbolOffset { name: &'a str, signed: bool },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Relocation<'a> {
    pub at: Location,
    pub link: Link<'a>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Function {
    pub name: String,
    pub segment: Segment,
    pub start: u32,
    pub end: u32,
}

#[derive(Debug)]
pub struct GoalObject<'a> {
    pub name: &'a str,
    pub segments: [&'a [u8]; Segment::COUNT],
    pub relocations: Vec<Relocation<'a>>,
}

impl<'a> GoalObject<'a> {
    pub fn read(bytes: &'a [u8]) -> Result<Self, String> {
        if bytes.len() < V3_HEADER_SIZE {
            return Err("truncated V3 GOAL object header".to_owned());
        }
        let segment_count = Segment::COUNT as u32;
        if word(bytes, 0)? != 0
            || word(bytes, 8)? != segment_count
            || word(bytes, 12)? != segment_count
        {
            return Err("expected a three-segment V3 GOAL object".to_owned());
        }
        let data_start = word(bytes, 4)? as usize;
        if data_start < V3_HEADER_SIZE || !data_start.is_multiple_of(16) || data_start > bytes.len()
        {
            return Err("invalid GOAL object segment-data offset".to_owned());
        }
        let name = terminated_string(
            &bytes[V3_NAME_OFFSET..V3_NAME_OFFSET + V3_NAME_SIZE],
            "GOAL object name",
        )?;
        let mut object = Self {
            name,
            segments: [&[]; Segment::COUNT],
            relocations: Vec::new(),
        };
        let mut link_starts = [0_usize; Segment::COUNT];
        let mut expected_data = data_start;
        for segment in Segment::ALL {
            let index = segment.index();
            let link_start = &mut link_starts[index];
            let info = V3_SEGMENT_INFO_OFFSET + index * V3_SEGMENT_INFO_SIZE;
            *link_start = word(bytes, info)? as usize;
            let start = data_start
                .checked_add(word(bytes, info + 4)? as usize)
                .ok_or_else(|| "GOAL object segment offset overflow".to_owned())?;
            let size = word(bytes, info + 8)? as usize;
            let end = start
                .checked_add(size)
                .ok_or_else(|| "GOAL object segment size overflow".to_owned())?;
            if word(bytes, info + 12)? != 0 || !size.is_multiple_of(4) || start != expected_data {
                return Err(format!("invalid layout for {} segment", segment.name()));
            }
            object.segments[index] = bytes.get(start..end).ok_or_else(|| {
                format!(
                    "GOAL object {} segment extends past the file",
                    segment.name()
                )
            })?;
            expected_data = align16(end);
        }
        if expected_data != bytes.len()
            || link_starts[Segment::Main.index()] != V3_HEADER_SIZE
            || link_starts.windows(2).any(|pair| pair[0] >= pair[1])
            || link_starts[Segment::TopLevel.index()] >= data_start
        {
            return Err("invalid GOAL object link or segment boundaries".to_owned());
        }
        for segment in Segment::ALL {
            let index = segment.index();
            let end = link_starts.get(index + 1).copied().unwrap_or(data_start);
            let mut cursor = Cursor {
                bytes,
                offset: link_starts[index],
                end,
            };
            object.read_links(segment, &mut cursor)?;
            if segment != Segment::TopLevel && cursor.offset != end {
                return Err(format!(
                    "unexpected bytes after {} segment relocation table",
                    segment.name()
                ));
            }
            if segment == Segment::TopLevel && align16(cursor.offset + 1) != data_start {
                return Err("invalid padding after final relocation table".to_owned());
            }
        }
        object
            .relocations
            .sort_unstable_by_key(|relocation| relocation.at);
        for pair in object.relocations.windows(2) {
            if pair[0].at == pair[1].at {
                return Err(format!(
                    "multiple relocations patch segment {} offset {:#x}",
                    pair[0].at.segment.name(),
                    pair[0].at.offset
                ));
            }
        }
        Ok(object)
    }

    pub fn word(&self, location: Location) -> Result<u32, String> {
        let segment = self.segments[location.segment.index()];
        if !location.offset.is_multiple_of(4) {
            return Err(format!("unaligned object offset {:#x}", location.offset));
        }
        word(segment, location.offset as usize)
    }

    pub fn relocation(&self, location: Location) -> Option<&Relocation<'a>> {
        self.relocations
            .binary_search_by_key(&location, |relocation| relocation.at)
            .ok()
            .map(|index| &self.relocations[index])
    }

    pub fn functions(&self) -> Result<Vec<Function>, String> {
        let mut functions = Vec::new();
        let mut global_names = HashMap::new();
        for (name, location) in self.global_functions() {
            global_names.insert(location, name.to_owned());
        }
        for (owner, slot, location) in self.method_functions() {
            global_names.insert(location, format!("(method {slot} {owner})"));
        }

        for segment in Segment::ALL {
            let mut tags: Vec<u32> = self
                .relocations
                .iter()
                .filter_map(|relocation| {
                    (relocation.at.segment == segment
                        && matches!(
                            relocation.link,
                            Link::TypePointer {
                                name: "function",
                                ..
                            }
                        ))
                    .then_some(relocation.at.offset)
                })
                .collect();
            tags.sort_unstable();
            if tags.is_empty() {
                continue;
            }
            let last_tag = *tags.last().unwrap();
            let words = self.segments[segment.index()];
            let mut final_return = None;
            for offset in (last_tag as usize..words.len()).step_by(4) {
                if word(words, offset)? == ee::encode_register(R::Jr, Gpr::R0, Gpr::Ra, Gpr::R0) {
                    final_return = Some(offset as u32);
                }
            }
            let data_start = final_return
                .and_then(|offset| offset.checked_add(8))
                .ok_or_else(|| {
                    format!(
                        "segment `{}` has functions but no final `jr ra` and delay slot",
                        segment.name()
                    )
                })?;
            for (index, tag) in tags.iter().copied().enumerate() {
                let start = tag + 4;
                let end = tags.get(index + 1).copied().unwrap_or(data_start);
                if end <= start || end as usize > words.len() {
                    return Err(format!(
                        "invalid function boundary in segment `{}` at {start:#x}",
                        segment.name()
                    ));
                }
                let name = global_names
                    .remove(&Location {
                        segment,
                        offset: start,
                    })
                    .unwrap_or_else(|| {
                        if segment == Segment::TopLevel && index == 0 {
                            format!("(top-level-login {})", self.name)
                        } else {
                            format!("{}-{start:04x}", segment.name())
                        }
                    });
                functions.push(Function {
                    name,
                    segment,
                    start,
                    end,
                });
            }
        }
        functions.sort_by_key(|function| (std::cmp::Reverse(function.segment), function.start));
        Ok(functions)
    }

    fn global_functions(&self) -> Vec<(&'a str, Location)> {
        let mut result = Vec::new();
        for relocation in &self.relocations {
            let Link::SymbolOffset { name, .. } = relocation.link else {
                continue;
            };
            if relocation.at.segment != Segment::TopLevel || relocation.at.offset < 8 {
                continue;
            }
            let Ok(store) = self.word(relocation.at) else {
                continue;
            };
            if !ee::has_immediate_opcode(store, I::Sw) || ee::gpr_rs(store) != Gpr::S7 {
                continue;
            }
            let high_at = Location {
                segment: Segment::TopLevel,
                offset: relocation.at.offset - 8,
            };
            let Some(Relocation {
                link: Link::SplitPointer { low_offset, target },
                ..
            }) = self.relocation(high_at)
            else {
                continue;
            };
            if *low_offset != high_at.offset + 4 || target.offset < 4 {
                continue;
            }
            let Ok(high) = self.word(high_at) else {
                continue;
            };
            let Ok(low) = self.word(Location {
                segment: Segment::TopLevel,
                offset: *low_offset,
            }) else {
                continue;
            };
            let register = ee::gpr_rt(store);
            if !ee::has_immediate_opcode(high, I::Lui)
                || !ee::has_immediate_opcode(low, I::Ori)
                || ee::gpr_rs(low) != ee::gpr_rt(high)
                || ee::gpr_rt(low) != register
            {
                continue;
            }
            if self
                .relocation(Location {
                    segment: target.segment,
                    offset: target.offset - 4,
                })
                .is_some_and(|relocation| {
                    matches!(
                        relocation.link,
                        Link::TypePointer {
                            name: "function",
                            ..
                        }
                    )
                })
            {
                result.push((name, *target));
            }
        }
        result
    }

    fn method_functions(&self) -> Vec<(&'a str, u32, Location)> {
        let mut result = Vec::new();
        for (index, relocation) in self.relocations.iter().enumerate() {
            if relocation.at.segment != Segment::TopLevel
                || !matches!(
                    relocation.link,
                    Link::SymbolOffset {
                        name: "method-set!",
                        ..
                    }
                )
            {
                continue;
            }
            let offset = relocation.at.offset;
            let read = |delta| {
                self.word(Location {
                    segment: Segment::TopLevel,
                    offset: offset + delta,
                })
                .ok()
            };
            if read(0).is_none_or(|word| {
                word & 0xffff_0000 != ee::encode_immediate(I::Lw, Gpr::T9.number(), Gpr::S7, 0)
            }) || read(4).is_none_or(|word| {
                word & 0xffff_0000 != ee::encode_immediate(I::Lw, Gpr::A0.number(), Gpr::S7, 0)
            }) || read(8).is_none_or(|word| {
                word & 0xffff_0000 != ee::encode_immediate(I::Addiu, Gpr::A1.number(), Gpr::R0, 0)
            }) || read(12).is_none_or(|word| {
                word & 0xffff_0000 != ee::encode_immediate(I::Lui, Gpr::V1.number(), Gpr::R0, 0)
            }) || read(16).is_none_or(|word| {
                word & 0xffff_0000 != ee::encode_immediate(I::Ori, Gpr::A2.number(), Gpr::V1, 0)
            }) || read(20) != Some(ee::encode_register(R::Jalr, Gpr::Ra, Gpr::T9, Gpr::R0))
            {
                continue;
            }
            let Some(owner) = self.relocations.get(index + 1) else {
                continue;
            };
            let Some(pointer) = self.relocations.get(index + 2) else {
                continue;
            };
            let Link::SymbolOffset { name, .. } = owner.link else {
                continue;
            };
            let Link::SplitPointer { low_offset, target } = pointer.link else {
                continue;
            };
            if owner.at
                != (Location {
                    segment: Segment::TopLevel,
                    offset: offset + 4,
                })
                || pointer.at
                    != (Location {
                        segment: Segment::TopLevel,
                        offset: offset + 12,
                    })
                || low_offset != offset + 16
                || target.offset < 4
            {
                continue;
            }
            if self
                .relocation(Location {
                    segment: target.segment,
                    offset: target.offset - 4,
                })
                .is_some_and(|relocation| {
                    matches!(
                        relocation.link,
                        Link::TypePointer {
                            name: "function",
                            ..
                        }
                    )
                })
            {
                result.push((name, read(8).unwrap() & 0xffff, target));
            }
        }
        result
    }

    fn read_links(&mut self, segment: Segment, cursor: &mut Cursor<'a>) -> Result<(), String> {
        let mut offset = -4_i64;
        let mut fixing = false;
        if cursor.peek()? != 0 {
            loop {
                loop {
                    let count = cursor.byte()?;
                    if fixing {
                        for _ in 0..count {
                            let at = Location {
                                segment,
                                offset: u32::try_from(offset)
                                    .map_err(|_| "invalid pointer relocation offset".to_owned())?,
                            };
                            let value = self.word(at)?;
                            let link = if value >> 24 == 0 {
                                Link::Pointer {
                                    target: Location {
                                        segment,
                                        offset: value,
                                    },
                                }
                            } else {
                                let distance = (value >> 12) & 15;
                                let target_segment = Segment::from_index(((value >> 8) & 15) as u8)
                                    .ok_or_else(|| {
                                        "invalid split pointer target segment".to_owned()
                                    })?;
                                let low_offset = at
                                    .offset
                                    .checked_add(4 * distance)
                                    .ok_or_else(|| "split pointer offset overflow".to_owned())?;
                                if distance == 0 {
                                    return Err("invalid split pointer relocation".to_owned());
                                }
                                let low = self.word(Location {
                                    segment,
                                    offset: low_offset,
                                })?;
                                Link::SplitPointer {
                                    low_offset,
                                    target: Location {
                                        segment: target_segment,
                                        offset: ((value & 255) << 16) | (low & 0xffff),
                                    },
                                }
                            };
                            self.relocations.push(Relocation { at, link });
                            offset += 4;
                        }
                    } else {
                        offset += i64::from(count) * 4;
                        if offset > self.segments[segment.index()].len() as i64 {
                            return Err("pointer relocation seek exceeds segment".to_owned());
                        }
                    }
                    if count != 255 {
                        break;
                    }
                    if cursor.peek()? == 0 {
                        cursor.byte()?;
                        fixing = !fixing;
                    }
                }
                fixing = !fixing;
                if cursor.peek()? == 0 {
                    break;
                }
            }
        }
        cursor.byte()?;
        while cursor.peek()? != 0 {
            let marker = cursor.peek()?;
            let is_type = marker & 128 != 0;
            if is_type {
                cursor.byte()?;
            }
            let name = cursor.string()?;
            let mut offset = 0_u32;
            loop {
                let mut seek = 0_u32;
                loop {
                    let byte = cursor.byte()?;
                    seek = seek
                        .checked_add(u32::from(byte) * 4)
                        .ok_or_else(|| "symbol relocation seek overflow".to_owned())?;
                    if byte != 255 {
                        break;
                    }
                }
                offset = offset
                    .checked_add(seek)
                    .ok_or_else(|| "symbol relocation offset overflow".to_owned())?;
                let at = Location { segment, offset };
                let value = self.word(at)?;
                let link = if value == u32::MAX {
                    if is_type {
                        Link::TypePointer {
                            name,
                            methods: marker & 127,
                        }
                    } else if name == "_empty_" {
                        Link::EmptyList
                    } else {
                        Link::SymbolPointer(name)
                    }
                } else if !is_type && name != "_empty_" && matches!(value & 0xffff, 0 | 0xffff) {
                    Link::SymbolOffset {
                        name,
                        signed: value & 0xffff == 0xffff,
                    }
                } else {
                    return Err(format!(
                        "invalid relocation placeholder at segment {segment} offset {offset:#x}"
                    ));
                };
                self.relocations.push(Relocation { at, link });
                if cursor.peek()? == 0 {
                    break;
                }
            }
            cursor.byte()?;
        }
        cursor.byte()?;
        Ok(())
    }
}

struct Cursor<'a> {
    bytes: &'a [u8],
    offset: usize,
    end: usize,
}

impl<'a> Cursor<'a> {
    fn peek(&self) -> Result<u8, String> {
        if self.offset >= self.end {
            return Err("unterminated GOAL object relocation table".to_owned());
        }
        self.bytes
            .get(self.offset)
            .copied()
            .ok_or_else(|| "truncated GOAL object relocation table".to_owned())
    }

    fn byte(&mut self) -> Result<u8, String> {
        let value = self.peek()?;
        self.offset += 1;
        Ok(value)
    }

    fn string(&mut self) -> Result<&'a str, String> {
        let start = self.offset;
        while self.byte()? != 0 {}
        std::str::from_utf8(&self.bytes[start..self.offset - 1])
            .map_err(|_| "invalid UTF-8 in relocation name".to_owned())
    }
}

fn word(bytes: &[u8], offset: usize) -> Result<u32, String> {
    let bytes = bytes
        .get(offset..offset.saturating_add(4))
        .ok_or_else(|| format!("truncated word at offset {offset:#x}"))?;
    Ok(u32::from_le_bytes(bytes.try_into().unwrap()))
}

fn terminated_string<'a>(bytes: &'a [u8], context: &str) -> Result<&'a str, String> {
    let end = bytes
        .iter()
        .position(|byte| *byte == 0)
        .ok_or_else(|| format!("unterminated {context}"))?;
    std::str::from_utf8(&bytes[..end]).map_err(|_| format!("invalid UTF-8 in {context}"))
}

fn align16(value: usize) -> usize {
    value.saturating_add(15) & !15
}
