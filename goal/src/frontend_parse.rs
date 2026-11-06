use std::fmt;

use crate::diagnostic::Diagnostic;
use crate::frontend::ExpressionPosition;
use crate::object::{Object, ObjectKind, ObjectStore, SymbolId, Value as ObjectValue};
use crate::type_system::TypeSpec;
use crate::value::Value;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SyntaxType {
    Any,
    Object(ObjectKind),
    List,
    Number,
}

impl SyntaxType {
    fn accepts(self, object: &Object) -> bool {
        match self {
            Self::Any => true,
            Self::Object(expected) => object.kind() == expected,
            Self::List => matches!(&object.value, ObjectValue::Pair(_) | ObjectValue::EmptyList),
            Self::Number => {
                matches!(
                    &object.value,
                    ObjectValue::Integer(_) | ObjectValue::Float(_)
                )
            }
        }
    }
}

impl fmt::Display for SyntaxType {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Any => formatter.write_str("object"),
            Self::Object(kind) => kind.fmt(formatter),
            Self::List => formatter.write_str("list"),
            Self::Number => formatter.write_str("number"),
        }
    }
}

pub trait ArgumentCompiler {
    fn compile_expression(
        &mut self,
        object: Object,
        position: ExpressionPosition,
    ) -> Result<Value, Diagnostic>;

    fn typecheck(&self, expected: TypeSpec, actual: TypeSpec) -> Result<(), String>;

    fn symbol_name(&self, symbol: SymbolId) -> Option<&str>;
}

#[derive(Debug)]
pub struct ParsedArgument {
    form: SymbolId,
    name: &'static str,
    position: usize,
    object: Object,
}

impl ParsedArgument {
    pub fn object(&self) -> &Object {
        &self.object
    }

    pub fn into_object(self) -> Object {
        self.object
    }

