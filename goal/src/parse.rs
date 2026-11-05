use std::fs;
use std::path::{Path, PathBuf};

use crate::diagnostic::Diagnostic;
use crate::object::{Object, ObjectStore};
use crate::span::{SourceId, SourceMap, SpanId, SpanInfo, SpanRange};

#[derive(Debug)]
pub struct ParseOutput {
    pub source_id: SourceId,
    pub objects: Vec<Object>,
}

#[derive(Debug, Default)]
pub struct Parser {
    pub objects: ObjectStore,
    pub sources: SourceMap,
}

impl Parser {
    pub fn parse(
        &mut self,
        path: impl Into<PathBuf>,
        text: impl Into<String>,
    ) -> Result<ParseOutput, Diagnostic> {
        let source_id = self
            .sources
            .add_source(path.into(), text.into())
            .map_err(Diagnostic::error)?;

        let (result, spans) = {
            let source_text = self
                .sources
                .source_text(source_id)
                .map_err(Diagnostic::error)?;
            let mut reader = Reader::new(source_text, source_id, &mut self.objects);
            let result = reader.read_all();
            (result, reader.spans)
        };
        self.sources
            .set_spans(source_id, spans)
            .map_err(Diagnostic::error)?;
        result.map(|objects| ParseOutput { source_id, objects })
    }

    pub fn parse_file(&mut self, path: impl AsRef<Path>) -> Result<ParseOutput, Diagnostic> {
        let path = path.as_ref();
        let text = fs::read_to_string(path)
            .map_err(|error| Diagnostic::error(format!("{}: {error}", path.display())))?;
        self.parse(path.to_path_buf(), text)
    }
}

struct Reader<'a> {
    input: &'a str,
    cursor: usize,
    source_id: SourceId,
    spans: Vec<SpanRange>,
    objects: &'a mut ObjectStore,
}

impl<'a> Reader<'a> {
    fn new(input: &'a str, source_id: SourceId, objects: &'a mut ObjectStore) -> Self {
        Self {
            input,
            cursor: 0,
            source_id,
            spans: Vec::new(),
            objects,
        }
    }

    fn read_all(&mut self) -> Result<Vec<Object>, Diagnostic> {
        let mut objects = Vec::new();
        loop {
            self.skip_ignored()?;
            if self.at_end() {
                return Ok(objects);
            }
            objects.push(self.read_object()?);
        }
    }

    fn read_object(&mut self) -> Result<Object, Diagnostic> {
        self.skip_ignored()?;
        if self.at_end() {
            return Err(self.error_at(
                self.cursor,
                self.cursor,
                "expected an object, found end of input",
            ));
        }

        let start = self.cursor;
        match self.peek_char().unwrap() {
            '(' => self.read_list(),
            ')' => {
                self.bump_char();
                Err(self.error_at(start, self.cursor, "unexpected `)`"))
            }
            '\'' => self.read_reader_macro("quote", 1),
            '`' => self.read_reader_macro("quasiquote", 1),
            ',' => {
                self.bump_char();
                let (name, prefix_end) = if self.consume_char('@') {
                    ("unquote-splicing", self.cursor)
                } else if self.consume_char('+') {
                    ("unquote-concat", self.cursor)
                } else {
                    ("unquote", self.cursor)
                };
                self.finish_reader_macro(name, start, prefix_end)
            }
            '"' => self.read_string(),
            '#' if self.remaining().starts_with("#(") => self.read_array(),
            '#' if self.remaining().starts_with("#\\") => self.read_character(),
            _ => self.read_atom(),
        }
    }

    fn read_list(&mut self) -> Result<Object, Diagnostic> {
        let start = self.cursor;
        self.bump_char();
        let mut items = Vec::new();

        loop {
            self.skip_ignored()?;
            if self.at_end() {
                return Err(self.error_at(start, self.cursor, "unterminated list"));
            }
            if self.consume_char(')') {
                let span = self.add_span(start, self.cursor)?;
                let tail = Object::empty_list(Some(span));
                return self
                    .objects
                    .list(items, tail, Some(span))
                    .map_err(|message| self.error_at(start, self.cursor, message));
            }
            if self.standalone_dot() {
                if items.is_empty() {
                    return Err(self.error_at(
                        self.cursor,
                        self.cursor + 1,
                        "a dotted list needs an item before `.`",
                    ));
                }
                self.bump_char();
                self.skip_ignored()?;
                if self.at_end() || self.peek_char() == Some(')') {
                    return Err(self.error_at(
                        self.cursor,
                        self.cursor,
                        "a dotted list needs an object after `.`",
                    ));
                }
                let tail = self.read_object()?;
                self.skip_ignored()?;
                if !self.consume_char(')') {
                    return Err(self.error_at(
                        self.cursor,
                        self.cursor,
                        "expected `)` after dotted-list tail",
                    ));
                }
                let span = self.add_span(start, self.cursor)?;
                return self
                    .objects
                    .list(items, tail, Some(span))
                    .map_err(|message| self.error_at(start, self.cursor, message));
            }
            items.push(self.read_object()?);
        }
    }

