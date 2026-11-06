use crate::diagnostic::Diagnostic;
use crate::object::{Object, ProcedureId, SymbolMap, Value};

use super::{Interpreter, KeywordParameter, Parameters, Procedure, QuasiquoteValue};

#[derive(Default)]
struct CallArguments {
    positional: Vec<Object>,
    keywords: SymbolMap<Object>,
}

#[derive(Clone, Copy)]
pub(super) enum ArgumentMode {
    Evaluate,
    Values,
    Raw,
}

impl Interpreter {
    pub(super) fn eval_expanded(
        &mut self,
        object: Object,
        environment: crate::object::EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        match object.value {
            Value::Symbol(symbol) => self
                .lookup(environment, symbol)
                .map_err(Self::internal_error)?
                .ok_or_else(|| {
                    let name = self.symbol_name(symbol).unwrap_or("<invalid-symbol>");
                    self.error(&object, format!("symbol `{name}` is not defined"))
                }),
            Value::Pair(_) => self.eval_pair(object, environment),
            Value::Integer(_)
            | Value::Float(_)
            | Value::String(_)
            | Value::Array(_)
            | Value::Lambda(_)
            | Value::Macro(_)
            | Value::Environment(_)
            | Value::StringHashTable(_)
            | Value::EmptyList => Ok(object),
        }
    }

    fn eval_pair(
        &mut self,
        form: Object,
        environment: crate::object::EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        let items = self.proper_list(&form, "function call")?;
        if items.is_empty() {
            return Err(self.error(&form, "cannot evaluate an empty call"));
        }
        if let Value::Symbol(head) = items[0].value {
            if head == self.core.quote {
                return self.eval_quote(&form, &items[1..]);
            }
            if head == self.core.quasiquote {
                return self.eval_quasiquote(&form, &items[1..], environment);
            }
            if head == self.core.define {
                return self.eval_define(&form, &items[1..], environment);
            }
            if head == self.core.set {
                return self.eval_set(&form, &items[1..], environment);
            }
            if head == self.core.lambda {
                return self.eval_callable(&form, &items[1..], environment, false);
            }
            if head == self.core.macro_symbol {
                return self.eval_callable(&form, &items[1..], environment, true);
            }
            if head == self.core.begin || head == self.core.top_level {
                return self.eval_sequence(&items[1..], environment);
            }
            if head == self.core.cond {
                return self.eval_cond(&form, &items[1..], environment);
            }
            if head == self.core.or {
                return self.eval_or(&items[1..], environment);
            }
            if head == self.core.and {
                return self.eval_and(&items[1..], environment);
            }
            if head == self.core.while_symbol {
                return self.eval_while(&form, &items[1..], environment);
            }
            if head == self.core.let_symbol {
                return self.eval_let(&form, &items[1..], environment, false);
            }
            if head == self.core.let_star {
                return self.eval_let(&form, &items[1..], environment, true);
            }
            if let Some(&builtin) = self.builtin_forms.get(&head) {
                let mut arguments = Vec::with_capacity(items.len() - 1);
                for argument in &items[1..] {
                    arguments.push(self.eval_expanded(argument.clone(), environment)?);
                }
                return builtin(self, &form, arguments, environment);
            }
        }

        let callable = self.eval_expanded(items[0].clone(), environment)?;
        match callable.value {
            Value::Lambda(id) => {
                self.call_procedure(id, &form, &items[1..], environment, ArgumentMode::Evaluate)
            }
            Value::Macro(_) => Err(self.error(
                &form,
                "GOOS macro reached evaluation without being expanded",
            )),
            _ => Err(self.error(&form, "head of form is not callable")),
        }
    }

    fn eval_quote(&self, form: &Object, arguments: &[Object]) -> Result<Object, Diagnostic> {
        self.expect_arity(form, arguments, 1)?;
        Ok(arguments[0].clone())
    }

