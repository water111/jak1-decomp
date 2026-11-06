use std::fs;
use std::path::{Path, PathBuf};

const HEADER_SIZE: usize = 64;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DgoEntry<'a> {
    pub name: &'a str,
    pub data: &'a [u8],
}

#[derive(Debug, Eq, PartialEq)]
pub struct Dgo<'a> {
    pub name: &'a str,
    pub entries: Vec<DgoEntry<'a>>,
}

pub fn read(bytes: &[u8]) -> Result<Dgo<'_>, String> {
    let header = bytes
        .get(..HEADER_SIZE)
        .ok_or_else(|| "truncated DGO header".to_owned())?;
    let entry_count = u32::from_le_bytes(header[..4].try_into().unwrap()) as usize;
    let name = header_name(&header[4..], "DGO")?;
    let mut offset = HEADER_SIZE;
    let mut entries = Vec::with_capacity(entry_count);
    for index in 0..entry_count {
        let header = bytes
            .get(offset..offset + HEADER_SIZE)
            .ok_or_else(|| format!("truncated header for DGO entry {index}"))?;
        let declared_size = u32::from_le_bytes(header[..4].try_into().unwrap()) as usize;
        let entry_name = header_name(&header[4..], "DGO entry")?;
        offset += HEADER_SIZE;
        let data = bytes
            .get(offset..offset + declared_size)
            .ok_or_else(|| format!("DGO entry `{entry_name}` extends beyond the archive"))?;
        entries.push(DgoEntry {
            name: entry_name,
            data,
        });
        offset = offset
            .checked_add(align16(declared_size))
            .ok_or_else(|| "DGO offset overflow".to_owned())?;
        if offset > bytes.len() {
            return Err(format!(
                "alignment after DGO entry `{entry_name}` extends beyond the archive"
            ));
        }
    }
    if offset != bytes.len() {
        return Err(format!(
            "DGO has {} unexpected trailing bytes",
            bytes.len() - offset
        ));
    }
    Ok(Dgo { name, entries })
}

pub fn unpack_file(input: &Path, output: &Path) -> Result<Vec<PathBuf>, String> {
    let bytes = fs::read(input)
        .map_err(|error| format!("could not read `{}`: {error}", input.display()))?;
    let dgo = read(&bytes)?;
    fs::create_dir_all(output)
        .map_err(|error| format!("could not create `{}`: {error}", output.display()))?;
    let mut paths = Vec::with_capacity(dgo.entries.len());
    for entry in dgo.entries {
        let name = object_file_name(entry.name)?;
        let path = output.join(name);
        if path.exists() {
            return Err(format!(
                "DGO contains duplicate output name `{}`",
                path.display()
            ));
        }
        fs::write(&path, entry.data)
            .map_err(|error| format!("could not write `{}`: {error}", path.display()))?;
        paths.push(path);
    }
    Ok(paths)
}

pub fn object_file_name(name: &str) -> Result<String, String> {
    if name.is_empty() || name == "." || name == ".." || name.contains(['/', '\\', '\0']) {
        return Err(format!("unsafe DGO entry name `{name}`"));
    }
    if name.to_ascii_lowercase().ends_with(".o") {
        Ok(name.to_owned())
    } else {
        Ok(format!("{name}.o"))
    }
}

fn header_name<'a>(bytes: &'a [u8], context: &str) -> Result<&'a str, String> {
    let end = bytes
        .iter()
        .position(|byte| *byte == 0)
        .ok_or_else(|| format!("unterminated {context} name"))?;
    if bytes[end + 1..].iter().any(|byte| *byte != 0) {
        return Err(format!("nonzero padding after {context} name"));
    }
    std::str::from_utf8(&bytes[..end]).map_err(|_| format!("invalid UTF-8 in {context} name"))
}

fn align16(value: usize) -> usize {
    value.saturating_add(15) & !15
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_archive_and_excludes_padding() {
        let mut bytes = vec![0; HEADER_SIZE];
        bytes[..4].copy_from_slice(&1_u32.to_le_bytes());
        bytes[4..10].copy_from_slice(b"TEST\0\0");
        bytes.extend([0; HEADER_SIZE]);
        bytes[64..68].copy_from_slice(&3_u32.to_le_bytes());
        bytes[68..72].copy_from_slice(b"foo\0");
        bytes.extend(b"abc");
        bytes.extend([0; 13]);
        let archive = read(&bytes).unwrap();
        assert_eq!(archive.name, "TEST");
        assert_eq!(
            archive.entries,
            vec![DgoEntry {
                name: "foo",
                data: b"abc"
            }]
        );
        assert_eq!(object_file_name("foo").unwrap(), "foo.o");
    }

    #[test]
    fn rejects_a_truncated_final_entry() {
        let mut bytes = vec![0; HEADER_SIZE];
        bytes[..4].copy_from_slice(&1_u32.to_le_bytes());
        bytes[4..10].copy_from_slice(b"TEST\0\0");
        bytes.extend([0; HEADER_SIZE]);
        bytes[64..68].copy_from_slice(&4_u32.to_le_bytes());
        bytes[68..72].copy_from_slice(b"foo\0");
        bytes.extend(b"abc");

        assert_eq!(
            read(&bytes).unwrap_err(),
            "DGO entry `foo` extends beyond the archive"
        );
    }
}
