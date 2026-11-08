use std::collections::HashMap;
use std::hash::{Hash, Hasher};

use crate::diagnostic::Diagnostic;
use crate::driver::FileId;
use crate::frontend_parse::{KeywordSpec, ListCursor, ParsedArgument, SyntaxType};
use crate::goos::Interpreter;
use crate::ir::{
    ARGUMENT_REGISTERS, ArithmeticOperation, BranchCondition, ConditionalMoveOperation,
    Cop0Register, DoublewordPart, FunctionId, Gpr, IntegerBitwiseOperation, IntegerComparison,
    IrInstruction, LabelId, LabelTable, MemoryLoad, PhysicalRegister, Register, ShiftOperation,
    VirtualRegister, VirtualRegisterId,
};
use crate::object::{Object, ObjectStore, SymbolId, SymbolMap, Value as ObjectValue, fnv64};
use crate::span::{SourceId, SpanInfo};
use crate::static_data::{
    FixupTarget, PoolEntry, PoolEntryKind, Segment, StaticData, StaticFixup, StaticLabelId,
};
use crate::symbol_table::{EMPTY_PAIR_OFFSET, TRUE_OFFSET};
use crate::type_system::{
    BUILTIN_FILE_ID, BitFieldDeclaration, FieldDeclaration, MethodDeclaration, OBJECT_METHOD_COUNT,
    PrintAs, RegisterClass, Type, TypeDefinition, TypeKind, TypeLayout, TypeSpec, TypeSystem,
    Visibility,
};
use crate::value::{LoadInfo, Place, PlaceBase, PlaceProjection, Value, ValueKind};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ExpressionPosition {
    Value,
    Statement,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FunctionIdentity {
    TopLevel,
    Global(SymbolId),
    Method { owner: Type, name: SymbolId },
    Anonymous { parent: FunctionId, ordinal: u32 },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RegisterReservation {
    pub register: VirtualRegisterId,
    pub start: usize,
    pub end: usize,
}

#[derive(Debug)]
pub struct FunctionData {
    pub identity: FunctionIdentity,
    pub parent: Option<FunctionId>,
    pub segment: Segment,
    pub return_type: TypeSpec,
    pub ir: Vec<IrInstruction>,
    pub registers: Vec<VirtualRegister>,
    pub single_web_registers: Vec<VirtualRegisterId>,
    pub register_reservations: Vec<RegisterReservation>,
    pub labels: LabelTable,
    uses_frame_pointer: bool,
    next_register_number: [u32; RegisterClass::REGISTER_FILE_COUNT],
}

impl FunctionData {
    pub fn new(
        identity: FunctionIdentity,
        parent: Option<FunctionId>,
        segment: Segment,
        return_type: TypeSpec,
    ) -> Self {
        Self {
            identity,
            parent,
            segment,
            return_type,
            ir: Vec::new(),
            registers: Vec::new(),
            single_web_registers: Vec::new(),
            register_reservations: Vec::new(),
            labels: LabelTable::default(),
            uses_frame_pointer: false,
            next_register_number: [0; RegisterClass::REGISTER_FILE_COUNT],
        }
    }

    pub fn allocate_register(
        &mut self,
        ty: TypeSpec,
        class: RegisterClass,
    ) -> Result<VirtualRegisterId, String> {
        let id = VirtualRegisterId(
            u32::try_from(self.registers.len()).map_err(|_| "exhausted virtual register IDs")?,
        );
        let class_index = class.register_file();
        let number = self.next_register_number[class_index];
        self.next_register_number[class_index] = number
            .checked_add(1)
            .ok_or_else(|| "exhausted virtual register numbers".to_owned())?;
        self.registers.push(VirtualRegister { ty, class, number });
        Ok(id)
    }

    pub fn allocate_gpr(&mut self, ty: TypeSpec) -> Result<VirtualRegisterId, String> {
        self.allocate_register(ty, RegisterClass::Gpr)
    }

    pub fn allocate_label(&mut self) -> Result<LabelId, String> {
        self.labels.allocate()
    }

    pub fn bind_label(&mut self, label: LabelId) -> Result<(), String> {
        self.labels.bind(label, self.ir.len())
    }

    pub fn emit(&mut self, instruction: IrInstruction) {
        self.uses_frame_pointer |= instruction.requires_frame_pointer();
        self.ir.push(instruction);
    }

    pub fn emit_move(&mut self, destination: VirtualRegisterId, source: impl Into<Register>) {
        self.emit(IrInstruction::move_register(destination, source));
    }

    pub fn emit_branch_likely(&mut self, condition: BranchCondition, target: LabelId) {
        self.emit(IrInstruction::branch_likely(condition, target));
    }

    pub fn emit_variable_shift(
        &mut self,
        operation: ShiftOperation,
        destination: VirtualRegisterId,
        value: impl Into<Register>,
        amount: impl Into<Register>,
    ) {
        self.emit(IrInstruction::variable_shift(
            operation,
            destination,
            value,
            amount,
        ));
    }

    pub fn emit_integer_subtract(
        &mut self,
        destination: VirtualRegisterId,
        left: impl Into<Register>,
        right: impl Into<Register>,
    ) {
        self.emit(IrInstruction::integer_subtract(destination, left, right));
    }

    pub fn emit_integer_compare(
        &mut self,
        comparison: IntegerComparison,
        signed: bool,
        destination: VirtualRegisterId,
        left: impl Into<Register>,
        right: impl Into<Register>,
    ) {
        self.emit(IrInstruction::integer_compare(
            comparison,
            signed,
            destination,
            left,
            right,
        ));
    }

    pub fn emit_integer_bitwise(
        &mut self,
        operation: IntegerBitwiseOperation,
        destination: VirtualRegisterId,
        left: impl Into<Register>,
        right: impl Into<Register>,
    ) {
        self.emit(IrInstruction::integer_bitwise(
            operation,
            destination,
            left,
            right,
        ));
    }

    pub fn emit_conditional_move(
        &mut self,
        operation: ConditionalMoveOperation,
        destination: impl Into<Register>,
        source: impl Into<Register>,
        condition: impl Into<Register>,
    ) {
        self.emit(IrInstruction::conditional_move(
            operation,
            destination,
            source,
            condition,
        ));
    }

    pub fn uses_frame_pointer(&self) -> bool {
        self.uses_frame_pointer
    }
}

#[derive(Clone, Copy, Debug)]
struct FloatConstant(f32);

impl PartialEq for FloatConstant {
    fn eq(&self, other: &Self) -> bool {
        self.0.to_bits() == other.0.to_bits()
    }
}

impl Eq for FloatConstant {}

impl Hash for FloatConstant {
    fn hash<H: Hasher>(&self, state: &mut H) {
        state.write_u32(self.0.to_bits());
    }
}

#[derive(Debug)]
pub struct FileContext {
    pub file_id: FileId,
    pub source_id: SourceId,
    pub visibility: Visibility,
    pub functions: Vec<FunctionData>,
    pub static_data: StaticData,
    next_anonymous_function: u32,
    float_constants: HashMap<FloatConstant, StaticLabelId>,
    u64_constants: HashMap<(Segment, u64), StaticLabelId>,
    strings: HashMap<(Segment, String), StaticLabelId>,
}

impl FileContext {
    pub fn new(file_id: FileId, source_id: SourceId, visibility: Visibility) -> Self {
        Self {
            file_id,
            source_id,
            visibility,
            functions: Vec::new(),
            static_data: StaticData::default(),
            next_anonymous_function: 0,
            float_constants: HashMap::new(),
            u64_constants: HashMap::new(),
            strings: HashMap::new(),
        }
    }

    fn reserve_function(
        &mut self,
        identity: FunctionIdentity,
        parent: Option<FunctionId>,
        segment: Segment,
        return_type: TypeSpec,
    ) -> Result<FunctionId, String> {
        let id =
            FunctionId(u32::try_from(self.functions.len()).map_err(|_| "exhausted function IDs")?);
        self.functions
            .push(FunctionData::new(identity, parent, segment, return_type));
        Ok(id)
    }

    fn allocate_anonymous_ordinal(&mut self) -> Result<u32, String> {
        let ordinal = self.next_anonymous_function;
        self.next_anonymous_function = self
            .next_anonymous_function
            .checked_add(1)
            .ok_or_else(|| "exhausted anonymous function ordinals".to_owned())?;
        Ok(ordinal)
    }

    fn intern_float_constant(&mut self, value: f32) -> Result<StaticLabelId, String> {
        let constant = FloatConstant(value);
        if let Some(label) = self.float_constants.get(&constant) {
            return Ok(*label);
        }
        let bytes = value.to_le_bytes();
        let offset = self.static_data.main.append(&bytes, 4)?;
        let label = self.static_data.add_label(Segment::Main, offset)?;
        self.static_data.add_pool_entry(PoolEntry {
            label,
            segment: Segment::Main,
            start: offset,
            end: offset + 4,
            alignment: 4,
            kind: PoolEntryKind::Float(value.to_bits()),
        })?;
        self.float_constants.insert(constant, label);
        Ok(label)
    }

    fn intern_u64_constant(
        &mut self,
        segment: Segment,
        value: u64,
    ) -> Result<StaticLabelId, String> {
        if let Some(label) = self.u64_constants.get(&(segment, value)) {
            return Ok(*label);
        }
        let section = self.static_data.section_mut(segment);
        let offset = section.append(&value.to_le_bytes(), 8)?;
        let label = self.static_data.add_label(segment, offset)?;
        self.static_data.add_pool_entry(PoolEntry {
            label,
            segment,
            start: offset,
            end: offset + 8,
            alignment: 8,
            kind: PoolEntryKind::Integer(value),
        })?;
        self.u64_constants.insert((segment, value), label);
        Ok(label)
    }

    fn intern_string(&mut self, segment: Segment, value: &str) -> Result<StaticLabelId, String> {
        let key = (segment, value.to_owned());
        if let Some(label) = self.strings.get(&key) {
            return Ok(*label);
        }
        let mut object = Vec::with_capacity(5 + value.len());
        object.extend_from_slice(
            &u32::try_from(value.len())
                .map_err(|_| "string constant exceeds u32".to_owned())?
                .to_le_bytes(),
        );
        object.extend_from_slice(value.as_bytes());
        object.push(0);
        object.resize((object.len() + 3) & !3, 0);

        let tag = self
            .static_data
            .section_mut(segment)
            .append(&u32::MAX.to_le_bytes(), 16)?;
        let pointer = tag
            .checked_add(4)
            .ok_or_else(|| "string constant offset overflow".to_owned())?;
        let offset = self.static_data.section_mut(segment).append(&object, 1)?;
        debug_assert_eq!(offset, pointer);
        let label = self.static_data.add_label(segment, pointer)?;
        self.static_data.add_fixup(StaticFixup {
            segment,
            offset: tag,
            target: FixupTarget::Type {
                name: SymbolId(fnv64(b"string")),
                methods: u8::try_from(OBJECT_METHOD_COUNT)
                    .map_err(|_| "object method count exceeds u8".to_owned())?,
            },
            addend: 0,
            span: None,
        })?;
        let end = u32::try_from(self.static_data.section(segment).len())
            .map_err(|_| "string pool exceeds u32".to_owned())?;
        self.static_data.add_pool_entry(PoolEntry {
            label,
            segment,
            start: tag,
            end,
            alignment: 16,
            kind: PoolEntryKind::String,
        })?;
        self.strings.insert(key, label);
        Ok(label)
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct ScopeId(pub u32);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct LexicalBinding {
    pub register: VirtualRegisterId,
}

#[derive(Debug)]
pub struct LexicalScope {
    pub parent: Option<ScopeId>,
    bindings: SymbolMap<LexicalBinding>,
}

impl LexicalScope {
    fn root() -> Self {
        Self {
            parent: None,
            bindings: SymbolMap::default(),
        }
    }

    fn child(parent: ScopeId) -> Self {
        Self {
            parent: Some(parent),
            bindings: SymbolMap::default(),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ReturnTarget {
    pub label: LabelId,
    pub result: Option<VirtualRegisterId>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BlockContext {
    pub name: Option<SymbolId>,
    pub end_label: LabelId,
    pub result: Option<VirtualRegisterId>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MethodContext {
    pub owner: Type,
    pub name: SymbolId,
}

#[derive(Debug)]
pub struct FunctionContext {
    pub function: FunctionId,
    pub scopes: Vec<LexicalScope>,
    pub current_scope: ScopeId,
    pub blocks: Vec<BlockContext>,
    pub return_target: Option<ReturnTarget>,
    named_labels: SymbolMap<LabelId>,
    pub method: Option<MethodContext>,
    pub behavior: Option<TypeSpec>,
}

impl FunctionContext {
    fn new(function: FunctionId, identity: FunctionIdentity) -> Self {
        let method = match identity {
            FunctionIdentity::Method { owner, name } => Some(MethodContext { owner, name }),
            _ => None,
        };
        Self {
            function,
            scopes: vec![LexicalScope::root()],
            current_scope: ScopeId(0),
            blocks: Vec::new(),
            return_target: None,
            named_labels: SymbolMap::default(),
            method,
            behavior: None,
        }
    }

    pub fn push_scope(&mut self) -> Result<ScopeId, String> {
        let id = ScopeId(u32::try_from(self.scopes.len()).map_err(|_| "exhausted scope IDs")?);
        self.scopes.push(LexicalScope::child(self.current_scope));
        self.current_scope = id;
        Ok(id)
    }

    pub fn pop_scope(&mut self) -> Result<(), String> {
        let parent = self.scopes[self.current_scope.0 as usize]
            .parent
            .ok_or_else(|| "cannot pop a function's root lexical scope".to_owned())?;
        self.current_scope = parent;
        Ok(())
    }

    pub fn define_local(&mut self, name: SymbolId, binding: LexicalBinding) -> Result<(), String> {
        let scope = &mut self.scopes[self.current_scope.0 as usize];
        if scope.bindings.insert(name, binding).is_some() {
            return Err("local name is already bound in this lexical scope".to_owned());
        }
        Ok(())
    }

    pub fn lookup_local(&self, name: SymbolId) -> Option<LexicalBinding> {
        let mut scope = Some(self.current_scope);
        while let Some(id) = scope {
            let lexical_scope = &self.scopes[id.0 as usize];
            if let Some(binding) = lexical_scope.bindings.get(&name) {
                return Some(*binding);
            }
            scope = lexical_scope.parent;
        }
        None
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MacroExpansionFrame {
    pub name: SymbolId,
    pub definition: Option<SpanInfo>,
    pub invocation: Option<SpanInfo>,
}

#[derive(Debug)]
pub struct FrontendContext {
    pub file: FileContext,
    pub function_stack: Vec<FunctionContext>,
    pub macro_frames: Vec<MacroExpansionFrame>,
    constant_expansions: Vec<SymbolId>,
    none_type: TypeSpec,
}

impl FrontendContext {
    pub fn new(file: FileContext, none_type: TypeSpec) -> Self {
        Self {
            file,
            function_stack: Vec::new(),
            macro_frames: Vec::new(),
            constant_expansions: Vec::new(),
            none_type,
        }
    }

    pub fn begin_function(
        &mut self,
        identity: FunctionIdentity,
        segment: Segment,
        return_type: TypeSpec,
    ) -> Result<FunctionId, String> {
        let parent = self.function_stack.last().map(|context| context.function);
        let function = self
            .file
            .reserve_function(identity, parent, segment, return_type)?;
        self.function_stack
            .push(FunctionContext::new(function, identity));
        Ok(function)
    }

    pub fn begin_anonymous_function(
        &mut self,
        segment: Segment,
        return_type: TypeSpec,
    ) -> Result<FunctionId, String> {
        let parent = self
            .function_stack
            .last()
            .map(|context| context.function)
            .ok_or_else(|| "anonymous function has no enclosing function".to_owned())?;
        let ordinal = self.file.allocate_anonymous_ordinal()?;
        self.begin_function(
            FunctionIdentity::Anonymous { parent, ordinal },
            segment,
            return_type,
        )
    }

    pub fn end_function(&mut self) -> Result<FunctionId, String> {
        let function = self
            .function_stack
            .last()
            .map(|context| context.function)
            .ok_or_else(|| "there is no current function".to_owned())?;
        let data = &mut self.file.functions[function.0 as usize];
        if data.return_type == self.none_type {
            let result = data.allocate_register(self.none_type, RegisterClass::Gpr)?;
            data.emit(IrInstruction::Move {
                destination: Register::Virtual(result),
                source: Register::Physical(PhysicalRegister::ZERO),
                must_emit: false,
            });
            data.emit(IrInstruction::Return {
                value: Register::Virtual(result),
                register: PhysicalRegister::RET,
            });
        }
        self.function_stack.pop();
        Ok(function)
    }

    pub fn current_function(&self) -> Result<&FunctionContext, String> {
        self.function_stack
            .last()
            .ok_or_else(|| "there is no current function".to_owned())
    }

    pub fn current_function_mut(&mut self) -> Result<&mut FunctionContext, String> {
        self.function_stack
            .last_mut()
            .ok_or_else(|| "there is no current function".to_owned())
    }

    pub fn current_function_data(&self) -> Result<&FunctionData, String> {
        let function = self.current_function()?.function;
        Ok(&self.file.functions[function.0 as usize])
    }

    pub fn current_function_data_mut(&mut self) -> Result<&mut FunctionData, String> {
        let function = self.current_function()?.function;
        Ok(&mut self.file.functions[function.0 as usize])
    }

    fn named_label(&mut self, name: SymbolId) -> Result<LabelId, String> {
        if let Some(label) = self.current_function()?.named_labels.get(&name) {
            return Ok(*label);
        }
        let label = self.current_function_data_mut()?.allocate_label()?;
        self.current_function_mut()?
            .named_labels
            .insert(name, label);
        Ok(label)
    }

    fn constant_u64(&mut self, ty: TypeSpec, value: u64) -> Result<Register, String> {
        let segment = self.current_function_data()?.segment;
        let label = self.file.intern_u64_constant(segment, value)?;
        let destination = self.current_function_data_mut()?.allocate_gpr(ty)?;
        self.current_function_data_mut()?
            .emit(IrInstruction::LoadConstant64 {
                destination: destination.into(),
                label,
            });
        Ok(destination.into())
    }

    fn call(&mut self, callee: Register, arguments: &[Register]) -> Result<(), String> {
        if arguments.len() > ARGUMENT_REGISTERS.len() {
            return Err(format!(
                "call has {} arguments, but GOAL supports at most {}",
                arguments.len(),
                ARGUMENT_REGISTERS.len()
            ));
        }
        let data = self.current_function_data_mut()?;
        data.emit(IrInstruction::Move {
            destination: Register::Physical(PhysicalRegister::CALL),
            source: callee,
            must_emit: false,
        });
        for (source, destination) in arguments.iter().zip(ARGUMENT_REGISTERS) {
            data.emit(IrInstruction::Move {
                destination: Register::Physical(destination),
                source: *source,
                must_emit: false,
            });
        }
        data.emit(IrInstruction::Call {
            argument_count: arguments.len() as u8,
        });
        data.emit(IrInstruction::CallDelay);
        Ok(())
    }

    pub fn finish(self) -> Result<FileContext, String> {
        if !self.function_stack.is_empty() {
            return Err("cannot finish a file while compiling a function".to_owned());
        }
        Ok(self.file)
    }

    pub fn add_macro_context(&self, mut diagnostic: Diagnostic) -> Diagnostic {
        for frame in self.macro_frames.iter().rev() {
            diagnostic = diagnostic
                .with_context("expanded from this GOAL macro invocation", frame.invocation);
            if frame.definition.is_some() {
                diagnostic = diagnostic.with_context("GOAL macro defined here", frame.definition);
            }
        }
        diagnostic
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct GlobalDefinition {
    pub owner: FileId,
    pub name: SymbolId,
    pub ty: TypeSpec,
    pub documentation: Option<String>,
    pub span: Option<SpanInfo>,
}

#[derive(Clone, Debug)]
struct ConstantDefinition {
    owner: FileId,
    value: Object,
}

#[derive(Clone, Debug)]
struct GoalMacroDefinition {
    owner: FileId,
    callable: Object,
    definition: Option<SpanInfo>,
}

pub struct FormCompileContext<'a> {
    pub types: &'a mut TypeSystem,
    pub goos: &'a mut Interpreter,
    pub context: &'a mut FrontendContext,
}

pub type FormHandler = for<'a> fn(
    &mut Frontend,
    &Object,
    ListCursor,
    ExpressionPosition,
    &mut FormCompileContext<'a>,
) -> Result<Value, Diagnostic>;

#[derive(Debug)]
pub struct Frontend {
    handlers: SymbolMap<FormHandler>,
    globals: SymbolMap<Vec<GlobalDefinition>>,
    constants: SymbolMap<Vec<ConstantDefinition>>,
    macros: SymbolMap<Vec<GoalMacroDefinition>>,
    none_type: TypeSpec,
}

impl Frontend {
    pub fn new(types: &mut TypeSystem) -> Self {
        let none_type = types.intern_type_spec("none");
        let type_type = types.intern_type_spec("type");
        let mut result = Self {
            handlers: SymbolMap::default(),
            globals: SymbolMap::default(),
            constants: SymbolMap::default(),
            macros: SymbolMap::default(),
            none_type,
        };
        result.register_form(SymbolId(fnv64(b"defun")), compile_defun);
        result.register_form(SymbolId(fnv64(b"defmethod")), compile_defmethod);
        result.register_form(SymbolId(fnv64(b"defmacro")), compile_defmacro);
        result.register_form(SymbolId(fnv64(b"define-extern")), compile_define_extern);
        result.register_form(SymbolId(fnv64(b"define")), compile_define);
        result.register_form(SymbolId(fnv64(b"defconstant")), compile_defconstant);
        result.register_form(SymbolId(fnv64(b"deftype")), compile_deftype);
        result.register_form(SymbolId(fnv64(b"none")), compile_none_form);
        for ty in types.builtin_types() {
            result
                .globals
                .entry(ty.symbol())
                .or_default()
                .push(GlobalDefinition {
                    owner: BUILTIN_FILE_ID,
                    name: ty.symbol(),
                    ty: type_type,
                    documentation: None,
                    span: None,
                });
        }
        result
    }

    pub fn register_form(&mut self, symbol: SymbolId, handler: FormHandler) {
        self.handlers.insert(symbol, handler);
    }

    pub fn form_handler(&self, symbol: SymbolId) -> Option<FormHandler> {
        self.handlers.get(&symbol).copied()
    }

    pub fn global_definition(
        &self,
        name: SymbolId,
        visibility: &Visibility,
    ) -> Result<Option<&GlobalDefinition>, String> {
        let Some(definition) = self
            .globals
            .get(&name)
            .and_then(|definitions| definitions.last())
        else {
            return Ok(None);
        };
        if !visibility.can_see(definition.owner) {
            return Err(format!(
                "latest definition of global symbol {:#x} belongs to file {}, which is not visible here",
                name.0, definition.owner
            ));
        }
        Ok(Some(definition))
    }

    fn macro_definition(
        &self,
        name: SymbolId,
        visibility: &Visibility,
    ) -> Result<Option<&GoalMacroDefinition>, String> {
        let Some(definition) = self
            .macros
            .get(&name)
            .and_then(|definitions| definitions.last())
        else {
            return Ok(None);
        };
        if !visibility.can_see(definition.owner) {
            return Err(format!(
                "latest definition of macro {:#x} belongs to file {}, which is not visible here",
                name.0, definition.owner
            ));
        }
        Ok(Some(definition))
    }

    fn constant_definition(
        &self,
        name: SymbolId,
        visibility: &Visibility,
    ) -> Result<Option<&ConstantDefinition>, String> {
        let Some(definition) = self
            .constants
            .get(&name)
            .and_then(|definitions| definitions.last())
        else {
            return Ok(None);
        };
        if !visibility.can_see(definition.owner) {
            return Err(format!(
                "latest definition of constant {:#x} belongs to file {}, which is not visible here",
                name.0, definition.owner
            ));
        }
        Ok(Some(definition))
    }

    pub fn remove_definitions(&mut self, owner: FileId) {
        self.remove_definitions_batch([owner]);
    }

    pub fn remove_definitions_batch(&mut self, owners: impl IntoIterator<Item = FileId>) {
        let mut removed = Vec::<u64>::new();
        for owner in owners {
            let word = owner as usize / 64;
            removed.resize(removed.len().max(word + 1), 0);
            removed[word] |= 1 << (owner % 64);
        }
        let is_removed = |owner: FileId| {
            removed
                .get(owner as usize / 64)
                .is_some_and(|word| word & (1 << (owner % 64)) != 0)
        };
        self.globals.retain(|_, definitions| {
            definitions.retain(|definition| !is_removed(definition.owner));
            !definitions.is_empty()
        });
        self.constants.retain(|_, definitions| {
            definitions.retain(|definition| !is_removed(definition.owner));
            !definitions.is_empty()
        });
        self.macros.retain(|_, definitions| {
            definitions.retain(|definition| !is_removed(definition.owner));
            !definitions.is_empty()
        });
    }

    pub fn goos_roots(&self) -> impl Iterator<Item = &Object> {
        self.macros
            .values()
            .flatten()
            .map(|definition| &definition.callable)
            .chain(
                self.constants
                    .values()
                    .flatten()
                    .map(|definition| &definition.value),
            )
    }

    pub fn compile_file(
        &mut self,
        file_id: FileId,
        source_id: SourceId,
        visibility: Visibility,
        types: &mut TypeSystem,
        goos: &mut Interpreter,
        forms: &[Object],
    ) -> Result<FileContext, Diagnostic> {
        let builtin_type_names = types
            .builtin_types()
            .map(|ty| types.type_name(ty).to_owned())
            .collect::<Vec<_>>();
        for name in builtin_type_names {
            goos.parser.objects.symbols.intern(&name);
        }
        let result = (|| {
            let mut context = FrontendContext::new(
                FileContext::new(file_id, source_id, visibility),
                self.none_type,
            );
            context
                .begin_function(
                    FunctionIdentity::TopLevel,
                    Segment::TopLevel,
                    self.none_type,
                )
                .map_err(Diagnostic::error)?;
            for form in forms {
                self.compile_top_level_form(types, goos, &mut context, form)?;
            }
            context.end_function().map_err(Diagnostic::error)?;
            context.finish().map_err(Diagnostic::error)
        })();
        if result.is_err() {
            self.remove_definitions(file_id);
            types.remove_definitions(file_id);
        }
        result
    }

    fn compile_top_level_form(
        &mut self,
        types: &mut TypeSystem,
        goos: &mut Interpreter,
        context: &mut FrontendContext,
        form: &Object,
    ) -> Result<(), Diagnostic> {
        let (head, rest) = form_parts(&goos.parser.objects, form)?;
        if goos.parser.objects.symbols.resolve(head) == Some("begin") {
            for nested in call_arguments(&goos.parser.objects, form, rest)? {
                self.compile_top_level_form(types, goos, context, &nested)?;
            }
            return Ok(());
        }
        if let Some(definition) = self
            .macro_definition(head, &context.file.visibility)
            .at(form)?
            .cloned()
        {
            let arguments = call_arguments(&goos.parser.objects, form, rest)?;
            let environment = goos.goal_environment();
            let expanded = goos
                .invoke_macro(&definition.callable, &arguments, environment)
                .map_err(|diagnostic| {
                    macro_diagnostic(diagnostic, form.span, definition.definition)
                })?;
            return self
                .compile_top_level_form(types, goos, context, &expanded)
                .map_err(|diagnostic| {
                    macro_diagnostic(diagnostic, form.span, definition.definition)
                });
        }
        let handler = self.form_handler(head).ok_or_else(|| {
            let name = goos
                .parser
                .objects
                .symbols
                .resolve(head)
                .unwrap_or("<unknown-form>");
            diagnostic_at(form, format!("GOAL form `{name}` is not implemented"))
        })?;
        let mut handler_context = FormCompileContext {
            types,
            goos,
            context,
        };
        handler(
            self,
            form,
            ListCursor::new(head, form.span, rest),
            ExpressionPosition::Statement,
            &mut handler_context,
        )?;
        Ok(())
    }
}

fn compile_define_extern(
    frontend: &mut Frontend,
    form: &Object,
    mut cursor: ListCursor,
    _: ExpressionPosition,
    handler: &mut FormCompileContext<'_>,
) -> Result<Value, Diagnostic> {
    let objects = &handler.goos.parser.objects;
    let name_argument = cursor.required(objects, "name")?;
    let name = name_argument.symbol(objects)?;
    let definition_span = name_argument.object().span.or(form.span);
    let mut type_argument = cursor.required(objects, "type")?;
    let documentation = if let ObjectValue::String(documentation) = &type_argument.object().value {
        let documentation = documentation.clone();
        type_argument = cursor.required(objects, "type")?;
        Some(documentation)
    } else {
        None
    };
    cursor.finish(objects)?;
    let ty = parse_type_spec(
        handler.types,
        objects,
        &handler.context.file.visibility,
        type_argument.object(),
    )?;
    check_global_redefinition(frontend, handler.types, name, ty, handler.context, form)?;
    frontend
        .globals
        .entry(name)
        .or_default()
        .push(GlobalDefinition {
            owner: handler.context.file.file_id,
            name,
            ty,
            documentation,
            span: definition_span,
        });
    Ok(Value::new(frontend.none_type, ValueKind::None))
}

fn compile_define(
    frontend: &mut Frontend,
    form: &Object,
    mut cursor: ListCursor,
    _: ExpressionPosition,
    handler: &mut FormCompileContext<'_>,
) -> Result<Value, Diagnostic> {
    let (name, definition_span, documentation, expression) = {
        let objects = &handler.goos.parser.objects;
        let name_argument = cursor.required(objects, "name")?;
        let name = name_argument.symbol(objects)?;
        let definition_span = name_argument.object().span.or(form.span);
        let mut value_argument = cursor.required(objects, "value")?;
        let documentation =
            if let ObjectValue::String(documentation) = &value_argument.object().value {
                let documentation = documentation.clone();
                value_argument = cursor.required(objects, "value")?;
                Some(documentation)
            } else {
                None
            };
        let expression = value_argument.into_object();
        cursor.finish(objects)?;
        (name, definition_span, documentation, expression)
    };
    let mut value = compile_expression(
        frontend,
        handler.types,
        handler.goos,
        handler.context,
        &expression,
    )?;
    let register = flush_gpr_operand(handler.context, &expression, &mut value)?;
    check_global_redefinition(
        frontend,
        handler.types,
        name,
        value.ty,
        handler.context,
        form,
    )?;
    frontend
        .globals
        .entry(name)
        .or_default()
        .push(GlobalDefinition {
            owner: handler.context.file.file_id,
            name,
            ty: value.ty,
            documentation,
            span: definition_span,
        });
    handler
        .context
        .current_function_data_mut()
        .at(form)?
        .emit(IrInstruction::StoreSymbol {
            source: register,
            symbol: name,
        });
    Ok(value)
}

fn compile_defconstant(
    frontend: &mut Frontend,
    form: &Object,
    mut cursor: ListCursor,
    _: ExpressionPosition,
    handler: &mut FormCompileContext<'_>,
) -> Result<Value, Diagnostic> {
    let objects = &handler.goos.parser.objects;
    let name = cursor.required(objects, "name")?.symbol(objects)?;
    let value = cursor.required(objects, "value")?.into_object();
    cursor.finish(objects)?;
    if frontend
        .global_definition(name, &handler.context.file.visibility)
        .at(form)?
        .is_some()
    {
        return Err(diagnostic_at(
            form,
            "constant name is already used by a global",
        ));
    }
    frontend
        .constants
        .entry(name)
        .or_default()
        .push(ConstantDefinition {
            owner: handler.context.file.file_id,
            value,
        });
    Ok(Value::new(frontend.none_type, ValueKind::None))
}

fn compile_none_form(
    frontend: &mut Frontend,
    _: &Object,
    cursor: ListCursor,
    _: ExpressionPosition,
    handler: &mut FormCompileContext<'_>,
) -> Result<Value, Diagnostic> {
    cursor.finish(&handler.goos.parser.objects)?;
    Ok(Value::new(frontend.none_type, ValueKind::None))
}

fn check_global_redefinition(
    frontend: &Frontend,
    types: &TypeSystem,
    name: SymbolId,
    ty: TypeSpec,
    context: &FrontendContext,
    form: &Object,
) -> Result<(), Diagnostic> {
    if let Some(existing) = frontend
        .global_definition(name, &context.file.visibility)
        .at(form)?
        && existing.ty != ty
    {
        let name = types.symbol_name(name).unwrap_or("<unknown-symbol>");
        return Err(diagnostic_at(
            form,
            format!(
                "global `{name}` was previously defined as {}, but this definition has type {}",
                types.format_type_spec(existing.ty),
                types.format_type_spec(ty)
            ),
        ));
    }
    Ok(())
}

fn parse_type_spec(
    types: &mut TypeSystem,
    objects: &ObjectStore,
    visibility: &Visibility,
    object: &Object,
) -> Result<TypeSpec, Diagnostic> {
    match object.value {
        ObjectValue::Symbol(symbol) => {
            let name = objects
                .symbols
                .resolve(symbol)
                .ok_or_else(|| diagnostic_at(object, "type has an unknown symbol ID"))?;
            let spec = types.intern_type_spec(name);
            if !types.is_declared(types.type_spec_base(spec), visibility) {
                return Err(diagnostic_at(object, format!("unknown type `{name}`")));
            }
            Ok(spec)
        }
        ObjectValue::Pair(_) => {
            let (base, rest) = form_parts(objects, object)?;
            let base_name = objects
                .symbols
                .resolve(base)
                .ok_or_else(|| diagnostic_at(object, "type has an unknown symbol ID"))?;
            if base_name == "const" {
                let mut arguments = ListCursor::new(base, object.span, rest);
                let result = parse_type_spec(
                    types,
                    objects,
                    visibility,
                    arguments.required(objects, "qualified type")?.object(),
                )?;
                arguments.finish(objects)?;
                return Ok(result);
            }
            let base = types.intern_type(base_name);
            if !types.is_declared(base, visibility) {
                return Err(diagnostic_at(object, format!("unknown type `{base_name}`")));
            }
            let arguments = ListCursor::new(base.symbol(), object.span, rest)
                .remaining(objects, "type argument")?
                .into_iter()
                .map(|argument| parse_type_spec(types, objects, visibility, argument.object()))
                .collect::<Result<Vec<_>, _>>()?;
            Ok(types.intern_compound_type_spec(base, arguments, None))
        }
        _ => Err(diagnostic_at(object, "expected a type specification")),
    }
}

fn compile_deftype(
    frontend: &mut Frontend,
    form: &Object,
    mut cursor: ListCursor,
    _: ExpressionPosition,
    handler: &mut FormCompileContext<'_>,
) -> Result<Value, Diagnostic> {
    let FormCompileContext {
        types,
        goos,
        context,
    } = handler;
    let objects = &goos.parser.objects;

    let name_argument = cursor.required(objects, "name")?;
    let name_symbol = name_argument.symbol(objects)?;
    let name_span = name_argument.object().span.or(form.span);
    let name_text = objects
        .symbols
        .resolve(name_symbol)
        .ok_or_else(|| diagnostic_at(name_argument.object(), "type has an unknown symbol ID"))?;
    let name = types.intern_type(name_text);

    let mut parents = cursor
        .required_syntax(objects, "parents", crate::frontend_parse::SyntaxType::List)?
        .list_cursor(objects)?;
    let parent_argument = parents.required(objects, "parent")?;
    let parent_symbol = parent_argument.symbol(objects)?;
    parents.finish(objects)?;
    let parent_text = objects.symbols.resolve(parent_symbol).ok_or_else(|| {
        diagnostic_at(parent_argument.object(), "parent has an unknown symbol ID")
    })?;
    let parent = types.intern_type(parent_text);
    let parent_spec = types.intern_type_spec(parent_text);

    let fields = cursor
        .required_syntax(objects, "fields", crate::frontend_parse::SyntaxType::List)?
        .list_cursor(objects)?
        .remaining(objects, "field")?;
    let options = cursor.remaining(objects, "option")?;
    let mut documentation = None;
    let mut methods = Vec::new();
    let mut options = options.into_iter();
    while let Some(option) = options.next() {
        if let ObjectValue::Symbol(option_name) = option.object().value {
            match objects.symbols.resolve(option_name) {
                Some(":documentation") => {
                    let value = options.next().ok_or_else(|| {
                        diagnostic_at(option.object(), ":documentation requires a string")
                    })?;
                    documentation = Some(value.string(objects)?.to_owned());
                }
                Some(name) => {
                    return Err(diagnostic_at(
                        option.object(),
                        format!("deftype option `{name}` is not implemented yet"),
                    ));
                }
                None => return Err(diagnostic_at(option.object(), "unknown deftype option")),
            }
            continue;
        }

        let option_object = option.object().clone();
        let option_span = option_object.span;
        let mut option_cursor = option.list_cursor(objects)?;
        let option_name = option_cursor
            .required(objects, "option name")?
            .symbol(objects)?;
        match objects.symbols.resolve(option_name) {
            Some(":documentation") => {
                let value = option_cursor.required(objects, "documentation")?;
                documentation = Some(value.string(objects)?.to_owned());
                option_cursor.finish(objects)?;
            }
            Some(":methods") => {
                for method in option_cursor.remaining(objects, "method")? {
                    let method_span = method.object().span.or(option_span);
                    let mut method = method.list_cursor(objects)?;
                    let method_name = method.required(objects, "method name")?.symbol(objects)?;
                    let parameters = method
                        .required_syntax(
                            objects,
                            "parameters",
                            crate::frontend_parse::SyntaxType::List,
                        )?
                        .list_cursor(objects)?
                        .remaining(objects, "parameter type")?
                        .into_iter()
                        .map(|parameter| {
                            parse_type_spec(
                                types,
                                objects,
                                &context.file.visibility,
                                parameter.object(),
                            )
                        })
                        .collect::<Result<Vec<_>, _>>()?;
                    let result = method.required(objects, "result type")?;
                    let result =
                        parse_type_spec(types, objects, &context.file.visibility, result.object())?;
                    method.finish(objects)?;
                    let mut declaration = MethodDeclaration::new(
                        method_name,
                        types.function_type(parameters, result),
                    );
                    declaration.span = method_span;
                    methods.push(declaration);
                }
            }
            Some(name) => {
                return Err(diagnostic_at(
                    &option_object,
                    format!("deftype option `{name}` is not implemented yet"),
                ));
            }
            None => return Err(diagnostic_at(&option_object, "unknown deftype option")),
        }
    }

    let integer = types.intern_type_spec("integer");
    let is_bit_field = types
        .is_subtype(integer, parent_spec, &context.file.visibility)
        .at(parent_argument.object())?;
    let parent_layout = types
        .layout(parent, &context.file.visibility)
        .at(parent_argument.object())?;
    let layout = if is_bit_field {
        TypeLayout::bit_field(
            parent_layout.size,
            parent_layout.alignment,
            parent_layout.load_signed,
        )
    } else if matches!(parent_layout.kind, TypeKind::Structure | TypeKind::Basic) {
        TypeLayout::structure(parent_layout.kind, parent_layout.boxed)
    } else {
        return Err(diagnostic_at(
            parent_argument.object(),
            "deftype parent must be an integer, structure, or basic type",
        ));
    };
    let mut definition = TypeDefinition::new(name, Some(parent), layout);
    definition.span = form.span;
    definition.options.documentation = documentation.clone();
    for method in methods {
        definition.add_method(method);
    }

    for field_argument in fields {
        let field_span = field_argument.object().span;
        let mut field = field_argument.list_cursor(objects)?;
        let field_name = field.required(objects, "field name")?.symbol(objects)?;
        let field_type_argument = field.required(objects, "field type")?;
        let field_type = parse_type_spec(
            types,
            objects,
            &context.file.visibility,
            field_type_argument.object(),
        )?;
        if is_bit_field {
            field.finish(objects)?;
            let mut declaration = BitFieldDeclaration::new(field_name, field_type);
            declaration.span = field_span;
            definition.add_bit_field(declaration);
        } else {
            let mut declaration = FieldDeclaration::new(field_name, field_type);
            declaration.span = field_span;
            if let Some(array_size) = field.optional(objects, "array size")? {
                declaration.array_size =
                    Some(u32::try_from(array_size.integer(objects)?).map_err(|_| {
                        diagnostic_at(array_size.object(), "array size must fit in a u32")
                    })?);
            }
            let options = field.remaining(objects, "field option")?;
            let mut options = options.into_iter();
            while let Some(option) = options.next() {
                let option_name = option.symbol(objects)?;
                match objects.symbols.resolve(option_name) {
                    Some(":dynamic") => declaration.dynamic = true,
                    Some(":offset") => {
                        let value = options.next().ok_or_else(|| {
                            diagnostic_at(option.object(), ":offset requires a byte offset")
                        })?;
                        declaration.offset =
                            Some(u32::try_from(value.integer(objects)?).map_err(|_| {
                                diagnostic_at(value.object(), "field offset must fit in a u32")
                            })?);
                    }
                    Some(":align") => {
                        let value = options.next().ok_or_else(|| {
                            diagnostic_at(option.object(), ":align requires a bit alignment")
                        })?;
                        declaration.align_bits =
                            Some(u32::try_from(value.integer(objects)?).map_err(|_| {
                                diagnostic_at(value.object(), "field alignment must fit in a u32")
                            })?);
                    }
                    Some(":print-as") => {
                        let value = options.next().ok_or_else(|| {
                            diagnostic_at(option.object(), ":print-as requires a formatter")
                        })?;
                        let formatter = value.symbol(objects)?;
                        declaration.print_as = match objects.symbols.resolve(formatter) {
                            Some("hex") => PrintAs::Hex,
                            Some("nothing") => PrintAs::Nothing,
                            Some("meters") => PrintAs::Meters,
                            Some("deg") => PrintAs::Degrees,
                            Some(name) => PrintAs::Type(types.intern_type(name)),
                            None => {
                                return Err(diagnostic_at(
                                    value.object(),
                                    "unknown :print-as formatter",
                                ));
                            }
                        };
                    }
                    Some(name) => {
                        return Err(diagnostic_at(
                            option.object(),
                            format!("field option `{name}` is not implemented yet"),
                        ));
                    }
                    None => {
                        return Err(diagnostic_at(option.object(), "unknown field option"));
                    }
                }
            }
            definition.add_field(declaration);
        }
    }

    let type_type = types.intern_type_spec("type");
    check_global_redefinition(frontend, types, name_symbol, type_type, context, form)?;
    let (layout, method_count, inspect_fields) = {
        let definition = types
            .define_type(context.file.file_id, definition, &context.file.visibility)
            .at(form)?;
        (
            definition.layout,
            definition.method_count,
            definition
                .fields
                .iter()
                .filter(|field| field.print_as != PrintAs::Nothing)
                .map(|field| (field.name, field.field_type, field.print_as.clone()))
                .chain(
                    definition
                        .bit_fields
                        .iter()
                        .filter(|field| field.print_as != PrintAs::Nothing)
                        .map(|field| (field.name, field.field_type, field.print_as.clone())),
                )
                .collect::<Vec<_>>(),
        )
    };
    frontend
        .globals
        .entry(name_symbol)
        .or_default()
        .push(GlobalDefinition {
            owner: context.file.file_id,
            name: name_symbol,
            ty: type_type,
            documentation,
            span: name_span,
        });

    let int_type = types.intern_type_spec("int");
    let new_name = types.intern_symbol("new");
    let size = u16::try_from(layout.size)
        .map_err(|_| diagnostic_at(form, "type size exceeds the runtime type flag field"))?;
    let flags = u64::from(size) | (u64::from(method_count) << 32);

    // Keep runtime type construction in the same visible order as the generated code.
    let mut new_method =
        compile_get_method_of_type(frontend, types, context, form, type_type, new_name)?;
    let new_method = flush_value(context, form, &mut new_method)?;
    let mut name_object = symbol_object(types, name_symbol);
    let name_object = flush_value(context, form, &mut name_object)?;
    let mut parent_object = compile_global_value(frontend, types, context, form, parent.symbol())?;
    let parent_object = flush_value(context, form, &mut parent_object)?;
    let flags_value = context.constant_u64(int_type, flags).at(form)?;
    context
        .call(new_method, &[name_object, parent_object, flags_value])
        .at(form)?;

    let inspect =
        generate_inspect_method(frontend, types, goos, context, form, name, &inspect_fields)?;
    let inspect_name = types.intern_symbol("inspect");
    let inspect_slot = types
        .method_slot(name, inspect_name, &context.file.visibility)
        .at(form)?;
    let type_spec = types.intern_compound_type_spec(name, [], None);
    let inspect_type = types.function_type([type_spec], type_spec);
    let method_set = goos.parser.objects.symbols.intern("method-set!");
    let type_value = compile_global_value(frontend, types, context, form, name_symbol)?;
    let slot_value = Value::new(int_type, ValueKind::Integer(i128::from(inspect_slot)));
    let function_value = Value::new(inspect_type, ValueKind::Function(inspect));
    compile_named_call(
        frontend,
        types,
        context,
        form,
        method_set,
        vec![type_value, slot_value, function_value],
        ExpressionPosition::Statement,
    )?;

    Ok(Value::new(frontend.none_type, ValueKind::None))
}

fn generate_inspect_method(
    frontend: &Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    form: &Object,
    owner: Type,
    fields: &[(SymbolId, TypeSpec, PrintAs)],
) -> Result<FunctionId, Diagnostic> {
    let inspect = goos.parser.objects.symbols.intern("inspect");
    let format = goos.parser.objects.symbols.intern("format");
    let true_symbol = goos.parser.objects.symbols.intern("#t");
    let owner_type = types.intern_compound_type_spec(owner, [], None);
    let owner_layout = types.layout(owner, &context.file.visibility).at(form)?;
    let function = context
        .begin_function(
            FunctionIdentity::Method {
                owner,
                name: inspect,
            },
            Segment::Debug,
            owner_type,
        )
        .at(form)?;
    let this = context
        .current_function_data_mut()
        .at(form)?
        .allocate_register(owner_type, owner_layout.register_class)
        .at(form)?;
    context
        .current_function_data_mut()
        .at(form)?
        .emit(IrInstruction::Move {
            destination: this.into(),
            source: Register::Physical(ARGUMENT_REGISTERS[0]),
            must_emit: false,
        });

    let mut formats = Vec::with_capacity(fields.len() + 1);
    formats.push("[~8x] ~A~%".to_owned());
    let integer = types.intern_type_spec("integer");
    for (field, field_type, print_as) in fields {
        let name = goos
            .parser
            .objects
            .symbols
            .resolve(*field)
            .unwrap_or("<unknown-field>");
        let formatter = match print_as {
            PrintAs::Hex => "~X",
            PrintAs::Nothing => unreachable!("hidden fields were filtered above"),
            PrintAs::Meters => "~m",
            PrintAs::Degrees => "~d",
            PrintAs::Type(_) => "~A",
            PrintAs::Default => {
                if types.type_name(types.type_spec_base(*field_type)) == "float" {
                    "~f"
                } else if types
                    .is_subtype(integer, *field_type, &context.file.visibility)
                    .at(form)?
                {
                    "~D"
                } else {
                    "~A"
                }
            }
        };
        formats.push(format!("~T{name}: {formatter}~%"));
    }
    for text in formats.iter().rev() {
        context.file.intern_string(Segment::Debug, text).at(form)?;
    }

    let header = string_constant(types, context, &formats[0]).at(form)?;
    let true_value = symbol_object(types, true_symbol);
    let this_value = Value::register(owner_type, this);
    let displayed_type = if owner_layout.kind == TypeKind::Basic {
        let type_field = types.intern_symbol("type");
        field_place(
            types,
            &context.file.visibility,
            Register::Virtual(this),
            owner_type,
            type_field,
        )
        .at(form)?
    } else {
        symbol_object(types, owner.symbol())
    };
    compile_named_call(
        frontend,
        types,
        context,
        form,
        format,
        vec![true_value, header, this_value, displayed_type],
        ExpressionPosition::Statement,
    )?;

    for ((field, _, _), text) in fields.iter().zip(formats.iter().skip(1)) {
        let true_value = symbol_object(types, true_symbol);
        let format_string = string_constant(types, context, text).at(form)?;
        let field_value = field_place(
            types,
            &context.file.visibility,
            Register::Virtual(this),
            owner_type,
            *field,
        )
        .at(form)?;
        compile_named_call(
            frontend,
            types,
            context,
            form,
            format,
            vec![true_value, format_string, field_value],
            ExpressionPosition::Statement,
        )?;
    }

    context
        .current_function_data_mut()
        .at(form)?
        .emit(IrInstruction::Return {
            value: this.into(),
            register: PhysicalRegister::RET,
        });
    context.end_function().at(form)?;
    Ok(function)
}

fn compile_defmacro(
    frontend: &mut Frontend,
    form: &Object,
    mut cursor: ListCursor,
    _: ExpressionPosition,
    handler: &mut FormCompileContext<'_>,
) -> Result<Value, Diagnostic> {
    let FormCompileContext {
        types: _,
        goos,
        context,
    } = handler;
    let objects = &goos.parser.objects;
    let name = cursor.required(objects, "name")?.symbol(objects)?;
    let parameters = cursor
        .required_syntax(
            objects,
            "parameters",
            crate::frontend_parse::SyntaxType::List,
        )?
        .into_object();
    let body: Vec<Object> = cursor
        .remaining_body(objects)?
        .into_iter()
        .map(|argument| argument.into_object())
        .collect();
    if body.is_empty() {
        return Err(diagnostic_at(form, "macro body cannot be empty"));
    }
    let callable = goos.compile_macro(parameters, body, form.span)?;
    frontend
        .macros
        .entry(name)
        .or_default()
        .push(GoalMacroDefinition {
            owner: context.file.file_id,
            callable,
            definition: form.span,
        });
    Ok(Value::new(frontend.none_type, ValueKind::None))
}

fn parse_parameters(
    types: &mut TypeSystem,
    objects: &ObjectStore,
    visibility: &Visibility,
    parameters: ParsedArgument,
    form: &Object,
) -> Result<Vec<(SymbolId, TypeSpec)>, Diagnostic> {
    let parameter_forms = parameters
        .list_cursor(objects)?
        .remaining(objects, "parameter")?;
    if parameter_forms.len() > ARGUMENT_REGISTERS.len() {
        return Err(diagnostic_at(
            form,
            format!(
                "function has {} parameters, but GOAL has only {} argument registers",
                parameter_forms.len(),
                ARGUMENT_REGISTERS.len()
            ),
        ));
    }

    let mut result = Vec::with_capacity(parameter_forms.len());
    for parameter in parameter_forms {
        let mut parameter = parameter.list_cursor(objects)?;
        let name = parameter.required(objects, "name")?.symbol(objects)?;
        let type_argument = parameter.required(objects, "type")?;
        let ty = parse_type_spec(types, objects, visibility, type_argument.object())?;
        parameter.finish(objects)?;
        if result.iter().any(|(existing, _)| *existing == name) {
            return Err(diagnostic_at(
                form,
                "function parameter names must be unique",
            ));
        }
        result.push((name, ty));
    }
    Ok(result)
}

struct FunctionBody<'a> {
    identity: FunctionIdentity,
    segment: Segment,
    parameters: &'a [(SymbolId, TypeSpec)],
    expressions: &'a [Object],
}

fn compile_function_body(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    form: &Object,
    source: FunctionBody<'_>,
) -> Result<(FunctionId, TypeSpec), Diagnostic> {
    let function = context
        .begin_function(source.identity, source.segment, frontend.none_type)
        .at(form)?;
    for (index, (name, ty)) in source.parameters.iter().copied().enumerate() {
        let layout = types
            .layout(types.type_spec_base(ty), &context.file.visibility)
            .at(form)?;
        let register_class = if layout.register_class == RegisterClass::Gpr128 {
            RegisterClass::Gpr128
        } else {
            RegisterClass::Gpr
        };
        let register = context
            .current_function_data_mut()
            .at(form)?
            .allocate_register(ty, register_class)
            .at(form)?;
        context
            .current_function_data_mut()
            .at(form)?
            .emit(IrInstruction::Move {
                destination: Register::Virtual(register),
                source: Register::Physical(ARGUMENT_REGISTERS[index]),
                must_emit: false,
            });
        context
            .current_function_mut()
            .at(form)?
            .define_local(name, LexicalBinding { register })
            .at(form)?;
    }

    let (last, statements) = source
        .expressions
        .split_last()
        .ok_or_else(|| diagnostic_at(form, "function body cannot be empty"))?;
    for expression in statements {
        compile_expression_in_position(
            frontend,
            types,
            goos,
            context,
            expression,
            ExpressionPosition::Statement,
        )?;
    }
    let mut result = compile_expression_in_position(
        frontend,
        types,
        goos,
        context,
        last,
        ExpressionPosition::Value,
    )?;
    if result.kind == ValueKind::None {
        context.current_function_data_mut().at(form)?.return_type = result.ty;
        context.end_function().at(form)?;
        return Ok((function, result.ty));
    }
    let result_register = flush_return_value(context, last, &mut result)?;
    let result_type = result.ty;
    let return_target = context.current_function().at(form)?.return_target;
    let data = context.current_function_data_mut().at(form)?;
    data.return_type = result_type;
    let result_register = if let Some(target) = return_target {
        let target_register = target
            .result
            .ok_or_else(|| diagnostic_at(form, "function return target has no result register"))?;
        data.emit(IrInstruction::Move {
            destination: target_register.into(),
            source: result_register,
            must_emit: false,
        });
        data.bind_label(target.label).at(form)?;
        Register::Virtual(target_register)
    } else {
        result_register
    };
    data.emit(IrInstruction::Return {
        value: result_register,
        register: PhysicalRegister::RET,
    });
    context.end_function().at(form)?;
    Ok((function, result_type))
}

fn take_documentation(body: &mut Vec<ParsedArgument>) -> Option<String> {
    if !body
        .first()
        .is_some_and(|argument| matches!(argument.object().value, ObjectValue::String(_)))
    {
        return None;
    }
    let ObjectValue::String(documentation) = body.remove(0).into_object().value else {
        unreachable!()
    };
    Some(documentation)
}

fn take_function_segment(
    objects: &ObjectStore,
    body: &mut Vec<ParsedArgument>,
) -> Result<Segment, Diagnostic> {
    let Some(first) = body.first() else {
        return Ok(Segment::Main);
    };
    let Ok((head, rest)) = form_parts(objects, first.object()) else {
        return Ok(Segment::Main);
    };
    if objects.symbols.resolve(head) != Some("declare") {
        return Ok(Segment::Main);
    }
    let declaration =
        ListCursor::new(head, first.object().span, rest).required(objects, "declaration")?;
    let mut declaration = declaration.list_cursor(objects)?;
    let kind = declaration
        .required(objects, "declaration name")?
        .symbol(objects)?;
    if objects.symbols.resolve(kind) != Some("segment") {
        return Err(diagnostic_at(
            first.object(),
            "only function segment declarations are implemented",
        ));
    }
    let segment = declaration.required(objects, "segment")?.symbol(objects)?;
    declaration.finish(objects)?;
    let segment = match objects.symbols.resolve(segment) {
        Some("main") => Segment::Main,
        Some("debug") => Segment::Debug,
        Some(name) => {
            return Err(diagnostic_at(
                first.object(),
                format!("unknown function segment `{name}`"),
            ));
        }
        None => return Err(diagnostic_at(first.object(), "unknown segment symbol")),
    };
    body.remove(0);
    Ok(segment)
}

fn compile_defun(
    frontend: &mut Frontend,
    form: &Object,
    mut cursor: ListCursor,
    _: ExpressionPosition,
    handler: &mut FormCompileContext<'_>,
) -> Result<Value, Diagnostic> {
    let FormCompileContext {
        types,
        goos,
        context,
    } = handler;
    let objects = &goos.parser.objects;
    let name_argument = cursor.required(objects, "function-name")?;
    let definition_span = name_argument.object().span.or(form.span);
    let name = name_argument.symbol(objects)?;
    let function_name = objects
        .symbols
        .resolve(name)
        .unwrap_or("<unknown-symbol>")
        .to_owned();
    let parameters = cursor.required_syntax(
        objects,
        "parameter-list",
        crate::frontend_parse::SyntaxType::List,
    )?;
    let parameters = parse_parameters(types, objects, &context.file.visibility, parameters, form)?;

    let mut body = cursor.remaining_body(objects)?;
    let documentation = take_documentation(&mut body);
    let segment = take_function_segment(objects, &mut body)?;
    let body = body
        .into_iter()
        .map(ParsedArgument::into_object)
        .collect::<Vec<_>>();
    let (function, result_type) = compile_function_body(
        frontend,
        types,
        goos,
        context,
        form,
        FunctionBody {
            identity: FunctionIdentity::Global(name),
            segment,
            parameters: &parameters,
            expressions: &body,
        },
    )?;

    let parameter_types = parameters.iter().map(|(_, ty)| *ty).collect::<Vec<_>>();
    let signature = types.function_type(parameter_types, result_type);
    if let Some(existing) = frontend
        .global_definition(name, &context.file.visibility)
        .at(form)?
        && existing.ty != signature
    {
        return Err(diagnostic_at(
            form,
            format!(
                "function `{function_name}` was previously defined as {}, but this definition has type {}",
                types.format_type_spec(existing.ty),
                types.format_type_spec(signature)
            ),
        ));
    }
    frontend
        .globals
        .entry(name)
        .or_default()
        .push(GlobalDefinition {
            owner: context.file.file_id,
            name,
            ty: signature,
            documentation,
            span: definition_span,
        });

    let debug_segment_symbol = goos.parser.objects.symbols.intern("*debug-segment*");
    let nothing_symbol = goos.parser.objects.symbols.intern("nothing");
    let function_temporary = context
        .current_function_data_mut()
        .at(form)?
        .allocate_register(signature, RegisterClass::Gpr)
        .at(form)?;
    let function_value = context
        .current_function_data_mut()
        .at(form)?
        .allocate_register(signature, RegisterClass::Gpr)
        .at(form)?;
    let value_register = Register::Virtual(function_value);
    if segment == Segment::Debug {
        let disabled = context
            .current_function_data_mut()
            .at(form)?
            .allocate_label()
            .at(form)?;
        let done = context
            .current_function_data_mut()
            .at(form)?
            .allocate_label()
            .at(form)?;
        let top_level = context.current_function_data_mut().at(form)?;
        top_level.emit(IrInstruction::GetSymbolValue {
            destination: value_register,
            symbol: debug_segment_symbol,
        });
        top_level.emit(IrInstruction::Branch {
            condition: BranchCondition::Equal(
                Register::Physical(PhysicalRegister::Gpr(Gpr::S7)),
                value_register,
            ),
            target: disabled,
            delay: crate::ir::BranchDelay::Normal,
        });
        top_level.emit(IrInstruction::Nop);
        top_level.emit(IrInstruction::LoadFunctionHigh {
            destination: function_temporary.into(),
        });
        top_level.emit(IrInstruction::LoadFunctionLow {
            destination: value_register,
            source: function_temporary.into(),
            function,
        });
        top_level.emit(IrInstruction::StoreSymbol {
            source: value_register,
            symbol: name,
        });
        top_level.emit(IrInstruction::Branch {
            condition: BranchCondition::Always,
            target: done,
            delay: crate::ir::BranchDelay::Normal,
        });
        top_level.emit(IrInstruction::Nop);
        top_level.bind_label(disabled).at(form)?;
        top_level.emit(IrInstruction::GetSymbolValue {
            destination: value_register,
            symbol: nothing_symbol,
        });
        top_level.emit(IrInstruction::StoreSymbol {
            source: value_register,
            symbol: name,
        });
        top_level.bind_label(done).at(form)?;
    } else {
        let top_level = context.current_function_data_mut().at(form)?;
        top_level.emit(IrInstruction::LoadFunctionHigh {
            destination: function_temporary.into(),
        });
        top_level.emit(IrInstruction::LoadFunctionLow {
            destination: value_register,
            source: function_temporary.into(),
            function,
        });
        top_level.emit(IrInstruction::StoreSymbol {
            source: value_register,
            symbol: name,
        });
    }
    Ok(Value::new(signature, ValueKind::Function(function)))
}

fn compile_defmethod(
    frontend: &mut Frontend,
    form: &Object,
    mut cursor: ListCursor,
    _: ExpressionPosition,
    handler: &mut FormCompileContext<'_>,
) -> Result<Value, Diagnostic> {
    let FormCompileContext {
        types,
        goos,
        context,
    } = handler;
    let objects = &goos.parser.objects;
    let method_name = cursor.required(objects, "method-name")?.symbol(objects)?;
    let owner_or_parameters = cursor.required(objects, "type or parameter-list")?;
    let has_explicit_owner = match owner_or_parameters.object().value {
        ObjectValue::Symbol(_) => true,
        ObjectValue::Pair(id) => matches!(
            objects
                .cons
                .get(id)
                .map_err(|message| diagnostic_at(form, message))?
                .car
                .value,
            ObjectValue::Symbol(_)
        ),
        ObjectValue::EmptyList => false,
        _ => {
            return Err(diagnostic_at(
                owner_or_parameters.object(),
                "method type or parameter list has invalid syntax",
            ));
        }
    };
    let (explicit_owner, parameters) = if has_explicit_owner {
        let owner = parse_type_spec(
            types,
            objects,
            &context.file.visibility,
            owner_or_parameters.object(),
        )?;
        let parameters = cursor.required_syntax(
            objects,
            "parameter-list",
            crate::frontend_parse::SyntaxType::List,
        )?;
        (Some(owner), parameters)
    } else {
        (None, owner_or_parameters)
    };
    let parameters = parse_parameters(types, objects, &context.file.visibility, parameters, form)?;
    let inferred_owner = parameters
        .first()
        .map(|(_, ty)| *ty)
        .ok_or_else(|| diagnostic_at(form, "method type cannot be inferred without parameters"))?;
    let owner_spec = explicit_owner.unwrap_or(inferred_owner);
    let owner = types.type_spec_base(owner_spec);

    let mut body = cursor.remaining_body(objects)?;
    take_documentation(&mut body);
    let body = body
        .into_iter()
        .map(ParsedArgument::into_object)
        .collect::<Vec<_>>();
    let method = types
        .lookup_method(owner, method_name, &context.file.visibility)
        .at(form)?;
    let slot = method.slot;
    let declared_signature = method.signature;
    let method_name_text = objects
        .symbols
        .resolve(method_name)
        .unwrap_or("<unknown-method>")
        .to_owned();
    let expected_signature = types.substitute_method_type(declared_signature, owner_spec);
    let (function, result_type) = compile_function_body(
        frontend,
        types,
        goos,
        context,
        form,
        FunctionBody {
            identity: FunctionIdentity::Method {
                owner,
                name: method_name,
            },
            segment: Segment::Main,
            parameters: &parameters,
            expressions: &body,
        },
    )?;
    let parameter_types = parameters.iter().map(|(_, ty)| *ty).collect::<Vec<_>>();
    let actual_signature = types.function_type(parameter_types, result_type);
    if actual_signature != expected_signature {
        return Err(diagnostic_at(
            form,
            format!(
                "method `{method_name_text}` has type {}, but {} requires {}",
                types.format_type_spec(actual_signature),
                types.type_name(owner),
                types.format_type_spec(expected_signature)
            ),
        ));
    }

    let int_type = types.intern_type_spec("int");
    let method_set = SymbolId(fnv64(b"method-set!"));
    let owner_value = compile_global_value(frontend, types, context, form, owner.symbol())?;
    let slot_value = Value::new(int_type, ValueKind::Integer(i128::from(slot)));
    let function_value = Value::new(actual_signature, ValueKind::Function(function));
    compile_named_call(
        frontend,
        types,
        context,
        form,
        method_set,
        vec![owner_value, slot_value, function_value],
        ExpressionPosition::Statement,
    )?;
    Ok(Value::new(frontend.none_type, ValueKind::None))
}

fn compile_expression(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
) -> Result<Value, Diagnostic> {
    compile_expression_in_position(
        frontend,
        types,
        goos,
        context,
        expression,
        ExpressionPosition::Value,
    )
}

fn compile_expression_in_position(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    position: ExpressionPosition,
) -> Result<Value, Diagnostic> {
    match expression.value {
        ObjectValue::Integer(value) => {
            let ty = types.intern_type_spec("int");
            Ok(Value::new(ty, ValueKind::Integer(value as i128)))
        }
        ObjectValue::Float(value) => {
            let ty = types.intern_type_spec("float");
            Ok(Value::new(ty, ValueKind::Float(value as f32)))
        }
        ObjectValue::String(ref value) => string_constant(types, context, value).at(expression),
        ObjectValue::Symbol(name) => {
            if matches!(goos.parser.objects.symbols.resolve(name), Some("#t" | "#f")) {
                return Ok(symbol_object(types, name));
            }
            if let Some(binding) = context
                .current_function()
                .at(expression)?
                .lookup_local(name)
            {
                let ty = context.current_function_data().at(expression)?.registers
                    [binding.register.0 as usize]
                    .ty;
                return Ok(Value::register(ty, binding.register));
            }
            if let Some(constant) = frontend
                .constant_definition(name, &context.file.visibility)
                .at(expression)?
                .cloned()
            {
                if context.constant_expansions.contains(&name) {
                    let name = goos
                        .parser
                        .objects
                        .symbols
                        .resolve(name)
                        .unwrap_or("<unknown>");
                    return Err(diagnostic_at(
                        expression,
                        format!("constant `{name}` expands recursively"),
                    ));
                }
                context.constant_expansions.push(name);
                let result = compile_expression_in_position(
                    frontend,
                    types,
                    goos,
                    context,
                    &constant.value,
                    position,
                );
                context.constant_expansions.pop();
                return result;
            }
            compile_global_value(frontend, types, context, expression, name)
        }
        ObjectValue::Pair(_) => {
            let (head_object, rest) = {
                let objects = &goos.parser.objects;
                let ObjectValue::Pair(id) = expression.value else {
                    unreachable!()
                };
                let cell = objects.cons.get(id).at(expression)?;
                (cell.car.clone(), cell.cdr.clone())
            };
            let ObjectValue::Symbol(head) = head_object.value else {
                let function = compile_expression(frontend, types, goos, context, &head_object)?;
                let arguments = call_arguments(&goos.parser.objects, expression, rest)?;
                return compile_call_expressions(
                    frontend, types, goos, context, expression, position, function, arguments,
                );
            };
            let name = goos.parser.objects.symbols.resolve(head).map(str::to_owned);
            if let Some(definition) = frontend
                .macro_definition(head, &context.file.visibility)
                .at(expression)?
                .cloned()
            {
                let arguments = ListCursor::new(head, expression.span, rest)
                    .remaining(&goos.parser.objects, "argument")?
                    .into_iter()
                    .map(|argument| argument.into_object())
                    .collect::<Vec<_>>();
                let environment = goos.goal_environment();
                let expanded = goos
                    .invoke_macro(&definition.callable, &arguments, environment)
                    .map_err(|diagnostic| {
                        macro_diagnostic(diagnostic, expression.span, definition.definition)
                    })?;
                return compile_expression_in_position(
                    frontend, types, goos, context, &expanded, position,
                )
                .map_err(|diagnostic| {
                    macro_diagnostic(diagnostic, expression.span, definition.definition)
                });
            }
            let operation = match name.as_deref() {
                Some("begin") => {
                    let mut handler = FormCompileContext {
                        types,
                        goos,
                        context,
                    };
                    return compile_begin(frontend, expression, head, rest, position, &mut handler);
                }
                Some("let") => {
                    let mut handler = FormCompileContext {
                        types,
                        goos,
                        context,
                    };
                    return compile_let(frontend, expression, head, rest, position, &mut handler);
                }
                Some("set!") => {
                    return compile_set(
                        frontend, types, goos, context, expression, head, rest, position,
                    );
                }
                Some("cond") => {
                    let mut handler = FormCompileContext {
                        types,
                        goos,
                        context,
                    };
                    return compile_cond(frontend, expression, head, rest, position, &mut handler);
                }
                Some("return") => {
                    return compile_return(frontend, types, goos, context, expression, head, rest);
                }
                Some("label") => {
                    return compile_label(goos, context, expression, head, rest);
                }
                Some("goto") => {
                    return compile_goto(goos, context, expression, head, rest);
                }
                Some("when-goto") => {
                    return compile_when_goto(
                        frontend, types, goos, context, expression, head, rest,
                    );
                }
                Some("and" | "or") => {
                    return compile_logical(frontend, types, goos, context, expression, head, rest);
                }
                Some("not") => {
                    return compile_not(frontend, types, goos, context, expression, head, rest);
                }
                Some("=" | "!=") => {
                    return compile_equality_value(
                        frontend, types, goos, context, expression, head, rest,
                    );
                }
                Some("quote") => {
                    return compile_quote(types, goos, expression, head, rest);
                }
                Some("the" | "the-as") => {
                    return compile_type_cast(
                        frontend, types, goos, context, expression, head, rest,
                    );
                }
                Some("car" | "cdr") => {
                    return compile_pair_access(
                        frontend, types, goos, context, expression, head, rest,
                    );
                }
                Some("new") => {
                    return compile_new(
                        frontend, types, goos, context, expression, head, rest, position,
                    );
                }
                Some("current-method-type") => {
                    return compile_current_method_type(
                        frontend, types, goos, context, expression, head, rest,
                    );
                }
                Some("rtype-of") => {
                    return compile_runtime_type(
                        frontend, types, goos, context, expression, head, rest,
                    );
                }
                Some("none") => {
                    ListCursor::new(head, expression.span, rest).finish(&goos.parser.objects)?;
                    return Ok(Value::new(context.none_type, ValueKind::None));
                }
                Some("nop!") => {
                    return compile_nop(goos, context, expression, head, rest);
                }
                Some("rlet") => {
                    return compile_rlet(
                        frontend, types, goos, context, expression, head, rest, position,
                    );
                }
                Some("aop-align") => {
                    return compile_aop_align(goos, context, expression, head, rest);
                }
                Some("m") => {
                    return compile_native_move(
                        frontend, types, goos, context, expression, head, rest,
                    );
                }
                Some("m.c0") => {
                    return compile_move_to_cop0(
                        frontend, types, goos, context, expression, head, rest,
                    );
                }
                Some("add" | "sub") => {
                    return compile_native_arithmetic(
                        frontend, types, goos, context, expression, head, rest,
                    );
                }
                Some("l.q" | "s.q") => {
                    return compile_native_memory(
                        frontend, types, goos, context, expression, head, rest,
                    );
                }
                Some("b.lt" | "b.ge" | "b.z") => {
                    return compile_native_branch(
                        frontend, types, goos, context, expression, head, rest,
                    );
                }
                Some("->") => {
                    return compile_field_access(
                        frontend, types, goos, context, expression, head, rest,
                    );
                }
                Some("&") => {
                    return compile_address_of(
                        frontend, types, goos, context, expression, head, rest,
                    );
                }
                Some("&+") => {
                    return compile_pointer_addition(
                        frontend, types, goos, context, expression, head, rest,
                    );
                }
                Some("method-of-type") => {
                    return compile_method_of_type(
                        frontend, types, goos, context, expression, head, rest,
                    );
                }
                Some("logior" | "logand" | "lognor" | "logxor") => {
                    return compile_bitwise(frontend, types, goos, context, expression, head, rest);
                }
                Some("abs") => {
                    return compile_abs(frontend, types, goos, context, expression, head, rest);
                }
                Some("min" | "max") => {
                    return compile_min_max(frontend, types, goos, context, expression, head, rest);
                }
                Some("ash") => {
                    return compile_ash(frontend, types, goos, context, expression, head, rest);
                }
                Some("<" | ">" | "<=" | ">=") => {
                    return compile_ordered_comparison_value(
                        frontend, types, goos, context, expression, head, rest,
                    );
                }
                Some("+") => ArithmeticOperation::Add,
                Some("-") => ArithmeticOperation::Subtract,
                Some("*") => ArithmeticOperation::Multiply,
                Some("/") => ArithmeticOperation::Divide,
                Some("mod" | "rem") => ArithmeticOperation::Remainder,
                Some(name) => {
                    if let Some(binding) = context
                        .current_function()
                        .at(expression)?
                        .lookup_local(head)
                    {
                        let ty = context.current_function_data().at(expression)?.registers
                            [binding.register.0 as usize]
                            .ty;
                        let function = Value::register(ty, binding.register);
                        let mut handler = FormCompileContext {
                            types,
                            goos,
                            context,
                        };
                        return compile_call_form(
                            frontend,
                            expression,
                            head,
                            rest,
                            position,
                            function,
                            &mut handler,
                        );
                    }
                    if frontend
                        .global_definition(head, &context.file.visibility)
                        .at(expression)?
                        .is_some()
                    {
                        let mut handler = FormCompileContext {
                            types,
                            goos,
                            context,
                        };
                        return compile_named_call_form(
                            frontend,
                            expression,
                            head,
                            rest,
                            position,
                            &mut handler,
                        );
                    }
                    return Err(diagnostic_at(
                        expression,
                        format!("unknown GOAL form `{name}`"),
                    ));
                }
                None => {
                    return Err(diagnostic_at(
                        expression,
                        "GOAL expression has an unknown form name",
                    ));
                }
            };
            compile_arithmetic(
                frontend,
                types,
                goos,
                context,
                expression,
                ListCursor::new(head, expression.span, rest),
                operation,
            )
        }
        _ => Err(diagnostic_at(
            expression,
            "this expression is not implemented yet",
        )),
    }
}

fn compile_sequence(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    form: &Object,
    expressions: &[Object],
    position: ExpressionPosition,
) -> Result<Value, Diagnostic> {
    let (last, statements) = expressions
        .split_last()
        .ok_or_else(|| diagnostic_at(form, "body cannot be empty"))?;
    for expression in statements {
        compile_expression_in_position(
            frontend,
            types,
            goos,
            context,
            expression,
            ExpressionPosition::Statement,
        )?;
    }
    compile_expression_in_position(frontend, types, goos, context, last, position)
}

fn compile_begin(
    frontend: &mut Frontend,
    expression: &Object,
    head: SymbolId,
    rest: Object,
    position: ExpressionPosition,
    handler: &mut FormCompileContext<'_>,
) -> Result<Value, Diagnostic> {
    let FormCompileContext {
        types,
        goos,
        context,
    } = handler;
    let body = ListCursor::new(head, expression.span, rest)
        .remaining_body(&goos.parser.objects)?
        .into_iter()
        .map(ParsedArgument::into_object)
        .collect::<Vec<_>>();
    compile_sequence(frontend, types, goos, context, expression, &body, position)
}

fn compile_let(
    frontend: &mut Frontend,
    expression: &Object,
    head: SymbolId,
    rest: Object,
    _: ExpressionPosition,
    handler: &mut FormCompileContext<'_>,
) -> Result<Value, Diagnostic> {
    let FormCompileContext {
        types,
        goos,
        context,
    } = handler;
    let (bindings, body) = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let bindings = cursor
            .required_syntax(objects, "bindings", crate::frontend_parse::SyntaxType::List)?
            .list_cursor(objects)?
            .remaining(objects, "binding")?
            .into_iter()
            .map(ParsedArgument::into_object)
            .collect::<Vec<_>>();
        let body = cursor
            .remaining_body(objects)?
            .into_iter()
            .map(ParsedArgument::into_object)
            .collect::<Vec<_>>();
        (bindings, body)
    };
    let mut locals = Vec::with_capacity(bindings.len());
    for binding in bindings {
        let (name, initializer) = {
            let objects = &goos.parser.objects;
            if !matches!(binding.value, ObjectValue::Pair(_)) {
                return Err(diagnostic_at(&binding, "let binding must be a list"));
            }
            let span = binding.span;
            let mut binding = ListCursor::new(head, span, binding);
            let name = binding.required(objects, "name")?.symbol(objects)?;
            let initializer = binding.required(objects, "initializer")?.into_object();
            binding.finish(objects)?;
            (name, initializer)
        };
        let mut value = compile_expression(frontend, types, goos, context, &initializer)?;
        let source = flush_value(context, &initializer, &mut value)?;
        let layout = types
            .layout(types.type_spec_base(value.ty), &context.file.visibility)
            .at(&initializer)?;
        let register = context
            .current_function_data_mut()
            .at(&initializer)?
            .allocate_register(value.ty, layout.register_class)
            .at(&initializer)?;
        context
            .current_function_data_mut()
            .at(&initializer)?
            .emit(IrInstruction::Move {
                destination: register.into(),
                source,
                must_emit: false,
            });
        locals.push((name, register));
    }
    context
        .current_function_mut()
        .at(expression)?
        .push_scope()
        .at(expression)?;
    for (name, register) in locals {
        context
            .current_function_mut()
            .at(expression)?
            .define_local(name, LexicalBinding { register })
            .at(expression)?;
    }
    let mut result = compile_sequence(
        frontend,
        types,
        goos,
        context,
        expression,
        &body,
        ExpressionPosition::Value,
    )?;
    if result.kind != ValueKind::None {
        let source = flush_value(context, expression, &mut result)?;
        let layout = types
            .layout(types.type_spec_base(result.ty), &context.file.visibility)
            .at(expression)?;
        let register = context
            .current_function_data_mut()
            .at(expression)?
            .allocate_register(result.ty, layout.register_class)
            .at(expression)?;
        context
            .current_function_data_mut()
            .at(expression)?
            .emit(IrInstruction::Move {
                destination: register.into(),
                source,
                must_emit: false,
            });
        result = Value::register(result.ty, register);
    }
    context
        .current_function_mut()
        .at(expression)?
        .pop_scope()
        .at(expression)?;
    Ok(result)
}

fn compile_set(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
    position: ExpressionPosition,
) -> Result<Value, Diagnostic> {
    let (place_expression, value_expression) = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let place = cursor.required(objects, "place")?.into_object();
        let value = cursor.required(objects, "value")?.into_object();
        cursor.finish(objects)?;
        (place, value)
    };

    if let ObjectValue::Symbol(name) = place_expression.value
        && let Some(binding) = context
            .current_function()
            .at(expression)?
            .lookup_local(name)
    {
        let expected = context.current_function_data().at(expression)?.registers
            [binding.register.0 as usize]
            .ty;
        let mut value = compile_expression(frontend, types, goos, context, &value_expression)?;
        types
            .typecheck_assignment(expected, value.ty, &context.file.visibility)
            .at(&value_expression)?;
        let source = flush_value(context, &value_expression, &mut value)?;
        context
            .current_function_data_mut()
            .at(expression)?
            .emit(IrInstruction::Move {
                destination: binding.register.into(),
                source,
                must_emit: false,
            });
        if position == ExpressionPosition::Statement {
            return Ok(Value::new(context.none_type, ValueKind::None));
        }
        let layout = types
            .layout(types.type_spec_base(expected), &context.file.visibility)
            .at(expression)?;
        let result = context
            .current_function_data_mut()
            .at(expression)?
            .allocate_register(expected, layout.register_class)
            .at(expression)?;
        context
            .current_function_data_mut()
            .at(expression)?
            .emit_move(result, binding.register);
        return Ok(Value::register(expected, result));
    }

    let place_value = compile_expression(frontend, types, goos, context, &place_expression)?;
    let place_type = place_value.ty;
    let place = place_value.into_place().map_err(|diagnostic| {
        diagnostic.with_context("set! target is not assignable", place_expression.span)
    })?;
    let mut value = compile_expression(frontend, types, goos, context, &value_expression)?;
    types
        .typecheck_assignment(place_type, value.ty, &context.file.visibility)
        .at(&value_expression)?;
    let source = flush_value(context, &value_expression, &mut value)?;
    let Place {
        base,
        projection: PlaceProjection::Whole(load),
    } = place
    else {
        return Err(diagnostic_at(
            &place_expression,
            "this kind of set! target is not implemented yet",
        ));
    };
    let (base, offset) = materialize_place_address(context, &place_expression, base)?;
    let operation = match (load.size, load.register_class) {
        (1, RegisterClass::Gpr) => crate::ir::MemoryStore::Byte,
        (2, RegisterClass::Gpr) => crate::ir::MemoryStore::Halfword,
        (4, RegisterClass::Gpr) => crate::ir::MemoryStore::Word,
        (8, RegisterClass::Gpr) => crate::ir::MemoryStore::Doubleword,
        (16, RegisterClass::Gpr128) => crate::ir::MemoryStore::Quadword,
        (4, RegisterClass::Fpr) => crate::ir::MemoryStore::FloatWord,
        _ => {
            return Err(diagnostic_at(
                &place_expression,
                "this memory store width and register class are not supported",
            ));
        }
    };
    let offset = i16::try_from(offset)
        .map_err(|_| diagnostic_at(&place_expression, "store offset exceeds 16 bits"))?;
    context
        .current_function_data_mut()
        .at(expression)?
        .emit(IrInstruction::StoreMemory {
            operation,
            source,
            base,
            offset,
        });
    if position == ExpressionPosition::Statement {
        return Ok(Value::new(context.none_type, ValueKind::None));
    }
    let layout = types
        .layout(types.type_spec_base(value.ty), &context.file.visibility)
        .at(expression)?;
    let result = context
        .current_function_data_mut()
        .at(expression)?
        .allocate_register(value.ty, layout.register_class)
        .at(expression)?;
    context
        .current_function_data_mut()
        .at(expression)?
        .emit_move(result, source);
    Ok(Value::register(value.ty, result))
}

fn invert_condition(condition: BranchCondition) -> Result<BranchCondition, String> {
    Ok(match condition {
        BranchCondition::Zero(register) => BranchCondition::NonZero(register),
        BranchCondition::NonZero(register) => BranchCondition::Zero(register),
        BranchCondition::Equal(left, right) => BranchCondition::NotEqual(left, right),
        BranchCondition::NotEqual(left, right) => BranchCondition::Equal(left, right),
        BranchCondition::Negative(register) => BranchCondition::NonNegative(register),
        BranchCondition::NonNegative(register) => BranchCondition::Negative(register),
        BranchCondition::Positive(register) => BranchCondition::NonPositive(register),
        BranchCondition::NonPositive(register) => BranchCondition::Positive(register),
        BranchCondition::Always => {
            return Err("an unconditional condition cannot be inverted".to_owned());
        }
    })
}

fn compile_condition(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
) -> Result<BranchCondition, Diagnostic> {
    if matches!(expression.value, ObjectValue::Pair(_)) {
        let (head, rest) = form_parts(&goos.parser.objects, expression)?;
        if let Some(definition) = frontend
            .macro_definition(head, &context.file.visibility)
            .at(expression)?
            .cloned()
        {
            let arguments = ListCursor::new(head, expression.span, rest)
                .remaining(&goos.parser.objects, "argument")?
                .into_iter()
                .map(ParsedArgument::into_object)
                .collect::<Vec<_>>();
            let environment = goos.goal_environment();
            let expanded = goos
                .invoke_macro(&definition.callable, &arguments, environment)
                .map_err(|diagnostic| {
                    macro_diagnostic(diagnostic, expression.span, definition.definition)
                })?;
            return compile_condition(frontend, types, goos, context, &expanded).map_err(
                |diagnostic| macro_diagnostic(diagnostic, expression.span, definition.definition),
            );
        }
    }
    let form = if matches!(expression.value, ObjectValue::Pair(_)) {
        Some(form_parts(&goos.parser.objects, expression)?)
    } else {
        None
    };
    match form.and_then(|(head, rest)| {
        goos.parser
            .objects
            .symbols
            .resolve(head)
            .map(|name| (name, head, rest))
    }) {
        Some(("not", head, rest)) => {
            let condition = {
                let objects = &goos.parser.objects;
                let mut cursor = ListCursor::new(head, expression.span, rest);
                let condition = cursor.required(objects, "condition")?.into_object();
                cursor.finish(objects)?;
                condition
            };
            invert_condition(compile_condition(
                frontend, types, goos, context, &condition,
            )?)
            .at(expression)
        }
        Some(("=" | "!=", head, rest)) => {
            let equality = matches!(goos.parser.objects.symbols.resolve(head), Some("="));
            let (left_expression, right_expression) = {
                let objects = &goos.parser.objects;
                let mut cursor = ListCursor::new(head, expression.span, rest);
                let left = cursor.required(objects, "left")?.into_object();
                let right = cursor.required(objects, "right")?.into_object();
                cursor.finish(objects)?;
                (left, right)
            };
            let mut right = compile_expression(frontend, types, goos, context, &right_expression)?;
            let right = flush_gpr_operand(context, &right_expression, &mut right)?;
            let mut left = compile_expression(frontend, types, goos, context, &left_expression)?;
            let left = flush_gpr_operand(context, &left_expression, &mut left)?;
            Ok(if equality {
                BranchCondition::Equal(left, right)
            } else {
                BranchCondition::NotEqual(left, right)
            })
        }
        Some(("zero?", head, rest)) => {
            let value_expression = {
                let objects = &goos.parser.objects;
                let mut cursor = ListCursor::new(head, expression.span, rest);
                let value = cursor.required(objects, "value")?.into_object();
                cursor.finish(objects)?;
                value
            };
            let mut value = compile_expression(frontend, types, goos, context, &value_expression)?;
            let value = flush_value(context, &value_expression, &mut value)?;
            Ok(BranchCondition::Zero(value))
        }
        Some(("<" | ">" | "<=" | ">=", head, rest)) => {
            compile_ordered_condition(frontend, types, goos, context, expression, head, rest)
        }
        _ => {
            let mut value = compile_expression(frontend, types, goos, context, expression)?;
            let value = flush_value(context, expression, &mut value)?;
            Ok(BranchCondition::NotEqual(
                Register::Physical(PhysicalRegister::Gpr(Gpr::S7)),
                value,
            ))
        }
    }
}

fn compile_equality_value(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let equality = matches!(goos.parser.objects.symbols.resolve(head), Some("="));
    let (left_expression, right_expression) = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let left = cursor.required(objects, "left")?.into_object();
        let right = cursor.required(objects, "right")?.into_object();
        cursor.finish(objects)?;
        (left, right)
    };
    let mut left = compile_expression(frontend, types, goos, context, &left_expression)?;
    let left = flush_gpr_operand(context, &left_expression, &mut left)?;
    let mut right = compile_expression(frontend, types, goos, context, &right_expression)?;
    let right = flush_gpr_operand(context, &right_expression, &mut right)?;
    let zero = Register::Physical(PhysicalRegister::ZERO);
    let comparison = if left == zero {
        right
    } else if right == zero {
        left
    } else {
        let int = types.intern_type_spec("int");
        let difference = context
            .current_function_data_mut()
            .at(expression)?
            .allocate_gpr(int)
            .at(expression)?;
        context
            .current_function_data_mut()
            .at(expression)?
            .emit_integer_subtract(difference, left, right);
        difference.into()
    };
    let symbol = types.intern_type_spec("symbol");
    let result = context
        .current_function_data_mut()
        .at(expression)?
        .allocate_gpr(symbol)
        .at(expression)?;
    let data = context.current_function_data_mut().at(expression)?;
    data.emit(IrInstruction::IntegerAddImmediate {
        destination: result.into(),
        source: Register::Physical(PhysicalRegister::Gpr(Gpr::S7)),
        value: TRUE_OFFSET,
    });
    data.emit_conditional_move(
        if equality {
            ConditionalMoveOperation::NonZero
        } else {
            ConditionalMoveOperation::Zero
        },
        result,
        Register::Physical(PhysicalRegister::Gpr(Gpr::S7)),
        comparison,
    );
    Ok(Value::register(symbol, result))
}

fn condition_value(
    types: &mut TypeSystem,
    context: &mut FrontendContext,
    expression: &Object,
    condition: BranchCondition,
) -> Result<Value, Diagnostic> {
    let (comparison, false_when) = match condition {
        BranchCondition::Equal(left, right) | BranchCondition::NotEqual(left, right) => {
            let comparison = if left == Register::Physical(PhysicalRegister::ZERO) {
                right
            } else if right == Register::Physical(PhysicalRegister::ZERO) {
                left
            } else {
                let int = types.intern_type_spec("int");
                let difference = context
                    .current_function_data_mut()
                    .at(expression)?
                    .allocate_gpr(int)
                    .at(expression)?;
                context
                    .current_function_data_mut()
                    .at(expression)?
                    .emit_integer_subtract(difference, left, right);
                difference.into()
            };
            (
                comparison,
                if matches!(condition, BranchCondition::Equal(_, _)) {
                    ConditionalMoveOperation::NonZero
                } else {
                    ConditionalMoveOperation::Zero
                },
            )
        }
        BranchCondition::Zero(register) => (register, ConditionalMoveOperation::NonZero),
        BranchCondition::NonZero(register) => (register, ConditionalMoveOperation::Zero),
        _ => {
            return Err(diagnostic_at(
                expression,
                "this condition cannot be used as a value yet",
            ));
        }
    };
    let symbol = types.intern_type_spec("symbol");
    let result = context
        .current_function_data_mut()
        .at(expression)?
        .allocate_gpr(symbol)
        .at(expression)?;
    let data = context.current_function_data_mut().at(expression)?;
    data.emit(IrInstruction::IntegerAddImmediate {
        destination: result.into(),
        source: Register::Physical(PhysicalRegister::Gpr(Gpr::S7)),
        value: TRUE_OFFSET,
    });
    data.emit_conditional_move(
        false_when,
        result,
        Register::Physical(PhysicalRegister::Gpr(Gpr::S7)),
        comparison,
    );
    Ok(Value::register(symbol, result))
}

fn empty_list_value(types: &mut TypeSystem) -> Value {
    Value::new(types.intern_type_spec("object"), ValueKind::EmptyList)
}

fn compile_not(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let operand = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let operand = cursor.required(objects, "value")?.into_object();
        cursor.finish(objects)?;
        operand
    };
    let mut value = compile_expression(frontend, types, goos, context, &operand)?;
    let value_register = flush_gpr_operand(context, &operand, &mut value)?;
    if let Some(IrInstruction::ConditionalMove {
        operation,
        destination,
        source: Register::Physical(PhysicalRegister::Gpr(Gpr::S7)),
        ..
    }) = context
        .current_function_data_mut()
        .at(expression)?
        .ir
        .last_mut()
        && *destination == value_register
    {
        *operation = match *operation {
            ConditionalMoveOperation::Zero => ConditionalMoveOperation::NonZero,
            ConditionalMoveOperation::NonZero => ConditionalMoveOperation::Zero,
        };
        return Ok(value);
    }
    let condition = BranchCondition::Equal(
        Register::Physical(PhysicalRegister::Gpr(Gpr::S7)),
        value_register,
    );
    let symbol = types.intern_type_spec("symbol");
    let result = context
        .current_function_data_mut()
        .at(expression)?
        .allocate_gpr(symbol)
        .at(expression)?;
    let done = context
        .current_function_data_mut()
        .at(expression)?
        .allocate_label()
        .at(expression)?;
    let data = context.current_function_data_mut().at(expression)?;
    data.emit(IrInstruction::Branch {
        condition,
        target: done,
        delay: crate::ir::BranchDelay::Normal,
    });
    data.emit(IrInstruction::IntegerAddImmediate {
        destination: result.into(),
        source: Register::Physical(PhysicalRegister::Gpr(Gpr::S7)),
        value: TRUE_OFFSET,
    });
    data.emit_move(result, PhysicalRegister::Gpr(Gpr::S7));
    data.bind_label(done).at(expression)?;
    Ok(Value::register(symbol, result))
}

fn compile_ordered_condition(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<BranchCondition, Diagnostic> {
    let (left_expression, right_expression) = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let left = cursor.required(objects, "left")?.into_object();
        let right = cursor.required(objects, "right")?.into_object();
        cursor.finish(objects)?;
        (left, right)
    };
    let operation = match goos
        .parser
        .objects
        .symbols
        .resolve(head)
        .expect("ordered comparison has a symbol name")
    {
        "<" => 0,
        ">=" => 1,
        ">" => 2,
        "<=" => 3,
        _ => unreachable!(),
    };
    let mut left_value = compile_expression(frontend, types, goos, context, &left_expression)?;
    let pointer_comparison = types.is_pointer(left_value.ty);
    let (left_register, left_signed) = if pointer_comparison {
        (
            flush_gpr_operand(context, &left_expression, &mut left_value)?,
            false,
        )
    } else {
        let signed = check_integer_operand_type(types, context, &left_expression, &left_value)?;
        let left = finish_integer_operand(context, &left_expression, &mut left_value, signed)?;
        (left.register, signed)
    };
    let right_register = if pointer_comparison {
        let mut right = compile_expression(frontend, types, goos, context, &right_expression)?;
        if !types.is_pointer(right.ty) {
            return Err(diagnostic_at(
                &right_expression,
                format!(
                    "pointer comparison requires a pointer, got `{}`",
                    types.format_type_spec(right.ty)
                ),
            ));
        }
        flush_gpr_operand(context, &right_expression, &mut right)?
    } else {
        compile_integer_operand(frontend, types, goos, context, &right_expression)?.register
    };
    let (left_register, right_register, inverted) = match operation {
        0 => (left_register, right_register, false),
        1 => (left_register, right_register, true),
        2 => (right_register, left_register, false),
        3 => (right_register, left_register, true),
        _ => unreachable!(),
    };
    let int = types.intern_type_spec("int");
    let result = context
        .current_function_data_mut()
        .at(expression)?
        .allocate_gpr(int)
        .at(expression)?;
    context
        .current_function_data_mut()
        .at(expression)?
        .emit_integer_compare(
            IntegerComparison::LessThan,
            left_signed,
            result,
            left_register,
            right_register,
        );
    Ok(if inverted {
        BranchCondition::Zero(result.into())
    } else {
        BranchCondition::NonZero(result.into())
    })
}

fn compile_ordered_comparison_value(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let condition =
        compile_ordered_condition(frontend, types, goos, context, expression, head, rest)?;
    condition_value(types, context, expression, condition)
}

fn compile_logical(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let is_or = matches!(goos.parser.objects.symbols.resolve(head), Some("or"));
    let operands = ListCursor::new(head, expression.span, rest)
        .remaining(&goos.parser.objects, "operand")?
        .into_iter()
        .map(ParsedArgument::into_object)
        .collect::<Vec<_>>();
    if operands.is_empty() {
        return Err(diagnostic_at(
            expression,
            "logical form requires at least one operand",
        ));
    }
    if operands.len() == 1 {
        return compile_expression(frontend, types, goos, context, &operands[0]);
    }

    let object = types.intern_type_spec("object");
    let end = context
        .current_function_data_mut()
        .at(expression)?
        .allocate_label()
        .at(expression)?;
    let mut result = None;
    let mut result_type = None;
    for operand in &operands[..operands.len() - 1] {
        let not_operand = if matches!(operand.value, ObjectValue::Pair(_)) {
            let (operand_head, operand_rest) = form_parts(&goos.parser.objects, operand)?;
            if goos.parser.objects.symbols.resolve(operand_head) == Some("not") {
                let mut cursor = ListCursor::new(operand_head, operand.span, operand_rest);
                let value = cursor
                    .required(&goos.parser.objects, "value")?
                    .into_object();
                cursor.finish(&goos.parser.objects)?;
                Some(value)
            } else {
                None
            }
        } else {
            None
        };
        if let Some(not_operand) = not_operand {
            let mut tested = compile_expression(frontend, types, goos, context, &not_operand)?;
            let tested = flush_value(context, &not_operand, &mut tested)?;
            let inverted = if let Some(IrInstruction::ConditionalMove {
                operation,
                destination,
                source: Register::Physical(PhysicalRegister::Gpr(Gpr::S7)),
                ..
            }) = context
                .current_function_data_mut()
                .at(operand)?
                .ir
                .last_mut()
                && *destination == tested
            {
                *operation = match *operation {
                    ConditionalMoveOperation::Zero => ConditionalMoveOperation::NonZero,
                    ConditionalMoveOperation::NonZero => ConditionalMoveOperation::Zero,
                };
                true
            } else {
                false
            };
            let destination = match result {
                Some(result) => result,
                None => {
                    let register = context
                        .current_function_data_mut()
                        .at(expression)?
                        .allocate_gpr(object)
                        .at(expression)?;
                    result = Some(register);
                    register
                }
            };
            let symbol = types.intern_type_spec("symbol");
            result_type = Some(match result_type {
                Some(previous) => types
                    .lowest_common_ancestor(previous, symbol, &context.file.visibility)
                    .at(operand)?,
                None => symbol,
            });
            let data = context.current_function_data_mut().at(operand)?;
            let conjunction = !is_or;
            data.emit_branch_likely(
                if conjunction == inverted {
                    BranchCondition::Equal(
                        Register::Physical(PhysicalRegister::Gpr(Gpr::S7)),
                        tested,
                    )
                } else {
                    BranchCondition::NotEqual(
                        Register::Physical(PhysicalRegister::Gpr(Gpr::S7)),
                        tested,
                    )
                },
                end,
            );
            if inverted {
                data.emit_move(destination, tested);
            } else if conjunction {
                data.emit_move(destination, PhysicalRegister::Gpr(Gpr::S7));
            } else {
                data.emit(IrInstruction::IntegerAddImmediate {
                    destination: destination.into(),
                    source: Register::Physical(PhysicalRegister::Gpr(Gpr::S7)),
                    value: TRUE_OFFSET,
                });
            }
            continue;
        }
        let mut value = compile_expression(frontend, types, goos, context, operand)?;
        result_type = Some(match result_type {
            Some(previous) => types
                .lowest_common_ancestor(previous, value.ty, &context.file.visibility)
                .at(operand)?,
            None => value.ty,
        });
        let value = flush_value(context, operand, &mut value)?;
        let destination = match result {
            Some(result) => result,
            None => {
                let register = context
                    .current_function_data_mut()
                    .at(expression)?
                    .allocate_gpr(object)
                    .at(expression)?;
                result = Some(register);
                register
            }
        };
        let data = context.current_function_data_mut().at(operand)?;
        data.emit_branch_likely(
            if is_or {
                BranchCondition::NotEqual(Register::Physical(PhysicalRegister::Gpr(Gpr::S7)), value)
            } else {
                BranchCondition::Equal(Register::Physical(PhysicalRegister::Gpr(Gpr::S7)), value)
            },
            end,
        );
        data.emit_move(destination, value);
    }

    let last = operands.last().expect("or has at least two operands");
    let mut value = compile_expression(frontend, types, goos, context, last)?;
    result_type = Some(
        types
            .lowest_common_ancestor(
                result_type.expect("or compiled an earlier operand"),
                value.ty,
                &context.file.visibility,
            )
            .at(last)?,
    );
    let value = flush_value(context, last, &mut value)?;
    let result = result.expect("or allocated its result register");
    context
        .current_function_data_mut()
        .at(last)?
        .emit_move(result, value);
    context
        .current_function_data_mut()
        .at(expression)?
        .bind_label(end)
        .at(expression)?;
    let result_type = result_type.expect("or has operands");
    context
        .current_function_data_mut()
        .at(expression)?
        .registers[result.0 as usize]
        .ty = result_type;
    Ok(Value::register(result_type, result))
}

fn compile_cond(
    frontend: &mut Frontend,
    expression: &Object,
    head: SymbolId,
    rest: Object,
    _: ExpressionPosition,
    handler: &mut FormCompileContext<'_>,
) -> Result<Value, Diagnostic> {
    let FormCompileContext {
        types,
        goos,
        context,
    } = handler;
    struct Clause {
        test: Object,
        body: Vec<Object>,
        is_else: bool,
    }

    let clauses = {
        let objects = &goos.parser.objects;
        let cursor = ListCursor::new(head, expression.span, rest);
        let clauses = cursor
            .remaining(objects, "clause")?
            .into_iter()
            .map(ParsedArgument::into_object)
            .collect::<Vec<_>>();
        if clauses.is_empty() {
            return Err(diagnostic_at(
                expression,
                "cond requires at least one clause",
            ));
        }
        let else_symbol = SymbolId(fnv64(b"else"));
        let mut parsed = Vec::with_capacity(clauses.len());
        let mut saw_else = false;
        for clause in clauses {
            if !matches!(clause.value, ObjectValue::Pair(_) | ObjectValue::EmptyList) {
                return Err(diagnostic_at(&clause, "cond clause must be a list"));
            }
            let clause_span = clause.span;
            let mut clause_cursor = ListCursor::new(head, clause_span, clause);
            let test = clause_cursor.required(objects, "test")?.into_object();
            let body = clause_cursor
                .remaining_body(objects)?
                .into_iter()
                .map(ParsedArgument::into_object)
                .collect::<Vec<_>>();
            let is_else =
                matches!(test.value, ObjectValue::Symbol(symbol) if symbol == else_symbol);
            if saw_else {
                return Err(match clause_span {
                    Some(span) => {
                        Diagnostic::error("cond cannot have a clause after else").with_span(span)
                    }
                    None => Diagnostic::error("cond cannot have a clause after else"),
                });
            }
            saw_else = is_else;
            parsed.push(Clause {
                test,
                body,
                is_else,
            });
        }
        parsed
    };
    let has_else = clauses.last().is_some_and(|clause| clause.is_else);
    let object_type = types.intern_type_spec("object");
    let result = context
        .current_function_data_mut()
        .at(expression)?
        .allocate_gpr(object_type)
        .at(expression)?;
    let end = context
        .current_function_data_mut()
        .at(expression)?
        .allocate_label()
        .at(expression)?;
    let mut result_types = Vec::with_capacity(clauses.len());

    for (index, clause) in clauses.iter().enumerate() {
        let is_last = index + 1 == clauses.len();
        let next = if clause.is_else {
            None
        } else {
            Some(
                context
                    .current_function_data_mut()
                    .at(&clause.test)?
                    .allocate_label()
                    .at(&clause.test)?,
            )
        };
        if let Some(next) = next {
            let condition = invert_condition(compile_condition(
                frontend,
                types,
                goos,
                context,
                &clause.test,
            )?)
            .at(&clause.test)?;
            let data = context.current_function_data_mut().at(&clause.test)?;
            data.emit(IrInstruction::Branch {
                condition,
                target: next,
                delay: crate::ir::BranchDelay::Normal,
            });
            if has_else {
                data.emit(IrInstruction::Nop);
            } else {
                data.emit(IrInstruction::Move {
                    destination: result.into(),
                    source: Register::Physical(PhysicalRegister::Gpr(crate::ir::Gpr::S7)),
                    must_emit: false,
                });
            }
        }

        let mut clause_result = if clause.body.is_empty() {
            Value::new(object_type, ValueKind::SymbolObject(SymbolId(fnv64(b"#f"))))
        } else {
            compile_sequence(
                frontend,
                types,
                goos,
                context,
                expression,
                &clause.body,
                ExpressionPosition::Value,
            )?
        };
        result_types.push(clause_result.ty);
        if clause_result.kind != ValueKind::None {
            let source = flush_value(context, expression, &mut clause_result)?;
            context
                .current_function_data_mut()
                .at(expression)?
                .emit(IrInstruction::Move {
                    destination: result.into(),
                    source,
                    must_emit: false,
                });
        }
        if !clause.is_else && !is_last {
            let data = context.current_function_data_mut().at(expression)?;
            data.emit(IrInstruction::Branch {
                condition: BranchCondition::Always,
                target: end,
                delay: crate::ir::BranchDelay::Normal,
            });
            data.emit(IrInstruction::Nop);
        }
        if let Some(next) = next {
            context
                .current_function_data_mut()
                .at(expression)?
                .bind_label(next)
                .at(expression)?;
        }
    }
    context
        .current_function_data_mut()
        .at(expression)?
        .bind_label(end)
        .at(expression)?;
    let mut result_type = result_types[0];
    for case_type in result_types.into_iter().skip(1) {
        result_type = types
            .lowest_common_ancestor(result_type, case_type, &context.file.visibility)
            .at(expression)?;
    }
    if !has_else && result_type == context.none_type {
        result_type = object_type;
    }
    let result_class = types
        .layout(types.type_spec_base(result_type), &context.file.visibility)
        .at(expression)?
        .register_class;
    let result_register = &mut context
        .current_function_data_mut()
        .at(expression)?
        .registers[result.0 as usize];
    result_register.ty = result_type;
    if result_class == RegisterClass::Gpr128 {
        result_register.class = RegisterClass::Gpr128;
    }
    Ok(Value::register(result_type, result))
}

fn compile_return(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let value_expression = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let value = cursor.required(objects, "value")?.into_object();
        cursor.finish(objects)?;
        value
    };
    let mut value = compile_expression(frontend, types, goos, context, &value_expression)?;
    let source = flush_return_value(context, &value_expression, &mut value)?;
    let target = if let Some(target) = context.current_function().at(expression)?.return_target {
        target
    } else {
        let data = context.current_function_data_mut().at(expression)?;
        let label = data.allocate_label().at(expression)?;
        let result = data.allocate_gpr(value.ty).at(expression)?;
        let target = ReturnTarget {
            label,
            result: Some(result),
        };
        context.current_function_mut().at(expression)?.return_target = Some(target);
        target
    };
    let result = target.result.expect("new return targets have a result");
    let data = context.current_function_data_mut().at(expression)?;
    data.emit(IrInstruction::Move {
        destination: result.into(),
        source,
        must_emit: true,
    });
    data.emit(IrInstruction::Branch {
        condition: BranchCondition::Always,
        target: target.label,
        delay: crate::ir::BranchDelay::Normal,
    });
    data.emit(IrInstruction::Nop);
    let dead_type = types.intern_type_spec("int");
    let dead = data.allocate_gpr(dead_type).at(expression)?;
    data.emit(IrInstruction::Move {
        destination: dead.into(),
        source: Register::Physical(PhysicalRegister::ZERO),
        must_emit: false,
    });
    Ok(Value::register(dead_type, dead))
}

fn compile_goto(
    goos: &Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let label = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let label = cursor.required(objects, "label")?.symbol(objects)?;
        cursor.finish(objects)?;
        label
    };
    let label = context.named_label(label).at(expression)?;
    let data = context.current_function_data_mut().at(expression)?;
    data.emit(IrInstruction::Branch {
        condition: BranchCondition::Always,
        target: label,
        delay: crate::ir::BranchDelay::Normal,
    });
    data.emit(IrInstruction::Nop);
    Ok(Value::new(context.none_type, ValueKind::None))
}

fn compile_label(
    goos: &Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let label = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let label = cursor.required(objects, "name")?.symbol(objects)?;
        cursor.finish(objects)?;
        label
    };
    let label = context.named_label(label).at(expression)?;
    context
        .current_function_data_mut()
        .at(expression)?
        .bind_label(label)
        .at(expression)?;
    Ok(Value::new(context.none_type, ValueKind::None))
}

fn compile_nop(
    goos: &Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    ListCursor::new(head, expression.span, rest).finish(&goos.parser.objects)?;
    context
        .current_function_data_mut()
        .at(expression)?
        .emit(IrInstruction::Nop);
    Ok(Value::new(context.none_type, ValueKind::None))
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum RletBindingExtent {
    Normal,
    Lexical,
}

fn compile_rlet(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
    position: ExpressionPosition,
) -> Result<Value, Diagnostic> {
    let (declarations, body) = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let declarations = cursor
            .required(objects, "register declarations")?
            .list_cursor(objects)?
            .remaining(objects, "register declaration")?
            .into_iter()
            .map(ParsedArgument::into_object)
            .collect::<Vec<_>>();
        let body = cursor
            .remaining_body(objects)?
            .into_iter()
            .map(ParsedArgument::into_object)
            .collect::<Vec<_>>();
        (declarations, body)
    };
    if body.is_empty() {
        return Err(diagnostic_at(expression, "rlet body cannot be empty"));
    }
    context
        .current_function_mut()
        .at(expression)?
        .push_scope()
        .at(expression)?;
    let mut lexical_registers = Vec::new();
    for declaration in declarations {
        let (name, options) = {
            let objects = &goos.parser.objects;
            let mut declaration = ListCursor::new(head, declaration.span, declaration);
            let name = declaration
                .required(objects, "register name")?
                .symbol(objects)?;
            let options = declaration
                .remaining(objects, "register option")?
                .into_iter()
                .map(ParsedArgument::into_object)
                .collect::<Vec<_>>();
            (name, options)
        };
        if options.len() % 2 != 0 {
            return Err(diagnostic_at(
                expression,
                "rlet register options must be keyword/value pairs",
            ));
        }
        let mut ty = None;
        let mut class = None;
        let mut binding_extent = RletBindingExtent::Normal;
        for option in options.chunks_exact(2) {
            let ObjectValue::Symbol(keyword) = option[0].value else {
                return Err(diagnostic_at(&option[0], "rlet option must be a keyword"));
            };
            match goos.parser.objects.symbols.resolve(keyword) {
                Some(":type") => {
                    ty = Some(parse_type_spec(
                        types,
                        &goos.parser.objects,
                        &context.file.visibility,
                        &option[1],
                    )?);
                }
                Some(":class") => {
                    let ObjectValue::Symbol(class_name) = option[1].value else {
                        return Err(diagnostic_at(&option[1], "rlet :class must be a symbol"));
                    };
                    class = Some(match goos.parser.objects.symbols.resolve(class_name) {
                        Some("gpr") => RegisterClass::Gpr,
                        Some("fpr") => RegisterClass::Fpr,
                        Some("gpr128" | "i128") => RegisterClass::Gpr128,
                        Some("vf") => {
                            return Err(diagnostic_at(
                                &option[1],
                                "virtual VF allocation is not implemented yet",
                            ));
                        }
                        Some(name) => {
                            return Err(diagnostic_at(
                                &option[1],
                                format!("unknown rlet register class `{name}`"),
                            ));
                        }
                        None => {
                            return Err(diagnostic_at(
                                &option[1],
                                "rlet register class has an unknown symbol ID",
                            ));
                        }
                    });
                }
                Some(":bind") => {
                    let mode = match option[1].value {
                        ObjectValue::Symbol(mode) => mode,
                        _ => quoted_symbol(&goos.parser.objects, &option[1], "rlet :bind mode")?,
                    };
                    binding_extent = match goos.parser.objects.symbols.resolve(mode) {
                        Some("#f") => RletBindingExtent::Normal,
                        Some("lexical") => RletBindingExtent::Lexical,
                        Some("lambda") => {
                            return Err(diagnostic_at(
                                &option[1],
                                "rlet :bind lambda is not implemented yet",
                            ));
                        }
                        Some(mode) => {
                            return Err(diagnostic_at(
                                &option[1],
                                format!("unknown rlet :bind mode `{mode}`"),
                            ));
                        }
                        None => {
                            return Err(diagnostic_at(
                                &option[1],
                                "rlet :bind mode has an unknown symbol ID",
                            ));
                        }
                    };
                }
                Some(":reg") => {
                    return Err(diagnostic_at(
                        &option[0],
                        "explicit rlet hardware registers are not implemented yet",
                    ));
                }
                Some(keyword) => {
                    return Err(diagnostic_at(
                        &option[0],
                        format!("unknown rlet option `{keyword}`"),
                    ));
                }
                None => {
                    return Err(diagnostic_at(
                        &option[0],
                        "rlet option has an unknown symbol ID",
                    ));
                }
            }
        }
        let ty = ty.unwrap_or_else(|| types.intern_type_spec("int"));
        let class = match class {
            Some(class) => class,
            None => {
                types
                    .layout(types.type_spec_base(ty), &context.file.visibility)
                    .at(expression)?
                    .register_class
            }
        };
        let register = context
            .current_function_data_mut()
            .at(expression)?
            .allocate_register(ty, class)
            .at(expression)?;
        context
            .current_function_data_mut()
            .at(expression)?
            .single_web_registers
            .push(register);
        if binding_extent == RletBindingExtent::Lexical {
            lexical_registers.push(register);
        }
        context
            .current_function_mut()
            .at(expression)?
            .define_local(name, LexicalBinding { register })
            .at(expression)?;
    }
    let reservation_start = context.current_function_data().at(expression)?.ir.len();
    let result = compile_sequence(frontend, types, goos, context, expression, &body, position)?;
    let reservation_end = context.current_function_data().at(expression)?.ir.len();
    for register in lexical_registers {
        context
            .current_function_data_mut()
            .at(expression)?
            .register_reservations
            .push(RegisterReservation {
                register,
                start: reservation_start,
                end: reservation_end,
            });
    }
    context
        .current_function_mut()
        .at(expression)?
        .pop_scope()
        .at(expression)?;
    Ok(result)
}

fn native_register(
    context: &FrontendContext,
    expression: &Object,
) -> Result<(VirtualRegisterId, TypeSpec, RegisterClass), Diagnostic> {
    let ObjectValue::Symbol(name) = expression.value else {
        return Err(diagnostic_at(
            expression,
            "native instruction register must be a local name",
        ));
    };
    let binding = context
        .current_function()
        .at(expression)?
        .lookup_local(name)
        .ok_or_else(|| diagnostic_at(expression, "unknown native instruction register"))?;
    let description = context
        .current_function_data()
        .at(expression)?
        .registers
        .get(binding.register.0 as usize)
        .copied()
        .ok_or_else(|| diagnostic_at(expression, "native register binding is invalid"))?;
    Ok((binding.register, description.ty, description.class))
}

fn compile_native_move(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let (destination, source_expression) = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let destination = cursor.required(objects, "destination")?.into_object();
        let source = cursor.required(objects, "source")?.into_object();
        cursor.finish(objects)?;
        (destination, source)
    };
    let (destination, _, _) = native_register(context, &destination)?;
    let mut source = compile_expression(frontend, types, goos, context, &source_expression)?;
    let source = flush_value(context, &source_expression, &mut source)?;
    context
        .current_function_data_mut()
        .at(expression)?
        .emit(IrInstruction::Move {
            destination: destination.into(),
            source,
            must_emit: true,
        });
    Ok(Value::new(context.none_type, ValueKind::None))
}

fn compile_move_to_cop0(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let (cop0_register, source_expression) = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let cop0_register = cursor.required(objects, "COP0 register")?.symbol(objects)?;
        let source = cursor.required(objects, "source")?.into_object();
        cursor.finish(objects)?;
        (cop0_register, source)
    };
    let register = match goos.parser.objects.symbols.resolve(cop0_register) {
        Some("cop0") => Cop0Register::Debug,
        Some("dab") => Cop0Register::DataAddressBreakpoint,
        Some("dabm") => Cop0Register::DataAddressBreakpointMask,
        Some(name) => {
            return Err(diagnostic_at(
                expression,
                format!("unknown writable COP0 register `{name}`"),
            ));
        }
        None => return Err(diagnostic_at(expression, "unknown COP0 register symbol")),
    };
    let mut source = compile_expression(frontend, types, goos, context, &source_expression)?;
    let source = flush_gpr_operand(context, &source_expression, &mut source)?;
    context
        .current_function_data_mut()
        .at(expression)?
        .emit(IrInstruction::MoveToCop0 { source, register });
    Ok(Value::new(context.none_type, ValueKind::None))
}

fn compile_native_arithmetic(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let add = goos.parser.objects.symbols.resolve(head) == Some("add");
    let (destination, left_expression, right_expression) = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let destination = cursor.required(objects, "destination")?.into_object();
        let left = cursor.required(objects, "left")?.into_object();
        let right = cursor.required(objects, "right")?.into_object();
        cursor.finish(objects)?;
        (destination, left, right)
    };
    let (destination, _, class) = native_register(context, &destination)?;
    if class != RegisterClass::Gpr {
        return Err(diagnostic_at(
            expression,
            "native add and sub require GPR operands",
        ));
    }
    let mut left = compile_expression(frontend, types, goos, context, &left_expression)?;
    let left = flush_gpr_operand(context, &left_expression, &mut left)?;
    if let ObjectValue::Integer(value) = right_expression.value {
        let immediate = if add {
            Some(value)
        } else {
            value.checked_neg()
        };
        if let Some(immediate) = immediate.and_then(|value| i16::try_from(value).ok()) {
            context.current_function_data_mut().at(expression)?.emit(
                IrInstruction::IntegerAddImmediate {
                    destination: destination.into(),
                    source: left,
                    value: immediate,
                },
            );
            return Ok(Value::new(context.none_type, ValueKind::None));
        }
    }
    let mut right = compile_expression(frontend, types, goos, context, &right_expression)?;
    let right = flush_gpr_operand(context, &right_expression, &mut right)?;
    context
        .current_function_data_mut()
        .at(expression)?
        .emit(IrInstruction::IntegerArithmetic {
            operation: if add {
                ArithmeticOperation::Add
            } else {
                ArithmeticOperation::Subtract
            },
            signed: false,
            destination: destination.into(),
            left,
            right,
        });
    Ok(Value::new(context.none_type, ValueKind::None))
}