    fn eval_define(
        &mut self,
        form: &Object,
        arguments: &[Object],
        environment: crate::object::EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        if arguments.is_empty() {
            return Err(self.error(form, "define needs a name and value"));
        }

        let (target_environment, name_index) = if let Value::Symbol(symbol) = arguments[0].value {
            if self.symbol_name(symbol)? == ":env" {
                if arguments.len() < 4 {
                    return Err(
                        self.error(form, "define :env needs an environment, name, and value")
                    );
                }
                let target = self.eval_expanded(arguments[1].clone(), environment)?;
                let Value::Environment(target) = target.value else {
                    return Err(
                        self.error(&arguments[1], "define :env target is not an environment")
                    );
                };
                (target, 2)
            } else {
                (environment, 0)
            }
        } else {
            (environment, 0)
        };

        if let Value::Pair(_) = arguments[name_index].value {
            if name_index != 0 {
                return Err(
                    self.error(form, "function shorthand is not supported with define :env")
                );
            }
            if arguments.len() < 2 {
                return Err(self.error(form, "function definition needs a body"));
            }
            let (signature, signature_tail) = self.list_parts(&arguments[0])?;
            let Some(Object {
                value: Value::Symbol(name),
                ..
            }) = signature.first()
            else {
                return Err(self.error(&arguments[0], "function name must be a symbol"));
            };
            let parameter_list =
                self.build_list(signature[1..].to_vec(), signature_tail, arguments[0].span)?;
            let parameters = self.parse_parameters(&parameter_list)?;
            let id = self.allocate_procedure(Procedure {
                parameters,
                body: arguments[1..].to_vec(),
                environment,
            })?;
            let value = Object::lambda(id, form.span);
            self.define_in(environment, *name, value.clone())?;
            return Ok(value);
        }

        if arguments.len() != name_index + 2 {
            return Err(self.error(form, "define needs exactly one name and one value"));
        }
        let Value::Symbol(name) = arguments[name_index].value else {
            return Err(self.error(&arguments[name_index], "define name must be a symbol"));
        };
        let value = self.eval_expanded(arguments[name_index + 1].clone(), environment)?;
        self.define_in(target_environment, name, value.clone())?;
        Ok(value)
    }

