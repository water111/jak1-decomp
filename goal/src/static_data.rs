use crate::ir::FunctionId;
use crate::object::SymbolId;
use crate::span::SpanInfo;

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
#[repr(u8)]
pub enum Segment {
    Main = 0,
    Debug = 1,
    TopLevel = 2,
}

impl Segment {
    pub const COUNT: usize = 3;
    pub const ALL: [Self; Self::COUNT] = [Self::Main, Self::Debug, Self::TopLevel];
    pub const DISPLAY_ORDER: [Self; Self::COUNT] = [Self::TopLevel, Self::Debug, Self::Main];

    pub const fn index(self) -> usize {
        self as usize
    }

    pub const fn from_index(index: u8) -> Option<Self> {
        match index {
            0 => Some(Self::Main),
            1 => Some(Self::Debug),
            2 => Some(Self::TopLevel),
            _ => None,
        }
    }

    pub const fn name(self) -> &'static str {
        match self {
            Self::Main => "main",
            Self::Debug => "debug",
            Self::TopLevel => "top-level",
        }
    }
}

impl std::fmt::Display for Segment {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(self.name())
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct StaticLabelId(pub u32);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct StaticLabel {
    pub segment: Segment,
    pub offset: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FixupTarget {
    Label(StaticLabelId),
    Function(FunctionId),
    Symbol(SymbolId),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct StaticFixup {
    pub segment: Segment,
    pub offset: u32,
    pub target: FixupTarget,
    pub addend: i32,
    pub span: Option<SpanInfo>,
}

#[derive(Debug, Eq, PartialEq)]
pub struct StaticSection {
    bytes: Vec<u8>,
    alignment: u32,
}

impl Default for StaticSection {
    fn default() -> Self {
        Self {
            bytes: Vec::new(),
            alignment: 1,
        }
    }
}

impl StaticSection {
    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }

    pub fn len(&self) -> usize {
        self.bytes.len()
    }

    pub fn is_empty(&self) -> bool {
        self.bytes.is_empty()
    }

    pub fn alignment(&self) -> u32 {
        self.alignment
    }

    pub fn align(&mut self, alignment: u32) -> Result<u32, String> {
        if !alignment.is_power_of_two() {
            return Err("static-data alignment must be a nonzero power of two".to_owned());
        }
        self.alignment = self.alignment.max(alignment);
        let aligned = self
            .bytes
            .len()
            .checked_add(alignment as usize - 1)
            .map(|offset| offset & !(alignment as usize - 1))
            .ok_or_else(|| "static-data offset overflow".to_owned())?;
        if aligned > u32::MAX as usize {
            return Err("static-data section exceeds u32".to_owned());
        }
        self.bytes.resize(aligned, 0);
        Ok(aligned as u32)
    }

    pub fn append(&mut self, bytes: &[u8], alignment: u32) -> Result<u32, String> {
        let offset = self.align(alignment)?;
        let new_len = self
            .bytes
            .len()
            .checked_add(bytes.len())
            .ok_or_else(|| "static-data section size overflow".to_owned())?;
        if new_len > u32::MAX as usize {
            return Err("static-data section exceeds u32".to_owned());
        }
        self.bytes.extend_from_slice(bytes);
        Ok(offset)
    }

    pub fn append_zeros(&mut self, size: u32, alignment: u32) -> Result<u32, String> {
        let offset = self.align(alignment)?;
        let new_len = self
            .bytes
            .len()
            .checked_add(size as usize)
            .ok_or_else(|| "static-data section size overflow".to_owned())?;
        if new_len > u32::MAX as usize {
            return Err("static-data section exceeds u32".to_owned());
        }
        self.bytes.resize(new_len, 0);
        Ok(offset)
    }
}

#[derive(Debug, Default)]
pub struct StaticData {
    pub main: StaticSection,
    pub debug: StaticSection,
    pub top_level: StaticSection,
    pub labels: Vec<StaticLabel>,
    pub fixups: Vec<StaticFixup>,
}

impl StaticData {
    pub fn section(&self, segment: Segment) -> &StaticSection {
        match segment {
            Segment::Main => &self.main,
            Segment::Debug => &self.debug,
            Segment::TopLevel => &self.top_level,
        }
    }

    pub fn section_mut(&mut self, segment: Segment) -> &mut StaticSection {
        match segment {
            Segment::Main => &mut self.main,
            Segment::Debug => &mut self.debug,
            Segment::TopLevel => &mut self.top_level,
        }
    }

    pub fn add_label(&mut self, segment: Segment, offset: u32) -> Result<StaticLabelId, String> {
        if offset as usize > self.section(segment).len() {
            return Err("static label is outside its section".to_owned());
        }
        let id = StaticLabelId(
            u32::try_from(self.labels.len()).map_err(|_| "exhausted static label IDs")?,
        );
        self.labels.push(StaticLabel { segment, offset });
        Ok(id)
    }

    pub fn add_fixup(&mut self, fixup: StaticFixup) -> Result<(), String> {
        let end = fixup
            .offset
            .checked_add(4)
            .ok_or_else(|| "static fixup offset overflow".to_owned())?;
        if end as usize > self.section(fixup.segment).len() {
            return Err("static fixup is outside its section".to_owned());
        }
        self.fixups.push(fixup);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn appends_aligned_bytes_and_records_compact_metadata() {
        let mut data = StaticData::default();
        assert_eq!(data.main.append(&[1, 2, 3], 1).unwrap(), 0);
        assert_eq!(data.main.append(&[4, 5, 6, 7], 4).unwrap(), 4);
        assert_eq!(data.main.bytes(), &[1, 2, 3, 0, 4, 5, 6, 7]);

        let label = data.add_label(Segment::Main, 4).unwrap();
        data.add_fixup(StaticFixup {
            segment: Segment::Main,
            offset: 4,
            target: FixupTarget::Label(label),
            addend: 0,
            span: None,
        })
        .unwrap();
        assert_eq!(data.labels[label.0 as usize].offset, 4);
        assert_eq!(data.fixups.len(), 1);
    }
}
