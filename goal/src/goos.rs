#[path = "goos_builtins.rs"]
mod builtins;
#[path = "goos_eval.rs"]
mod eval;
#[path = "goos_expand.rs"]
mod expand;
#[path = "goos_printer.rs"]
mod printer;

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use crate::diagnostic::Diagnostic;
use crate::object::SymbolMap;
use crate::object::{
    BuiltinId, ConsId, EnvironmentId, Object, ProcedureId, StringHashTableId, SymbolId, Value,
};
use crate::parse::Parser;
use crate::span::SpanInfo;

use builtins::BUILTINS;

const STANDARD_LIBRARY: &str = include_str!("../goos/goos-lib.gs");

#[derive(Clone, Debug)]
struct KeywordParameter {
    name: SymbolId,
    default: Option<Object>,
}

#[derive(Clone, Debug, Default)]
struct Parameters {
    positional: Vec<SymbolId>,
    keywords: Vec<KeywordParameter>,
    rest: Option<SymbolId>,
}

#[derive(Clone, Debug)]
struct Procedure {
    parameters: Parameters,
    body: Vec<Object>,
    environment: EnvironmentId,
}

#[derive(Debug)]
struct Environment {
    parent: Option<EnvironmentId>,
    bindings: SymbolMap<Object>,
}

#[derive(Debug)]
enum ArenaSlot<T> {
    Occupied { value: T, marked: bool },
    Free { next: Option<u32> },
}

#[derive(Debug)]
struct Arena<T> {
    slots: Vec<ArenaSlot<T>>,
    free_head: Option<u32>,
}

impl<T> Default for Arena<T> {
    fn default() -> Self {
        Self {
            slots: Vec::new(),
            free_head: None,
        }
    }
}

impl<T> Arena<T> {
    fn allocate(&mut self, value: T) -> Result<u32, String> {
        if let Some(index) = self.free_head {
            let slot = &mut self.slots[index as usize];
            let ArenaSlot::Free { next } = slot else {
                return Err("internal compiler error: corrupt arena free list".to_owned());
            };
            self.free_head = *next;
            *slot = ArenaSlot::Occupied {
                value,
                marked: false,
            };
            return Ok(index);
        }
        let index = u32::try_from(self.slots.len())
            .map_err(|_| "internal compiler error: exhausted arena IDs".to_owned())?;
        self.slots.push(ArenaSlot::Occupied {
            value,
            marked: false,
        });
        Ok(index)
    }

    fn get(&self, index: u32, kind: &str) -> Result<&T, String> {
        match self.slots.get(index as usize) {
            Some(ArenaSlot::Occupied { value, .. }) => Ok(value),
            Some(ArenaSlot::Free { .. }) => {
                Err(format!("internal compiler error: stale {kind} ID {index}"))
            }
            None => Err(format!(
                "internal compiler error: invalid {kind} ID {index}"
            )),
        }
    }

    fn get_mut(&mut self, index: u32, kind: &str) -> Result<&mut T, String> {
        match self.slots.get_mut(index as usize) {
            Some(ArenaSlot::Occupied { value, .. }) => Ok(value),
            Some(ArenaSlot::Free { .. }) => {
                Err(format!("internal compiler error: stale {kind} ID {index}"))
            }
            None => Err(format!(
                "internal compiler error: invalid {kind} ID {index}"
            )),
        }
    }

    fn clear_marks(&mut self) {
        for slot in &mut self.slots {
            if let ArenaSlot::Occupied { marked, .. } = slot {
                *marked = false;
            }
        }
    }

    fn mark(&mut self, index: u32, kind: &str) -> Result<bool, String> {
        match self.slots.get_mut(index as usize) {
            Some(ArenaSlot::Occupied { marked, .. }) if !*marked => {
                *marked = true;
                Ok(true)
            }
            Some(ArenaSlot::Occupied { .. }) => Ok(false),
            Some(ArenaSlot::Free { .. }) => Err(format!(
                "internal compiler error: object refers to stale {kind} ID {index}"
            )),
            None => Err(format!(
                "internal compiler error: object refers to invalid {kind} ID {index}"
            )),
        }
    }

    fn sweep(&mut self) -> usize {
        let mut freed = 0;
        for index in 0..self.slots.len() {
            if matches!(self.slots[index], ArenaSlot::Occupied { marked: false, .. }) {
                self.slots[index] = ArenaSlot::Free {
                    next: self.free_head,
                };
                self.free_head = Some(index as u32);
                freed += 1;
            }
        }
        freed
    }
}

