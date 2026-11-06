use crate::diagnostic::Diagnostic;
use crate::object::{EnvironmentId, Object, StringHashTableId, Value};
use std::collections::HashSet;

use super::Interpreter;
use super::eval::ArgumentMode;

pub(super) type BuiltinForm =
    fn(&mut Interpreter, &Object, Vec<Object>, EnvironmentId) -> Result<Object, Diagnostic>;

pub(super) const BUILTIN_FORMS: &[(&str, BuiltinForm)] = &[
    ("+", Interpreter::builtin_add),
    ("-", Interpreter::builtin_subtract),
    ("*", Interpreter::builtin_multiply),
    ("/", Interpreter::builtin_divide),
    ("=", Interpreter::builtin_numeric_equal),
    ("<", Interpreter::builtin_less),
    (">", Interpreter::builtin_greater),
    ("<=", Interpreter::builtin_less_equal),
    (">=", Interpreter::builtin_greater_equal),
    ("eq?", Interpreter::builtin_equal),
    ("cons", Interpreter::builtin_cons),
    ("car", Interpreter::builtin_car),
    ("cdr", Interpreter::builtin_cdr),
    ("set-car!", Interpreter::builtin_set_car),
    ("set-cdr!", Interpreter::builtin_set_cdr),
    ("null?", Interpreter::builtin_null),
    ("type?", Interpreter::builtin_type),
    ("gensym", Interpreter::builtin_gensym),
    ("eval", Interpreter::builtin_eval),
    ("apply", Interpreter::builtin_apply),
    ("print", Interpreter::builtin_print),
    ("inspect", Interpreter::builtin_print),
    ("read", Interpreter::builtin_read),
    ("load-file", Interpreter::builtin_load_file),
    ("fmt", Interpreter::builtin_format),
    ("error", Interpreter::builtin_error),
    ("string-ref", Interpreter::builtin_string_ref),
    ("string-length", Interpreter::builtin_string_length),
    ("string-append", Interpreter::builtin_string_append),
    (
        "string-starts-with?",
        Interpreter::builtin_string_starts_with,
    ),
    ("string-ends-with?", Interpreter::builtin_string_ends_with),
    ("string-split", Interpreter::builtin_string_split),
    ("string-substr", Interpreter::builtin_string_substring),
    ("ash", Interpreter::builtin_arithmetic_shift),
    ("symbol->string", Interpreter::builtin_symbol_to_string),
    ("string->symbol", Interpreter::builtin_string_to_symbol),
    ("int->string", Interpreter::builtin_integer_to_string),
    (
        "make-string-hash-table",
        Interpreter::builtin_make_string_hash_table,
    ),
    ("hash-table-set!", Interpreter::builtin_hash_table_set),
    (
        "hash-table-try-ref",
        Interpreter::builtin_hash_table_try_ref,
    ),
    ("array-length", Interpreter::builtin_array_length),
    ("array-ref", Interpreter::builtin_array_ref),
    ("exit", Interpreter::builtin_exit),
];

impl Interpreter {
    fn builtin_add(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.numeric_fold(form, &arguments, NumericOperation::Add)
    }

    fn builtin_subtract(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.numeric_fold(form, &arguments, NumericOperation::Subtract)
    }

    fn builtin_multiply(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.numeric_fold(form, &arguments, NumericOperation::Multiply)
    }

    fn builtin_divide(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.numeric_fold(form, &arguments, NumericOperation::Divide)
    }

    fn builtin_numeric_equal(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.numeric_compare(form, &arguments, |left, right| left == right)
    }

    fn builtin_less(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.numeric_compare(form, &arguments, |left, right| left < right)
    }

    fn builtin_greater(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.numeric_compare(form, &arguments, |left, right| left > right)
    }

    fn builtin_less_equal(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.numeric_compare(form, &arguments, |left, right| left <= right)
    }

    fn builtin_greater_equal(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.numeric_compare(form, &arguments, |left, right| left >= right)
    }