    fn read_array(&mut self) -> Result<Object, Diagnostic> {
        let start = self.cursor;
        self.cursor += 2;
        let mut items = Vec::new();
        loop {
            self.skip_ignored()?;
            if self.at_end() {
                return Err(self.error_at(start, self.cursor, "unterminated array"));
            }
            if self.consume_char(')') {
                let span = self.add_span(start, self.cursor)?;
                return Ok(Object::array(items, Some(span)));
            }
            items.push(self.read_object()?);
        }
    }

    fn read_reader_macro(
        &mut self,
        name: &'static str,
        prefix_length: usize,
    ) -> Result<Object, Diagnostic> {
        let start = self.cursor;
        self.cursor += prefix_length;
        self.finish_reader_macro(name, start, self.cursor)
    }

    fn finish_reader_macro(
        &mut self,
        name: &'static str,
        start: usize,
        prefix_end: usize,
    ) -> Result<Object, Diagnostic> {
        let value = self.read_object()?;
        let end = self.cursor;
        let operator_span = self.add_span(start, prefix_end)?;
        let operator = Object::symbol(self.objects.symbols.intern(name), Some(operator_span));
        let list_span = self.add_span(start, end)?;
        let tail = Object::empty_list(Some(list_span));
        self.objects
            .list(vec![operator, value], tail, Some(list_span))
            .map_err(|message| self.error_at(start, end, message))
    }

    fn read_string(&mut self) -> Result<Object, Diagnostic> {
        let start = self.cursor;
        self.bump_char();
        let mut value = String::new();
        loop {
            let Some(character) = self.peek_char() else {
                return Err(self.error_at(start, self.cursor, "unterminated string"));
            };
            self.bump_char();
            match character {
                '"' => {
                    let span = self.add_span(start, self.cursor)?;
                    return Ok(Object::string(value, Some(span)));
                }
                '\\' => {
                    let escape_start = self.cursor - 1;
                    let Some(escaped) = self.peek_char() else {
                        return Err(self.error_at(
                            escape_start,
                            self.cursor,
                            "unterminated string escape",
                        ));
                    };
                    self.bump_char();
                    value.push(match escaped {
                        '"' => '"',
                        '\\' => '\\',
                        'n' => '\n',
                        'r' => '\r',
                        't' => '\t',
                        '0' => '\0',
                        'b' => '\u{0008}',
                        'f' => '\u{000c}',
                        _ => {
                            return Err(self.error_at(
                                escape_start,
                                self.cursor,
                                format!("unknown string escape `\\{escaped}`"),
                            ));
                        }
                    });
                }
                _ => value.push(character),
            }
        }
    }

    fn read_character(&mut self) -> Result<Object, Diagnostic> {
        let start = self.cursor;
        self.cursor += 2;
        let Some(first) = self.peek_char() else {
            return Err(self.error_at(start, self.cursor, "incomplete character literal"));
        };

        let character = if first.is_ascii_alphabetic() {
            let name_start = self.cursor;
            self.read_token_end();
            let name = &self.input[name_start..self.cursor];
            match name {
                "space" => ' ',
                "newline" => '\n',
                "tab" => '\t',
                "return" => '\r',
                _ => {
                    let mut characters = name.chars();
                    match (characters.next(), characters.next()) {
                        (Some(character), None) => character,
                        _ => {
                            return Err(self.error_at(
                                start,
                                self.cursor,
                                format!("unknown character name `{name}`"),
                            ));
                        }
                    }
                }
            }
        } else {
            self.bump_char();
            first
        };
        let span = self.add_span(start, self.cursor)?;
        Ok(Object::integer(i64::from(u32::from(character)), Some(span)))
    }