fn compile_native_memory(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let load = goos.parser.objects.symbols.resolve(head) == Some("l.q");
    let (register_expression, base_expression, offset) = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let register = cursor.required(objects, "register")?.into_object();
        let base = cursor.required(objects, "base")?.into_object();
        let offset = cursor
            .optional(objects, "offset")?
            .map(|offset| offset.integer(objects))
            .transpose()?
            .unwrap_or(0);
        cursor.finish(objects)?;
        (register, base, offset)
    };
    let offset = i16::try_from(offset)
        .map_err(|_| diagnostic_at(expression, "native memory offset exceeds 16 bits"))?;
    let (register, _, class) = native_register(context, &register_expression)?;
    if !matches!(class, RegisterClass::Gpr | RegisterClass::Gpr128) {
        return Err(diagnostic_at(
            &register_expression,
            "l.q and s.q require a GPR",
        ));
    }
    let mut base = compile_expression(frontend, types, goos, context, &base_expression)?;
    let base = flush_gpr_operand(context, &base_expression, &mut base)?;
    let instruction = if load {
        IrInstruction::LoadMemory {
            operation: MemoryLoad::Quadword,
            destination: register.into(),
            base,
            offset,
        }
    } else {
        IrInstruction::StoreMemory {
            operation: crate::ir::MemoryStore::Quadword,
            source: register.into(),
            base,
            offset,
        }
    };
    context
        .current_function_data_mut()
        .at(expression)?
        .emit(instruction);
    Ok(Value::new(context.none_type, ValueKind::None))
}

