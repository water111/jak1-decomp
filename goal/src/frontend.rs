use crate::diagnostic::Diagnostic;
use crate::driver::FileId;
use crate::frontend_parse::ListCursor;
use crate::ir::{
    FunctionId, IrInstruction, LabelId, LabelTable, PhysicalRegister, Register, VirtualRegister,
    VirtualRegisterId,
};
use crate::object::{Object, SymbolId, SymbolMap};
use crate::span::{SourceId, SpanInfo};
use crate::static_data::{Segment, StaticData};
use crate::type_system::{RegisterClass, Type, TypeSpec, Visibility};
use crate::value::Value;

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

#[derive(Debug)]
pub struct FunctionData {
    pub identity: FunctionIdentity,
    pub parent: Option<FunctionId>,
    pub segment: Segment,
    pub return_type: TypeSpec,
    pub ir: Vec<IrInstruction>,
    pub registers: Vec<VirtualRegister>,
    pub labels: LabelTable,
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
            labels: LabelTable::default(),
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
        self.registers.push(VirtualRegister {
            ty,
            class,
            number,
            force_single_web: false,
        });
        Ok(id)
    }

    pub fn allocate_label(&mut self) -> Result<LabelId, String> {
        self.labels.allocate()
    }

    pub fn bind_label(&mut self, label: LabelId) -> Result<(), String> {
        self.labels.bind(label, self.ir.len())
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
    none_type: TypeSpec,
}

impl FrontendContext {
    pub fn new(file: FileContext, none_type: TypeSpec) -> Self {
        Self {
            file,
            function_stack: Vec::new(),
            macro_frames: Vec::new(),
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
            data.ir.push(IrInstruction::LoadConstant {
                destination: Register::Virtual(result),
                value: 0,
            });
            data.ir.push(IrInstruction::Return {
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

pub type FormHandler = fn(
    &mut Frontend,
    &Object,
    ListCursor,
    &mut FrontendContext,
    ExpressionPosition,
) -> Result<Value, Diagnostic>;

#[derive(Debug)]
pub struct Frontend {
    handlers: SymbolMap<FormHandler>,
    none_type: TypeSpec,
}

impl Frontend {
    pub fn new(none_type: TypeSpec) -> Self {
        Self {
            handlers: SymbolMap::default(),
            none_type,
        }
    }

    pub fn register_form(&mut self, symbol: SymbolId, handler: FormHandler) {
        self.handlers.insert(symbol, handler);
    }

    pub fn form_handler(&self, symbol: SymbolId) -> Option<FormHandler> {
        self.handlers.get(&symbol).copied()
    }

    pub fn compile_file(
        &mut self,
        file_id: FileId,
        source_id: SourceId,
        visibility: Visibility,
        forms: &[Object],
    ) -> Result<FileContext, Diagnostic> {
        if !forms.is_empty() {
            return Err(Diagnostic::error(
                "GOAL IR generation is not implemented for nonempty files",
            ));
        }
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
        context.end_function().map_err(Diagnostic::error)?;
        context.finish().map_err(Diagnostic::error)
    }
}

#[cfg(test)]
mod tests {
    use crate::parse::Parser;
    use crate::type_system::TypeSystem;

    use super::*;

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
                IrInstruction::LoadConstant { value: 0, .. },
                IrInstruction::Return { .. }
            ]
        ));
    }
}
