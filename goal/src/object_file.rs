use std::collections::HashMap;

use crate::codegen::OpcodeFunction;
use crate::frontend::FunctionIdentity;
use crate::goal_object::{
    V3_HEADER_SIZE, V3_NAME_OFFSET, V3_NAME_SIZE, V3_SEGMENT_INFO_OFFSET, V3_SEGMENT_INFO_SIZE,
};
use crate::ir::FunctionId;
use crate::object::SymbolTable;
use crate::static_data::{FixupTarget, Segment, StaticData};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ObjectFileOptions {
    pub function_methods: u8,
}

impl Default for ObjectFileOptions {
    fn default() -> Self {
        Self {
            function_methods: 9,
        }
    }
}

#[derive(Debug)]
struct TypeLink {
    name: String,
    methods: u8,
    offset: u32,
}

#[derive(Debug)]
struct NamedLink {
    name: String,
    offset: u32,
}

#[derive(Debug, Default)]
struct ObjectSegment {
    bytes: Vec<u8>,
    pointers: Vec<u32>,
    types: Vec<TypeLink>,
    symbols: Vec<NamedLink>,
}

pub fn write_v3(
    name: &str,
    functions: &[OpcodeFunction],
    static_data: &StaticData,
    symbols: &SymbolTable,
    options: ObjectFileOptions,
) -> Result<Vec<u8>, String> {
    validate_name(name, "object")?;
    if name.len() >= V3_NAME_SIZE {
        return Err("object name must fit in the 64-byte V3 name field".to_owned());
    }
    if options.function_methods >= 128 {
        return Err("function method count must fit in seven bits".to_owned());
    }
    let top_levels = functions
        .iter()
        .filter(|function| function.identity == FunctionIdentity::TopLevel)
        .count();
    if top_levels != 1 {
        return Err(format!(
            "an object must contain exactly one top-level function, found {top_levels}"
        ));
    }
    if functions.iter().any(|function| {
        (function.identity == FunctionIdentity::TopLevel) != (function.segment == Segment::TopLevel)
    }) {
        return Err("only the top-level function may use the top-level segment".to_owned());
    }

    let mut segments: [ObjectSegment; Segment::COUNT] =
        std::array::from_fn(|_| ObjectSegment::default());
    let mut entries = HashMap::new();
    for segment in [Segment::Main, Segment::Debug] {
        for function in functions.iter().rev().filter(|function| {
            function.segment == segment && function.identity != FunctionIdentity::TopLevel
        }) {
            append_function(
                &mut segments[segment.index()],
                function,
                symbols,
                options.function_methods,
                &mut entries,
            )?;
        }
    }
    for function in functions
        .iter()
        .filter(|function| function.identity == FunctionIdentity::TopLevel)
    {
        if function.segment != Segment::TopLevel {
            return Err("the top-level function must be in the top-level segment".to_owned());
        }
        append_function(
            &mut segments[Segment::TopLevel.index()],
            function,
            symbols,
            options.function_methods,
            &mut entries,
        )?;
    }

    let mut static_bases = [0_u32; Segment::COUNT];
    for segment in Segment::ALL {
        let index = segment.index();
        let section = static_data.section(segment);
        align(&mut segments[index].bytes, section.alignment() as usize);
        static_bases[index] = u32::try_from(segments[index].bytes.len())
            .map_err(|_| "object segment exceeds u32".to_owned())?;
        segments[index].bytes.extend_from_slice(section.bytes());
    }
    apply_static_fixups(&mut segments, static_data, symbols, &static_bases, &entries)?;

    let mut output = vec![0; V3_HEADER_SIZE];
    output[V3_NAME_OFFSET..V3_NAME_OFFSET + name.len()].copy_from_slice(name.as_bytes());
    let mut link_offsets = [0_u32; Segment::COUNT];
    for (index, segment) in segments.iter_mut().enumerate() {
        link_offsets[index] =
            u32::try_from(output.len()).map_err(|_| "link tables exceed u32".to_owned())?;
        segment.pointers.sort_unstable();
        output.extend(encode_links(segment)?);
    }
    output.push(0);
    align(&mut output, 16);
    let data_start = u32::try_from(output.len()).map_err(|_| "object exceeds u32".to_owned())?;
    output[4..8].copy_from_slice(&data_start.to_le_bytes());
    let segment_count = Segment::COUNT as u32;
    output[8..12].copy_from_slice(&segment_count.to_le_bytes());
    output[12..16].copy_from_slice(&segment_count.to_le_bytes());

    for (index, segment) in segments.iter().enumerate() {
        let info = V3_SEGMENT_INFO_OFFSET + index * V3_SEGMENT_INFO_SIZE;
        let data_offset =
            u32::try_from(output.len()).map_err(|_| "object exceeds u32".to_owned())? - data_start;
        let size = u32::try_from(segment.bytes.len())
            .map_err(|_| "object segment exceeds u32".to_owned())?;
        output[info..info + 4].copy_from_slice(&link_offsets[index].to_le_bytes());
        output[info + 4..info + 8].copy_from_slice(&data_offset.to_le_bytes());
        output[info + 8..info + 12].copy_from_slice(&size.to_le_bytes());
        output.extend_from_slice(&segment.bytes);
        align(&mut output, 16);
    }
    Ok(output)
}