fn compile_native_branch(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let delay_keyword = goos.parser.objects.symbols.intern(":delay");
    let operation = goos.parser.objects.symbols.resolve(head).map(str::to_owned);
    let (value_expression, label, delay_expression) = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let value = cursor.required(objects, "value")?.into_object();
        if matches!(operation.as_deref(), Some("b.lt" | "b.ge")) {
            let zero = cursor.required(objects, "zero")?;
            if zero.integer(objects)? != 0 {
                return Err(diagnostic_at(
                    zero.object(),
                    "b.lt and b.ge currently require zero as their right operand",
                ));
            }
        }
        let label = cursor.required(objects, "label")?.symbol(objects)?;
        let mut keywords = cursor.keywords(
            objects,
            &[KeywordSpec {
                symbol: delay_keyword,
                name: "delay",
                required: true,
                syntax: SyntaxType::Any,
            }],
        )?;
        let delay = keywords.take(delay_keyword).unwrap().into_object();
        (value, label, delay)
    };
    let mut value = compile_expression(frontend, types, goos, context, &value_expression)?;
    let value = flush_gpr_operand(context, &value_expression, &mut value)?;
    let condition = match operation.as_deref() {
        Some("b.lt") => BranchCondition::Negative(value),
        Some("b.ge") => BranchCondition::NonNegative(value),
        Some("b.z") => BranchCondition::Zero(value),
        _ => unreachable!(),
    };
    let target = context.named_label(label).at(expression)?;
    context
        .current_function_data_mut()
        .at(expression)?
        .emit(IrInstruction::Branch {
            condition,
            target,
            delay: crate::ir::BranchDelay::Normal,
        });
    let before_delay = context.current_function_data().at(expression)?.ir.len();
    compile_expression_in_position(
        frontend,
        types,
        goos,
        context,
        &delay_expression,
        ExpressionPosition::Statement,
    )?;
    let after_delay = context.current_function_data().at(expression)?.ir.len();
    if after_delay != before_delay + 1 {
        return Err(diagnostic_at(
            &delay_expression,
            "native branch delay expression must emit exactly one instruction",
        ));
    }
    Ok(Value::new(context.none_type, ValueKind::None))
}

