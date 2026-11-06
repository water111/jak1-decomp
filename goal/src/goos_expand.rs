use crate::diagnostic::Diagnostic;
use crate::object::{EnvironmentId, Object, Value};

use super::Interpreter;
use super::eval::ArgumentMode;

impl Interpreter {
    pub fn expand_goos_macros(
        &mut self,
        object: Object,
        environment: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        let Value::Pair(_) = object.value else {
            return Ok(object);
        };
        let (items, tail) = self.list_parts(&object)?;
        if items.is_empty() {
            return Ok(object);
        }
        if tail.value != Value::EmptyList {
            return Err(self.error(&object, "executable form must be a proper list"));
        }

        if let Value::Symbol(head) = items[0].value {
            if head == self.core.quote {
                return Ok(object);
            }
            if head == self.core.quasiquote {
                if items.len() != 2 {
                    return Err(self.error(&object, "quasiquote needs exactly one argument"));
                }
                let template = self.expand_quasiquote_template(items[1].clone(), environment, 1)?;
                return self.build_proper_list(vec![items[0].clone(), template], object.span);
            }
            if head == self.core.lambda || head == self.core.macro_symbol {
                if items.len() < 3 {
                    return Err(self.error(&object, "callable needs parameters and a body"));
                }
                let mut expanded = Vec::with_capacity(items.len());
                expanded.push(items[0].clone());
                expanded.push(self.expand_parameter_spec(items[1].clone(), environment)?);
                for body in &items[2..] {
                    expanded.push(self.expand_goos_macros(body.clone(), environment)?);
                }
                return self.build_proper_list(expanded, object.span);
            }
            if head == self.core.define {
                return self.expand_define(object.span, items, environment);
            }
            if head == self.core.set {
                if items.len() != 3 {
                    return Err(self.error(&object, "set! needs a name and value"));
                }
                let value = self.expand_goos_macros(items[2].clone(), environment)?;
                return self.build_proper_list(
                    vec![items[0].clone(), items[1].clone(), value],
                    object.span,
                );
            }
            if head == self.core.let_symbol || head == self.core.let_star {
                return self.expand_let(object.span, items, environment);
            }
            if head == self.core.cond {
                return self.expand_cond(object.span, items, environment);
            }

            if let Some(Value::Macro(id)) = self
                .lookup(environment, head)
                .map_err(Self::internal_error)?
                .map(|object| object.value)
            {
                let expanded =
                    self.call_procedure(id, &object, &items[1..], environment, ArgumentMode::Raw)?;
                return self.expand_goos_macros(expanded, environment);
            }
        }

        let mut expanded = Vec::with_capacity(items.len());
        for item in items {
            expanded.push(self.expand_goos_macros(item, environment)?);
        }
        self.build_proper_list(expanded, object.span)
    }

    fn expand_define(
        &mut self,
        span: Option<crate::span::SpanInfo>,
        items: Vec<Object>,
        environment: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        if items.len() < 3 {
            return Err(Diagnostic::error("define needs a name and value"));
        }
        let mut expanded = Vec::with_capacity(items.len());
        expanded.push(items[0].clone());

        let env_form = matches!(
            items.get(1).map(|object| &object.value),
            Some(Value::Symbol(symbol)) if self.symbol_name(*symbol).ok() == Some(":env")
        );
        if env_form {
            if items.len() != 5 {
                return Err(Diagnostic::error(
                    "define :env needs an environment, name, and value",
                ));
            }
            expanded.push(items[1].clone());
            expanded.push(self.expand_goos_macros(items[2].clone(), environment)?);
            expanded.push(items[3].clone());
            expanded.push(self.expand_goos_macros(items[4].clone(), environment)?);
            return self.build_proper_list(expanded, span);
        }

        expanded.push(if matches!(items[1].value, Value::Pair(_)) {
            self.expand_function_signature(items[1].clone(), environment)?
        } else {
            items[1].clone()
        });
        if matches!(items[1].value, Value::Pair(_)) {
            for body in &items[2..] {
                expanded.push(self.expand_goos_macros(body.clone(), environment)?);
            }
        } else {
            if items.len() != 3 {
                return Err(Diagnostic::error("define needs exactly one value"));
            }
            expanded.push(self.expand_goos_macros(items[2].clone(), environment)?);
        }
        self.build_proper_list(expanded, span)
    }

    fn expand_function_signature(
        &mut self,
        signature: Object,
        environment: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        let (items, tail) = self.list_parts(&signature)?;
        if items.is_empty() {
            return Err(self.error(&signature, "function signature cannot be empty"));
        }
        let parameters = self.build_list(items[1..].to_vec(), tail, signature.span)?;
        let parameters = self.expand_parameter_spec(parameters, environment)?;
        let (mut parameters, tail) = self.list_parts(&parameters)?;
        parameters.insert(0, items[0].clone());
        self.build_list(parameters, tail, signature.span)
    }