    fn read_atom(&mut self) -> Result<Object, Diagnostic> {
        let start = self.cursor;
        self.read_token_end();
        if self.cursor == start {
            self.bump_char();
            return Err(self.error_at(start, self.cursor, "unexpected reader character"));
        }
        let token = &self.input[start..self.cursor];
        let span = self.add_span(start, self.cursor)?;

        if let Some(digits) = token
            .strip_prefix("#x")
            .or_else(|| token.strip_prefix("#X"))
        {
            return i64::from_str_radix(digits, 16)
                .map(|value| Object::integer(value, Some(span)))
                .map_err(|_| self.error_at(start, self.cursor, "invalid hexadecimal integer"));
        }
        if let Some(digits) = token
            .strip_prefix("#b")
            .or_else(|| token.strip_prefix("#B"))
        {
            return i64::from_str_radix(digits, 2)
                .map(|value| Object::integer(value, Some(span)))
                .map_err(|_| self.error_at(start, self.cursor, "invalid binary integer"));
        }
        if let Ok(value) = token.parse::<i64>() {
            return Ok(Object::integer(value, Some(span)));
        }
        let unsigned = token.strip_prefix(['+', '-']).unwrap_or(token);
        if !unsigned.starts_with('.')
            && token.bytes().any(|byte| byte.is_ascii_digit())
            && let Ok(value) = token.parse::<f64>()
        {
            return Ok(Object::float(value, Some(span)));
        }

        let symbol = self.objects.symbols.intern(token);
        Ok(Object::symbol(symbol, Some(span)))
    }

    fn skip_ignored(&mut self) -> Result<(), Diagnostic> {
        loop {
            while self.peek_char().is_some_and(char::is_whitespace) {
                self.bump_char();
            }
            if self.remaining().starts_with(';') {
                while self.peek_char().is_some_and(|character| character != '\n') {
                    self.bump_char();
                }
                continue;
            }
            if self.remaining().starts_with("#|") {
                let start = self.cursor;
                self.cursor += 2;
                let mut depth = 1_u32;
                while depth != 0 {
                    if self.at_end() {
                        return Err(self.error_at(
                            start,
                            self.cursor,
                            "unterminated block comment",
                        ));
                    }
                    if self.remaining().starts_with("#|") {
                        self.cursor += 2;
                        depth += 1;
                    } else if self.remaining().starts_with("|#") {
                        self.cursor += 2;
                        depth -= 1;
                    } else {
                        self.bump_char();
                    }
                }
                continue;
            }
            return Ok(());
        }
    }

    fn read_token_end(&mut self) {
        while let Some(character) = self.peek_char() {
            if is_delimiter(character) {
                break;
            }
            self.bump_char();
        }
    }

    fn standalone_dot(&self) -> bool {
        let mut characters = self.remaining().chars();
        characters.next() == Some('.') && characters.next().is_none_or(is_delimiter)
    }

    fn add_span(&mut self, start: usize, end: usize) -> Result<SpanInfo, Diagnostic> {
        let span_id = u32::try_from(self.spans.len())
            .map(SpanId)
            .map_err(|_| Diagnostic::error("exhausted span IDs"))?;
        self.spans.push(SpanRange {
            start: start as u32,
            end: end as u32,
        });
        Ok(SpanInfo {
            source_id: self.source_id,
            span_id,
        })
    }

    fn error_at(&mut self, start: usize, end: usize, message: impl Into<String>) -> Diagnostic {
        let mut diagnostic = Diagnostic::error(message);
        if let Ok(span) = self.add_span(start, end) {
            diagnostic.span = Some(span);
        }
        diagnostic
    }

    fn consume_char(&mut self, expected: char) -> bool {
        if self.peek_char() == Some(expected) {
            self.bump_char();
            true
        } else {
            false
        }
    }

    fn bump_char(&mut self) {
        self.cursor += self.peek_char().unwrap().len_utf8();
    }

    fn peek_char(&self) -> Option<char> {
        self.remaining().chars().next()
    }

    fn remaining(&self) -> &str {
        &self.input[self.cursor..]
    }

    fn at_end(&self) -> bool {
        self.cursor == self.input.len()
    }
}