fn compile_aop_align(
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let error_keyword = goos.parser.objects.symbols.intern(":error");
    let (bits, error_mode) = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let bits = cursor
            .required(objects, "alignment bits")?
            .integer(objects)?;
        let mut keywords = cursor.keywords(
            objects,
            &[KeywordSpec {
                symbol: error_keyword,
                name: "error",
                required: false,
                syntax: SyntaxType::Any,
            }],
        )?;
        let error_mode = keywords
            .take(error_keyword)
            .map(|mode| mode.symbol(objects))
            .transpose()?;
        (bits, error_mode)
    };
    if let Some(error_mode) = error_mode
        && goos.parser.objects.symbols.resolve(error_mode) != Some(":silent")
    {
        return Err(diagnostic_at(
            expression,
            "aop-align reporting modes are not implemented yet",
        ));
    }
    if bits != 64 {
        return Err(diagnostic_at(
            expression,
            "only 64-bit instruction alignment is implemented yet",
        ));
    }
    context
        .current_function_data_mut()
        .at(expression)?
        .emit(IrInstruction::Align { bits: bits as u16 });
    Ok(Value::new(context.none_type, ValueKind::None))
}

fn compile_type_cast(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let (type_expression, value_expression) = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let ty = cursor.required(objects, "type")?.into_object();
        let value = cursor.required(objects, "value")?.into_object();
        cursor.finish(objects)?;
        (ty, value)
    };
    let ty = if let ObjectValue::Pair(_) = type_expression.value {
        let (type_head, type_rest) = form_parts(&goos.parser.objects, &type_expression)?;
        if goos.parser.objects.symbols.resolve(type_head) == Some("current-method-type") {
            ListCursor::new(type_head, type_expression.span, type_rest)
                .finish(&goos.parser.objects)?;
            let method = context
                .current_function()
                .at(expression)?
                .method
                .ok_or_else(|| {
                    diagnostic_at(
                        &type_expression,
                        "current-method-type requires a method context",
                    )
                })?;
            types.intern_compound_type_spec(method.owner, [], None)
        } else {
            parse_type_spec(
                types,
                &goos.parser.objects,
                &context.file.visibility,
                &type_expression,
            )?
        }
    } else {
        parse_type_spec(
            types,
            &goos.parser.objects,
            &context.file.visibility,
            &type_expression,
        )?
    };
    let mut value = compile_expression(frontend, types, goos, context, &value_expression)?;
    value.ty = ty;
    Ok(value)
}