    fn builtin_equal(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 2)?;
        let mut visited = HashSet::new();
        Ok(self.boolean(self.objects_equal(&arguments[0], &arguments[1], &mut visited)?))
    }

    fn builtin_cons(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 2)?;
        self.parser
            .objects
            .cons(arguments[0].clone(), arguments[1].clone(), form.span)
            .map_err(Interpreter::internal_error)
    }

    fn builtin_car(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.builtin_pair_part(form, &arguments, true)
    }

    fn builtin_cdr(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.builtin_pair_part(form, &arguments, false)
    }

    fn builtin_pair_part(
        &mut self,
        form: &Object,
        arguments: &[Object],
        car: bool,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, arguments, 1)?;
        let Value::Pair(id) = arguments[0].value else {
            return Err(self.error(&arguments[0], "expected a pair"));
        };
        let cell = self.pair(id)?;
        Ok(if car { cell.car } else { cell.cdr })
    }

    fn builtin_set_car(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.builtin_set_pair_part(form, &arguments, true)
    }

    fn builtin_set_cdr(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.builtin_set_pair_part(form, &arguments, false)
    }

    fn builtin_set_pair_part(
        &mut self,
        form: &Object,
        arguments: &[Object],
        car: bool,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, arguments, 2)?;
        let Value::Pair(id) = arguments[0].value else {
            return Err(self.error(&arguments[0], "expected a pair"));
        };
        let cell = self
            .parser
            .objects
            .cons
            .get_mut(id)
            .map_err(Interpreter::internal_error)?;
        if car {
            cell.car = arguments[1].clone();
        } else {
            cell.cdr = arguments[1].clone();
        }
        Ok(arguments[0].clone())
    }

    fn builtin_null(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 1)?;
        Ok(self.boolean(arguments[0].value == Value::EmptyList))
    }

    fn builtin_eval(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        environment: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 1)?;
        let expanded = self.expand_goos_macros(arguments[0].clone(), environment)?;
        self.eval_expanded(expanded, environment)
    }

    fn builtin_print(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 1)?;
        println!("{}", self.format_object(&arguments[0])?);
        Ok(Object::empty_list(None))
    }

    fn builtin_load_file(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 1)?;
        let path = self.expect_string(&arguments[0])?.to_owned();
        self.load_file(path)
    }

    fn builtin_error(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 1)?;
        let message = match &arguments[0].value {
            Value::String(message) => message.clone(),
            _ => self.format_object(&arguments[0])?,
        };
        Err(self.error(form, message))
    }

    fn builtin_string_ref(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 2)?;
        let string = self.expect_string(&arguments[0])?;
        let index = self.expect_nonnegative_index(&arguments[1])?;
        let character = string
            .chars()
            .nth(index)
            .ok_or_else(|| self.error(form, "string index is out of bounds"))?;
        Ok(Object::integer(i64::from(u32::from(character)), form.span))
    }

    fn builtin_string_length(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 1)?;
        Ok(Object::integer(
            self.expect_string(&arguments[0])?.chars().count() as i64,
            form.span,
        ))
    }

    fn builtin_string_append(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        let mut output = String::new();
        for argument in &arguments {
            output.push_str(self.expect_string(argument)?);
        }
        Ok(Object::string(output, form.span))
    }

    fn builtin_string_starts_with(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.builtin_string_boundary(form, &arguments, true)
    }

    fn builtin_string_ends_with(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.builtin_string_boundary(form, &arguments, false)
    }

    fn builtin_string_boundary(
        &mut self,
        form: &Object,
        arguments: &[Object],
        starts_with: bool,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, arguments, 2)?;
        let value = self.expect_string(&arguments[0])?;
        let pattern = self.expect_string(&arguments[1])?;
        Ok(self.boolean(if starts_with {
            value.starts_with(pattern)
        } else {
            value.ends_with(pattern)
        }))
    }

    fn builtin_string_split(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 2)?;
        let value = self.expect_string(&arguments[0])?.to_owned();
        let separator = self.expect_string(&arguments[1])?.to_owned();
        let parts = value
            .split(&separator)
            .map(|part| Object::string(part.to_owned(), None))
            .collect();
        self.build_proper_list(parts, form.span)
    }

    fn builtin_arithmetic_shift(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 2)?;
        let value = self.expect_integer(&arguments[0])?;
        let amount = self.expect_integer(&arguments[1])?;
        let result = if amount >= 0 {
            value.checked_shl(amount as u32)
        } else {
            value.checked_shr(amount.unsigned_abs() as u32)
        }
        .ok_or_else(|| self.error(form, "invalid arithmetic shift"))?;
        Ok(Object::integer(result, form.span))
    }

    fn builtin_symbol_to_string(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 1)?;
        let Value::Symbol(symbol) = arguments[0].value else {
            return Err(self.error(&arguments[0], "expected a symbol"));
        };
        Ok(Object::string(
            self.symbol_name(symbol)?.to_owned(),
            form.span,
        ))
    }

    fn builtin_string_to_symbol(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 1)?;
        let name = self.expect_string(&arguments[0])?.to_owned();
        let symbol = self.intern(&name);
        Ok(Object::symbol(symbol, form.span))
    }

    fn builtin_integer_to_string(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 1)?;
        Ok(Object::string(
            self.expect_integer(&arguments[0])?.to_string(),
            form.span,
        ))
    }

    fn builtin_make_string_hash_table(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 0)?;
        let id = self
            .hash_tables
            .allocate(Default::default())
            .map(StringHashTableId)
            .map_err(Interpreter::internal_error)?;
        Ok(Object::string_hash_table(id, form.span))
    }

    fn builtin_array_length(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 1)?;
        let Value::Array(items) = &arguments[0].value else {
            return Err(self.error(&arguments[0], "expected an array"));
        };
        Ok(Object::integer(items.len() as i64, form.span))
    }

    fn builtin_array_ref(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 2)?;
        let Value::Array(items) = &arguments[0].value else {
            return Err(self.error(&arguments[0], "expected an array"));
        };
        let index = self.expect_nonnegative_index(&arguments[1])?;
        items
            .get(index)
            .cloned()
            .ok_or_else(|| self.error(form, "array index is out of bounds"))
    }

    fn builtin_exit(
        &mut self,
        _: &Object,
        _: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        Ok(Object::empty_list(None))
    }

    fn numeric_fold(
        &self,
        form: &Object,
        arguments: &[Object],
        operation: NumericOperation,
    ) -> Result<Object, Diagnostic> {
        if arguments.is_empty() {
            return match operation {
                NumericOperation::Add => Ok(Object::integer(0, form.span)),
                NumericOperation::Multiply => Ok(Object::integer(1, form.span)),
                _ => Err(self.error(form, "numeric operation needs an argument")),
            };
        }
        let float_mode = arguments
            .iter()
            .any(|argument| matches!(argument.value, Value::Float(_)));
        if float_mode {
            let mut values = arguments.iter().map(|value| self.number(value));
            let first = values.next().transpose()?.unwrap();
            let result = match operation {
                NumericOperation::Add => values.try_fold(first, |a, b| Ok(a + b?))?,
                NumericOperation::Multiply => values.try_fold(first, |a, b| Ok(a * b?))?,
                NumericOperation::Subtract if arguments.len() == 1 => -first,
                NumericOperation::Subtract => values.try_fold(first, |a, b| Ok(a - b?))?,
                NumericOperation::Divide if arguments.len() == 1 => 1.0 / first,
                NumericOperation::Divide => values.try_fold(first, |a, b| Ok(a / b?))?,
            };
            Ok(Object::float(result, form.span))
        } else {
            let mut values = arguments.iter().map(|value| self.expect_integer(value));
            let first = values.next().transpose()?.unwrap();
            let result = match operation {
                NumericOperation::Add => values.try_fold(first, |a, b| {
                    a.checked_add(b?)
                        .ok_or_else(|| self.error(form, "integer overflow"))
                })?,
                NumericOperation::Multiply => values.try_fold(first, |a, b| {
                    a.checked_mul(b?)
                        .ok_or_else(|| self.error(form, "integer overflow"))
                })?,
                NumericOperation::Subtract if arguments.len() == 1 => first
                    .checked_neg()
                    .ok_or_else(|| self.error(form, "integer overflow"))?,
                NumericOperation::Subtract => values.try_fold(first, |a, b| {
                    a.checked_sub(b?)
                        .ok_or_else(|| self.error(form, "integer overflow"))
                })?,
                NumericOperation::Divide if arguments.len() == 1 => {
                    if first == 0 {
                        return Err(self.error(form, "division by zero"));
                    }
                    1 / first
                }
                NumericOperation::Divide => values.try_fold(first, |a, b| {
                    let b = b?;
                    a.checked_div(b)
                        .ok_or_else(|| self.error(form, "division by zero or overflow"))
                })?,
            };
            Ok(Object::integer(result, form.span))
        }
    }

    fn numeric_compare(
        &self,
        form: &Object,
        arguments: &[Object],
        compare: impl Fn(f64, f64) -> bool,
    ) -> Result<Object, Diagnostic> {
        if arguments.len() < 2 {
            return Err(self.error(form, "numeric comparison needs at least two arguments"));
        }
        let mut previous = self.number(&arguments[0])?;
        for argument in &arguments[1..] {
            let current = self.number(argument)?;
            if !compare(previous, current) {
                return Ok(self.false_object());
            }
            previous = current;
        }
        Ok(self.true_object())
    }

    fn number(&self, object: &Object) -> Result<f64, Diagnostic> {
        match object.value {
            Value::Integer(value) => Ok(value as f64),
            Value::Float(value) => Ok(value),
            _ => Err(self.error(object, "expected a number")),
        }
    }

    fn expect_integer(&self, object: &Object) -> Result<i64, Diagnostic> {
        match object.value {
            Value::Integer(value) => Ok(value),
            _ => Err(self.error(object, "expected an integer")),
        }
    }

    fn expect_nonnegative_index(&self, object: &Object) -> Result<usize, Diagnostic> {
        let value = self.expect_integer(object)?;
        usize::try_from(value).map_err(|_| self.error(object, "index must be nonnegative"))
    }

    fn expect_string<'a>(&self, object: &'a Object) -> Result<&'a str, Diagnostic> {
        match &object.value {
            Value::String(value) => Ok(value),
            _ => Err(self.error(object, "expected a string")),
        }
    }

    fn builtin_type(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 2)?;
        let Value::Symbol(expected) = arguments[0].value else {
            return Err(self.error(&arguments[0], "type? expects a quoted type symbol"));
        };
        let actual = match arguments[1].value {
            Value::EmptyList => "empty-list",
            Value::Integer(_) => "integer",
            Value::Float(_) => "float",
            Value::Symbol(_) => "symbol",
            Value::String(_) => "string",
            Value::Pair(_) => "pair",
            Value::Array(_) => "array",
            Value::Lambda(_) => "lambda",
            Value::Macro(_) => "macro",
            Value::Environment(_) => "environment",
            Value::StringHashTable(_) => "string-hash-table",
        };
        Ok(self.boolean(self.symbol_name(expected)? == actual))
    }

    fn builtin_gensym(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        if arguments.len() > 1 {
            return Err(self.error(form, "gensym accepts at most one prefix"));
        }
        let prefix = if let Some(prefix) = arguments.first() {
            match &prefix.value {
                Value::String(value) => value.clone(),
                Value::Symbol(symbol) => self.symbol_name(*symbol)?.to_owned(),
                _ => return Err(self.error(prefix, "gensym prefix must be a string or symbol")),
            }
        } else {
            "g".to_owned()
        };
        let name = format!("{prefix}__{}", self.gensym_counter);
        self.gensym_counter += 1;
        let symbol = self.intern(&name);
        Ok(Object::symbol(symbol, form.span))
    }

    fn builtin_apply(
        &mut self,
        form: &Object,
        mut arguments: Vec<Object>,
        environment: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        if arguments.len() < 2 {
            return Err(self.error(form, "apply needs a callable and argument list"));
        }
        let callable = arguments.remove(0);
        let tail = arguments.pop().unwrap();
        arguments.extend(self.proper_list(&tail, "apply argument")?);
        match callable.value {
            Value::Lambda(id) => {
                self.call_procedure(id, form, &arguments, environment, ArgumentMode::Values)
            }
            _ => Err(self.error(&callable, "apply expected a callable")),
        }
    }

    fn builtin_read(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 1)?;
        let text = self.expect_string(&arguments[0])?.to_owned();
        let path = format!("<read:{}>", self.read_counter);
        self.read_counter += 1;
        let parsed = self.parser.parse(path, text)?;
        if parsed.objects.len() != 1 {
            return Err(self.error(form, "read expects exactly one object"));
        }
        Ok(parsed.objects.into_iter().next().unwrap())
    }

    fn builtin_format(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        if arguments.len() < 2 {
            return Err(self.error(form, "fmt needs a destination and format string"));
        }
        let pattern = self.expect_string(&arguments[1])?;
        let mut output = String::with_capacity(pattern.len());
        let mut values = arguments[2..].iter();
        let mut remaining = pattern;
        while let Some(index) = remaining.find("{}") {
            output.push_str(&remaining[..index]);
            let value = values
                .next()
                .ok_or_else(|| self.error(form, "not enough fmt arguments"))?;
            output.push_str(&self.object_fragment(value)?);
            remaining = &remaining[index + 2..];
        }
        output.push_str(remaining);
        if values.next().is_some() {
            return Err(self.error(form, "too many fmt arguments"));
        }
        if self.truthy(&arguments[0]) {
            print!("{output}");
            Ok(Object::empty_list(None))
        } else {
            Ok(Object::string(output, form.span))
        }
    }

    fn builtin_string_substring(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        if !(2..=3).contains(&arguments.len()) {
            return Err(self.error(
                form,
                "string-substr needs string, start, and optional length",
            ));
        }
        let characters: Vec<char> = self.expect_string(&arguments[0])?.chars().collect();
        let start = self.expect_nonnegative_index(&arguments[1])?;
        let end = if arguments.len() == 3 {
            start
                .checked_add(self.expect_nonnegative_index(&arguments[2])?)
                .ok_or_else(|| self.error(form, "substring range overflow"))?
        } else {
            characters.len()
        };
        if start > end || end > characters.len() {
            return Err(self.error(form, "substring range is out of bounds"));
        }
        Ok(Object::string(
            characters[start..end].iter().collect(),
            form.span,
        ))
    }

    fn hash_key(&self, object: &Object) -> Result<String, Diagnostic> {
        match &object.value {
            Value::String(value) => Ok(value.clone()),
            Value::Symbol(symbol) => Ok(self.symbol_name(*symbol)?.to_owned()),
            _ => Err(self.error(object, "hash table key must be a string or symbol")),
        }
    }

    fn builtin_hash_table_set(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 3)?;
        let Value::StringHashTable(id) = arguments[0].value else {
            return Err(self.error(&arguments[0], "expected a string hash table"));
        };
        let key = self.hash_key(&arguments[1])?;
        self.hash_tables
            .get_mut(id.0, "string hash table")
            .map_err(Self::internal_error)?
            .insert(key, arguments[2].clone());
        Ok(Object::empty_list(None))
    }

    fn builtin_hash_table_try_ref(
        &mut self,
        form: &Object,
        arguments: Vec<Object>,
        _: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, &arguments, 2)?;
        let Value::StringHashTable(id) = arguments[0].value else {
            return Err(self.error(&arguments[0], "expected a string hash table"));
        };
        let key = self.hash_key(&arguments[1])?;
        let value = self
            .hash_tables
            .get(id.0, "string hash table")
            .map_err(Self::internal_error)?
            .get(&key)
            .cloned();
        self.parser
            .objects
            .cons(
                self.boolean(value.is_some()),
                value.unwrap_or_else(|| Object::empty_list(None)),
                form.span,
            )
            .map_err(Self::internal_error)
    }

    fn objects_equal(
        &self,
        left: &Object,
        right: &Object,
        visited: &mut HashSet<(crate::object::ConsId, crate::object::ConsId)>,
    ) -> Result<bool, Diagnostic> {
        match (&left.value, &right.value) {
            (Value::Pair(a), Value::Pair(b)) => {
                if !visited.insert((*a, *b)) {
                    return Ok(true);
                }
                let a = self.pair(*a)?;
                let b = self.pair(*b)?;
                Ok(self.objects_equal(&a.car, &b.car, visited)?
                    && self.objects_equal(&a.cdr, &b.cdr, visited)?)
            }
            (Value::Array(a), Value::Array(b)) => {
                if a.len() != b.len() {
                    return Ok(false);
                }
                for (a, b) in a.iter().zip(b) {
                    if !self.objects_equal(a, b, visited)? {
                        return Ok(false);
                    }
                }
                Ok(true)
            }
            _ => Ok(left.value == right.value),
        }
    }
}

#[derive(Clone, Copy)]
enum NumericOperation {
    Add,
    Subtract,
    Multiply,
    Divide,
}