    fn eval_set(
        &mut self,
        form: &Object,
        arguments: &[Object],
        environment: crate::object::EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, arguments, 2)?;
        let Value::Symbol(name) = arguments[0].value else {
            return Err(self.error(&arguments[0], "set! target must be a symbol"));
        };
        let value = self.eval_expanded(arguments[1].clone(), environment)?;
        if !self
            .set_existing(environment, name, value.clone())
            .map_err(Self::internal_error)?
        {
            return Err(self.error(&arguments[0], "cannot set an undefined symbol"));
        }
        Ok(value)
    }

    fn eval_callable(
        &mut self,
        form: &Object,
        arguments: &[Object],
        environment: crate::object::EnvironmentId,
        is_macro: bool,
    ) -> Result<Object, Diagnostic> {
        if arguments.len() < 2 {
            return Err(self.error(form, "callable needs parameters and a body"));
        }
        let parameters = self.parse_parameters(&arguments[0])?;
        let id = self.allocate_procedure(Procedure {
            parameters,
            body: arguments[1..].to_vec(),
            environment,
        })?;
        Ok(if is_macro {
            Object::macro_object(id, form.span)
        } else {
            Object::lambda(id, form.span)
        })
    }

    fn allocate_procedure(&mut self, procedure: Procedure) -> Result<ProcedureId, Diagnostic> {
        self.procedures
            .allocate(procedure)
            .map(ProcedureId)
            .map_err(Self::internal_error)
    }

    fn eval_sequence(
        &mut self,
        expressions: &[Object],
        environment: crate::object::EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        let mut result = Object::empty_list(None);
        for expression in expressions {
            result = self.eval_expanded(expression.clone(), environment)?;
        }
        Ok(result)
    }

    fn eval_cond(
        &mut self,
        form: &Object,
        clauses: &[Object],
        environment: crate::object::EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        if clauses.is_empty() {
            return Err(self.error(form, "cond needs at least one clause"));
        }
        for clause in clauses {
            let items = self.proper_list(clause, "cond clause")?;
            if items.is_empty() {
                return Err(self.error(clause, "cond clause cannot be empty"));
            }
            let is_else = items[0].value == Value::Symbol(self.core.else_symbol);
            let condition = if is_else {
                self.true_object()
            } else {
                self.eval_expanded(items[0].clone(), environment)?
            };
            if self.truthy(&condition) {
                if items.len() == 1 {
                    return Ok(condition);
                }
                return self.eval_sequence(&items[1..], environment);
            }
        }
        Ok(self.false_object())
    }

    fn eval_or(
        &mut self,
        expressions: &[Object],
        environment: crate::object::EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        for expression in expressions {
            let value = self.eval_expanded(expression.clone(), environment)?;
            if self.truthy(&value) {
                return Ok(value);
            }
        }
        Ok(self.false_object())
    }

    fn eval_and(
        &mut self,
        expressions: &[Object],
        environment: crate::object::EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        let mut result = self.true_object();
        for expression in expressions {
            result = self.eval_expanded(expression.clone(), environment)?;
            if !self.truthy(&result) {
                return Ok(self.false_object());
            }
        }
        Ok(result)
    }

    fn eval_while(
        &mut self,
        form: &Object,
        arguments: &[Object],
        environment: crate::object::EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        if arguments.len() < 2 {
            return Err(self.error(form, "while needs a condition and body"));
        }
        let mut result = self.false_object();
        while {
            let condition = self.eval_expanded(arguments[0].clone(), environment)?;
            self.truthy(&condition)
        } {
            result = self.eval_sequence(&arguments[1..], environment)?;
        }
        Ok(result)
    }

    fn eval_let(
        &mut self,
        form: &Object,
        arguments: &[Object],
        environment: crate::object::EnvironmentId,
        sequential: bool,
    ) -> Result<Object, Diagnostic> {
        if arguments.len() < 2 {
            return Err(self.error(form, "let needs bindings and a body"));
        }
        let bindings = self.proper_list(&arguments[0], "let bindings")?;
        let local = self.allocate_environment(Some(environment))?;
        for binding in bindings {
            let pair = self.proper_list(&binding, "let binding")?;
            if pair.len() != 2 {
                return Err(self.error(&binding, "let binding needs a name and value"));
            }
            let Value::Symbol(name) = pair[0].value else {
                return Err(self.error(&pair[0], "let binding name must be a symbol"));
            };
            let value_environment = if sequential { local } else { environment };
            let value = self.eval_expanded(pair[1].clone(), value_environment)?;
            self.define_in(local, name, value)?;
        }
        self.eval_sequence(&arguments[1..], local)
    }

    pub(super) fn call_procedure(
        &mut self,
        id: ProcedureId,
        form: &Object,
        raw_arguments: &[Object],
        caller_environment: crate::object::EnvironmentId,
        argument_mode: ArgumentMode,
    ) -> Result<Object, Diagnostic> {
        let procedure = self
            .procedures
            .get(id.0, "procedure")
            .cloned()
            .map_err(Self::internal_error)?;
        let supplied = self.parse_call_arguments(form, raw_arguments)?;
        let local = self.allocate_environment(Some(procedure.environment))?;

        if supplied.positional.len() < procedure.parameters.positional.len() {
            return Err(self.error(form, "not enough positional arguments"));
        }
        if procedure.parameters.rest.is_none()
            && supplied.positional.len() > procedure.parameters.positional.len()
        {
            return Err(self.error(form, "too many positional arguments"));
        }

        for (name, value) in procedure
            .parameters
            .positional
            .iter()
            .zip(&supplied.positional)
        {
            let value = if matches!(argument_mode, ArgumentMode::Evaluate) {
                self.eval_expanded(value.clone(), caller_environment)?
            } else {
                value.clone()
            };
            self.define_in(local, *name, value)?;
        }

        for parameter in &procedure.parameters.keywords {
            let value = if let Some(value) = supplied.keywords.get(&parameter.name) {
                value.clone()
            } else if let Some(default) = &parameter.default {
                default.clone()
            } else {
                let name = self.symbol_name(parameter.name)?;
                return Err(self.error(form, format!("missing keyword argument :{name}")));
            };
            let value = if matches!(argument_mode, ArgumentMode::Evaluate)
                || (matches!(argument_mode, ArgumentMode::Values)
                    && !supplied.keywords.contains_key(&parameter.name))
            {
                self.eval_expanded(value, caller_environment)?
            } else {
                value
            };
            self.define_in(local, parameter.name, value)?;
        }
        for name in supplied.keywords.keys() {
            if !procedure
                .parameters
                .keywords
                .iter()
                .any(|parameter| parameter.name == *name)
            {
                let name = self.symbol_name(*name)?;
                return Err(self.error(form, format!("unexpected keyword argument :{name}")));
            }
        }

        if let Some(rest_name) = procedure.parameters.rest {
            let mut rest = Vec::new();
            for value in supplied
                .positional
                .iter()
                .skip(procedure.parameters.positional.len())
            {
                rest.push(if matches!(argument_mode, ArgumentMode::Evaluate) {
                    self.eval_expanded(value.clone(), caller_environment)?
                } else {
                    value.clone()
                });
            }
            let rest = self.build_proper_list(rest, None)?;
            self.define_in(local, rest_name, rest)?;
        }
        self.eval_sequence(&procedure.body, local)
    }

    fn parse_call_arguments(
        &mut self,
        form: &Object,
        arguments: &[Object],
    ) -> Result<CallArguments, Diagnostic> {
        let mut result = CallArguments::default();
        let mut index = 0;
        while index < arguments.len() {
            if let Value::Symbol(symbol) = arguments[index].value {
                let name = self.symbol_name(symbol)?;
                if let Some(keyword) = name.strip_prefix(':').map(str::to_owned) {
                    if index + 1 == arguments.len() {
                        return Err(self.error(form, "keyword argument has no value"));
                    }
                    let keyword = self.intern(&keyword);
                    if result
                        .keywords
                        .insert(keyword, arguments[index + 1].clone())
                        .is_some()
                    {
                        return Err(self.error(form, "keyword argument was supplied twice"));
                    }
                    index += 2;
                    continue;
                }
            }
            result.positional.push(arguments[index].clone());
            index += 1;
        }
        Ok(result)
    }

    fn parse_parameters(&self, object: &Object) -> Result<Parameters, Diagnostic> {
        let (items, tail) = self.list_parts(object)?;
        let mut result = Parameters::default();
        let mut index = 0;
        let mut keyword_mode = false;
        while index < items.len() {
            if items[index].value == Value::Symbol(self.core.rest) {
                if index + 2 != items.len() || tail.value != Value::EmptyList {
                    return Err(self.error(object, "&rest must be followed by one final name"));
                }
                let Value::Symbol(name) = items[index + 1].value else {
                    return Err(self.error(&items[index + 1], "&rest name must be a symbol"));
                };
                result.rest = Some(name);
                return Ok(result);
            }
            if items[index].value == Value::Symbol(self.core.key) {
                keyword_mode = true;
                index += 1;
                if index == items.len() {
                    return Err(self.error(object, "&key must be followed by a parameter"));
                }
            }

            if keyword_mode {
                let parameter = match items[index].value {
                    Value::Symbol(name) => KeywordParameter {
                        name,
                        default: None,
                    },
                    Value::Pair(_) => {
                        let definition = self.proper_list(&items[index], "keyword parameter")?;
                        if definition.len() != 2 {
                            return Err(self.error(
                                &items[index],
                                "keyword parameter needs a name and default",
                            ));
                        }
                        let Value::Symbol(name) = definition[0].value else {
                            return Err(self
                                .error(&definition[0], "keyword parameter name must be a symbol"));
                        };
                        KeywordParameter {
                            name,
                            default: Some(definition[1].clone()),
                        }
                    }
                    _ => {
                        return Err(
                            self.error(&items[index], "keyword parameter must be a symbol or pair")
                        );
                    }
                };
                if result
                    .keywords
                    .iter()
                    .any(|existing| existing.name == parameter.name)
                {
                    return Err(self.error(&items[index], "duplicate keyword parameter"));
                }
                result.keywords.push(parameter);
            } else {
                let Value::Symbol(name) = items[index].value else {
                    return Err(self.error(&items[index], "parameter must be a symbol"));
                };
                result.positional.push(name);
            }
            index += 1;
        }

        match tail.value {
            Value::EmptyList => {}
            Value::Symbol(name) => result.rest = Some(name),
            _ => return Err(self.error(&tail, "dotted parameter tail must be a symbol")),
        }
        Ok(result)
    }

    fn eval_quasiquote(
        &mut self,
        form: &Object,
        arguments: &[Object],
        environment: crate::object::EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        self.expect_arity(form, arguments, 1)?;
        match self.quasiquote(arguments[0].clone(), environment, 1, false)? {
            QuasiquoteValue::One(value) => Ok(value),
            QuasiquoteValue::Splice(_) => Err(self.error(form, "cannot splice at quasiquote root")),
        }
    }

    fn quasiquote(
        &mut self,
        object: Object,
        environment: crate::object::EnvironmentId,
        depth: usize,
        allow_splice: bool,
    ) -> Result<QuasiquoteValue, Diagnostic> {
        if let Value::Pair(_) = object.value {
            let (items, tail) = self.list_parts(&object)?;
            if let Some(Value::Symbol(head)) = items.first().map(|item| &item.value) {
                if *head == self.core.unquote && depth == 1 {
                    if items.len() != 2 || tail.value != Value::EmptyList {
                        return Err(self.error(&object, "unquote needs exactly one argument"));
                    }
                    return Ok(QuasiquoteValue::One(
                        self.eval_expanded(items[1].clone(), environment)?,
                    ));
                }
                if *head == self.core.unquote_splicing && depth == 1 {
                    if !allow_splice {
                        return Err(self.error(&object, "unquote-splicing is only valid in a list"));
                    }
                    if items.len() != 2 || tail.value != Value::EmptyList {
                        return Err(
                            self.error(&object, "unquote-splicing needs exactly one argument")
                        );
                    }
                    let value = self.eval_expanded(items[1].clone(), environment)?;
                    return Ok(QuasiquoteValue::Splice(
                        self.proper_list(&value, "unquote-splicing result")?,
                    ));
                }
                if *head == self.core.unquote_concat && depth == 1 {
                    if items.len() != 2 || tail.value != Value::EmptyList {
                        return Err(self.error(&object, "unquote-concat needs one argument list"));
                    }
                    let expressions = self.proper_list(&items[1], "unquote-concat argument")?;
                    let mut name = String::new();
                    for expression in expressions {
                        let value = self.eval_expanded(expression, environment)?;
                        name.push_str(&self.object_fragment(&value)?);
                    }
                    let symbol = self.intern(&name);
                    return Ok(QuasiquoteValue::One(Object::symbol(symbol, None)));
                }
                if *head == self.core.quasiquote {
                    return self.quasiquote_preserved(items, tail, environment, depth + 1);
                }
                if (*head == self.core.unquote
                    || *head == self.core.unquote_splicing
                    || *head == self.core.unquote_concat)
                    && depth > 1
                {
                    return self.quasiquote_preserved(items, tail, environment, depth - 1);
                }
            }

            let mut result = Vec::new();
            for item in items {
                match self.quasiquote(item, environment, depth, true)? {
                    QuasiquoteValue::One(value) => result.push(value),
                    QuasiquoteValue::Splice(values) => result.extend(values),
                }
            }
            let tail = match tail.value {
                Value::EmptyList => Object::empty_list(None),
                _ => match self.quasiquote(tail, environment, depth, false)? {
                    QuasiquoteValue::One(value) => value,
                    QuasiquoteValue::Splice(_) => unreachable!(),
                },
            };
            return Ok(QuasiquoteValue::One(self.build_list(result, tail, None)?));
        }
        if let Value::Array(items) = object.value {
            let mut result = Vec::with_capacity(items.len());
            for item in items {
                match self.quasiquote(item, environment, depth, false)? {
                    QuasiquoteValue::One(item) => result.push(item),
                    QuasiquoteValue::Splice(_) => unreachable!(),
                }
            }
            return Ok(QuasiquoteValue::One(Object::array(result, None)));
        }
        Ok(QuasiquoteValue::One(Object::new(object.value, None)))
    }

    fn quasiquote_preserved(
        &mut self,
        items: Vec<Object>,
        tail: Object,
        environment: crate::object::EnvironmentId,
        depth: usize,
    ) -> Result<QuasiquoteValue, Diagnostic> {
        let mut result = Vec::with_capacity(items.len());
        for item in items {
            match self.quasiquote(item, environment, depth, false)? {
                QuasiquoteValue::One(item) => result.push(item),
                QuasiquoteValue::Splice(_) => unreachable!(),
            }
        }
        let tail = if tail.value == Value::EmptyList {
            Object::empty_list(None)
        } else {
            match self.quasiquote(tail, environment, depth, false)? {
                QuasiquoteValue::One(tail) => tail,
                QuasiquoteValue::Splice(_) => unreachable!(),
            }
        };
        Ok(QuasiquoteValue::One(self.build_list(result, tail, None)?))
    }
}