fn compile_pair_access(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let value_expression = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let value = cursor.required(objects, "pair")?.into_object();
        cursor.finish(objects)?;
        value
    };
    let mut pair = compile_expression(frontend, types, goos, context, &value_expression)?;
    let base = flush_value(context, &value_expression, &mut pair)?;
    let offset = match goos.parser.objects.symbols.resolve(head) {
        Some("car") => -2,
        Some("cdr") => 2,
        _ => unreachable!(),
    };
    Ok(Value::place(
        types.intern_type_spec("object"),
        Place {
            base: PlaceBase::Memory { base, offset },
            projection: PlaceProjection::Whole(LoadInfo {
                size: 4,
                sign_extend: true,
                register_class: RegisterClass::Gpr,
            }),
        },
    ))
}

fn compile_new(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
    position: ExpressionPosition,
) -> Result<Value, Diagnostic> {
    let (allocation_expression, type_expression, argument_expressions) = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let allocation = cursor.required(objects, "allocation")?.into_object();
        let runtime_type = cursor.required(objects, "runtime type")?.into_object();
        let arguments = cursor
            .remaining(objects, "constructor argument")?
            .into_iter()
            .map(ParsedArgument::into_object)
            .collect::<Vec<_>>();
        (allocation, runtime_type, arguments)
    };
    let allocation = quoted_symbol(
        &goos.parser.objects,
        &allocation_expression,
        "new allocation",
    )?;
    if goos.parser.objects.symbols.resolve(allocation) != Some("global") {
        return Err(diagnostic_at(
            &allocation_expression,
            "only global allocation is implemented for new",
        ));
    }
    let runtime_type = quoted_symbol(&goos.parser.objects, &type_expression, "new runtime type")?;
    let runtime_type_name = goos
        .parser
        .objects
        .symbols
        .resolve(runtime_type)
        .ok_or_else(|| diagnostic_at(&type_expression, "runtime type has an unknown symbol ID"))?;
    let runtime_type = types.intern_type(runtime_type_name);
    if !types.is_declared(runtime_type, &context.file.visibility) {
        return Err(diagnostic_at(
            &type_expression,
            format!("unknown runtime type `{runtime_type_name}`"),
        ));
    }
    let runtime_type_spec = types.intern_compound_type_spec(runtime_type, [], None);
    let new = types.intern_symbol("new");
    let method =
        compile_get_method_of_type(frontend, types, context, expression, runtime_type_spec, new)?;
    let allocation = symbol_object(types, allocation);
    let runtime_type_object =
        compile_global_value(frontend, types, context, expression, runtime_type.symbol())?;
    let mut arguments = Vec::with_capacity(argument_expressions.len() + 2);
    arguments.push(allocation);
    arguments.push(runtime_type_object);
    for argument in &argument_expressions {
        arguments.push(compile_expression(
            frontend, types, goos, context, argument,
        )?);
    }
    compile_function_call(types, context, expression, method, arguments, position)
}

fn compile_runtime_type(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let value_expression = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let value = cursor.required(objects, "object")?.into_object();
        cursor.finish(objects)?;
        value
    };
    let mut value = compile_expression(frontend, types, goos, context, &value_expression)?;
    let source = flush_gpr_operand(context, &value_expression, &mut value)?;
    let int = types.intern_type_spec("int");
    let type_type = types.intern_type_spec("type");
    let discriminator = context
        .current_function_data_mut()
        .at(expression)?
        .allocate_gpr(int)
        .at(expression)?;
    let result = context
        .current_function_data_mut()
        .at(expression)?
        .allocate_gpr(type_type)
        .at(expression)?;
    let done = context
        .current_function_data_mut()
        .at(expression)?
        .allocate_label()
        .at(expression)?;
    let data = context.current_function_data_mut().at(expression)?;
    data.emit(IrInstruction::IntegerShiftImmediate {
        operation: ShiftOperation::Left,
        destination: discriminator.into(),
        value: source,
        amount: 61,
    });
    data.emit(IrInstruction::Branch {
        condition: BranchCondition::Zero(discriminator.into()),
        target: done,
        delay: crate::ir::BranchDelay::Likely,
    });
    data.emit(IrInstruction::GetSymbolValue {
        destination: result.into(),
        symbol: SymbolId(fnv64(b"binteger")),
    });
    data.emit(IrInstruction::Branch {
        condition: BranchCondition::Positive(discriminator.into()),
        target: done,
        delay: crate::ir::BranchDelay::Likely,
    });
    data.emit(IrInstruction::GetSymbolValue {
        destination: result.into(),
        symbol: SymbolId(fnv64(b"pair")),
    });
    data.emit(IrInstruction::LoadMemory {
        operation: MemoryLoad::UnsignedWord,
        destination: result.into(),
        base: source,
        offset: -4,
    });
    data.bind_label(done).at(expression)?;
    Ok(Value::register(type_type, result))
}

fn compile_current_method_type(
    frontend: &Frontend,
    types: &mut TypeSystem,
    goos: &Interpreter,
    context: &FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    ListCursor::new(head, expression.span, rest).finish(&goos.parser.objects)?;
    let Some(method) = context.current_function().at(expression)?.method else {
        return Ok(symbol_object(types, SymbolId(fnv64(b"#f"))));
    };
    compile_global_value(frontend, types, context, expression, method.owner.symbol())
}

fn compile_method_of_type(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let objects = &goos.parser.objects;
    let mut cursor = ListCursor::new(head, expression.span, rest);
    let owner = cursor.required(objects, "type")?.into_object();
    let method = cursor.required(objects, "method")?.symbol(objects)?;
    cursor.finish(objects)?;
    let static_type_name = match owner.value {
        ObjectValue::Symbol(name) => Some(name),
        ObjectValue::Pair(_) => form_parts(objects, &owner).ok().map(|(name, _)| name),
        _ => None,
    };
    let static_type = static_type_name
        .and_then(|name| objects.symbols.resolve(name))
        .map(|name| types.intern_type(name))
        .is_some_and(|ty| types.is_declared(ty, &context.file.visibility));
    if static_type {
        let owner = parse_type_spec(types, objects, &context.file.visibility, &owner)?;
        return compile_get_method_of_type(frontend, types, context, expression, owner, method);
    }
    let mut runtime_type = compile_expression(frontend, types, goos, context, &owner)?;
    let type_type = types.intern_type_spec("type");
    types
        .typecheck(type_type, runtime_type.ty, &context.file.visibility)
        .at(&owner)?;
    let compile_time_type = types.intern_type_spec("object");
    compile_get_method_from_runtime_type(
        types,
        context,
        expression,
        compile_time_type,
        method,
        &mut runtime_type,
    )
}

fn quoted_symbol(
    objects: &ObjectStore,
    expression: &Object,
    description: &'static str,
) -> Result<SymbolId, Diagnostic> {
    let (head, rest) = form_parts(objects, expression)?;
    if objects.symbols.resolve(head) != Some("quote") {
        return Err(diagnostic_at(
            expression,
            format!("{description} must be quoted"),
        ));
    }
    let mut cursor = ListCursor::new(head, expression.span, rest);
    let symbol = cursor.required(objects, description)?.symbol(objects)?;
    cursor.finish(objects)?;
    Ok(symbol)
}

fn compile_when_goto(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let (condition, label) = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let condition = cursor.required(objects, "condition")?.into_object();
        let label = cursor.required(objects, "label")?.symbol(objects)?;
        cursor.finish(objects)?;
        (condition, label)
    };
    let condition = compile_condition(frontend, types, goos, context, &condition)?;
    let label = context.named_label(label).at(expression)?;
    let data = context.current_function_data_mut().at(expression)?;
    data.emit(IrInstruction::Branch {
        condition,
        target: label,
        delay: crate::ir::BranchDelay::Normal,
    });
    data.emit(IrInstruction::Nop);
    Ok(Value::new(context.none_type, ValueKind::None))
}

fn compile_field_access(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let (base_expression, accessors) = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let base = cursor.required(objects, "object")?.into_object();
        let accessors = cursor
            .remaining(objects, "field or index")?
            .into_iter()
            .map(ParsedArgument::into_object)
            .collect::<Vec<_>>();
        (base, accessors)
    };
    let mut value = compile_expression(frontend, types, goos, context, &base_expression)?;
    if accessors.is_empty() {
        if types
            .deref_info(value.ty, &context.file.visibility)
            .at(expression)?
            .is_none()
        {
            return Err(diagnostic_at(
                expression,
                "field access without an accessor requires a pointer",
            ));
        }
        let zero = Object::integer(0, expression.span);
        return compile_index_access(frontend, types, goos, context, &zero, value);
    }
    for accessor in accessors {
        if types
            .deref_info(value.ty, &context.file.visibility)
            .at(&accessor)?
            .is_some()
        {
            value = compile_index_access(frontend, types, goos, context, &accessor, value)?;
            continue;
        }
        let ObjectValue::Symbol(field) = accessor.value else {
            return Err(diagnostic_at(
                &accessor,
                "structure field name must be a symbol",
            ));
        };
        let base_type = value.ty;
        let base = flush_value(context, &accessor, &mut value)?;
        value =
            field_place(types, &context.file.visibility, base, base_type, field).at(&accessor)?;
    }
    Ok(value)
}

fn compile_address_of(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let target = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let target = cursor.required(objects, "place")?.into_object();
        cursor.finish(objects)?;
        target
    };
    let value = compile_expression(frontend, types, goos, context, &target)?;
    let ty = types.pointer_type(value.ty);
    match value.kind {
        ValueKind::Place(Place {
            base: PlaceBase::Memory { base, offset },
            ..
        })
        | ValueKind::IntegerOffset { base, offset } => {
            Ok(Value::new(ty, ValueKind::IntegerOffset { base, offset }))
        }
        ValueKind::Place(Place {
            base:
                PlaceBase::Indexed {
                    base,
                    index,
                    offset,
                    ..
                },
            ..
        })
        | ValueKind::IndexedAddress {
            base,
            index,
            offset,
        } => Ok(Value::new(
            ty,
            ValueKind::IndexedAddress {
                base,
                index,
                offset,
            },
        )),
        _ => Err(diagnostic_at(
            &target,
            "expression is not an addressable place",
        )),
    }
}

fn materialize_place_address(
    context: &mut FrontendContext,
    expression: &Object,
    base: PlaceBase,
) -> Result<(Register, i64), Diagnostic> {
    match base {
        PlaceBase::Memory { base, offset } => Ok((base, offset)),
        PlaceBase::Indexed {
            base,
            index,
            offset,
            address_type,
        } => {
            let address = context
                .current_function_data_mut()
                .at(expression)?
                .allocate_gpr(address_type)
                .at(expression)?;
            let (left, right) = if offset == 0 {
                (base, index)
            } else {
                (index, base)
            };
            context.current_function_data_mut().at(expression)?.emit(
                IrInstruction::IntegerArithmetic {
                    operation: ArithmeticOperation::Add,
                    signed: false,
                    destination: address.into(),
                    left,
                    right,
                },
            );
            Ok((address.into(), offset))
        }
        _ => Err(diagnostic_at(
            expression,
            "place does not have a materializable memory address",
        )),
    }
}