fn append_function(
    segment: &mut ObjectSegment,
    function: &OpcodeFunction,
    symbols: &SymbolTable,
    methods: u8,
    entries: &mut HashMap<FunctionId, (Segment, u32)>,
) -> Result<(), String> {
    align(&mut segment.bytes, 8);
    let tag =
        u32::try_from(segment.bytes.len()).map_err(|_| "object segment exceeds u32".to_owned())?;
    push_word(&mut segment.bytes, u32::MAX);
    segment.types.push(TypeLink {
        name: "function".to_owned(),
        methods,
        offset: tag,
    });
    let entry = tag + 4;
    if entries
        .insert(function.id, (function.segment, entry))
        .is_some()
    {
        return Err(format!("duplicate function ID {}", function.id.0));
    }
    for word in &function.words {
        push_word(&mut segment.bytes, *word);
    }
    for link in &function.symbol_links {
        let name = symbols
            .resolve(link.symbol)
            .ok_or_else(|| format!("unknown symbol ID {:#x}", link.symbol.0))?;
        validate_name(name, "symbol")?;
        let offset = entry
            .checked_add(link.offset)
            .ok_or_else(|| "function link offset overflow".to_owned())?;
        patch_symbol_addend(&mut segment.bytes, offset, link.addend)?;
        segment.symbols.push(NamedLink {
            name: name.to_owned(),
            offset,
        });
    }
    push_word(&mut segment.bytes, 0);
    push_word(&mut segment.bytes, 0);
    Ok(())
}

fn apply_static_fixups(
    segments: &mut [ObjectSegment; Segment::COUNT],
    static_data: &StaticData,
    symbols: &SymbolTable,
    static_bases: &[u32; Segment::COUNT],
    entries: &HashMap<FunctionId, (Segment, u32)>,
) -> Result<(), String> {
    for fixup in &static_data.fixups {
        let source_segment = fixup.segment;
        let source = static_bases[source_segment.index()]
            .checked_add(fixup.offset)
            .ok_or_else(|| "static fixup offset overflow".to_owned())?;
        match fixup.target {
            FixupTarget::Symbol(symbol) => {
                let name = symbols
                    .resolve(symbol)
                    .ok_or_else(|| format!("unknown symbol ID {:#x}", symbol.0))?;
                validate_name(name, "symbol")?;
                patch_symbol_addend(
                    &mut segments[source_segment.index()].bytes,
                    source,
                    fixup.addend,
                )?;
                segments[source_segment.index()].symbols.push(NamedLink {
                    name: name.to_owned(),
                    offset: source,
                });
            }
            FixupTarget::Label(label) => {
                let label = static_data
                    .labels
                    .get(label.0 as usize)
                    .ok_or_else(|| "static fixup refers to an unknown label".to_owned())?;
                let target_segment = label.segment;
                let target = static_bases[target_segment.index()]
                    .checked_add(label.offset)
                    .and_then(|target| target.checked_add_signed(fixup.addend))
                    .ok_or_else(|| "static pointer target overflow".to_owned())?;
                add_pointer(segments, source_segment, source, target_segment, target)?;
            }
            FixupTarget::Function(function) => {
                let &(target_segment, target) = entries.get(&function).ok_or_else(|| {
                    format!(
                        "static fixup refers to function {} with no code",
                        function.0
                    )
                })?;
                let target = target
                    .checked_add_signed(fixup.addend)
                    .ok_or_else(|| "function pointer target overflow".to_owned())?;
                add_pointer(segments, source_segment, source, target_segment, target)?;
            }
        }
    }
    Ok(())
}