#[derive(Clone, Copy, Debug)]
struct CoreSymbols {
    true_symbol: SymbolId,
    false_symbol: SymbolId,
    quote: SymbolId,
    quasiquote: SymbolId,
    unquote: SymbolId,
    unquote_splicing: SymbolId,
    unquote_concat: SymbolId,
    define: SymbolId,
    set: SymbolId,
    lambda: SymbolId,
    macro_symbol: SymbolId,
    begin: SymbolId,
    top_level: SymbolId,
    cond: SymbolId,
    else_symbol: SymbolId,
    or: SymbolId,
    and: SymbolId,
    while_symbol: SymbolId,
    let_symbol: SymbolId,
    let_star: SymbolId,
    rest: SymbolId,
    key: SymbolId,
}

impl CoreSymbols {
    fn new(parser: &mut Parser) -> Self {
        let symbols = &mut parser.objects.symbols;
        Self {
            true_symbol: symbols.intern("#t"),
            false_symbol: symbols.intern("#f"),
            quote: symbols.intern("quote"),
            quasiquote: symbols.intern("quasiquote"),
            unquote: symbols.intern("unquote"),
            unquote_splicing: symbols.intern("unquote-splicing"),
            unquote_concat: symbols.intern("unquote-concat"),
            define: symbols.intern("define"),
            set: symbols.intern("set!"),
            lambda: symbols.intern("lambda"),
            macro_symbol: symbols.intern("macro"),
            begin: symbols.intern("begin"),
            top_level: symbols.intern("top-level"),
            cond: symbols.intern("cond"),
            else_symbol: symbols.intern("else"),
            or: symbols.intern("or"),
            and: symbols.intern("and"),
            while_symbol: symbols.intern("while"),
            let_symbol: symbols.intern("let"),
            let_star: symbols.intern("let*"),
            rest: symbols.intern("&rest"),
            key: symbols.intern("&key"),
        }
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct GoosCollectionStats {
    pub cons_freed: usize,
    pub procedures_freed: usize,
    pub environments_freed: usize,
    pub hash_tables_freed: usize,
}

#[derive(Clone, Copy, Debug)]
enum TraceTarget {
    Pair(ConsId),
    Procedure(ProcedureId),
    Environment(EnvironmentId),
    HashTable(StringHashTableId),
}

#[derive(Debug)]
pub struct Interpreter {
    pub parser: Parser,
    procedures: Arena<Procedure>,
    environments: Arena<Environment>,
    hash_tables: Arena<HashMap<String, Object>>,
    core: CoreSymbols,
    global_environment: EnvironmentId,
    goal_environment: EnvironmentId,
    gensym_counter: u64,
    read_counter: u64,
}

impl Interpreter {
    pub fn new() -> Result<Self, Diagnostic> {
        let mut interpreter = Self::new_without_standard_library()?;
        interpreter.load_source("<goos-lib.gs>", STANDARD_LIBRARY)?;
        Ok(interpreter)
    }

    pub fn new_without_standard_library() -> Result<Self, Diagnostic> {
        let mut parser = Parser::default();
        let core = CoreSymbols::new(&mut parser);
        let mut environments = Arena::default();
        let global_environment = EnvironmentId(
            environments
                .allocate(Environment {
                    parent: None,
                    bindings: SymbolMap::default(),
                })
                .map_err(Diagnostic::error)?,
        );
        let goal_environment = EnvironmentId(
            environments
                .allocate(Environment {
                    parent: Some(global_environment),
                    bindings: SymbolMap::default(),
                })
                .map_err(Diagnostic::error)?,
        );
        let mut interpreter = Self {
            parser,
            procedures: Arena::default(),
            environments,
            hash_tables: Arena::default(),
            core,
            global_environment,
            goal_environment,
            gensym_counter: 0,
            read_counter: 0,
        };
        interpreter.initialize_globals()?;
        Ok(interpreter)
    }

    fn initialize_globals(&mut self) -> Result<(), Diagnostic> {
        self.define_in(
            self.global_environment,
            self.core.true_symbol,
            Object::symbol(self.core.true_symbol, None),
        )?;
        self.define_in(
            self.global_environment,
            self.core.false_symbol,
            Object::symbol(self.core.false_symbol, None),
        )?;
        let global_name = self.intern("*global-env*");
        let goal_name = self.intern("*goal-env*");
        self.define_in(
            self.global_environment,
            global_name,
            Object::environment(self.global_environment, None),
        )?;
        self.define_in(
            self.global_environment,
            goal_name,
            Object::environment(self.goal_environment, None),
        )?;
        self.define_in(
            self.goal_environment,
            global_name,
            Object::environment(self.global_environment, None),
        )?;
        self.define_in(
            self.goal_environment,
            goal_name,
            Object::environment(self.goal_environment, None),
        )?;

        for (index, builtin) in BUILTINS.iter().enumerate() {
            let id = BuiltinId(
                u16::try_from(index).map_err(|_| Diagnostic::error("exhausted builtin IDs"))?,
            );
            let name = self.intern(builtin.0);
            self.define_in(self.global_environment, name, Object::builtin(id, None))?;
        }
        Ok(())
    }

    pub fn global_environment(&self) -> EnvironmentId {
        self.global_environment
    }

    pub fn goal_environment(&self) -> EnvironmentId {
        self.goal_environment
    }

    pub fn intern(&mut self, name: &str) -> SymbolId {
        self.parser.objects.symbols.intern(name)
    }

    pub fn load_file(&mut self, path: impl AsRef<Path>) -> Result<Object, Diagnostic> {
        let path = path.as_ref();
        let text = std::fs::read_to_string(path)
            .map_err(|error| Diagnostic::error(format!("{}: {error}", path.display())))?;
        self.load_source(path.to_path_buf(), &text)
    }

    pub fn load_source(
        &mut self,
        path: impl Into<PathBuf>,
        text: &str,
    ) -> Result<Object, Diagnostic> {
        let parsed = self.parser.parse(path, text)?;
        let mut result = Object::empty_list(None);
        for form in parsed.objects {
            let expanded = self.expand_goos_macros(form, self.global_environment)?;
            result = self.eval_expanded(expanded, self.global_environment)?;
        }
        Ok(result)
    }

    pub fn eval(&mut self, object: Object) -> Result<Object, Diagnostic> {
        let expanded = self.expand_goos_macros(object, self.global_environment)?;
        self.eval_expanded(expanded, self.global_environment)
    }

    pub fn eval_in_environment(
        &mut self,
        object: Object,
        environment: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        let expanded = self.expand_goos_macros(object, environment)?;
        self.eval_expanded(expanded, environment)
    }

    pub fn eval_text(&mut self, text: &str) -> Result<Object, Diagnostic> {
        let name = format!("<eval:{}>", self.read_counter);
        self.read_counter += 1;
        self.load_source(name, text)
    }

    pub fn global_binding(&self, name: &str) -> Result<Option<Object>, String> {
        self.lookup(
            self.global_environment,
            SymbolId(crate::object::fnv64(name.as_bytes())),
        )
    }

    pub fn set_global_binding(&mut self, name: &str, value: Object) -> Result<(), Diagnostic> {
        let name = self.intern(name);
        self.define_in(self.global_environment, name, value)
    }

    pub fn binding_in(
        &self,
        environment: EnvironmentId,
        name: &str,
    ) -> Result<Option<Object>, String> {
        self.lookup(environment, SymbolId(crate::object::fnv64(name.as_bytes())))
    }

    pub fn invoke_macro(
        &mut self,
        macro_object: &Object,
        arguments: &[Object],
        caller_environment: EnvironmentId,
    ) -> Result<Object, Diagnostic> {
        let Value::Macro(id) = macro_object.value else {
            return Err(self.error(macro_object, "object is not a macro"));
        };
        self.call_procedure(
            id,
            macro_object,
            arguments,
            caller_environment,
            eval::ArgumentMode::Raw,
        )
    }

    pub fn collect_garbage(
        &mut self,
        additional_roots: &[Object],
    ) -> Result<GoosCollectionStats, String> {
        self.parser.objects.cons.clear_marks();
        self.procedures.clear_marks();
        self.environments.clear_marks();
        self.hash_tables.clear_marks();

        let mut pending = vec![
            TraceTarget::Environment(self.global_environment),
            TraceTarget::Environment(self.goal_environment),
        ];
        for root in additional_roots {
            trace_object(root, &mut pending);
        }

        while let Some(target) = pending.pop() {
            match target {
                TraceTarget::Pair(id) => {
                    if self.parser.objects.cons.mark(id)? {
                        let cell = self.parser.objects.cons.get(id)?;
                        trace_object(&cell.car, &mut pending);
                        trace_object(&cell.cdr, &mut pending);
                    }
                }
                TraceTarget::Procedure(id) => {
                    if self.procedures.mark(id.0, "procedure")? {
                        let procedure = self.procedures.get(id.0, "procedure")?;
                        pending.push(TraceTarget::Environment(procedure.environment));
                        for parameter in &procedure.parameters.keywords {
                            if let Some(default) = &parameter.default {
                                trace_object(default, &mut pending);
                            }
                        }
                        for expression in &procedure.body {
                            trace_object(expression, &mut pending);
                        }
                    }
                }
                TraceTarget::Environment(id) => {
                    if self.environments.mark(id.0, "environment")? {
                        let environment = self.environments.get(id.0, "environment")?;
                        if let Some(parent) = environment.parent {
                            pending.push(TraceTarget::Environment(parent));
                        }
                        for value in environment.bindings.values() {
                            trace_object(value, &mut pending);
                        }
                    }
                }
                TraceTarget::HashTable(id) => {
                    if self.hash_tables.mark(id.0, "string hash table")? {
                        for value in self.hash_tables.get(id.0, "string hash table")?.values() {
                            trace_object(value, &mut pending);
                        }
                    }
                }
            }
        }

        Ok(GoosCollectionStats {
            cons_freed: self.parser.objects.cons.sweep(),
            procedures_freed: self.procedures.sweep(),
            environments_freed: self.environments.sweep(),
            hash_tables_freed: self.hash_tables.sweep(),
        })
    }

    fn allocate_environment(
        &mut self,
        parent: Option<EnvironmentId>,
    ) -> Result<EnvironmentId, Diagnostic> {
        self.environments
            .allocate(Environment {
                parent,
                bindings: SymbolMap::default(),
            })
            .map(EnvironmentId)
            .map_err(Diagnostic::error)
    }

    fn define_in(
        &mut self,
        environment: EnvironmentId,
        name: SymbolId,
        value: Object,
    ) -> Result<(), Diagnostic> {
        self.environments
            .get_mut(environment.0, "environment")
            .map_err(Diagnostic::error)?
            .bindings
            .insert(name, value);
        Ok(())
    }

    fn lookup(&self, environment: EnvironmentId, name: SymbolId) -> Result<Option<Object>, String> {
        let mut current = Some(environment);
        while let Some(id) = current {
            let environment = self.environments.get(id.0, "environment")?;
            if let Some(value) = environment.bindings.get(&name) {
                return Ok(Some(value.clone()));
            }
            current = environment.parent;
        }
        Ok(None)
    }

    fn set_existing(
        &mut self,
        environment: EnvironmentId,
        name: SymbolId,
        value: Object,
    ) -> Result<bool, String> {
        let mut current = Some(environment);
        while let Some(id) = current {
            let parent = self.environments.get(id.0, "environment")?.parent;
            let environment = self.environments.get_mut(id.0, "environment")?;
            if let std::collections::hash_map::Entry::Occupied(mut binding) =
                environment.bindings.entry(name)
            {
                binding.insert(value);
                return Ok(true);
            }
            current = parent;
        }
        Ok(false)
    }

    fn true_object(&self) -> Object {
        Object::symbol(self.core.true_symbol, None)
    }

    fn false_object(&self) -> Object {
        Object::symbol(self.core.false_symbol, None)
    }

    fn boolean(&self, value: bool) -> Object {
        if value {
            self.true_object()
        } else {
            self.false_object()
        }
    }

    fn truthy(&self, object: &Object) -> bool {
        object.value != Value::Symbol(self.core.false_symbol)
    }

    fn error(&self, object: &Object, message: impl Into<String>) -> Diagnostic {
        let diagnostic = Diagnostic::error(message);
        match object.span {
            Some(span) => diagnostic.with_span(span),
            None => diagnostic,
        }
    }

    fn internal_error(message: impl Into<String>) -> Diagnostic {
        Diagnostic::error(message)
    }

    fn pair(&self, id: ConsId) -> Result<crate::object::ConsCell, Diagnostic> {
        self.parser
            .objects
            .cons
            .get(id)
            .cloned()
            .map_err(Self::internal_error)
    }

    fn list_parts(&self, object: &Object) -> Result<(Vec<Object>, Object), Diagnostic> {
        let mut items = Vec::new();
        let mut current = object.clone();
        loop {
            match current.value {
                Value::Pair(id) => {
                    let cell = self.pair(id)?;
                    items.push(cell.car);
                    current = cell.cdr;
                }
                _ => return Ok((items, current)),
            }
        }
    }

    fn proper_list(&self, object: &Object, context: &str) -> Result<Vec<Object>, Diagnostic> {
        let (items, tail) = self.list_parts(object)?;
        if tail.value != Value::EmptyList {
            return Err(self.error(object, format!("{context} must be a proper list")));
        }
        Ok(items)
    }

    fn build_list(
        &mut self,
        items: Vec<Object>,
        tail: Object,
        span: Option<SpanInfo>,
    ) -> Result<Object, Diagnostic> {
        self.parser
            .objects
            .list(items, tail, span)
            .map_err(Self::internal_error)
    }

    fn build_proper_list(
        &mut self,
        items: Vec<Object>,
        span: Option<SpanInfo>,
    ) -> Result<Object, Diagnostic> {
        self.build_list(items, Object::empty_list(span), span)
    }

    fn symbol_name(&self, symbol: SymbolId) -> Result<&str, Diagnostic> {
        self.parser
            .objects
            .symbols
            .resolve(symbol)
            .ok_or_else(|| Self::internal_error(format!("unknown symbol ID {}", symbol.0)))
    }

    fn expect_arity(
        &self,
        form: &Object,
        arguments: &[Object],
        expected: usize,
    ) -> Result<(), Diagnostic> {
        if arguments.len() != expected {
            return Err(self.error(
                form,
                format!("expected {expected} arguments, got {}", arguments.len()),
            ));
        }
        Ok(())
    }
}

enum QuasiquoteValue {
    One(Object),
    Splice(Vec<Object>),
}

fn trace_object(object: &Object, pending: &mut Vec<TraceTarget>) {
    match &object.value {
        Value::Pair(id) => pending.push(TraceTarget::Pair(*id)),
        Value::Array(items) => {
            for item in items {
                trace_object(item, pending);
            }
        }
        Value::Lambda(id) | Value::Macro(id) => {
            pending.push(TraceTarget::Procedure(*id));
        }
        Value::Environment(id) => pending.push(TraceTarget::Environment(*id)),
        Value::StringHashTable(id) => pending.push(TraceTarget::HashTable(*id)),
        Value::String(_)
        | Value::Symbol(_)
        | Value::Float(_)
        | Value::Integer(_)
        | Value::Builtin(_)
        | Value::EmptyList => {}
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn integer(object: Object) -> i64 {
        let Value::Integer(value) = object.value else {
            panic!("expected integer, got {object:?}");
        };
        value
    }

    #[test]
    fn loads_standard_library_and_evaluates_scheme() {
        let mut goos = Interpreter::new().unwrap();
        assert!(matches!(
            goos.global_binding("defun").unwrap().unwrap().value,
            Value::Macro(_)
        ));
        assert!(goos.global_binding("define-function").unwrap().is_none());
        assert!(goos.global_binding("read-file").unwrap().is_none());
        assert!(goos.global_binding("try-load-file").unwrap().is_none());
        assert!(
            goos.global_binding("get-environment-variable")
                .unwrap()
                .is_none()
        );
        assert!(matches!(
            goos.global_binding("load-file").unwrap().unwrap().value,
            Value::Builtin(_)
        ));
        assert_eq!(
            integer(
                goos.eval_text("(defun square (x) (* x x)) (square 6)")
                    .unwrap()
            ),
            36
        );
        assert_eq!(integer(goos.eval_text("(length '(1 2 3 4))").unwrap()), 4);
        assert_eq!(
            integer(goos.eval_text("(fold-left + 0 '(1 2 3 4))").unwrap()),
            10
        );
    }

    #[test]
    fn expands_goos_macros_when_preparing_function_bodies() {
        let mut goos = Interpreter::new().unwrap();
        let result = goos
            .eval_text(
                "(define expansion-count 0)\n\
                 (defmacro count-expansion ()\n\
                   (set! expansion-count (+ expansion-count 1))\n\
                   `(+ 20 22))\n\
                 (define (answer) (count-expansion))\n\
                 (answer)\n\
                 (answer)\n\
                 expansion-count",
            )
            .unwrap();
        assert_eq!(integer(result), 1);
    }

    #[test]
    fn expands_macro_bodies_once_when_the_macro_is_defined() {
        let mut goos = Interpreter::new().unwrap();
        let result = goos
            .eval_text(
                "(define expansion-count 0)\n\
                 (defmacro inner ()\n\
                   (set! expansion-count (+ expansion-count 1))\n\
                   `(+ 1 1))\n\
                 (defmacro outer () (inner))\n\
                 (outer)\n\
                 (outer)\n\
                 expansion-count",
            )
            .unwrap();
        assert_eq!(integer(result), 1);
    }

    #[test]
    fn closures_capture_lexical_environments() {
        let mut goos = Interpreter::new().unwrap();
        let result = goos
            .eval_text(
                "(define (make-counter value)\n\
                   (lambda ()\n\
                     (set! value (+ value 1))\n\
                     value))\n\
                 (define counter (make-counter 10))\n\
                 (counter)\n\
                 (counter)",
            )
            .unwrap();
        assert_eq!(integer(result), 12);
    }

    #[test]
    fn supports_keyword_defaults_and_rest_arguments() {
        let mut goos = Interpreter::new().unwrap();
        let result = goos
            .eval_text(
                "((lambda (a &key (b 2) &rest remaining)\n\
                    (list a b remaining))\n\
                  1 :b 3 4 5)",
            )
            .unwrap();
        assert_eq!(goos.format_object(&result).unwrap(), "(1 3 (4 5))");
    }

    #[test]
    fn quasiquote_supports_splicing_and_symbol_concatenation() {
        let mut goos = Interpreter::new().unwrap();
        let result = goos
            .eval_text(
                "(let ((a 'foo) (b 12))\n\
                   `(,+(a b) ,a ,@(list b)))",
            )
            .unwrap();
        assert_eq!(goos.format_object(&result).unwrap(), "(foo12 foo 12)");
    }

    #[test]
    fn supports_parallel_and_sequential_let() {
        let mut goos = Interpreter::new().unwrap();
        let parallel = goos
            .eval_text("(let ((x 1)) (let ((x 2) (y x)) y))")
            .unwrap();
        let sequential = goos
            .eval_text("(let ((x 1)) (let* ((x 2) (y x)) y))")
            .unwrap();
        assert_eq!(integer(parallel), 1);
        assert_eq!(integer(sequential), 2);
    }

    #[test]
    fn standard_library_letrec_supports_mutual_recursion() {
        let mut goos = Interpreter::new().unwrap();
        let result = goos
            .eval_text(
                "(letrec ((even? (lambda (n)\n\
                                   (if (= n 0) #t (odd? (- n 1)))))\n\
                           (odd? (lambda (n)\n\
                                  (if (= n 0) #f (even? (- n 1))))))\n\
                   (even? 10))",
            )
            .unwrap();
        assert_eq!(result.value, Value::Symbol(goos.core.true_symbol));
    }

    #[test]
    fn mutable_pairs_and_hash_tables_hold_objects() {
        let mut goos = Interpreter::new().unwrap();
        let result = goos
            .eval_text(
                "(let ((pair (cons 1 2))\n\
                        (table (make-string-hash-table)))\n\
                   (set-car! pair 3)\n\
                   (set-cdr! pair 4)\n\
                   (hash-table-set! table 'key pair)\n\
                   (list pair (hash-table-try-ref table \"key\")))",
            )
            .unwrap();
        assert_eq!(goos.format_object(&result).unwrap(), "((3 . 4) (#t 3 . 4))");
    }

    #[test]
    fn evaluation_errors_retain_source_spans() {
        let mut goos = Interpreter::new().unwrap();
        let diagnostic = goos.eval_text("(car 1)").unwrap_err();
        let rendered = goos.parser.sources.render_diagnostic(&diagnostic).unwrap();
        assert!(rendered.contains("expected a pair"));
        assert!(rendered.contains("<eval:"));
        assert!(rendered.contains("(car 1)"));
    }

    #[test]
    fn garbage_collection_traces_closures_and_reclaims_call_frames() {
        let mut goos = Interpreter::new().unwrap();
        goos.eval_text(
            "(define (make-closure value) (lambda () value))\n\
             (define kept-closure (make-closure 7))\n\
             (define kept-table (make-string-hash-table))\n\
             (hash-table-set! kept-table \"value\" '(kept pair))\n\
             ((lambda (value) value) '(temporary list))",
        )
        .unwrap();
        let stats = goos.collect_garbage(&[]).unwrap();
        assert!(stats.environments_freed > 0);
        assert!(stats.cons_freed > 0);
        assert_eq!(integer(goos.eval_text("(kept-closure)").unwrap()), 7);
        let retained = goos
            .eval_text("(cdr (hash-table-try-ref kept-table \"value\"))")
            .unwrap();
        assert_eq!(goos.format_object(&retained).unwrap(), "(kept pair)");
    }
}