fn compile_pointer_addition(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let (base_expression, offset_expression) = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let base = cursor.required(objects, "pointer")?.into_object();
        let offset = cursor.required(objects, "byte offset")?.into_object();
        cursor.finish(objects)?;
        (base, offset)
    };
    let mut base = compile_expression(frontend, types, goos, context, &base_expression)?;
    if types.type_name(types.type_spec_base(base.ty)) != "pointer" {
        return Err(diagnostic_at(
            &base_expression,
            format!(
                "pointer arithmetic requires a pointer, got `{}`",
                types.format_type_spec(base.ty)
            ),
        ));
    }
    let mut offset = compile_expression(frontend, types, goos, context, &offset_expression)?;
    let integer = types.intern_type_spec("integer");
    types
        .typecheck(integer, offset.ty, &context.file.visibility)
        .at(&offset_expression)?;
    let result_type = base.ty;
    if let ValueKind::Integer(offset) = offset.kind {
        let offset = i64::try_from(offset)
            .map_err(|_| diagnostic_at(&offset_expression, "pointer offset exceeds 64 bits"))?;
        let (base, previous_offset) = match base.kind {
            ValueKind::IntegerOffset { base, offset } => (base, offset),
            ValueKind::SymbolObject(symbol) if symbol == SymbolId(fnv64(b"#f")) => {
                (Register::Physical(PhysicalRegister::Gpr(Gpr::S7)), 0)
            }
            _ => (flush_value(context, &base_expression, &mut base)?, 0),
        };
        let offset = previous_offset
            .checked_add(offset)
            .ok_or_else(|| diagnostic_at(&offset_expression, "pointer offset overflows"))?;
        return Ok(Value::new(
            result_type,
            ValueKind::IntegerOffset { base, offset },
        ));
    }
    let base = flush_value(context, &base_expression, &mut base)?;
    let offset = flush_gpr_operand(context, &offset_expression, &mut offset)?;
    let result = context
        .current_function_data_mut()
        .at(expression)?
        .allocate_gpr(result_type)
        .at(expression)?;
    context
        .current_function_data_mut()
        .at(expression)?
        .emit(IrInstruction::IntegerArithmetic {
            operation: ArithmeticOperation::Add,
            signed: false,
            destination: result.into(),
            left: base,
            right: offset,
        });
    Ok(Value::register(result_type, result))
}

fn compile_index_access(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    index_expression: &Object,
    mut array: Value,
) -> Result<Value, Diagnostic> {
    let deref = types
        .deref_info(array.ty, &context.file.visibility)
        .at(index_expression)?
        .ok_or_else(|| diagnostic_at(index_expression, "value is not indexable"))?;
    if !deref.memory_deref {
        return Err(diagnostic_at(
            index_expression,
            "inline array indexing is not implemented yet",
        ));
    }
    let (base, array_offset) = match array.kind {
        ValueKind::IntegerOffset { base, offset } => (
            base,
            offset
                .checked_add(i64::from(deref.offset))
                .ok_or_else(|| diagnostic_at(index_expression, "array base offset overflows"))?,
        ),
        _ => (
            flush_value(context, index_expression, &mut array)?,
            i64::from(deref.offset),
        ),
    };
    let place_base = if let ObjectValue::Integer(index) = index_expression.value {
        let index = u32::try_from(index)
            .map_err(|_| diagnostic_at(index_expression, "array index cannot be negative"))?;
        let element_offset = i64::from(index)
            .checked_mul(i64::from(deref.stride))
            .ok_or_else(|| diagnostic_at(index_expression, "array index offset overflows"))?;
        let offset = array_offset
            .checked_add(element_offset)
            .ok_or_else(|| diagnostic_at(index_expression, "array index offset overflows"))?;
        PlaceBase::Memory { base, offset }
    } else {
        let index = compile_integer_operand(frontend, types, goos, context, index_expression)?;
        let scaled = if deref.stride == 1 {
            index.register
        } else if deref.stride.is_power_of_two() {
            let scaled = context
                .current_function_data_mut()
                .at(index_expression)?
                .allocate_gpr(index.ty)
                .at(index_expression)?;
            context
                .current_function_data_mut()
                .at(index_expression)?
                .emit(IrInstruction::IntegerShiftImmediate {
                    operation: ShiftOperation::Left,
                    destination: scaled.into(),
                    value: index.register,
                    amount: deref.stride.trailing_zeros() as u8,
                });
            scaled.into()
        } else {
            let mut stride = Value::new(index.ty, ValueKind::Integer(i128::from(deref.stride)));
            let stride = flush_value(context, index_expression, &mut stride)?;
            let scaled = context
                .current_function_data_mut()
                .at(index_expression)?
                .allocate_gpr(index.ty)
                .at(index_expression)?;
            context
                .current_function_data_mut()
                .at(index_expression)?
                .emit(IrInstruction::IntegerArithmetic {
                    operation: ArithmeticOperation::Multiply,
                    signed: false,
                    destination: scaled.into(),
                    left: index.register,
                    right: stride,
                });
            scaled.into()
        };
        PlaceBase::Indexed {
            base,
            index: scaled,
            offset: array_offset,
            address_type: array.ty,
        }
    };
    let size = deref
        .load_size
        .ok_or_else(|| diagnostic_at(index_expression, "indexed value has no load size"))?;
    let result_type = types.coerce_to_register_type(deref.result_type);
    Ok(Value::place(
        result_type,
        Place {
            base: place_base,
            projection: PlaceProjection::Whole(LoadInfo {
                size,
                sign_extend: deref.sign_extend,
                register_class: deref.register_class,
            }),
        },
    ))
}

fn compile_named_call_form(
    frontend: &mut Frontend,
    expression: &Object,
    name: SymbolId,
    rest: Object,
    position: ExpressionPosition,
    handler: &mut FormCompileContext<'_>,
) -> Result<Value, Diagnostic> {
    let function =
        compile_global_value(frontend, handler.types, handler.context, expression, name)?;
    compile_call_form(
        frontend, expression, name, rest, position, function, handler,
    )
}

fn compile_call_form(
    frontend: &mut Frontend,
    expression: &Object,
    name: SymbolId,
    rest: Object,
    position: ExpressionPosition,
    function: Value,
    handler: &mut FormCompileContext<'_>,
) -> Result<Value, Diagnostic> {
    let FormCompileContext {
        types,
        goos,
        context,
    } = handler;
    let argument_expressions = ListCursor::new(name, expression.span, rest)
        .remaining(&goos.parser.objects, "argument")?
        .into_iter()
        .map(ParsedArgument::into_object)
        .collect::<Vec<_>>();
    compile_call_expressions(
        frontend,
        types,
        goos,
        context,
        expression,
        position,
        function,
        argument_expressions,
    )
}

fn compile_call_expressions(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    position: ExpressionPosition,
    mut function: Value,
    argument_expressions: Vec<Object>,
) -> Result<Value, Diagnostic> {
    let (parameters, result_type, _) =
        call_signature(types, expression, function.ty, argument_expressions.len())?;
    let function = prepare_call_value(context, expression, &mut function)?;
    let mut arguments = Vec::with_capacity(argument_expressions.len());
    for (index, argument_expression) in argument_expressions.iter().enumerate() {
        let mut argument = compile_expression_in_position(
            frontend,
            types,
            goos,
            context,
            argument_expression,
            ExpressionPosition::Value,
        )?;
        if index < parameters.len() {
            types
                .typecheck(parameters[index], argument.ty, &context.file.visibility)
                .map_err(|message| {
                    diagnostic_at(
                        argument_expression,
                        format!(
                            "function argument {} has the wrong type: {message}",
                            index + 1
                        ),
                    )
                })?;
        }
        arguments.push(prepare_call_value(
            context,
            argument_expression,
            &mut argument,
        )?);
    }
    context.call(function, &arguments).at(expression)?;
    compile_call_result(types, context, expression, result_type, position)
}

fn compile_global_value(
    frontend: &Frontend,
    types: &TypeSystem,
    context: &FrontendContext,
    expression: &Object,
    name: SymbolId,
) -> Result<Value, Diagnostic> {
    let definition = frontend
        .global_definition(name, &context.file.visibility)
        .at(expression)?
        .ok_or_else(|| diagnostic_at(expression, "unknown variable"))?;
    let ty = definition.ty;
    let layout = types
        .layout(types.type_spec_base(ty), &context.file.visibility)
        .at(expression)?;
    Ok(Value::place(
        ty,
        Place {
            base: PlaceBase::Global(name),
            projection: PlaceProjection::Whole(LoadInfo {
                size: layout.load_size,
                sign_extend: layout.load_signed,
                register_class: layout.register_class,
            }),
        },
    ))
}

fn compile_quote(
    types: &mut TypeSystem,
    goos: &Interpreter,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let objects = &goos.parser.objects;
    let mut cursor = ListCursor::new(head, expression.span, rest);
    let quoted = cursor.required(objects, "object")?.into_object();
    cursor.finish(objects)?;
    match quoted.value {
        ObjectValue::Symbol(symbol) => Ok(symbol_object(types, symbol)),
        ObjectValue::EmptyList => Ok(empty_list_value(types)),
        _ => Err(diagnostic_at(
            &quoted,
            "only quoted symbols and the empty list are implemented yet",
        )),
    }
}

fn symbol_object(types: &mut TypeSystem, symbol: SymbolId) -> Value {
    let ty = types.intern_type_spec("symbol");
    Value::new(ty, ValueKind::SymbolObject(symbol))
}

fn string_constant(
    types: &mut TypeSystem,
    context: &mut FrontendContext,
    value: &str,
) -> Result<Value, String> {
    let segment = context.current_function_data()?.segment;
    let label = context.file.intern_string(segment, value)?;
    let string = types.intern_type_spec("string");
    Ok(Value::new(string, ValueKind::StaticAddress(label)))
}

fn constant_index(
    types: &TypeSystem,
    visibility: &Visibility,
    base: Register,
    array_offset: u32,
    array_type: TypeSpec,
    index: u32,
) -> Result<Value, String> {
    let deref = types
        .deref_info(array_type, visibility)?
        .ok_or_else(|| "value is not an array or pointer".to_owned())?;
    if !deref.memory_deref {
        return Err("constant indexing does not refer to memory".to_owned());
    }
    let element_offset = index
        .checked_mul(deref.stride)
        .ok_or_else(|| "constant array index overflows its byte offset".to_owned())?;
    let offset = array_offset
        .checked_add(element_offset)
        .ok_or_else(|| "constant array index overflows its byte offset".to_owned())?;
    let size = deref
        .load_size
        .ok_or_else(|| "memory dereference has no load size".to_owned())?;
    Ok(Value::place(
        deref.result_type,
        Place {
            base: PlaceBase::Memory {
                base,
                offset: i64::from(offset),
            },
            projection: PlaceProjection::Whole(LoadInfo {
                size,
                sign_extend: deref.sign_extend,
                register_class: deref.register_class,
            }),
        },
    ))
}

fn field_place(
    types: &mut TypeSystem,
    visibility: &Visibility,
    base: Register,
    base_type: TypeSpec,
    field_name: SymbolId,
) -> Result<Value, String> {
    let base_type = types.type_spec_base(base_type);
    if let Ok(field) = types.lookup_bit_field(base_type, field_name, visibility) {
        let field_type = field.field.field_type;
        let offset = u8::try_from(field.field.offset_bits)
            .map_err(|_| "bit-field offset exceeds 255 bits".to_owned())?;
        let size = u8::try_from(field.field.size_bits)
            .map_err(|_| "bit-field size exceeds 255 bits".to_owned())?;
        let container = types.layout(base_type, visibility)?;
        return Ok(Value::place(
            field_type,
            Place {
                base: PlaceBase::Register(base),
                projection: PlaceProjection::BitField(crate::value::BitFieldInfo {
                    offset,
                    size,
                    sign_extend: field.sign_extend,
                    use_128_bits: container.register_class == RegisterClass::Gpr128,
                }),
            },
        ));
    }

    let (needs_deref, stored_type, declared_offset) = {
        let field = types.lookup_field(base_type, field_name, visibility)?;
        (field.needs_deref, field.result_type, field.field.offset)
    };
    let object_offset = types.layout(base_type, visibility)?.offset;
    let offset = i64::from(declared_offset) - i64::from(object_offset);
    if !needs_deref {
        return Ok(Value::new(
            stored_type,
            ValueKind::IntegerOffset { base, offset },
        ));
    }
    let layout = types.layout(types.type_spec_base(stored_type), visibility)?;
    let result_type = types.coerce_to_register_type(stored_type);
    Ok(Value::place(
        result_type,
        Place {
            base: PlaceBase::Memory { base, offset },
            projection: PlaceProjection::Whole(LoadInfo {
                size: layout.load_size,
                sign_extend: layout.load_signed,
                register_class: layout.register_class,
            }),
        },
    ))
}

fn compile_get_method_of_type(
    frontend: &Frontend,
    types: &mut TypeSystem,
    context: &mut FrontendContext,
    expression: &Object,
    compile_time_type: TypeSpec,
    method_name: SymbolId,
) -> Result<Value, Diagnostic> {
    let base_type = types.type_spec_base(compile_time_type);
    let mut runtime_type =
        compile_global_value(frontend, types, context, expression, base_type.symbol())?;
    compile_get_method_from_runtime_type(
        types,
        context,
        expression,
        compile_time_type,
        method_name,
        &mut runtime_type,
    )
}

fn compile_get_method_from_runtime_type(
    types: &mut TypeSystem,
    context: &mut FrontendContext,
    expression: &Object,
    compile_time_type: TypeSpec,
    method_name: SymbolId,
    runtime_type: &mut Value,
) -> Result<Value, Diagnostic> {
    let base_type = types.type_spec_base(compile_time_type);
    let (slot, signature) = {
        let method = types
            .lookup_method(base_type, method_name, &context.file.visibility)
            .at(expression)?;
        (method.slot, method.signature)
    };
    let signature = types.substitute_method_type(signature, compile_time_type);

    let runtime_type = flush_value(context, expression, runtime_type)?;

    let type_type = types.intern_type("type");
    let method_table = types.intern_symbol("method-table");
    let (method_table_offset, declared_method_table_type) = {
        let (field_offset, result_type) = {
            let field = types
                .lookup_field(type_type, method_table, &context.file.visibility)
                .at(expression)?;
            (field.field.offset, field.result_type)
        };
        let object_offset = types
            .layout(type_type, &context.file.visibility)
            .at(expression)?
            .offset;
        (
            field_offset
                .checked_sub(object_offset)
                .ok_or_else(|| diagnostic_at(expression, "method table has an invalid offset"))?,
            result_type,
        )
    };
    if types
        .deref_info(declared_method_table_type, &context.file.visibility)
        .at(expression)?
        .is_none()
    {
        return Err(diagnostic_at(
            expression,
            "type.method-table is not indexable",
        ));
    }
    let method_pointer_type = types.pointer_type(signature);
    constant_index(
        types,
        &context.file.visibility,
        runtime_type,
        method_table_offset,
        method_pointer_type,
        u32::from(slot),
    )
    .at(expression)
}

fn compile_function_call(
    types: &TypeSystem,
    context: &mut FrontendContext,
    expression: &Object,
    mut function: Value,
    mut arguments: Vec<Value>,
    position: ExpressionPosition,
) -> Result<Value, Diagnostic> {
    let (parameters, result_type, _) =
        call_signature(types, expression, function.ty, arguments.len())?;
    for (index, (expected, actual)) in parameters.iter().zip(&arguments).enumerate() {
        types
            .typecheck(*expected, actual.ty, &context.file.visibility)
            .map_err(|message| {
                diagnostic_at(
                    expression,
                    format!(
                        "function argument {} has the wrong type: {message}",
                        index + 1
                    ),
                )
            })?;
    }

    let function = prepare_call_value(context, expression, &mut function)?;
    let mut argument_registers = Vec::with_capacity(arguments.len());
    for argument in &mut arguments {
        argument_registers.push(prepare_call_value(context, expression, argument)?);
    }
    context.call(function, &argument_registers).at(expression)?;
    compile_call_result(types, context, expression, result_type, position)
}

fn call_signature(
    types: &TypeSystem,
    expression: &Object,
    function: TypeSpec,
    argument_count: usize,
) -> Result<(Vec<TypeSpec>, TypeSpec, Option<usize>), Diagnostic> {
    let function_type = types.type_spec_base(function);
    if types.type_name(function_type) != "function" {
        return Err(diagnostic_at(expression, "call target is not a function"));
    }
    let signature = types.type_spec_arguments(function);
    let (&result_type, parameters) = signature
        .split_last()
        .ok_or_else(|| diagnostic_at(expression, "function type has no return type"))?;
    let varargs_start = parameters
        .iter()
        .position(|parameter| types.type_name(types.type_spec_base(*parameter)) == "_varargs_");
    let fixed_count = varargs_start.unwrap_or(parameters.len());
    if varargs_start.is_some() && argument_count < fixed_count {
        return Err(diagnostic_at(
            expression,
            format!(
                "function expects at least {fixed_count} arguments, but {argument_count} were provided"
            ),
        ));
    }
    if varargs_start.is_none() && parameters.len() != argument_count {
        return Err(diagnostic_at(
            expression,
            format!(
                "function expects {} arguments, but {} were provided",
                parameters.len(),
                argument_count
            ),
        ));
    }
    if argument_count > ARGUMENT_REGISTERS.len() {
        return Err(diagnostic_at(
            expression,
            format!(
                "call has {} arguments, but GOAL supports at most {}",
                argument_count,
                ARGUMENT_REGISTERS.len()
            ),
        ));
    }
    Ok((
        parameters[..fixed_count].to_vec(),
        result_type,
        varargs_start,
    ))
}

fn compile_call_result(
    _: &TypeSystem,
    _: &mut FrontendContext,
    _: &Object,
    result_type: TypeSpec,
    position: ExpressionPosition,
) -> Result<Value, Diagnostic> {
    if position == ExpressionPosition::Statement {
        return Ok(Value::new(
            result_type,
            ValueKind::Register(Register::Physical(PhysicalRegister::RET)),
        ));
    }
    Ok(Value::register(result_type, PhysicalRegister::RET))
}

fn compile_named_call(
    frontend: &Frontend,
    types: &TypeSystem,
    context: &mut FrontendContext,
    expression: &Object,
    name: SymbolId,
    arguments: Vec<Value>,
    position: ExpressionPosition,
) -> Result<Value, Diagnostic> {
    let function = compile_global_value(frontend, types, context, expression, name)?;
    compile_function_call(types, context, expression, function, arguments, position)
}

fn prepare_call_value(
    context: &mut FrontendContext,
    expression: &Object,
    value: &mut Value,
) -> Result<Register, Diagnostic> {
    let needs_copy = matches!(value.kind, ValueKind::Register(_));
    let source = flush_value(context, expression, value)?;
    if !needs_copy {
        return Ok(source);
    }
    let destination = context
        .current_function_data_mut()
        .at(expression)?
        .allocate_gpr(value.ty)
        .at(expression)?;
    context
        .current_function_data_mut()
        .at(expression)?
        .emit(IrInstruction::Move {
            destination: destination.into(),
            source,
            must_emit: false,
        });
    Ok(destination.into())
}

fn flush_gpr_operand(
    context: &mut FrontendContext,
    expression: &Object,
    value: &mut Value,
) -> Result<Register, Diagnostic> {
    match value.kind {
        ValueKind::Integer(0) => Ok(Register::Physical(PhysicalRegister::ZERO)),
        _ => flush_value(context, expression, value),
    }
}

fn flush_value(
    context: &mut FrontendContext,
    expression: &Object,
    value: &mut Value,
) -> Result<Register, Diagnostic> {
    let register = match value.kind {
        ValueKind::Register(register) => return Ok(register),
        ValueKind::Integer(integer) => {
            let integer = i64::try_from(integer)
                .map_err(|_| diagnostic_at(expression, "integer constant exceeds 64 bits"))?;
            if i16::try_from(integer).is_err()
                && !(u32::try_from(integer).is_ok() && integer & 0xffff == 0)
            {
                context
                    .constant_u64(value.ty, integer as u64)
                    .at(expression)?
            } else {
                let register = context
                    .current_function_data_mut()
                    .at(expression)?
                    .allocate_gpr(value.ty)
                    .at(expression)?;
                context.current_function_data_mut().at(expression)?.emit(
                    IrInstruction::LoadConstant {
                        destination: register.into(),
                        value: integer,
                    },
                );
                register.into()
            }
        }
        ValueKind::EmptyList => {
            let register = context
                .current_function_data_mut()
                .at(expression)?
                .allocate_gpr(value.ty)
                .at(expression)?;
            context.current_function_data_mut().at(expression)?.emit(
                IrInstruction::IntegerAddImmediate {
                    destination: register.into(),
                    source: Register::Physical(PhysicalRegister::Gpr(Gpr::S7)),
                    value: EMPTY_PAIR_OFFSET,
                },
            );
            register.into()
        }
        ValueKind::IntegerOffset { base, offset } => {
            let register = context
                .current_function_data_mut()
                .at(expression)?
                .allocate_gpr(value.ty)
                .at(expression)?;
            if let Ok(offset) = i16::try_from(offset) {
                context.current_function_data_mut().at(expression)?.emit(
                    IrInstruction::IntegerAddImmediate {
                        destination: register.into(),
                        source: base,
                        value: offset,
                    },
                );
            } else {
                let constant = if u16::try_from(offset).is_ok() {
                    let constant = context
                        .current_function_data_mut()
                        .at(expression)?
                        .allocate_gpr(value.ty)
                        .at(expression)?;
                    context.current_function_data_mut().at(expression)?.emit(
                        IrInstruction::LoadConstant {
                            destination: constant.into(),
                            value: offset,
                        },
                    );
                    Register::Virtual(constant)
                } else {
                    context
                        .constant_u64(value.ty, offset as u64)
                        .at(expression)?
                };
                context.current_function_data_mut().at(expression)?.emit(
                    IrInstruction::IntegerArithmetic {
                        operation: ArithmeticOperation::Add,
                        signed: true,
                        destination: register.into(),
                        left: constant,
                        right: base,
                    },
                );
            }
            register.into()
        }
        ValueKind::IndexedAddress {
            base,
            index,
            offset,
        } => {
            let register = context
                .current_function_data_mut()
                .at(expression)?
                .allocate_gpr(value.ty)
                .at(expression)?;
            let data = context.current_function_data_mut().at(expression)?;
            if offset == 0 {
                data.emit(IrInstruction::IntegerArithmetic {
                    operation: ArithmeticOperation::Add,
                    signed: false,
                    destination: register.into(),
                    left: Register::Physical(PhysicalRegister::ZERO),
                    right: index,
                });
            } else {
                let offset = i16::try_from(offset).map_err(|_| {
                    diagnostic_at(expression, "indexed address offset exceeds 16 bits")
                })?;
                data.emit(IrInstruction::IntegerAddImmediate {
                    destination: register.into(),
                    source: index,
                    value: offset,
                });
            }
            data.emit(IrInstruction::IntegerArithmetic {
                operation: ArithmeticOperation::Add,
                signed: false,
                destination: register.into(),
                left: register.into(),
                right: base,
            });
            register.into()
        }
        ValueKind::Float(float) => {
            let label = context.file.intern_float_constant(float).at(expression)?;
            let register = context
                .current_function_data_mut()
                .at(expression)?
                .allocate_register(value.ty, RegisterClass::Fpr)
                .at(expression)?;
            context.current_function_data_mut().at(expression)?.emit(
                IrInstruction::LoadFloatConstant {
                    destination: register.into(),
                    label,
                },
            );
            register.into()
        }
        ValueKind::SymbolObject(symbol) => {
            let register = context
                .current_function_data_mut()
                .at(expression)?
                .allocate_gpr(value.ty)
                .at(expression)?;
            if symbol == SymbolId(fnv64(b"#f")) {
                context
                    .current_function_data_mut()
                    .at(expression)?
                    .emit(IrInstruction::Move {
                        destination: register.into(),
                        source: Register::Physical(PhysicalRegister::Gpr(Gpr::S7)),
                        must_emit: false,
                    });
            } else {
                context.current_function_data_mut().at(expression)?.emit(
                    IrInstruction::GetSymbol {
                        destination: register.into(),
                        symbol,
                    },
                );
            }
            register.into()
        }
        ValueKind::Function(function) => {
            let temporary = context
                .current_function_data_mut()
                .at(expression)?
                .allocate_gpr(value.ty)
                .at(expression)?;
            let register = context
                .current_function_data_mut()
                .at(expression)?
                .allocate_gpr(value.ty)
                .at(expression)?;
            context.current_function_data_mut().at(expression)?.emit(
                IrInstruction::LoadFunctionHigh {
                    destination: temporary.into(),
                },
            );
            context.current_function_data_mut().at(expression)?.emit(
                IrInstruction::LoadFunctionLow {
                    destination: register.into(),
                    source: temporary.into(),
                    function,
                },
            );
            register.into()
        }
        ValueKind::StaticAddress(label) => {
            let register = context
                .current_function_data_mut()
                .at(expression)?
                .allocate_gpr(value.ty)
                .at(expression)?;
            context.current_function_data_mut().at(expression)?.emit(
                IrInstruction::LoadStaticAddress {
                    destination: register.into(),
                    label,
                },
            );
            register.into()
        }
        ValueKind::Place(Place {
            base: PlaceBase::Global(symbol),
            projection: PlaceProjection::Whole(load),
        }) => {
            if load.register_class != RegisterClass::Gpr {
                return Err(diagnostic_at(
                    expression,
                    "loading a non-GPR global is not implemented yet",
                ));
            }
            let register = context
                .current_function_data_mut()
                .at(expression)?
                .allocate_gpr(value.ty)
                .at(expression)?;
            context.current_function_data_mut().at(expression)?.emit(
                IrInstruction::GetSymbolValue {
                    destination: register.into(),
                    symbol,
                },
            );
            register.into()
        }
        ValueKind::Place(Place {
            base,
            projection: PlaceProjection::Whole(load),
        }) => {
            let (base, offset) = materialize_place_address(context, expression, base)?;
            let operation = match (load.size, load.sign_extend, load.register_class) {
                (1, true, RegisterClass::Gpr) => MemoryLoad::SignedByte,
                (1, false, RegisterClass::Gpr) => MemoryLoad::UnsignedByte,
                (2, true, RegisterClass::Gpr) => MemoryLoad::SignedHalfword,
                (2, false, RegisterClass::Gpr) => MemoryLoad::UnsignedHalfword,
                (4, true, RegisterClass::Gpr) => MemoryLoad::SignedWord,
                (4, false, RegisterClass::Gpr) => MemoryLoad::UnsignedWord,
                (8, _, RegisterClass::Gpr) => MemoryLoad::Doubleword,
                (16, _, RegisterClass::Gpr128) => MemoryLoad::Quadword,
                (4, false, RegisterClass::Fpr) => MemoryLoad::FloatWord,
                _ => {
                    return Err(diagnostic_at(
                        expression,
                        "this memory load is not implemented yet",
                    ));
                }
            };
            let offset = i16::try_from(offset).map_err(|_| {
                diagnostic_at(
                    expression,
                    "memory offset exceeds a signed 16-bit immediate",
                )
            })?;
            let register = context
                .current_function_data_mut()
                .at(expression)?
                .allocate_register(value.ty, load.register_class)
                .at(expression)?;
            context
                .current_function_data_mut()
                .at(expression)?
                .emit(IrInstruction::LoadMemory {
                    operation,
                    destination: register.into(),
                    base,
                    offset,
                });
            register.into()
        }
        ValueKind::Place(Place {
            base: PlaceBase::Register(base),
            projection: PlaceProjection::BitField(field),
        }) => {
            if !field.use_128_bits || field.size != 32 || !matches!(field.offset, 0 | 32 | 64 | 96)
            {
                return Err(diagnostic_at(
                    expression,
                    "this bit-field extraction is not implemented yet",
                ));
            }
            let source = if field.offset >= 64 {
                let int_type = value.ty;
                let upper = context
                    .current_function_data_mut()
                    .at(expression)?
                    .allocate_gpr(int_type)
                    .at(expression)?;
                context.current_function_data_mut().at(expression)?.emit(
                    IrInstruction::Gpr128Upper64 {
                        destination: upper.into(),
                        source: base,
                    },
                );
                Register::Virtual(upper)
            } else {
                base
            };
            let register = context
                .current_function_data_mut()
                .at(expression)?
                .allocate_gpr(value.ty)
                .at(expression)?;
            context
                .current_function_data_mut()
                .at(expression)?
                .emit(IrInstruction::ExtractWord {
                    part: if field.offset % 64 == 0 {
                        DoublewordPart::Low
                    } else {
                        DoublewordPart::High
                    },
                    destination: register.into(),
                    source,
                });
            register.into()
        }
        ValueKind::None => {
            return Err(diagnostic_at(expression, "none has no register value"));
        }
        ValueKind::StackAddress(_) | ValueKind::Place(_) => {
            return Err(diagnostic_at(
                expression,
                "this value cannot be flushed to a register yet",
            ));
        }
    };
    value.kind = ValueKind::Register(register);
    Ok(register)
}