fn is_delimiter(character: char) -> bool {
    character.is_whitespace() || matches!(character, '(' | ')' | '\'' | '`' | ',' | '"' | ';')
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::object::{ConsId, SymbolId, Value};

    fn symbol_name(parser: &Parser, object: &Object) -> String {
        let Value::Symbol(id) = object.value else {
            panic!("expected symbol, got {object:?}");
        };
        parser.objects.symbols.resolve(id).unwrap().to_owned()
    }

    fn list_elements<'a>(parser: &'a Parser, object: &'a Object) -> (Vec<&'a Object>, &'a Object) {
        let mut elements = Vec::new();
        let mut current = object;
        loop {
            match current.value {
                Value::Pair(id) => {
                    let cell = parser.objects.cons.get(id).unwrap();
                    elements.push(&cell.car);
                    current = &cell.cdr;
                }
                _ => return (elements, current),
            }
        }
    }

    #[test]
    fn reads_atoms_arrays_characters_and_strings() {
        let mut parser = Parser::default();
        let output = parser
            .parse(
                "atoms.gc",
                "#t #f 12 -9 1.5 #xff #b101 #\\a #\\space \"a\\n\\\"b\" #(1 two)",
            )
            .unwrap();

        assert_eq!(symbol_name(&parser, &output.objects[0]), "#t");
        assert_eq!(symbol_name(&parser, &output.objects[1]), "#f");
        assert_eq!(output.objects[2].value, Value::Integer(12));
        assert_eq!(output.objects[3].value, Value::Integer(-9));
        assert_eq!(output.objects[4].value, Value::Float(1.5));
        assert_eq!(output.objects[5].value, Value::Integer(255));
        assert_eq!(output.objects[6].value, Value::Integer(5));
        assert_eq!(output.objects[7].value, Value::Integer('a' as i64));
        assert_eq!(output.objects[8].value, Value::Integer(' ' as i64));
        assert_eq!(output.objects[9].value, Value::String("a\n\"b".to_owned()));
        let Value::Array(array) = &output.objects[10].value else {
            panic!("expected array");
        };
        assert_eq!(array.len(), 2);
        assert_eq!(array[0].value, Value::Integer(1));
        assert_eq!(symbol_name(&parser, &array[1]), "two");
    }

    #[test]
    fn reads_proper_and_dotted_lists() {
        let mut parser = Parser::default();
        let output = parser.parse("lists.gc", "(a b) (a b . c)").unwrap();

        let (proper, proper_tail) = list_elements(&parser, &output.objects[0]);
        assert_eq!(proper.len(), 2);
        assert_eq!(symbol_name(&parser, proper[0]), "a");
        assert_eq!(symbol_name(&parser, proper[1]), "b");
        assert_eq!(proper_tail.value, Value::EmptyList);

        let (dotted, dotted_tail) = list_elements(&parser, &output.objects[1]);
        assert_eq!(dotted.len(), 2);
        assert_eq!(symbol_name(&parser, dotted_tail), "c");
    }

    #[test]
    fn expands_reader_macros() {
        let mut parser = Parser::default();
        let output = parser.parse("macros.gc", "'x `x ,x ,@x ,+x").unwrap();
        let expected = [
            "quote",
            "quasiquote",
            "unquote",
            "unquote-splicing",
            "unquote-concat",
        ];

        for (object, expected_operator) in output.objects.iter().zip(expected) {
            let (items, tail) = list_elements(&parser, object);
            assert_eq!(items.len(), 2);
            assert_eq!(symbol_name(&parser, items[0]), expected_operator);
            assert_eq!(symbol_name(&parser, items[1]), "x");
            assert_eq!(tail.value, Value::EmptyList);
        }
    }

    #[test]
    fn skips_nested_and_line_comments() {
        let mut parser = Parser::default();
        let output = parser
            .parse(
                "comments.gc",
                "a #| outer #| inner |# still outer |# b ; line\nc",
            )
            .unwrap();
        let names: Vec<String> = output
            .objects
            .iter()
            .map(|object| symbol_name(&parser, object))
            .collect();
        assert_eq!(names, ["a", "b", "c"]);
    }

    #[test]
    fn interns_each_symbol_once_with_fnv_ids() {
        let mut parser = Parser::default();
        let output = parser.parse("symbols.gc", "same same").unwrap();
        let Value::Symbol(first) = output.objects[0].value else {
            panic!();
        };
        let Value::Symbol(second) = output.objects[1].value else {
            panic!();
        };
        assert_eq!(first, second);
        assert_eq!(first, SymbolId(crate::object::fnv64(b"same")));
        assert_eq!(parser.objects.symbols.len(), 1);
    }

    #[test]
    fn reports_spanned_errors() {
        let mut parser = Parser::default();
        let diagnostic = parser.parse("bad.gc", "(a\n b").unwrap_err();
        let rendered = parser.sources.render_diagnostic(&diagnostic).unwrap();
        assert!(rendered.contains("bad.gc:1:1"));
        assert!(rendered.contains("unterminated list"));
        assert!(rendered.contains("1 | (a"));
    }

    #[test]
    fn reparsing_invalidates_old_source_spans() {
        let mut parser = Parser::default();
        let first = parser.parse("same.gc", "first").unwrap();
        let old_span = first.objects[0].span.unwrap();
        let second = parser.parse("same.gc", "second").unwrap();

        assert_ne!(first.source_id, second.source_id);
        assert!(parser.sources.resolve_span(old_span).is_err());
        assert!(
            parser
                .sources
                .resolve_span(second.objects[0].span.unwrap())
                .is_ok()
        );
    }

    #[test]
    fn type_sizes_stay_compact() {
        assert_eq!(std::mem::size_of::<ConsId>(), 4);
        assert_eq!(std::mem::size_of::<SymbolId>(), 8);
        assert!(std::mem::size_of::<Object>() <= 40);
    }
}