fn add_pointer(
    segments: &mut [ObjectSegment; Segment::COUNT],
    source_segment: Segment,
    source: u32,
    target_segment: Segment,
    target: u32,
) -> Result<(), String> {
    if source_segment != target_segment {
        return Err("a single-word static pointer cannot cross object segments".to_owned());
    }
    if target >= 1 << 24 {
        return Err("static pointer target exceeds the V3 direct-pointer range".to_owned());
    }
    write_word(&mut segments[source_segment.index()].bytes, source, target)?;
    segments[source_segment.index()].pointers.push(source);
    Ok(())
}

fn patch_symbol_addend(bytes: &mut [u8], offset: u32, addend: i32) -> Result<(), String> {
    let low = match addend {
        0 => 0,
        -1 => u16::MAX,
        _ => return Err("V3 symbol links currently support only addends 0 and -1".to_owned()),
    };
    let word = read_word(bytes, offset)?;
    write_word(bytes, offset, (word & 0xffff_0000) | u32::from(low))
}

fn encode_links(segment: &ObjectSegment) -> Result<Vec<u8>, String> {
    let mut output = Vec::new();
    let mut cursor = -1_i64;
    let mut pointers = segment.pointers.iter().copied().peekable();
    while let Some(offset) = pointers.next() {
        if !offset.is_multiple_of(4) {
            return Err("pointer link is not word-aligned".to_owned());
        }
        let word = i64::from(offset / 4);
        let seek = u32::try_from(word - cursor)
            .map_err(|_| "pointer links are not strictly increasing".to_owned())?;
        run(&mut output, seek);
        let mut count = 1_u32;
        while pointers
            .peek()
            .is_some_and(|next| *next == offset + count * 4)
        {
            pointers.next();
            count += 1;
        }
        run(&mut output, count);
        cursor = word + i64::from(count);
    }
    output.push(0);

    let type_names = historical_name_order(segment.types.iter().map(|link| link.name.as_str()));
    for name in type_names {
        let mut links: Vec<_> = segment
            .types
            .iter()
            .filter(|link| link.name == name)
            .collect();
        links.sort_unstable_by_key(|link| link.offset);
        let methods = links[0].methods;
        if links.iter().any(|link| link.methods != methods) {
            return Err(format!(
                "runtime type `{name}` has inconsistent method counts"
            ));
        }
        output.push(0x80 | methods);
        output.extend_from_slice(name.as_bytes());
        output.push(0);
        encode_named_offsets(&mut output, links.iter().map(|link| link.offset))?;
    }

    let symbol_names = historical_name_order(segment.symbols.iter().map(|link| link.name.as_str()));
    for name in symbol_names {
        let mut offsets: Vec<_> = segment
            .symbols
            .iter()
            .filter_map(|link| (link.name == name).then_some(link.offset))
            .collect();
        offsets.sort_unstable();
        output.extend_from_slice(name.as_bytes());
        output.push(0);
        encode_named_offsets(&mut output, offsets)?;
    }
    output.push(0);
    Ok(output)
}