    pub fn name(&self) -> &'static str {
        self.name
    }

    pub fn position(&self) -> usize {
        self.position
    }

    pub fn symbol(&self, objects: &ObjectStore) -> Result<SymbolId, Diagnostic> {
        match self.object.value {
            ObjectValue::Symbol(symbol) => Ok(symbol),
            _ => Err(self.error(
                objects,
                format!("must be a symbol, got {}", self.object.kind()),
            )),
        }
    }

    pub fn integer(&self, objects: &ObjectStore) -> Result<i64, Diagnostic> {
        match self.object.value {
            ObjectValue::Integer(value) => Ok(value),
            _ => Err(self.error(
                objects,
                format!("must be an integer, got {}", self.object.kind()),
            )),
        }
    }

    pub fn float(&self, objects: &ObjectStore) -> Result<f64, Diagnostic> {
        match self.object.value {
            ObjectValue::Float(value) => Ok(value),
            _ => Err(self.error(
                objects,
                format!("must be a float, got {}", self.object.kind()),
            )),
        }
    }

    pub fn string<'a>(&'a self, objects: &ObjectStore) -> Result<&'a str, Diagnostic> {
        match &self.object.value {
            ObjectValue::String(value) => Ok(value),
            _ => Err(self.error(
                objects,
                format!("must be a string, got {}", self.object.kind()),
            )),
        }
    }

    pub fn expect_syntax(
        self,
        objects: &ObjectStore,
        expected: SyntaxType,
    ) -> Result<Self, Diagnostic> {
        if expected.accepts(&self.object) {
            return Ok(self);
        }
        Err(self.error(
            objects,
            format!("must be {expected}, got {}", self.object.kind()),
        ))
    }

    pub fn list_cursor(self, objects: &ObjectStore) -> Result<ListCursor, Diagnostic> {
        let argument = self.expect_syntax(objects, SyntaxType::List)?;
        Ok(ListCursor::new(
            argument.form,
            argument.object.span,
            argument.object,
        ))
    }

    pub fn compile(
        self,
        compiler: &mut impl ArgumentCompiler,
        position: ExpressionPosition,
        expected: Option<TypeSpec>,
    ) -> Result<Value, Diagnostic> {
        let span = self.object.span;
        let form = compiler
            .symbol_name(self.form)
            .unwrap_or("<unknown-form>")
            .to_owned();
        let argument_name = self.name;
        let value = compiler
            .compile_expression(self.object, position)
            .map_err(|diagnostic| {
                diagnostic.with_context(
                    format!("while compiling argument `{argument_name}` to `{form}`"),
                    span,
                )
            })?;
        if let Some(expected) = expected
            && let Err(message) = compiler.typecheck(expected, value.ty)
        {
            let diagnostic = Diagnostic::error(format!(
                "argument `{argument_name}` to `{form}` has the wrong type: {message}"
            ));
            return Err(match span {
                Some(span) => diagnostic.with_span(span),
                None => diagnostic,
            });
        }
        Ok(value)
    }

    fn error(&self, objects: &ObjectStore, message: String) -> Diagnostic {
        let form = objects
            .symbols
            .resolve(self.form)
            .unwrap_or("<unknown-form>");
        let diagnostic =
            Diagnostic::error(format!("argument `{}` to `{form}` {message}", self.name));
        match self.object.span {
            Some(span) => diagnostic.with_span(span),
            None => diagnostic,
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub struct KeywordSpec {
    pub symbol: SymbolId,
    pub name: &'static str,
    pub required: bool,
    pub syntax: SyntaxType,
}

#[derive(Debug, Default)]
pub struct KeywordArguments {
    values: Vec<(SymbolId, ParsedArgument)>,
}

impl KeywordArguments {
    pub fn get(&self, keyword: SymbolId) -> Option<&ParsedArgument> {
        self.values
            .iter()
            .find(|(symbol, _)| *symbol == keyword)
            .map(|(_, argument)| argument)
    }

    pub fn take(&mut self, keyword: SymbolId) -> Option<ParsedArgument> {
        let index = self
            .values
            .iter()
            .position(|(symbol, _)| *symbol == keyword)?;
        Some(self.values.swap_remove(index).1)
    }

    pub fn is_empty(&self) -> bool {
        self.values.is_empty()
    }
}

#[derive(Debug)]
pub struct ListCursor {
    form: SymbolId,
    form_span: Option<crate::span::SpanInfo>,
    tail: Object,
    positional_index: usize,
}

impl ListCursor {
    pub fn new(form: SymbolId, form_span: Option<crate::span::SpanInfo>, rest: Object) -> Self {
        Self {
            form,
            form_span,
            tail: rest,
            positional_index: 0,
        }
    }

    pub fn required(
        &mut self,
        objects: &ObjectStore,
        name: &'static str,
    ) -> Result<ParsedArgument, Diagnostic> {
        self.next(objects, name)?.ok_or_else(|| {
            self.form_error(
                objects,
                format!(
                    "missing required argument `{name}` at position {}",
                    self.positional_index + 1
                ),
            )
        })
    }

    pub fn required_syntax(
        &mut self,
        objects: &ObjectStore,
        name: &'static str,
        syntax: SyntaxType,
    ) -> Result<ParsedArgument, Diagnostic> {
        self.required(objects, name)?.expect_syntax(objects, syntax)
    }

    pub fn optional(
        &mut self,
        objects: &ObjectStore,
        name: &'static str,
    ) -> Result<Option<ParsedArgument>, Diagnostic> {
        let Some(next) = self.peek(objects)? else {
            return Ok(None);
        };
        if is_keyword(objects, &next) {
            return Ok(None);
        }
        self.next(objects, name)
    }

    pub fn optional_syntax(
        &mut self,
        objects: &ObjectStore,
        name: &'static str,
        syntax: SyntaxType,
    ) -> Result<Option<ParsedArgument>, Diagnostic> {
        self.optional(objects, name)?
            .map(|argument| argument.expect_syntax(objects, syntax))
            .transpose()
    }

    pub fn remaining(
        mut self,
        objects: &ObjectStore,
        name: &'static str,
    ) -> Result<Vec<ParsedArgument>, Diagnostic> {
        let mut result = Vec::new();
        while let Some(argument) = self.next(objects, name)? {
            result.push(argument);
        }
        Ok(result)
    }

    pub fn remaining_body(self, objects: &ObjectStore) -> Result<Vec<ParsedArgument>, Diagnostic> {
        self.remaining(objects, "body")
    }

    pub fn keywords(
        mut self,
        objects: &ObjectStore,
        specs: &[KeywordSpec],
    ) -> Result<KeywordArguments, Diagnostic> {
        let mut result = KeywordArguments::default();
        while let Some(keyword) = self.next(objects, "keyword")? {
            let ObjectValue::Symbol(symbol) = &keyword.object.value else {
                return Err(keyword.error(objects, "must be a keyword symbol".to_owned()));
            };
            let symbol = *symbol;
            let Some(spec) = specs.iter().find(|spec| spec.symbol == symbol) else {
                return Err(keyword.error(objects, "is not a recognized keyword".to_owned()));
            };
            if result.get(symbol).is_some() {
                return Err(keyword.error(objects, "is specified more than once".to_owned()));
            }
            let value = self.required_syntax(objects, spec.name, spec.syntax)?;
            result.values.push((symbol, value));
        }
        for spec in specs {
            if spec.required && result.get(spec.symbol).is_none() {
                return Err(self.form_error(
                    objects,
                    format!("missing required keyword `:{}`", spec.name),
                ));
            }
        }
        Ok(result)
    }

    pub fn finish(self, objects: &ObjectStore) -> Result<(), Diagnostic> {
        match self.tail.value {
            ObjectValue::EmptyList => Ok(()),
            ObjectValue::Pair(_) => {
                Err(self.form_error(objects, "has too many arguments".to_owned()))
            }
            _ => Err(self.form_error(objects, "has an improper argument list".to_owned())),
        }
    }

    fn peek(&self, objects: &ObjectStore) -> Result<Option<Object>, Diagnostic> {
        match self.tail.value {
            ObjectValue::EmptyList => Ok(None),
            ObjectValue::Pair(id) => objects
                .cons
                .get(id)
                .map(|cell| Some(cell.car.clone()))
                .map_err(|message| self.internal_error(message)),
            _ => Err(self.form_error(objects, "has an improper argument list".to_owned())),
        }
    }

    fn next(
        &mut self,
        objects: &ObjectStore,
        name: &'static str,
    ) -> Result<Option<ParsedArgument>, Diagnostic> {
        let id = match &self.tail.value {
            ObjectValue::Pair(id) => *id,
            ObjectValue::EmptyList => return Ok(None),
            _ => {
                return Err(self.form_error(objects, "has an improper argument list".to_owned()));
            }
        };
        let cell = objects
            .cons
            .get(id)
            .map_err(|message| self.internal_error(message))?;
        let object = cell.car.clone();
        self.tail = cell.cdr.clone();
        let position = self.positional_index;
        self.positional_index += 1;
        Ok(Some(ParsedArgument {
            form: self.form,
            name,
            position,
            object,
        }))
    }

    fn form_error(&self, objects: &ObjectStore, message: String) -> Diagnostic {
        let form = objects
            .symbols
            .resolve(self.form)
            .unwrap_or("<unknown-form>");
        let diagnostic = Diagnostic::error(format!("form `{form}` {message}"));
        match self.form_span {
            Some(span) => diagnostic.with_span(span),
            None => diagnostic,
        }
    }

    fn internal_error(&self, message: String) -> Diagnostic {
        let diagnostic = Diagnostic::error(message);
        match self.form_span {
            Some(span) => diagnostic.with_span(span),
            None => diagnostic,
        }
    }
}

fn is_keyword(objects: &ObjectStore, object: &Object) -> bool {
    let ObjectValue::Symbol(symbol) = &object.value else {
        return false;
    };
    objects
        .symbols
        .resolve(*symbol)
        .is_some_and(|name| name.starts_with(':'))
}

#[cfg(test)]
mod tests {
    use crate::parse::Parser;
    use crate::value::ValueKind;

    use super::*;

    struct TestCompiler {
        form: SymbolId,
        integer: TypeSpec,
        string: TypeSpec,
    }

    impl ArgumentCompiler for TestCompiler {
        fn compile_expression(
            &mut self,
            object: Object,
            _: ExpressionPosition,
        ) -> Result<Value, Diagnostic> {
            match object.value {
                ObjectValue::Integer(value) => {
                    Ok(Value::new(self.integer, ValueKind::Integer(value as i128)))
                }
                _ => Ok(Value::new(self.string, ValueKind::None)),
            }
        }

        fn typecheck(&self, expected: TypeSpec, actual: TypeSpec) -> Result<(), String> {
            if expected == actual {
                Ok(())
            } else {
                Err("expected `string`, got `integer`".to_owned())
            }
        }

        fn symbol_name(&self, symbol: SymbolId) -> Option<&str> {
            (symbol == self.form).then_some("demo")
        }
    }

    fn form_parts(parser: &Parser, form: &Object) -> (SymbolId, Object) {
        let ObjectValue::Pair(id) = form.value else {
            panic!("expected form");
        };
        let cell = parser.objects.cons.get(id).unwrap();
        let ObjectValue::Symbol(head) = cell.car.value else {
            panic!("expected form name");
        };
        (head, cell.cdr.clone())
    }

    #[test]
    fn cursor_checks_syntax_positionals_and_keywords() {
        let mut parser = Parser::default();
        let parsed = parser.parse("test.gc", "(demo name 12 :count 3)").unwrap();
        let form = &parsed.objects[0];
        let (name, rest) = form_parts(&parser, form);
        let count = parser.objects.symbols.intern(":count");
        let mut cursor = ListCursor::new(name, form.span, rest);

        let symbol = cursor
            .required_syntax(
                &parser.objects,
                "name",
                SyntaxType::Object(ObjectKind::Symbol),
            )
            .unwrap();
        assert_eq!(
            parser
                .objects
                .symbols
                .resolve(symbol.symbol(&parser.objects).unwrap()),
            Some("name")
        );
        let number = cursor
            .required_syntax(&parser.objects, "value", SyntaxType::Number)
            .unwrap();
        assert_eq!(number.integer(&parser.objects).unwrap(), 12);
        assert!(
            cursor
                .optional(&parser.objects, "optional")
                .unwrap()
                .is_none()
        );

        let mut keywords = cursor
            .keywords(
                &parser.objects,
                &[KeywordSpec {
                    symbol: count,
                    name: "count",
                    required: true,
                    syntax: SyntaxType::Object(ObjectKind::Integer),
                }],
            )
            .unwrap();
        assert_eq!(
            keywords
                .take(count)
                .unwrap()
                .integer(&parser.objects)
                .unwrap(),
            3
        );
    }

    #[test]
    fn compiled_arguments_add_name_and_type_context() {
        let mut parser = Parser::default();
        let parsed = parser.parse("test.gc", "(demo 12)").unwrap();
        let form = &parsed.objects[0];
        let (name, rest) = form_parts(&parser, form);
        let mut cursor = ListCursor::new(name, form.span, rest);
        let argument = cursor.required(&parser.objects, "value").unwrap();

        let mut types = crate::type_system::TypeSystem::new();
        let integer = types.intern_type_spec("integer");
        let string = types.intern_type_spec("string");
        let mut compiler = TestCompiler {
            form: name,
            integer,
            string,
        };
        let error = argument
            .compile(&mut compiler, ExpressionPosition::Value, Some(string))
            .unwrap_err();
        assert!(error.message.contains("argument `value` to `demo`"));
        assert!(error.message.contains("expected `string`, got `integer`"));
        assert!(error.span.is_some());
    }
}