    fn expand_parameter_spec(
        &mut self,
        parameters: Object,
        environment: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        let (items, tail) = self.list_parts(&parameters)?;
        let mut expanded = Vec::with_capacity(items.len());
        let mut keyword_mode = false;
        for item in items {
            if item.value == Value::Symbol(self.core.key) {
                keyword_mode = true;
                expanded.push(item);
                continue;
            }
            if keyword_mode && matches!(item.value, Value::Pair(_)) {
                let definition = self.proper_list(&item, "keyword parameter")?;
                if definition.len() != 2 {
                    return Err(self.error(&item, "keyword parameter needs a name and default"));
                }
                let default = self.expand_goos_macros(definition[1].clone(), environment)?;
                expanded
                    .push(self.build_proper_list(vec![definition[0].clone(), default], item.span)?);
            } else {
                expanded.push(item);
            }
        }
        self.build_list(expanded, tail, parameters.span)
    }

    fn expand_let(
        &mut self,
        span: Option<crate::span::SpanInfo>,
        items: Vec<Object>,
        environment: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        // Binding pairs are syntax, not calls. Expand each initializer and body without
        // attempting to invoke a macro whose name happens to match a binding name.
        if items.len() < 3 {
            return Err(Diagnostic::error("let needs bindings and a body"));
        }
        let bindings = self.proper_list(&items[1], "let bindings")?;
        let mut expanded_bindings = Vec::with_capacity(bindings.len());
        for binding in bindings {
            let pair = self.proper_list(&binding, "let binding")?;
            if pair.len() != 2 {
                return Err(self.error(&binding, "let binding needs a name and value"));
            }
            let value = self.expand_goos_macros(pair[1].clone(), environment)?;
            expanded_bindings
                .push(self.build_proper_list(vec![pair[0].clone(), value], binding.span)?);
        }
        let bindings = self.build_proper_list(expanded_bindings, items[1].span)?;
        let mut expanded = vec![items[0].clone(), bindings];
        for body in &items[2..] {
            expanded.push(self.expand_goos_macros(body.clone(), environment)?);
        }
        self.build_proper_list(expanded, span)
    }

    fn expand_cond(
        &mut self,
        span: Option<crate::span::SpanInfo>,
        items: Vec<Object>,
        environment: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        // Clauses are syntax, not calls. Each expression in a clause is an independent
        // expansion context, including clauses whose condition is a bare symbol.
        let mut expanded = vec![items[0].clone()];
        for clause in &items[1..] {
            let clause_items = self.proper_list(clause, "cond clause")?;
            let mut expanded_clause = Vec::with_capacity(clause_items.len());
            for expression in clause_items {
                expanded_clause.push(self.expand_goos_macros(expression, environment)?);
            }
            expanded.push(self.build_proper_list(expanded_clause, clause.span)?);
        }
        self.build_proper_list(expanded, span)
    }

    fn expand_quasiquote_template(
        &mut self,
        object: Object,
        environment: EnvironmentId,
        depth: usize,
    ) -> Result<Object, Diagnostic> {
        match object.value {
            Value::Pair(_) => {
                let (items, tail) = self.list_parts(&object)?;
                if let Some(Value::Symbol(head)) = items.first().map(|item| &item.value) {
                    if (*head == self.core.unquote
                        || *head == self.core.unquote_splicing
                        || *head == self.core.unquote_concat)
                        && depth == 1
                    {
                        if items.len() != 2 || tail.value != Value::EmptyList {
                            return Err(self.error(&object, "unquote form needs one argument"));
                        }
                        let argument = if *head == self.core.unquote_concat {
                            let expressions = self.proper_list(&items[1], "unquote-concat")?;
                            let mut expanded = Vec::with_capacity(expressions.len());
                            for expression in expressions {
                                expanded.push(self.expand_goos_macros(expression, environment)?);
                            }
                            self.build_proper_list(expanded, items[1].span)?
                        } else {
                            self.expand_goos_macros(items[1].clone(), environment)?
                        };
                        return self
                            .build_proper_list(vec![items[0].clone(), argument], object.span);
                    }
                    if *head == self.core.quasiquote {
                        return self.rebuild_quasiquote(
                            items,
                            tail,
                            environment,
                            depth + 1,
                            object.span,
                        );
                    }
                    if (*head == self.core.unquote
                        || *head == self.core.unquote_splicing
                        || *head == self.core.unquote_concat)
                        && depth > 1
                    {
                        return self.rebuild_quasiquote(
                            items,
                            tail,
                            environment,
                            depth - 1,
                            object.span,
                        );
                    }
                }
                self.rebuild_quasiquote(items, tail, environment, depth, object.span)
            }
            Value::Array(items) => {
                let mut expanded = Vec::with_capacity(items.len());
                for item in items {
                    expanded.push(self.expand_quasiquote_template(item, environment, depth)?);
                }
                Ok(Object::array(expanded, object.span))
            }
            _ => Ok(object),
        }
    }

    fn rebuild_quasiquote(
        &mut self,
        items: Vec<Object>,
        tail: Object,
        environment: EnvironmentId,
        depth: usize,
        span: Option<crate::span::SpanInfo>,
    ) -> Result<Object, Diagnostic> {
        let mut expanded = Vec::with_capacity(items.len());
        for item in items {
            expanded.push(self.expand_quasiquote_template(item, environment, depth)?);
        }
        let tail = if tail.value == Value::EmptyList {
            tail
        } else {
            self.expand_quasiquote_template(tail, environment, depth)?
        };
        self.build_list(expanded, tail, span)
    }
}