fn flush_return_value(
    context: &mut FrontendContext,
    expression: &Object,
    value: &mut Value,
) -> Result<Register, Diagnostic> {
    if value.kind == ValueKind::Integer(0) {
        return Ok(Register::Physical(PhysicalRegister::ZERO));
    }
    flush_value(context, expression, value)
}

fn macro_diagnostic(
    diagnostic: Diagnostic,
    invocation: Option<SpanInfo>,
    definition: Option<SpanInfo>,
) -> Diagnostic {
    let diagnostic =
        diagnostic.with_context("expanded from this GOAL macro invocation", invocation);
    if definition.is_some() {
        diagnostic.with_context("GOAL macro defined here", definition)
    } else {
        diagnostic
    }
}

fn compile_bitwise(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let (left_expression, right_expression, operation) = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let left = cursor.required(objects, "left")?.into_object();
        let right = cursor.required(objects, "right")?.into_object();
        cursor.finish(objects)?;
        let operation = match objects.symbols.resolve(head) {
            Some("logior") => IntegerBitwiseOperation::Or,
            Some("logand") => IntegerBitwiseOperation::And,
            Some("lognor") => IntegerBitwiseOperation::Nor,
            Some("logxor") => IntegerBitwiseOperation::Xor,
            _ => unreachable!(),
        };
        (left, right, operation)
    };
    let mut left = compile_expression(frontend, types, goos, context, &left_expression)?;
    let left_signed = check_integer_operand_type(types, context, &left_expression, &left)?;
    let result_type = types.intern_type_spec("int");

    if operation != IntegerBitwiseOperation::Nor
        && let ValueKind::Integer(value) = left.kind
        && let Ok(value) = u16::try_from(value)
    {
        let mut right = compile_expression(frontend, types, goos, context, &right_expression)?;
        let right_signed = check_integer_operand_type(types, context, &right_expression, &right)?;
        let source =
            finish_integer_operand(context, &right_expression, &mut right, right_signed)?.register;
        let result = context
            .current_function_data_mut()
            .at(expression)?
            .allocate_gpr(result_type)
            .at(expression)?;
        context.current_function_data_mut().at(expression)?.emit(
            IrInstruction::IntegerBitwiseImmediate {
                operation,
                destination: result.into(),
                source,
                value,
            },
        );
        return Ok(Value::register(result_type, result));
    }

    let left = finish_integer_operand(context, &left_expression, &mut left, left_signed)?;
    let mut right = compile_expression(frontend, types, goos, context, &right_expression)?;
    let right_signed = check_integer_operand_type(types, context, &right_expression, &right)?;
    if operation != IntegerBitwiseOperation::Nor
        && let ValueKind::Integer(value) = right.kind
        && let Ok(value) = u16::try_from(value)
    {
        let result = context
            .current_function_data_mut()
            .at(expression)?
            .allocate_gpr(result_type)
            .at(expression)?;
        context.current_function_data_mut().at(expression)?.emit(
            IrInstruction::IntegerBitwiseImmediate {
                operation,
                destination: result.into(),
                source: left.register,
                value,
            },
        );
        return Ok(Value::register(result_type, result));
    }
    let right = finish_integer_operand(context, &right_expression, &mut right, right_signed)?;
    let data = context.current_function_data_mut().at(expression)?;
    let result = data.allocate_gpr(result_type).at(expression)?;
    data.emit_integer_bitwise(operation, result, left.register, right.register);
    Ok(Value::register(result_type, result))
}

fn compile_abs(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let operand_expression = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let operand = cursor.required(objects, "operand")?.into_object();
        cursor.finish(objects)?;
        operand
    };
    let operand = compile_integer_operand(frontend, types, goos, context, &operand_expression)?;
    let result_type = types.intern_type_spec("int");

    let data = context.current_function_data_mut().at(expression)?;
    let result = data.allocate_gpr(result_type).at(expression)?;
    let done = data.allocate_label().at(expression)?;
    data.emit_move(result, operand.register);
    data.emit_branch_likely(BranchCondition::Negative(result.into()), done);
    data.emit_integer_subtract(result, PhysicalRegister::ZERO, result);
    data.bind_label(done).at(expression)?;
    Ok(Value::register(result_type, result))
}

fn compile_min_max(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let (left_expression, right_expression, operation) = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let left = cursor.required(objects, "left")?.into_object();
        let right = cursor.required(objects, "right")?.into_object();
        cursor.finish(objects)?;
        let operation = if objects.symbols.resolve(head) == Some("min") {
            ConditionalMoveOperation::Zero
        } else {
            ConditionalMoveOperation::NonZero
        };
        (left, right, operation)
    };

    let mut left = compile_integer_operand(frontend, types, goos, context, &left_expression)?;
    left.register = temp_gpr_copy(context, &left_expression, left.ty, left.register)?;
    let mut right = compile_integer_operand(frontend, types, goos, context, &right_expression)?;
    right.register = temp_gpr_copy(context, &right_expression, right.ty, right.register)?;
    let result_type = types.intern_type_spec("int");

    let data = context.current_function_data_mut().at(expression)?;
    let comparison = data.allocate_gpr(result_type).at(expression)?;
    data.emit_integer_compare(
        IntegerComparison::LessThan,
        true,
        comparison,
        left.register,
        right.register,
    );
    data.emit_conditional_move(operation, left.register, right.register, comparison);
    let read = data.allocate_gpr(result_type).at(expression)?;
    data.emit_move(read, left.register);
    let result = data.allocate_gpr(result_type).at(expression)?;
    data.emit_move(result, read);
    Ok(Value::register(result_type, result))
}

fn compile_ash(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    head: SymbolId,
    rest: Object,
) -> Result<Value, Diagnostic> {
    let (value_expression, shift_expression) = {
        let objects = &goos.parser.objects;
        let mut cursor = ListCursor::new(head, expression.span, rest);
        let value = cursor.required(objects, "value")?.into_object();
        let shift = cursor.required(objects, "shift-amount")?.into_object();
        cursor.finish(objects)?;
        (value, shift)
    };

    let value = compile_integer_operand(frontend, types, goos, context, &value_expression)?;
    let mut shift_value = compile_expression(frontend, types, goos, context, &shift_expression)?;
    let shift_signed = check_integer_operand_type(types, context, &shift_expression, &shift_value)?;
    let right_shift = if value.signed {
        ShiftOperation::ArithmeticRight
    } else {
        ShiftOperation::LogicalRight
    };
    let result_type = types.intern_type_spec("int");

    if let ValueKind::Integer(amount) = shift_value.kind {
        let amount = i8::try_from(amount).map_err(|_| {
            diagnostic_at(&shift_expression, "ash amount must be between -63 and 63")
        })?;
        if !(-63..=63).contains(&amount) {
            return Err(diagnostic_at(
                &shift_expression,
                "ash amount must be between -63 and 63",
            ));
        }
        let destination = context
            .current_function_data_mut()
            .at(expression)?
            .allocate_gpr(result_type)
            .at(expression)?;
        context.current_function_data_mut().at(expression)?.emit(
            IrInstruction::IntegerShiftImmediate {
                operation: if amount >= 0 {
                    ShiftOperation::Left
                } else {
                    right_shift
                },
                destination: destination.into(),
                value: value.register,
                amount: amount.unsigned_abs(),
            },
        );
        return Ok(Value::register(result_type, destination));
    }

    let shift = finish_integer_operand(context, &shift_expression, &mut shift_value, shift_signed)?;

    let data = context.current_function_data_mut().at(expression)?;
    let working_value = data.allocate_gpr(value.ty).at(expression)?;
    data.emit_move(working_value, value.register);
    let result = data.allocate_gpr(result_type).at(expression)?;
    let negated_shift = data.allocate_gpr(result_type).at(expression)?;
    let done = data.allocate_label().at(expression)?;

    data.emit_branch_likely(BranchCondition::NonNegative(shift.register), done);
    data.emit_variable_shift(ShiftOperation::Left, result, working_value, shift.register);
    data.emit_integer_subtract(negated_shift, PhysicalRegister::ZERO, shift.register);
    data.emit_variable_shift(right_shift, result, working_value, negated_shift);
    data.bind_label(done).at(expression)?;
    Ok(Value::register(result_type, result))
}

struct IntegerOperand {
    ty: TypeSpec,
    register: Register,
    signed: bool,
}

fn compile_integer_operand(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
) -> Result<IntegerOperand, Diagnostic> {
    let mut value = compile_expression(frontend, types, goos, context, expression)?;
    let signed = check_integer_operand_type(types, context, expression, &value)?;
    finish_integer_operand(context, expression, &mut value, signed)
}

fn check_integer_operand_type(
    types: &mut TypeSystem,
    context: &FrontendContext,
    expression: &Object,
    value: &Value,
) -> Result<bool, Diagnostic> {
    let integer = types.intern_type_spec("integer");
    types
        .typecheck(integer, value.ty, &context.file.visibility)
        .at(expression)?;
    let layout = types
        .layout(types.type_spec_base(value.ty), &context.file.visibility)
        .at(expression)?;
    if layout.register_class != RegisterClass::Gpr {
        return Err(diagnostic_at(
            expression,
            format!(
                "integer arithmetic cannot use `{}`",
                types.format_type_spec(value.ty)
            ),
        ));
    }
    types
        .integer_is_signed(value.ty, &context.file.visibility)
        .at(expression)
}

fn finish_integer_operand(
    context: &mut FrontendContext,
    expression: &Object,
    value: &mut Value,
    signed: bool,
) -> Result<IntegerOperand, Diagnostic> {
    let register = flush_gpr_operand(context, expression, value)?;
    Ok(IntegerOperand {
        ty: value.ty,
        register,
        signed,
    })
}

fn temp_gpr_copy(
    context: &mut FrontendContext,
    expression: &Object,
    ty: TypeSpec,
    source: Register,
) -> Result<Register, Diagnostic> {
    let data = context.current_function_data_mut().at(expression)?;
    let temporary = data.allocate_gpr(ty).at(expression)?;
    data.emit_move(temporary, source);
    Ok(temporary.into())
}

#[derive(Clone, Copy)]
enum NumericMode {
    Integer { signed: bool },
    Float,
}

fn flatten_integer_addition(
    objects: &ObjectStore,
    expression: &Object,
    result: &mut Vec<Object>,
) -> Result<(), Diagnostic> {
    if let ObjectValue::Pair(_) = expression.value {
        let (head, rest) = form_parts(objects, expression)?;
        if objects.symbols.resolve(head) == Some("+") {
            for operand in
                ListCursor::new(head, expression.span, rest).remaining(objects, "operand")?
            {
                flatten_integer_addition(objects, operand.object(), result)?;
            }
            return Ok(());
        }
    }
    result.push(expression.clone());
    Ok(())
}

fn compile_integer_addition(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    operands: &[Object],
) -> Result<Value, Diagnostic> {
    let integer_type = types.intern_type_spec("integer");
    let result_type = types.intern_type_spec("int");
    let mut result = None;
    let mut constant = 0_i64;
    for operand in operands {
        let mut value = compile_expression(frontend, types, goos, context, operand)?;
        types
            .typecheck(integer_type, value.ty, &context.file.visibility)
            .at(operand)?;
        let register = match value.kind {
            ValueKind::Integer(value) => {
                let value = i64::try_from(value)
                    .map_err(|_| diagnostic_at(operand, "integer constant exceeds 64 bits"))?;
                constant = constant
                    .checked_add(value)
                    .ok_or_else(|| diagnostic_at(operand, "integer addition overflow"))?;
                continue;
            }
            ValueKind::IntegerOffset { base, offset } => {
                constant = constant
                    .checked_add(offset)
                    .ok_or_else(|| diagnostic_at(operand, "integer addition overflow"))?;
                base
            }
            _ => flush_value(context, operand, &mut value)?,
        };
        result = Some(if let Some(left) = result {
            let destination = context
                .current_function_data_mut()
                .at(expression)?
                .allocate_gpr(result_type)
                .at(expression)?;
            context.current_function_data_mut().at(expression)?.emit(
                IrInstruction::IntegerArithmetic {
                    operation: ArithmeticOperation::Add,
                    signed: true,
                    destination: destination.into(),
                    left,
                    right: register,
                },
            );
            Register::Virtual(destination)
        } else {
            register
        });
    }
    Ok(match result {
        Some(base) if constant != 0 => Value::new(
            result_type,
            ValueKind::IntegerOffset {
                base,
                offset: constant,
            },
        ),
        Some(register) => Value::new(result_type, ValueKind::Register(register)),
        None => Value::new(result_type, ValueKind::Integer(i128::from(constant))),
    })
}

fn compile_power_of_two_multiply(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    value_expression: &Object,
    multiplier: i64,
) -> Result<Value, Diagnostic> {
    let mut value = compile_expression(frontend, types, goos, context, value_expression)?;
    let integer = types.intern_type_spec("integer");
    types
        .typecheck(integer, value.ty, &context.file.visibility)
        .at(value_expression)?;
    let source = flush_value(context, value_expression, &mut value)?;
    let result_type = types.intern_type_spec("int");
    let destination = context
        .current_function_data_mut()
        .at(expression)?
        .allocate_gpr(result_type)
        .at(expression)?;
    context.current_function_data_mut().at(expression)?.emit(
        IrInstruction::IntegerShiftImmediate {
            operation: ShiftOperation::Left,
            destination: destination.into(),
            value: source,
            amount: multiplier.trailing_zeros() as u8,
        },
    );
    Ok(Value::register(result_type, destination))
}

fn compile_arithmetic(
    frontend: &mut Frontend,
    types: &mut TypeSystem,
    goos: &mut Interpreter,
    context: &mut FrontendContext,
    expression: &Object,
    cursor: ListCursor,
    operation: ArithmeticOperation,
) -> Result<Value, Diagnostic> {
    let operands = cursor.remaining(&goos.parser.objects, "operand")?;
    if operands.is_empty() {
        return Err(diagnostic_at(
            expression,
            "arithmetic requires at least one operand",
        ));
    }
    if operation == ArithmeticOperation::Add {
        let mut flattened = Vec::new();
        for operand in &operands {
            flatten_integer_addition(&goos.parser.objects, operand.object(), &mut flattened)?;
        }
        if flattened
            .first()
            .is_some_and(|operand| matches!(operand.value, ObjectValue::Integer(_)))
        {
            return compile_integer_addition(
                frontend, types, goos, context, expression, &flattened,
            );
        }
    }
    if operation == ArithmeticOperation::Multiply && operands.len() == 2 {
        let first = operands[0].object();
        let second = operands[1].object();
        if let ObjectValue::Integer(multiplier) = first.value
            && multiplier.is_positive()
            && multiplier & (multiplier - 1) == 0
        {
            return compile_power_of_two_multiply(
                frontend, types, goos, context, expression, second, multiplier,
            );
        }
        if let ObjectValue::Integer(multiplier) = second.value
            && multiplier.is_positive()
            && multiplier & (multiplier - 1) == 0
        {
            return compile_power_of_two_multiply(
                frontend, types, goos, context, expression, first, multiplier,
            );
        }
    }
    if matches!(
        operation,
        ArithmeticOperation::Divide | ArithmeticOperation::Remainder
    ) && operands.len() != 2
    {
        return Err(diagnostic_at(
            expression,
            "division and remainder require exactly two operands",
        ));
    }

    let mut operands = operands.into_iter();
    let first_expression = operands.next().unwrap().into_object();
    let mut first_value = compile_expression(frontend, types, goos, context, &first_expression)?;
    let float = types.intern_type_spec("float");
    let integer = types.intern_type_spec("integer");
    let visibility = &context.file.visibility;
    let mode = if types.typecheck(float, first_value.ty, visibility).is_ok() {
        NumericMode::Float
    } else if types.typecheck(integer, first_value.ty, visibility).is_ok() {
        let layout = types
            .layout(types.type_spec_base(first_value.ty), visibility)
            .at(&first_expression)?;
        NumericMode::Integer {
            signed: layout.load_signed,
        }
    } else {
        return Err(diagnostic_at(
            &first_expression,
            format!(
                "arithmetic requires a number, got `{}`",
                types.format_type_spec(first_value.ty)
            ),
        ));
    };
    let numeric_type = match mode {
        NumericMode::Integer { .. } => integer,
        NumericMode::Float => float,
    };
    if operation == ArithmeticOperation::Remainder && matches!(mode, NumericMode::Float) {
        return Err(diagnostic_at(
            expression,
            "floating-point remainder is not supported by the EE",
        ));
    }
    let result_type = first_value.ty;
    if operation == ArithmeticOperation::Divide
        && let NumericMode::Integer { signed } = mode
        && operands.as_slice().len() == 1
        && let ObjectValue::Integer(divisor) = operands.as_slice()[0].object().value
        && divisor > 0
        && divisor & (divisor - 1) == 0
    {
        let source = flush_gpr_operand(context, &first_expression, &mut first_value)?;
        let destination = context
            .current_function_data_mut()
            .at(expression)?
            .allocate_gpr(result_type)
            .at(expression)?;
        context.current_function_data_mut().at(expression)?.emit(
            IrInstruction::IntegerShiftImmediate {
                operation: if signed {
                    ShiftOperation::ArithmeticRight
                } else {
                    ShiftOperation::LogicalRight
                },
                destination: destination.into(),
                value: source,
                amount: divisor.trailing_zeros() as u8,
            },
        );
        return Ok(Value::register(result_type, destination));
    }
    let mut result = flush_numeric(
        types,
        context,
        mode,
        numeric_type,
        &mut first_value,
        &first_expression,
    )?;
    let mut operand_count = 1;
    for operand in operands {
        operand_count += 1;
        let operand_expression = operand.into_object();
        let mut value = compile_expression(frontend, types, goos, context, &operand_expression)?;
        if operation == ArithmeticOperation::Add
            && matches!(mode, NumericMode::Integer { .. })
            && let ValueKind::Integer(constant) = value.kind
        {
            let constant = i64::try_from(constant).map_err(|_| {
                diagnostic_at(&operand_expression, "integer constant exceeds 64 bits")
            })?;
            if constant == 0 {
                continue;
            }
            if let Ok(constant) = i16::try_from(constant) {
                let destination = context
                    .current_function_data_mut()
                    .at(expression)?
                    .allocate_gpr(result_type)
                    .at(expression)?;
                context.current_function_data_mut().at(expression)?.emit(
                    IrInstruction::IntegerAddImmediate {
                        destination: destination.into(),
                        source: result,
                        value: constant,
                    },
                );
                result = destination.into();
                continue;
            }
        }
        let right = flush_numeric(
            types,
            context,
            mode,
            numeric_type,
            &mut value,
            &operand_expression,
        )?;
        result = emit_arithmetic(
            context,
            expression,
            operation,
            mode,
            result_type,
            result,
            right,
        )?;
    }
    if operation == ArithmeticOperation::Subtract && operand_count == 1 {
        result = match mode {
            NumericMode::Integer { .. } => emit_arithmetic(
                context,
                expression,
                ArithmeticOperation::Subtract,
                mode,
                result_type,
                Register::Physical(PhysicalRegister::Gpr(crate::ir::Gpr::R0)),
                result,
            )?,
            NumericMode::Float => {
                let mut zero = Value::new(float, ValueKind::Float(0.0));
                let zero = flush_value(context, expression, &mut zero)?;
                emit_arithmetic(
                    context,
                    expression,
                    ArithmeticOperation::Subtract,
                    mode,
                    result_type,
                    zero,
                    result,
                )?
            }
        };
    }
    Ok(Value::new(result_type, ValueKind::Register(result)))
}

fn emit_arithmetic(
    context: &mut FrontendContext,
    expression: &Object,
    operation: ArithmeticOperation,
    mode: NumericMode,
    ty: TypeSpec,
    left: Register,
    right: Register,
) -> Result<Register, Diagnostic> {
    let class = match mode {
        NumericMode::Integer { .. } => RegisterClass::Gpr,
        NumericMode::Float => RegisterClass::Fpr,
    };
    let result = context
        .current_function_data_mut()
        .at(expression)?
        .allocate_register(ty, class)
        .at(expression)?;
    let instruction = match mode {
        NumericMode::Integer { signed } => IrInstruction::IntegerArithmetic {
            operation,
            signed,
            destination: Register::Virtual(result),
            left,
            right,
        },
        NumericMode::Float => IrInstruction::FloatArithmetic {
            operation,
            destination: Register::Virtual(result),
            left,
            right,
        },
    };
    context
        .current_function_data_mut()
        .at(expression)?
        .emit(instruction);
    Ok(result.into())
}

fn flush_numeric(
    types: &TypeSystem,
    context: &mut FrontendContext,
    mode: NumericMode,
    expected: TypeSpec,
    value: &mut Value,
    expression: &Object,
) -> Result<Register, Diagnostic> {
    if let Err(message) = types.typecheck(expected, value.ty, &context.file.visibility) {
        return Err(diagnostic_at(expression, message));
    }
    match mode {
        NumericMode::Float => flush_float(types, context, expected, value, expression),
        NumericMode::Integer { .. } => {
            let integer = types
                .layout(types.type_spec_base(value.ty), &context.file.visibility)
                .at(expression)?;
            if integer.register_class != RegisterClass::Gpr {
                return Err(diagnostic_at(
                    expression,
                    format!(
                        "integer arithmetic cannot use `{}`",
                        types.format_type_spec(value.ty)
                    ),
                ));
            }
            flush_gpr_operand(context, expression, value)
        }
    }
}

fn flush_float(
    types: &TypeSystem,
    context: &mut FrontendContext,
    float: TypeSpec,
    value: &mut Value,
    expression: &Object,
) -> Result<Register, Diagnostic> {
    if let Err(error) = types.typecheck(float, value.ty, &context.file.visibility) {
        return Err(diagnostic_at(expression, error.to_string()));
    }
    let register = flush_value(context, expression, value)?;
    let class = match register {
        Register::Virtual(register) => {
            context.current_function_data().at(expression)?.registers[register.0 as usize].class
        }
        Register::Physical(register) => register.class(),
    };
    match class {
        RegisterClass::Fpr => Ok(register),
        RegisterClass::Gpr => {
            let converted = context
                .current_function_data_mut()
                .at(expression)?
                .allocate_register(value.ty, RegisterClass::Fpr)
                .at(expression)?;
            context
                .current_function_data_mut()
                .at(expression)?
                .emit(IrInstruction::Move {
                    destination: converted.into(),
                    source: register,
                    must_emit: false,
                });
            let converted = Register::Virtual(converted);
            value.kind = ValueKind::Register(converted);
            Ok(converted)
        }
        _ => Err(diagnostic_at(
            expression,
            "float value has an invalid register class",
        )),
    }
}

fn form_parts(objects: &ObjectStore, form: &Object) -> Result<(SymbolId, Object), Diagnostic> {
    let ObjectValue::Pair(id) = form.value else {
        return Err(diagnostic_at(
            form,
            "top-level GOAL expression must be a list",
        ));
    };
    let cell = objects.cons.get(id).at(form)?;
    let ObjectValue::Symbol(head) = cell.car.value else {
        return Err(diagnostic_at(&cell.car, "GOAL form name must be a symbol"));
    };
    Ok((head, cell.cdr.clone()))
}

fn call_arguments(
    objects: &ObjectStore,
    call: &Object,
    mut rest: Object,
) -> Result<Vec<Object>, Diagnostic> {
    let mut result = Vec::new();
    loop {
        match rest.value {
            ObjectValue::EmptyList => return Ok(result),
            ObjectValue::Pair(id) => {
                let cell = objects.cons.get(id).at(call)?;
                result.push(cell.car.clone());
                rest = cell.cdr.clone();
            }
            _ => {
                return Err(diagnostic_at(
                    call,
                    "function call has an improper argument list",
                ));
            }
        }
    }
}

fn diagnostic_at(object: &Object, message: impl Into<String>) -> Diagnostic {
    let diagnostic = Diagnostic::error(message);
    match object.span {
        Some(span) => diagnostic.with_span(span),
        None => diagnostic,
    }
}

trait DiagnosticResultExt<T> {
    fn at(self, object: &Object) -> Result<T, Diagnostic>;
}

impl<T> DiagnosticResultExt<T> for Result<T, String> {
    fn at(self, object: &Object) -> Result<T, Diagnostic> {
        self.map_err(|message| diagnostic_at(object, message))
    }
}

#[cfg(test)]
mod tests {
    use crate::parse::Parser;
    use crate::type_system::TypeSystem;

    use super::*;

