use std::collections::HashMap;
use std::num::NonZeroU32;
use std::path::{Path, PathBuf};

use crate::diagnostic::{Diagnostic, DiagnosticLevel};

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct SourceId(NonZeroU32);

impl SourceId {
    fn from_index(index: usize) -> Result<Self, String> {
        let raw = u32::try_from(index)
            .ok()
            .and_then(|index| index.checked_add(1))
            .and_then(NonZeroU32::new)
            .ok_or_else(|| "exhausted source IDs".to_owned())?;
        Ok(Self(raw))
    }

    pub fn index(self) -> u32 {
        self.0.get() - 1
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct SpanId(pub u32);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SpanInfo {
    pub source_id: SourceId,
    pub span_id: SpanId,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SpanRange {
    pub start: u32,
    pub end: u32,
}

#[derive(Debug)]
struct SourceFile {
    path: PathBuf,
    text: String,
    line_offsets: Vec<u32>,
    spans: Vec<SpanRange>,
}

#[derive(Debug)]
pub struct ResolvedSpan<'a> {
    pub path: &'a Path,
    pub text: &'a str,
    pub range: SpanRange,
    pub line: u32,
    pub column: u32,
}

#[derive(Debug, Default)]
pub struct SourceMap {
    sources: Vec<Option<SourceFile>>,
    current_by_path: HashMap<PathBuf, SourceId>,
}

impl SourceMap {
    pub fn add_source(&mut self, path: PathBuf, text: String) -> Result<SourceId, String> {
        if text.len() > u32::MAX as usize {
            return Err(format!("source file `{}` is too large", path.display()));
        }
        let source_id = SourceId::from_index(self.sources.len())?;

        let mut line_offsets = Vec::with_capacity(text.len() / 32 + 1);
        line_offsets.push(0);
        for (index, byte) in text.bytes().enumerate() {
            if byte == b'\n' {
                line_offsets.push((index + 1) as u32);
            }
        }

        if let Some(old_id) = self.current_by_path.insert(path.clone(), source_id) {
            self.sources[old_id.index() as usize] = None;
        }
        self.sources.push(Some(SourceFile {
            path,
            text,
            line_offsets,
            spans: Vec::new(),
        }));
        Ok(source_id)
    }

    pub fn source_text(&self, source_id: SourceId) -> Result<&str, String> {
        Ok(&self.source(source_id)?.text)
    }

    pub fn set_spans(&mut self, source_id: SourceId, spans: Vec<SpanRange>) -> Result<(), String> {
        self.source_mut(source_id)?.spans = spans;
        Ok(())
    }

    pub fn current_source(&self, path: &Path) -> Option<SourceId> {
        self.current_by_path.get(path).copied()
    }

    pub fn resolve_span(&self, span: SpanInfo) -> Result<ResolvedSpan<'_>, String> {
        let source = self.source(span.source_id)?;
        let range = *source.spans.get(span.span_id.0 as usize).ok_or_else(|| {
            format!(
                "internal compiler error: invalid span {} for source {}",
                span.span_id.0,
                span.source_id.index()
            )
        })?;
        if range.start > range.end || range.end as usize > source.text.len() {
            return Err(format!(
                "internal compiler error: corrupt span {} for source {}",
                span.span_id.0,
                span.source_id.index()
            ));
        }

        let line_index = source
            .line_offsets
            .partition_point(|offset| *offset <= range.start)
            - 1;
        let line_start = source.line_offsets[line_index] as usize;
        let column = source.text[line_start..range.start as usize]
            .chars()
            .count() as u32
            + 1;
        Ok(ResolvedSpan {
            path: &source.path,
            text: &source.text,
            range,
            line: line_index as u32 + 1,
            column,
        })
    }

    pub fn render_diagnostic(&self, diagnostic: &Diagnostic) -> Result<String, String> {
        let level = match diagnostic.level {
            DiagnosticLevel::Warning => "warning",
            DiagnosticLevel::Error => "error",
        };
        let Some(span) = diagnostic.span else {
            return Ok(format!("{level}: {}", diagnostic.message));
        };
        let resolved = self.resolve_span(span)?;
        let start = resolved.range.start as usize;
        let line_start = resolved.text[..start]
            .rfind('\n')
            .map_or(0, |index| index + 1);
        let mut line_end = resolved.text[start..]
            .find('\n')
            .map_or(resolved.text.len(), |index| start + index);
        if line_end > line_start && resolved.text.as_bytes()[line_end - 1] == b'\r' {
            line_end -= 1;
        }
        let shown_line = &resolved.text[line_start..line_end];
        let highlight_end = (resolved.range.end as usize).min(line_end).max(start);
        let highlight_width = resolved.text[start..highlight_end].chars().count().max(1);
        let gutter_width = resolved.line.to_string().len();
        let padding = " ".repeat((resolved.column - 1) as usize);
        let carets = "^".repeat(highlight_width);

        Ok(format!(
            "{level}: {}\n --> {}:{}:{}\n{space:>width$} |\n{} | {shown_line}\n{space:>width$} | {padding}{carets}",
            diagnostic.message,
            resolved.path.display(),
            resolved.line,
            resolved.column,
            resolved.line,
            space = "",
            width = gutter_width,
        ))
    }

    fn source(&self, source_id: SourceId) -> Result<&SourceFile, String> {
        self.sources
            .get(source_id.index() as usize)
            .and_then(Option::as_ref)
            .ok_or_else(|| {
                format!(
                    "internal compiler error: source {} is stale or invalid",
                    source_id.index()
                )
            })
    }

    fn source_mut(&mut self, source_id: SourceId) -> Result<&mut SourceFile, String> {
        self.sources
            .get_mut(source_id.index() as usize)
            .and_then(Option::as_mut)
            .ok_or_else(|| {
                format!(
                    "internal compiler error: source {} is stale or invalid",
                    source_id.index()
                )
            })
    }
}