fn encode_named_offsets(
    output: &mut Vec<u8>,
    offsets: impl IntoIterator<Item = u32>,
) -> Result<(), String> {
    let mut previous = 0;
    for offset in offsets {
        if !offset.is_multiple_of(4) || offset < previous {
            return Err("named links must have increasing word-aligned offsets".to_owned());
        }
        run(output, (offset - previous) / 4);
        previous = offset;
    }
    output.push(0);
    Ok(())
}

fn run(output: &mut Vec<u8>, mut count: u32) {
    while count >= 255 {
        output.push(255);
        count -= 255;
    }
    output.push(count as u8);
}

fn historical_name_order<'a>(names: impl IntoIterator<Item = &'a str>) -> Vec<&'a str> {
    let mut table = NameTable::new();
    for name in names {
        table.insert(name);
    }
    table.buckets.into_iter().flatten().collect()
}

struct NameTable<'a> {
    buckets: Vec<Option<&'a str>>,
    limit: usize,
    count: usize,
}

impl<'a> NameTable<'a> {
    fn new() -> Self {
        Self {
            buckets: vec![None; 103],
            limit: 66,
            count: 0,
        }
    }

    fn insert(&mut self, name: &'a str) {
        if self.count == self.limit {
            let mut size = (self.buckets.len() as f32 * 1.2) as usize | 1;
            for divisor in [3, 5, 7] {
                if size.is_multiple_of(divisor) {
                    size += divisor - 1;
                }
            }
            self.limit = (self.limit * size).div_ceil(self.buckets.len());
            let old = std::mem::replace(&mut self.buckets, vec![None; size]);
            for old_name in old.into_iter().flatten() {
                let slot = self.slot(old_name);
                self.buckets[slot] = Some(old_name);
            }
        }
        let slot = self.slot(name);
        if self.buckets[slot].is_none() {
            self.buckets[slot] = Some(name);
            self.count += 1;
        }
    }

    fn slot(&self, name: &str) -> usize {
        let mut slot = usize::from(name_hash(name)) % self.buckets.len();
        while self.buckets[slot].is_some_and(|old| old != name) {
            slot = (slot + 1) % self.buckets.len();
        }
        slot
    }
}

fn name_hash(name: &str) -> u16 {
    let mut words = name.as_bytes().chunks(2).rev().map(|characters| {
        let low = u32::from(characters[0] | 0x20) | 0x2000;
        characters.get(1).map_or(low, |high| {
            low | (u32::from(*high | 0x20) << 16) | 0x2000_0000
        })
    });
    let Some(last) = words.next() else {
        return 0;
    };
    let mut hash = (name.len() as u32).wrapping_mul(2).wrapping_add(last);
    for word in words {
        hash = word.wrapping_add(hash << 4).wrapping_add(hash >> 7);
    }
    hash.wrapping_add(hash >> 16) as u16
}

fn validate_name(name: &str, kind: &str) -> Result<(), String> {
    if name.is_empty() || !name.is_ascii() || name.contains('\0') {
        Err(format!(
            "{kind} name must be nonempty ASCII without NUL bytes"
        ))
    } else {
        Ok(())
    }
}

fn push_word(bytes: &mut Vec<u8>, word: u32) {
    bytes.extend_from_slice(&word.to_le_bytes());
}

fn read_word(bytes: &[u8], offset: u32) -> Result<u32, String> {
    let offset = offset as usize;
    let bytes = bytes
        .get(offset..offset + 4)
        .ok_or_else(|| "object link is outside its segment".to_owned())?;
    Ok(u32::from_le_bytes(bytes.try_into().unwrap()))
}

fn write_word(bytes: &mut [u8], offset: u32, word: u32) -> Result<(), String> {
    let offset = offset as usize;
    let destination = bytes
        .get_mut(offset..offset + 4)
        .ok_or_else(|| "object link is outside its segment".to_owned())?;
    destination.copy_from_slice(&word.to_le_bytes());
    Ok(())
}