    #[test]
    fn builtin_type_objects_are_ordinary_globals_of_type_type() {
        let mut types = TypeSystem::new();
        let frontend = Frontend::new(&mut types);
        let uint128 = types.intern_type("uint128");
        let type_type = types.intern_type_spec("type");
        let definition = frontend
            .global_definition(uint128.symbol(), &Visibility::new(1, []))
            .unwrap()
            .unwrap();
        assert_eq!(definition.owner, BUILTIN_FILE_ID);
        assert_eq!(definition.ty, type_type);
    }

    #[test]
    fn function_contexts_are_temporary_and_function_data_is_retained() {
        let mut parser = Parser::default();
        let source = parser.parse("test.gc", "()").unwrap().source_id;
        let file = FileContext::new(8, source, Visibility::new(8, [1, 3, 70]));
        let mut types = TypeSystem::new();
        let none = types.intern_type_spec("none");
        let integer = types.intern_type_spec("integer");
        let mut context = FrontendContext::new(file, none);
        let name = parser.objects.symbols.intern("outer");
        let outer = context
            .begin_function(FunctionIdentity::Global(name), Segment::Main, integer)
            .unwrap();

        let register = context
            .current_function_data_mut()
            .unwrap()
            .allocate_register(integer, RegisterClass::Gpr)
            .unwrap();
        let local = parser.objects.symbols.intern("local");
        context
            .current_function_mut()
            .unwrap()
            .define_local(local, LexicalBinding { register })
            .unwrap();

        let nested = context
            .begin_anonymous_function(Segment::Main, integer)
            .unwrap();
        assert_eq!(
            context.current_function().unwrap().lookup_local(local),
            None
        );
        assert_eq!(
            context.file.functions[nested.0 as usize].parent,
            Some(outer)
        );
        context.end_function().unwrap();
        assert_eq!(
            context.current_function().unwrap().lookup_local(local),
            Some(LexicalBinding { register })
        );
        context.end_function().unwrap();

        let file = context.finish().unwrap();
        assert_eq!(file.functions.len(), 2);
        assert_eq!(file.functions[0].registers.len(), 1);
        assert!(file.visibility.can_see(70));
    }

    #[test]
    fn rlet_bindings_use_one_web_and_lexical_adds_a_scope_reservation() {
        let mut goos = Interpreter::new().unwrap();
        let parsed = goos
            .parser
            .parse(
                "test.gc",
                "(defun reserve ()\n  (rlet ((held :bind 'lexical) (normal))\n    (m held 1)\n    (m normal 2)\n    0))",
            )
            .unwrap();
        let mut types = TypeSystem::new();
        let mut frontend = Frontend::new(&mut types);
        let file = frontend
            .compile_file(
                1,
                parsed.source_id,
                Visibility::new(1, []),
                &mut types,
                &mut goos,
                &parsed.objects,
            )
            .unwrap();
        let reserve = goos.parser.objects.symbols.intern("reserve");
        let function = file
            .functions
            .iter()
            .find(|function| function.identity == FunctionIdentity::Global(reserve))
            .unwrap();

        assert_eq!(function.single_web_registers.len(), 2);
        assert_eq!(function.register_reservations.len(), 1);
        let reservation = function.register_reservations[0];
        assert!(reservation.start < reservation.end);
        assert!(
            function
                .single_web_registers
                .contains(&reservation.register)
        );
        assert_eq!(
            function.registers[reservation.register.0 as usize].class,
            RegisterClass::Gpr
        );
    }

    #[test]
    fn ordered_pointer_comparisons_are_unsigned() {
        let mut goos = Interpreter::new().unwrap();
        let parsed = goos
            .parser
            .parse(
                "test.gc",
                "(defun pointer-order ((left pointer) (right pointer))\n  (< left right)\n  (> left right)\n  (<= left right)\n  (>= left right))",
            )
            .unwrap();
        let mut types = TypeSystem::new();
        let mut frontend = Frontend::new(&mut types);
        let file = frontend
            .compile_file(
                1,
                parsed.source_id,
                Visibility::new(1, []),
                &mut types,
                &mut goos,
                &parsed.objects,
            )
            .unwrap();
        let name = goos.parser.objects.symbols.intern("pointer-order");
        let function = file
            .functions
            .iter()
            .find(|function| function.identity == FunctionIdentity::Global(name))
            .unwrap();
        let comparisons = function
            .ir
            .iter()
            .filter_map(|instruction| match instruction {
                IrInstruction::IntegerCompare { signed, .. } => Some(*signed),
                _ => None,
            })
            .collect::<Vec<_>>();

        assert_eq!(comparisons, vec![false; 4]);
    }

    #[test]
    fn defconstant_retains_and_compiles_arbitrary_objects() {
        let mut goos = Interpreter::new().unwrap();
        let parsed = goos
            .parser
            .parse(
                "test.gc",
                "(defconstant LIMIT (the pointer #x8000000))\n(defun below-limit ((value pointer)) (< value LIMIT))",
            )
            .unwrap();
        let mut types = TypeSystem::new();
        let mut frontend = Frontend::new(&mut types);
        let file = frontend
            .compile_file(
                1,
                parsed.source_id,
                Visibility::new(1, []),
                &mut types,
                &mut goos,
                &parsed.objects,
            )
            .unwrap();
        let name = goos.parser.objects.symbols.intern("below-limit");
        let function = file
            .functions
            .iter()
            .find(|function| function.identity == FunctionIdentity::Global(name))
            .unwrap();

        assert!(
            frontend
                .goos_roots()
                .any(|object| matches!(object.value, ObjectValue::Pair(_)))
        );
        assert!(function.ir.iter().any(|instruction| matches!(
            instruction,
            IrInstruction::IntegerCompare { signed: false, .. }
        )));
    }

    #[test]
    fn finishing_a_none_function_emits_an_ordinary_zero_return() {
        let mut parser = Parser::default();
        let source = parser.parse("test.gc", "").unwrap().source_id;
        let file = FileContext::new(8, source, Visibility::new(8, []));
        let mut types = TypeSystem::new();
        let none = types.intern_type_spec("none");
        let mut context = FrontendContext::new(file, none);
        let name = parser.objects.symbols.intern("returns-none");
        let function = context
            .begin_function(FunctionIdentity::Global(name), Segment::Main, none)
            .unwrap();

        context.end_function().unwrap();
        let function = &context.file.functions[function.0 as usize];
        assert_eq!(function.registers.len(), 1);
        assert!(matches!(
            function.ir.as_slice(),
            [
                IrInstruction::Move {
                    source: Register::Physical(PhysicalRegister::ZERO),
                    ..
                },
                IrInstruction::Return { .. }
            ]
        ));
    }

    #[test]
    fn defun_definitions_stack_typecheck_and_reveal_earlier_files() {
        let mut goos = Interpreter::new().unwrap();
        let mut types = TypeSystem::new();
        let object = types.intern_type_spec("object");
        let expected_signature = types.function_type([object], object);
        let mut frontend = Frontend::new(&mut types);

        let first = goos
            .parser
            .parse("first.gc", "(defun identity ((value object)) value)")
            .unwrap();
        frontend
            .compile_file(
                1,
                first.source_id,
                Visibility::new(1, []),
                &mut types,
                &mut goos,
                &first.objects,
            )
            .unwrap();
        let identity = goos.parser.objects.symbols.intern("identity");
        assert_eq!(
            frontend
                .global_definition(identity, &Visibility::new(1, []))
                .unwrap()
                .unwrap()
                .ty,
            expected_signature
        );

        let second = goos
            .parser
            .parse("second.gc", "(defun identity ((value object)) value)")
            .unwrap();
        frontend
            .compile_file(
                2,
                second.source_id,
                Visibility::new(2, [1]),
                &mut types,
                &mut goos,
                &second.objects,
            )
            .unwrap();
        assert_eq!(frontend.globals[&identity].len(), 2);
        assert_eq!(
            frontend
                .global_definition(identity, &Visibility::new(2, [1]))
                .unwrap()
                .unwrap()
                .owner,
            2
        );
        assert!(
            frontend
                .global_definition(identity, &Visibility::new(3, [1]))
                .unwrap_err()
                .contains("not visible")
        );

        frontend.remove_definitions(2);
        assert_eq!(
            frontend
                .global_definition(identity, &Visibility::new(1, []))
                .unwrap()
                .unwrap()
                .owner,
            1
        );

        let changed = goos
            .parser
            .parse("changed.gc", "(defun identity ((value int)) value)")
            .unwrap();
        let error = frontend
            .compile_file(
                3,
                changed.source_id,
                Visibility::new(3, [1]),
                &mut types,
                &mut goos,
                &changed.objects,
            )
            .unwrap_err();
        assert!(error.message.contains("previously defined"));
        assert_eq!(frontend.globals[&identity].len(), 1);
    }

    #[test]
    fn goal_macros_expand_on_use_and_are_owned_by_their_file() {
        let mut goos = Interpreter::new().unwrap();
        let mut types = TypeSystem::new();
        let mut frontend = Frontend::new(&mut types);

        let macros = goos
            .parser
            .parse(
                "macros.gc",
                "(defmacro bit-complement (arg) `(lognor ,arg 0))",
            )
            .unwrap();
        frontend
            .compile_file(
                1,
                macros.source_id,
                Visibility::new(1, []),
                &mut types,
                &mut goos,
                &macros.objects,
            )
            .unwrap();

        let roots = frontend.goos_roots().cloned().collect::<Vec<_>>();
        goos.collect_garbage(&roots).unwrap();
        let use_site = goos
            .parser
            .parse(
                "use.gc",
                "(defun complement ((value int)) (bit-complement value))",
            )
            .unwrap();
        let file = frontend
            .compile_file(
                2,
                use_site.source_id,
                Visibility::new(2, [1]),
                &mut types,
                &mut goos,
                &use_site.objects,
            )
            .unwrap();
        assert!(file.functions.iter().any(|function| {
            function.ir.iter().any(|instruction| {
                matches!(
                    instruction,
                    IrInstruction::IntegerBitwise {
                        operation: IntegerBitwiseOperation::Nor,
                        right: Register::Physical(PhysicalRegister::ZERO),
                        ..
                    }
                )
            })
        }));

        let name = goos.parser.objects.symbols.intern("bit-complement");
        frontend.remove_definitions(1);
        assert!(
            frontend
                .macro_definition(name, &Visibility::new(2, [1]))
                .unwrap()
                .is_none()
        );
    }

    #[test]
    fn externs_and_defines_track_types_and_emit_distinct_symbol_loads() {
        let mut goos = Interpreter::new().unwrap();
        let parsed = goos
            .parser
            .parse(
                "globals.gc",
                "(defun false-func () '#f)\n\
                 (defun true-func () '#t)\n\
                 (define-extern _format (function _varargs_ object))\n\
                 (define format _format)",
            )
            .unwrap();
        let mut types = TypeSystem::new();
        let mut frontend = Frontend::new(&mut types);
        let file = frontend
            .compile_file(
                1,
                parsed.source_id,
                Visibility::new(1, []),
                &mut types,
                &mut goos,
                &parsed.objects,
            )
            .unwrap();

        let format = goos.parser.objects.symbols.intern("format");
        let underscore_format = goos.parser.objects.symbols.intern("_format");
        let varargs = types.intern_type_spec("_varargs_");
        let object = types.intern_type_spec("object");
        let expected = types.function_type([varargs], object);
        assert_eq!(
            frontend
                .global_definition(format, &Visibility::new(1, []))
                .unwrap()
                .unwrap()
                .ty,
            expected
        );
        assert!(file.functions[0].ir.iter().any(|instruction| matches!(
            instruction,
            IrInstruction::GetSymbolValue { symbol, .. } if *symbol == underscore_format
        )));
        assert!(file.functions[0].ir.iter().any(|instruction| matches!(
            instruction,
            IrInstruction::StoreSymbol { symbol, .. } if *symbol == format
        )));
        assert!(file.functions.iter().skip(1).any(|function| {
            function.ir.iter().any(|instruction| {
                matches!(
                    instruction,
                    IrInstruction::Move {
                        source: Register::Physical(PhysicalRegister::Gpr(Gpr::S7)),
                        ..
                    }
                )
            })
        }));
        assert!(file.functions.iter().skip(1).any(|function| {
            function.ir.iter().any(|instruction| {
                matches!(
                    instruction,
                    IrInstruction::GetSymbol { symbol, .. }
                        if *symbol == SymbolId(fnv64(b"#t"))
                )
            })
        }));
    }

    #[test]
    fn deftype_layouts_vec4s_and_emits_its_runtime_type_creation() {
        let mut goos = Interpreter::new().unwrap();
        let parsed = goos
            .parser
            .parse(
                "vec4s.gc",
                "(define-extern format (function _varargs_ object))\n\
                 (define-extern method-set! (function type int object none))\n\
                 (deftype vec4s (uint128)\n  ((x float) (y float) (z float) (w float)))\n\
                 (defmethod print ((this vec4s))\n\
                   (format #t \"#<vector ~F ~F ~F ~F @ #x~X>\"\n\
                     (-> this x) (-> this y) (-> this z) (-> this w) this)\n\
                   this)",
            )
            .unwrap();
        let mut types = TypeSystem::new();
        let mut frontend = Frontend::new(&mut types);
        let visibility = Visibility::new(1, []);
        let file = frontend
            .compile_file(
                1,
                parsed.source_id,
                visibility.clone(),
                &mut types,
                &mut goos,
                &parsed.objects,
            )
            .unwrap();

        let vec4s = types.intern_type("vec4s");
        let definition = types.definition(vec4s, &visibility).unwrap();
        assert_eq!(
            definition.layout.kind,
            crate::type_system::TypeKind::BitField
        );
        assert_eq!(definition.layout.size, 16);
        assert_eq!(definition.layout.register_class, RegisterClass::Gpr128);
        assert_eq!(
            definition.method_count,
            crate::type_system::OBJECT_METHOD_COUNT
        );
        assert_eq!(definition.bit_fields.len(), 4);
        for (field, offset) in definition.bit_fields.iter().zip([0, 32, 64, 96]) {
            assert_eq!(field.offset_bits, offset);
            assert_eq!(field.size_bits, 32);
        }

        let top_level = &file.functions[0];
        let calls = top_level
            .ir
            .iter()
            .filter_map(|instruction| match instruction {
                IrInstruction::Call { argument_count } => Some(*argument_count),
                _ => None,
            })
            .collect::<Vec<_>>();
        assert_eq!(calls, [3, 3, 3]);
        assert_eq!(
            file.static_data.top_level.bytes(),
            0x0000_0009_0000_0010_u64.to_le_bytes()
        );

        let inspect = file
            .functions
            .iter()
            .find(|function| {
                function.segment == Segment::Debug
                    && function.identity
                        == FunctionIdentity::Method {
                            owner: vec4s,
                            name: SymbolId(fnv64(b"inspect")),
                        }
            })
            .unwrap();
        assert_eq!(
            inspect
                .ir
                .iter()
                .filter(|instruction| matches!(instruction, IrInstruction::Call { .. }))
                .count(),
            5
        );
        let first_call = inspect
            .ir
            .iter()
            .position(|instruction| matches!(instruction, IrInstruction::Call { .. }))
            .unwrap();
        let call_copies = &inspect.ir[first_call - 5..first_call];
        let expected_destinations = [
            PhysicalRegister::CALL,
            ARGUMENT_REGISTERS[0],
            ARGUMENT_REGISTERS[1],
            ARGUMENT_REGISTERS[2],
            ARGUMENT_REGISTERS[3],
        ];
        assert!(call_copies.iter().zip(expected_destinations).all(
            |(instruction, destination)| matches!(
                instruction,
                IrInstruction::Move {
                    destination: Register::Physical(actual),
                    ..
                } if *actual == destination
            )
        ));
        assert!(inspect.ir[..first_call - 5].iter().any(|instruction| {
            matches!(
                instruction,
                IrInstruction::Move {
                    destination: Register::Virtual(_),
                    source: Register::Virtual(_),
                    ..
                }
            )
        }));
        assert!(
            inspect
                .ir
                .iter()
                .any(|instruction| matches!(instruction, IrInstruction::Gpr128Upper64 { .. }))
        );
        assert_eq!(
            file.static_data
                .fixups
                .iter()
                .filter(|fixup| {
                    fixup.segment == Segment::Debug
                        && matches!(fixup.target, FixupTarget::Type { .. })
                })
                .count(),
            5
        );

        let print = file
            .functions
            .iter()
            .find(|function| {
                function.segment == Segment::Main
                    && function.identity
                        == FunctionIdentity::Method {
                            owner: vec4s,
                            name: SymbolId(fnv64(b"print")),
                        }
            })
            .unwrap();
        assert!(
            print.ir.iter().any(|instruction| matches!(
                instruction,
                IrInstruction::Call { argument_count: 7 }
            ))
        );
        assert_eq!(
            file.static_data
                .fixups
                .iter()
                .filter(|fixup| {
                    fixup.segment == Segment::Main
                        && matches!(fixup.target, FixupTarget::Type { .. })
                })
                .count(),
            1
        );
    }

    #[test]
    fn basic_type_methods_use_runtime_offsets_and_align16_macro() {
        let mut goos = Interpreter::new().unwrap();
        let parsed = goos
            .parser
            .parse(
                "bfloat.gc",
                "(define-extern format (function _varargs_ object))\n\
                 (define-extern method-set! (function type int object none))\n\
                 (deftype bfloat (basic) ((data float)))\n\
                 (defmethod print ((this bfloat))\n\
                   (format #t \"~f\" (-> this data))\n\
                   this)\n\
                 (defmacro align16 (value)\n\
                   `(logand #xfffffff0 (+ 15 ,value)))\n\
                 (defmethod asize-of ((this type))\n\
                   (align16 (+ (* 4 (-> this allocated-length)) 28)))",
            )
            .unwrap();
        let mut types = TypeSystem::new();
        let mut frontend = Frontend::new(&mut types);
        let visibility = Visibility::new(1, []);
        let file = frontend
            .compile_file(
                1,
                parsed.source_id,
                visibility.clone(),
                &mut types,
                &mut goos,
                &parsed.objects,
            )
            .unwrap();

        let bfloat = types.intern_type("bfloat");
        let definition = types.definition(bfloat, &visibility).unwrap();
        assert_eq!(definition.layout.kind, TypeKind::Basic);
        assert_eq!(definition.layout.size, 8);
        assert_eq!(definition.fields[0].offset, 4);

        let print = types.intern_symbol("print");
        let inspect = types.intern_symbol("inspect");
        let type_type = types.intern_type("type");
        let asize = types.intern_symbol("asize-of");
        let function = |owner, name| {
            file.functions
                .iter()
                .find(|function| function.identity == FunctionIdentity::Method { owner, name })
                .unwrap()
        };
        let inspector = function(bfloat, inspect);
        assert!(inspector.ir.iter().any(|instruction| matches!(
            instruction,
            IrInstruction::LoadMemory {
                operation: MemoryLoad::UnsignedWord,
                offset: -4,
                ..
            }
        )));
        assert!(inspector.ir.iter().any(|instruction| matches!(
            instruction,
            IrInstruction::LoadMemory {
                operation: MemoryLoad::FloatWord,
                offset: 0,
                ..
            }
        )));
        assert!(
            function(bfloat, print)
                .ir
                .iter()
                .any(|instruction| matches!(
                    instruction,
                    IrInstruction::Call { argument_count: 3 }
                ))
        );

        let asize = function(type_type, asize);
        assert!(asize.ir.iter().any(|instruction| matches!(
            instruction,
            IrInstruction::LoadMemory {
                operation: MemoryLoad::UnsignedHalfword,
                offset: 14,
                ..
            }
        )));
        assert!(asize.ir.iter().any(|instruction| matches!(
            instruction,
            IrInstruction::IntegerShiftImmediate {
                operation: ShiftOperation::Left,
                amount: 2,
                ..
            }
        )));
        assert!(asize.ir.iter().any(|instruction| matches!(
            instruction,
            IrInstruction::IntegerAddImmediate { value: 43, .. }
        )));
    }

    #[test]
    fn basic_type_loop_mutates_its_local_and_returns_early() {
        let mut goos = Interpreter::new().unwrap();
        let parsed = goos
            .parser
            .parse(
                "basic-type.gc",
                "(defmacro if (condition true-case &rest false-case)\n\
                   (if (> (length false-case) 1)\n\
                       (error \"if accepts at most one false case\") #f)\n\
                   (if (null? false-case)\n\
                       `(cond (,condition ,true-case))\n\
                       `(cond (,condition ,true-case) (else ,(car false-case)))))\n\
                 (defmacro until (test &rest body)\n\
                   (let ((reloop (gensym \"until\")))\n\
                     `(begin (label ,reloop) ,@body\n\
                             (when-goto (not ,test) ,reloop) #f)))\n\
                 (defun basic-type? ((value (const basic)) (parent-type (const type)))\n\
                   (let ((current-type (-> value type)) (root-type object))\n\
                     (until (= current-type root-type)\n\
                       (if (= current-type parent-type) (return #t))\n\
                       (set! current-type (-> current-type parent))))\n\
                   #f)",
            )
            .unwrap();
        let mut types = TypeSystem::new();
        let mut frontend = Frontend::new(&mut types);
        let file = frontend
            .compile_file(
                1,
                parsed.source_id,
                Visibility::new(1, []),
                &mut types,
                &mut goos,
                &parsed.objects,
            )
            .unwrap();
        let function = file
            .functions
            .iter()
            .find(|function| {
                function.identity == FunctionIdentity::Global(SymbolId(fnv64(b"basic-type?")))
            })
            .unwrap();

        assert!(function.ir.iter().any(|instruction| matches!(
            instruction,
            IrInstruction::LoadMemory {
                operation: MemoryLoad::UnsignedWord,
                offset: -4,
                ..
            }
        )));
        assert!(function.ir.iter().any(|instruction| matches!(
            instruction,
            IrInstruction::LoadMemory {
                operation: MemoryLoad::UnsignedWord,
                offset: 4,
                ..
            }
        )));
        assert_eq!(
            function
                .ir
                .iter()
                .filter(|instruction| matches!(instruction, IrInstruction::Branch { .. }))
                .count(),
            3
        );
        assert!(function.ir.iter().any(|instruction| matches!(
            instruction,
            IrInstruction::Move {
                must_emit: true,
                ..
            }
        )));
    }

    #[test]
    fn cond_result_type_is_the_lca_of_clause_results_not_implicit_false() {
        let mut goos = Interpreter::new().unwrap();
        let parsed = goos
            .parser
            .parse(
                "cond.gc",
                "(defun choose ((value int)) (cond ((= value 0) 7)))",
            )
            .unwrap();
        let mut types = TypeSystem::new();
        let int = types.intern_type_spec("int");
        let mut frontend = Frontend::new(&mut types);
        let file = frontend
            .compile_file(
                1,
                parsed.source_id,
                Visibility::new(1, []),
                &mut types,
                &mut goos,
                &parsed.objects,
            )
            .unwrap();
        let choose = file
            .functions
            .iter()
            .find(|function| {
                function.identity == FunctionIdentity::Global(SymbolId(fnv64(b"choose")))
            })
            .unwrap();
        assert_eq!(choose.return_type, int);
    }

    #[test]
    fn logical_or_short_circuits_and_method_table_supports_variable_indices() {
        let mut goos = Interpreter::new().unwrap();
        let parsed = goos
            .parser
            .parse(
                "type-helpers.gc",
                "(defun either ((left type) (right type))\n\
                   (or (= left right) (= left 0)))\n\
                 (defun method-at ((owner type) (index int))\n\
                   (-> owner method-table index))",
            )
            .unwrap();
        let mut types = TypeSystem::new();
        let mut frontend = Frontend::new(&mut types);
        let file = frontend
            .compile_file(
                1,
                parsed.source_id,
                Visibility::new(1, []),
                &mut types,
                &mut goos,
                &parsed.objects,
            )
            .unwrap();
        let function = |name: &[u8]| {
            file.functions
                .iter()
                .find(|function| {
                    function.identity == FunctionIdentity::Global(SymbolId(fnv64(name)))
                })
                .unwrap()
        };
        let either = function(b"either");
        assert!(either.ir.iter().any(|instruction| matches!(
            instruction,
            IrInstruction::Branch {
                delay: crate::ir::BranchDelay::Likely,
                ..
            }
        )));

        let method = function(b"method-at");
        assert!(method.ir.iter().any(|instruction| matches!(
            instruction,
            IrInstruction::IntegerShiftImmediate {
                operation: ShiftOperation::Left,
                amount: 2,
                ..
            }
        )));
        assert!(method.ir.iter().any(|instruction| matches!(
            instruction,
            IrInstruction::IntegerArithmetic {
                operation: ArithmeticOperation::Add,
                ..
            }
        )));
        assert!(method.ir.iter().any(|instruction| matches!(
            instruction,
            IrInstruction::LoadMemory {
                operation: MemoryLoad::UnsignedWord,
                offset: 16,
                ..
            }
        )));
    }

    #[test]
    fn literal_values_stay_lazy_and_flush_only_once() {
        let mut goos = Interpreter::new().unwrap();
        let parsed = goos.parser.parse("lazy.gc", "1.0 0").unwrap();
        let mut types = TypeSystem::new();
        let none = types.intern_type_spec("none");
        let mut frontend = Frontend::new(&mut types);
        let file = FileContext::new(1, parsed.source_id, Visibility::new(1, []));
        let mut context = FrontendContext::new(file, none);
        context
            .begin_function(FunctionIdentity::TopLevel, Segment::TopLevel, none)
            .unwrap();

        let mut float = compile_expression(
            &mut frontend,
            &mut types,
            &mut goos,
            &mut context,
            &parsed.objects[0],
        )
        .unwrap();
        assert!(matches!(float.kind, ValueKind::Float(1.0)));
        assert!(context.current_function_data().unwrap().ir.is_empty());
        assert!(context.file.static_data.main.is_empty());

        let first = flush_value(&mut context, &parsed.objects[0], &mut float).unwrap();
        let second = flush_value(&mut context, &parsed.objects[0], &mut float).unwrap();
        assert_eq!(first, second);
        assert_eq!(context.current_function_data().unwrap().ir.len(), 1);
        assert_eq!(context.current_function_data().unwrap().registers.len(), 1);
        assert_eq!(context.file.static_data.main.len(), 4);

        let mut zero = compile_expression(
            &mut frontend,
            &mut types,
            &mut goos,
            &mut context,
            &parsed.objects[1],
        )
        .unwrap();
        let first = flush_value(&mut context, &parsed.objects[1], &mut zero).unwrap();
        let second = flush_value(&mut context, &parsed.objects[1], &mut zero).unwrap();
        assert_eq!(first, second);
        assert!(matches!(first, Register::Virtual(_)));
        assert_eq!(context.current_function_data().unwrap().ir.len(), 2);
        assert_eq!(context.current_function_data().unwrap().registers.len(), 2);
    }
}