fn align(bytes: &mut Vec<u8>, alignment: usize) {
    bytes.resize((bytes.len() + alignment - 1) & !(alignment - 1), 0);
}

#[cfg(test)]
mod tests {
    use crate::codegen::generate_file;
    use crate::frontend::{FileContext, Frontend};
    use crate::goal_object::{Link, Location};
    use crate::parse::Parser;
    use crate::static_data::StaticFixup;
    use crate::type_system::{TypeSystem, Visibility};

    use super::*;

    fn empty_file(name: &str) -> FileContext {
        let mut parser = Parser::default();
        let parsed = parser.parse(name, "").unwrap();
        let mut types = TypeSystem::new();
        let none = types.intern_type_spec("none");
        Frontend::new(none)
            .compile_file(0, parsed.source_id, Visibility::new(0, []), &parsed.objects)
            .unwrap()
    }

    #[test]
    fn empty_gstring_header_matches_the_original_object() {
        let file = empty_file("gstring-h.gc");
        let functions = generate_file(&file).unwrap();
        let bytes = write_v3(
            "gstring-h",
            &functions,
            &file.static_data,
            &SymbolTable::default(),
            ObjectFileOptions::default(),
        )
        .unwrap();

        let mut expected = vec![0; 192];
        expected[4..8].copy_from_slice(&160_u32.to_le_bytes());
        expected[8..12].copy_from_slice(&(Segment::COUNT as u32).to_le_bytes());
        expected[12..16].copy_from_slice(&(Segment::COUNT as u32).to_le_bytes());
        expected[16..25].copy_from_slice(b"gstring-h");
        expected[80..84].copy_from_slice(&128_u32.to_le_bytes());
        expected[96..100].copy_from_slice(&130_u32.to_le_bytes());
        expected[112..116].copy_from_slice(&132_u32.to_le_bytes());
        expected[120..124].copy_from_slice(&24_u32.to_le_bytes());
        expected[128..146].copy_from_slice(&[
            0, 0, 0, 0, 0, 0x89, b'f', b'u', b'n', b'c', b't', b'i', b'o', b'n', 0, 0, 0, 0,
        ]);
        expected[160..184].copy_from_slice(&[
            0xff, 0xff, 0xff, 0xff, 0x25, 0x10, 0, 0, 8, 0, 0xe0, 3, 0x2d, 0xe8, 0xa0, 3, 0, 0, 0,
            0, 0, 0, 0, 0,
        ]);
        assert_eq!(bytes, expected);
    }

    #[test]
    fn historical_hash_matches_known_lisp_values() {
        assert_eq!(name_hash("function"), 59819);
        assert_eq!(name_hash("format"), 39012);
    }

    #[test]
    fn packs_static_data_and_pointer_links_after_code() {
        let mut file = empty_file("static.gc");
        file.static_data.main.append_zeros(8, 4).unwrap();
        let label = file.static_data.add_label(Segment::Main, 4).unwrap();
        file.static_data
            .add_fixup(StaticFixup {
                segment: Segment::Main,
                offset: 0,
                target: FixupTarget::Label(label),
                addend: 0,
                span: None,
            })
            .unwrap();

        let bytes = write_v3(
            "static",
            &generate_file(&file).unwrap(),
            &file.static_data,
            &SymbolTable::default(),
            ObjectFileOptions::default(),
        )
        .unwrap();
        let object = crate::goal_object::GoalObject::read(&bytes).unwrap();
        assert_eq!(
            object.segments[Segment::Main.index()],
            [4, 0, 0, 0, 0, 0, 0, 0]
        );
        assert_eq!(
            object
                .relocation(Location {
                    segment: Segment::Main,
                    offset: 0
                })
                .unwrap()
                .link,
            Link::Pointer {
                target: Location {
                    segment: Segment::Main,
                    offset: 4
                }
            }
        );
    }
}
