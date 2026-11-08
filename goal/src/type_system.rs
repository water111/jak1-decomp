use crate::driver::FileId;
use crate::object::{IdentityMap, SymbolId, fnv64};
use crate::span::SpanInfo;
use std::collections::{HashMap, HashSet};

pub const BUILTIN_FILE_ID: FileId = FileId::MAX;
pub const POINTER_SIZE: u32 = 4;
pub const RUNTIME_TYPE_METHODS_OFFSET: u32 = 16;
pub const OBJECT_METHOD_COUNT: u16 = 9;

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct RuntimeType(SymbolId);

impl RuntimeType {
    pub fn symbol(self) -> SymbolId {
        self.0
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct Type(SymbolId);

impl Type {
    pub fn symbol(self) -> SymbolId {
        self.0
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct TypeSpec(u32);

#[derive(Clone, Debug, Eq, Hash, PartialEq)]
struct TypeSpecData {
    base: Type,
    arguments: Box<[TypeSpec]>,
    behavior: Option<TypeSpec>,
}

#[derive(Clone, Debug)]
pub struct Visibility {
    file: FileId,
    visible_files: Vec<u64>,
    unrestricted: bool,
}

impl Visibility {
    pub fn new(file: FileId, dependencies: impl IntoIterator<Item = FileId>) -> Self {
        let dependencies: Vec<FileId> = dependencies.into_iter().collect();
        let mut visible_files = vec![
            0;
            dependencies
                .iter()
                .copied()
                .max()
                .map_or(0, |id| id as usize / 64 + 1)
        ];
        for dependency in dependencies {
            visible_files[dependency as usize / 64] |= 1 << (dependency % 64);
        }
        Self {
            file,
            visible_files,
            unrestricted: false,
        }
    }

    pub fn unrestricted() -> Self {
        Self {
            file: BUILTIN_FILE_ID,
            visible_files: Vec::new(),
            unrestricted: true,
        }
    }

    pub fn can_see(&self, owner: FileId) -> bool {
        self.unrestricted
            || owner == BUILTIN_FILE_ID
            || owner == self.file
            || self
                .visible_files
                .get(owner as usize / 64)
                .is_some_and(|word| word & (1 << (owner % 64)) != 0)
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum RegisterClass {
    Gpr,
    Fpr,
    Vf,
    Gpr128,
}

impl RegisterClass {
    pub const REGISTER_FILE_COUNT: usize = 3;

    pub const fn register_file(self) -> usize {
        match self {
            Self::Gpr | Self::Gpr128 => 0,
            Self::Fpr => 1,
            Self::Vf => 2,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TypeKind {
    None,
    Value,
    Structure,
    Basic,
    BitField,
}

impl TypeKind {
    pub fn is_reference(self) -> bool {
        matches!(self, Self::Structure | Self::Basic)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TypeLayout {
    pub kind: TypeKind,
    pub size: u32,
    pub alignment: u32,
    pub load_size: u32,
    pub load_signed: bool,
    pub register_class: RegisterClass,
    pub boxed: bool,
    pub offset: u32,
    pub dynamic: bool,
    pub inline_array_alignment: u32,
}

impl TypeLayout {
    pub fn none() -> Self {
        Self {
            kind: TypeKind::None,
            size: 0,
            alignment: 1,
            load_size: 0,
            load_signed: false,
            register_class: RegisterClass::Gpr,
            boxed: false,
            offset: 0,
            dynamic: false,
            inline_array_alignment: 1,
        }
    }

    pub fn value(
        size: u32,
        alignment: u32,
        load_signed: bool,
        register_class: RegisterClass,
    ) -> Self {
        Self {
            kind: TypeKind::Value,
            size,
            alignment,
            load_size: size,
            load_signed,
            register_class,
            boxed: false,
            offset: 0,
            dynamic: false,
            inline_array_alignment: alignment,
        }
    }

    pub fn structure(kind: TypeKind, boxed: bool) -> Self {
        debug_assert!(matches!(kind, TypeKind::Structure | TypeKind::Basic));
        let offset = if kind == TypeKind::Basic {
            POINTER_SIZE
        } else {
            0
        };
        Self {
            kind,
            size: 0,
            alignment: 16,
            load_size: POINTER_SIZE,
            load_signed: false,
            register_class: RegisterClass::Gpr,
            boxed,
            offset,
            dynamic: false,
            inline_array_alignment: 16,
        }
    }

    pub fn bit_field(size: u32, alignment: u32, load_signed: bool) -> Self {
        let mut result = Self::value(size, alignment, load_signed, RegisterClass::Gpr);
        result.kind = TypeKind::BitField;
        result
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FieldAccess {
    Public,
    Private,
    ReadOnly,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PrintAs {
    Default,
    Hex,
    Nothing,
    Meters,
    Degrees,
    Type(Type),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum OverlaySelector {
    Field(SymbolId),
    Index(u32),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum OverlaySpec {
    ByteOffset(u32),
    Field(SymbolId),
    Path {
        field: SymbolId,
        selectors: Vec<OverlaySelector>,
    },
}

#[derive(Clone, Debug)]
pub struct FieldDeclaration {
    pub name: SymbolId,
    pub field_type: TypeSpec,
    pub array_size: Option<u32>,
    pub dynamic: bool,
    pub offset: Option<u32>,
    pub overlay_at: Option<OverlaySpec>,
    pub inline: bool,
    pub align_bits: Option<u32>,
    pub print_as: PrintAs,
    pub accessor: bool,
    pub replace: bool,
    pub access: FieldAccess,
    pub span: Option<SpanInfo>,
}

impl FieldDeclaration {
    pub fn new(name: SymbolId, field_type: TypeSpec) -> Self {
        Self {
            name,
            field_type,
            array_size: None,
            dynamic: false,
            offset: None,
            overlay_at: None,
            inline: false,
            align_bits: None,
            print_as: PrintAs::Default,
            accessor: true,
            replace: false,
            access: FieldAccess::Public,
            span: None,
        }
    }
}

#[derive(Clone, Debug)]
pub struct FieldDefinition {
    pub name: SymbolId,
    pub field_type: TypeSpec,
    pub offset: u32,
    pub size: u32,
    pub alignment: u32,
    pub array_size: Option<u32>,
    pub dynamic: bool,
    pub overlay: bool,
    pub inline: bool,
    pub print_as: PrintAs,
    pub accessor: bool,
    pub replaces_parent: bool,
    pub access: FieldAccess,
    pub span: Option<SpanInfo>,
}

#[derive(Clone, Debug)]
pub struct BitFieldDeclaration {
    pub name: SymbolId,
    pub field_type: TypeSpec,
    pub offset_bits: Option<u32>,
    pub size_bits: Option<u32>,
    pub overlay_at_bits: Option<u32>,
    pub print_as: PrintAs,
    pub accessor: bool,
    pub replace: bool,
    pub span: Option<SpanInfo>,
}

impl BitFieldDeclaration {
    pub fn new(name: SymbolId, field_type: TypeSpec) -> Self {
        Self {
            name,
            field_type,
            offset_bits: None,
            size_bits: None,
            overlay_at_bits: None,
            print_as: PrintAs::Default,
            accessor: true,
            replace: false,
            span: None,
        }
    }
}

#[derive(Clone, Debug)]
pub struct BitFieldDefinition {
    pub name: SymbolId,
    pub field_type: TypeSpec,
    pub offset_bits: u32,
    pub size_bits: u32,
    pub overlay: bool,
    pub print_as: PrintAs,
    pub accessor: bool,
    pub replaces_parent: bool,
    pub span: Option<SpanInfo>,
}

#[derive(Clone, Debug)]
pub struct MethodDeclaration {
    pub name: SymbolId,
    pub signature: TypeSpec,
    pub slot: Option<u16>,
    pub private: bool,
    pub no_virtual: bool,
    pub span: Option<SpanInfo>,
}

impl MethodDeclaration {
    pub fn new(name: SymbolId, signature: TypeSpec) -> Self {
        Self {
            name,
            signature,
            slot: None,
            private: false,
            no_virtual: false,
            span: None,
        }
    }
}

#[derive(Clone, Debug)]
pub struct MethodDefinition {
    pub name: SymbolId,
    pub signature: TypeSpec,
    pub slot: u16,
    pub defined_in: Type,
    pub private: bool,
    pub no_virtual: bool,
    pub overrides_parent: bool,
    pub span: Option<SpanInfo>,
}

#[derive(Clone, Debug)]
pub struct TypeOptions {
    pub documentation: Option<String>,
    pub generate_inspector: bool,
    pub allow_array_access: bool,
    pub c_type: bool,
    pub dynamic_size: bool,
    pub pad_inline_arrays: bool,
    pub large_type: bool,
    pub friends: Vec<TypeSpec>,
    pub states: Vec<SymbolId>,
    pub state_methods: Vec<SymbolId>,
}

impl Default for TypeOptions {
    fn default() -> Self {
        Self {
            documentation: None,
            generate_inspector: true,
            allow_array_access: false,
            c_type: false,
            dynamic_size: false,
            pad_inline_arrays: false,
            large_type: false,
            friends: Vec::new(),
            states: Vec::new(),
            state_methods: Vec::new(),
        }
    }
}

#[derive(Clone, Debug)]
pub struct TypeDeclaration {
    pub name: Type,
    pub parent: Option<Type>,
    pub runtime_type: Option<RuntimeType>,
    pub method_count: Option<u16>,
    pub span: Option<SpanInfo>,
}

impl TypeDeclaration {
    pub fn new(name: Type, parent: Option<Type>) -> Self {
        Self {
            name,
            parent,
            runtime_type: None,
            method_count: None,
            span: None,
        }
    }
}

#[derive(Clone, Debug)]
pub struct TypeDefinition {
    pub name: Type,
    pub parent: Option<Type>,
    pub runtime_type: RuntimeType,
    pub layout: TypeLayout,
    pub fields: Vec<FieldDefinition>,
    pub bit_fields: Vec<BitFieldDefinition>,
    pub methods: Vec<MethodDefinition>,
    pub method_count: u16,
    pub options: TypeOptions,
    pub span: Option<SpanInfo>,
    field_declarations: Vec<FieldDeclaration>,
    bit_field_declarations: Vec<BitFieldDeclaration>,
    method_declarations: Vec<MethodDeclaration>,
}

impl TypeDefinition {
    pub fn new(name: Type, parent: Option<Type>, layout: TypeLayout) -> Self {
        Self {
            name,
            parent,
            runtime_type: RuntimeType(name.0),
            layout,
            fields: Vec::new(),
            bit_fields: Vec::new(),
            methods: Vec::new(),
            method_count: 0,
            options: TypeOptions::default(),
            span: None,
            field_declarations: Vec::new(),
            bit_field_declarations: Vec::new(),
            method_declarations: Vec::new(),
        }
    }

    pub fn add_field(&mut self, field: FieldDeclaration) {
        self.field_declarations.push(field);
    }

    pub fn add_bit_field(&mut self, field: BitFieldDeclaration) {
        self.bit_field_declarations.push(field);
    }

    pub fn add_method(&mut self, method: MethodDeclaration) {
        self.method_declarations.push(method);
    }
}

#[derive(Clone, Debug)]
pub struct EnumEntry {
    pub name: SymbolId,
    pub value: i64,
    pub span: Option<SpanInfo>,
}

#[derive(Clone, Debug)]
pub struct EnumDefinition {
    pub name: Type,
    pub parent: Type,
    pub runtime_type: RuntimeType,
    pub entries: Vec<EnumEntry>,
    pub bitfield: bool,
    pub documentation: Option<String>,
    pub span: Option<SpanInfo>,
}

#[derive(Clone, Debug)]
struct Owned<T> {
    owner: FileId,
    value: T,
}

pub type TypeResult<T> = Result<T, String>;

#[derive(Clone, Debug)]
pub struct FieldLookup<'a> {
    pub field: &'a FieldDefinition,
    pub owner_type: Type,
    pub result_type: TypeSpec,
    pub needs_deref: bool,
}

#[derive(Clone, Debug)]
pub struct BitFieldLookup<'a> {
    pub field: &'a BitFieldDefinition,
    pub owner_type: Type,
    pub sign_extend: bool,
}

#[derive(Clone, Debug)]
pub struct DerefInfo {
    pub memory_deref: bool,
    pub sign_extend: bool,
    pub register_class: RegisterClass,
    pub stride: u32,
    pub offset: u32,
    pub load_size: Option<u32>,
    pub result_type: TypeSpec,
}

#[derive(Debug)]
pub struct TypeSystem {
    names: IdentityMap<SymbolId, Box<str>>,
    specs: Vec<TypeSpecData>,
    spec_ids: HashMap<TypeSpecData, TypeSpec>,
    declarations: IdentityMap<Type, Vec<Owned<TypeDeclaration>>>,
    definitions: IdentityMap<Type, Vec<Owned<TypeDefinition>>>,
    enums: IdentityMap<Type, Vec<Owned<EnumDefinition>>>,
}

impl Default for TypeSystem {
    fn default() -> Self {
        Self::new()
    }
}

impl TypeSystem {
    pub fn new() -> Self {
        let mut result = Self {
            names: IdentityMap::default(),
            specs: Vec::new(),
            spec_ids: HashMap::new(),
            declarations: IdentityMap::default(),
            definitions: IdentityMap::default(),
            enums: IdentityMap::default(),
        };
        result.install_builtin_types();
        result
    }

    pub fn intern_symbol(&mut self, name: &str) -> SymbolId {
        let id = SymbolId(fnv64(name.as_bytes()));
        if let Some(existing) = self.names.get(&id) {
            debug_assert_eq!(&**existing, name, "FNV-64 symbol hash collision");
        } else {
            self.names.insert(id, Box::from(name));
        }
        id
    }

    pub fn intern_type(&mut self, name: &str) -> Type {
        Type(self.intern_symbol(name))
    }

    pub fn intern_runtime_type(&mut self, name: &str) -> RuntimeType {
        RuntimeType(self.intern_symbol(name))
    }

    pub fn symbol_name(&self, symbol: SymbolId) -> Option<&str> {
        self.names.get(&symbol).map(Box::as_ref)
    }

    pub fn builtin_types(&self) -> impl Iterator<Item = Type> + '_ {
        self.definitions.iter().filter_map(|(ty, definitions)| {
            definitions
                .iter()
                .any(|definition| definition.owner == BUILTIN_FILE_ID)
                .then_some(*ty)
        })
    }

    pub fn type_name(&self, ty: Type) -> &str {
        self.symbol_name(ty.0).unwrap_or("<unknown-type-id>")
    }

    pub fn intern_compound_type_spec(
        &mut self,
        base: Type,
        arguments: impl IntoIterator<Item = TypeSpec>,
        behavior: Option<TypeSpec>,
    ) -> TypeSpec {
        let data = TypeSpecData {
            base,
            arguments: arguments.into_iter().collect(),
            behavior,
        };
        if let Some(id) = self.spec_ids.get(&data) {
            return *id;
        }
        let id = TypeSpec(u32::try_from(self.specs.len()).expect("exhausted type-spec IDs"));
        self.specs.push(data.clone());
        self.spec_ids.insert(data, id);
        id
    }

    pub fn intern_type_spec(&mut self, name: &str) -> TypeSpec {
        let ty = self.intern_type(name);
        self.intern_compound_type_spec(ty, [], None)
    }

    pub fn pointer_type(&mut self, pointee: TypeSpec) -> TypeSpec {
        let pointer = self.intern_type("pointer");
        self.intern_compound_type_spec(pointer, [pointee], None)
    }

    pub fn inline_array_type(&mut self, element: TypeSpec) -> TypeSpec {
        let inline_array = self.intern_type("inline-array");
        self.intern_compound_type_spec(inline_array, [element], None)
    }

    pub fn function_type(
        &mut self,
        parameters: impl IntoIterator<Item = TypeSpec>,
        result: TypeSpec,
    ) -> TypeSpec {
        let function = self.intern_type("function");
        let mut arguments: Vec<TypeSpec> = parameters.into_iter().collect();
        arguments.push(result);
        self.intern_compound_type_spec(function, arguments, None)
    }

    pub fn with_behavior(&mut self, spec: TypeSpec, behavior: TypeSpec) -> TypeSpec {
        let data = self.spec_data(spec).clone();
        self.intern_compound_type_spec(data.base, data.arguments.iter().copied(), Some(behavior))
    }

    pub fn type_spec_base(&self, spec: TypeSpec) -> Type {
        self.spec_data(spec).base
    }

    pub fn is_pointer(&self, spec: TypeSpec) -> bool {
        self.type_spec_base(spec).symbol() == SymbolId(fnv64(b"pointer"))
    }

    pub fn type_spec_arguments(&self, spec: TypeSpec) -> &[TypeSpec] {
        &self.spec_data(spec).arguments
    }

    pub fn type_spec_behavior(&self, spec: TypeSpec) -> Option<TypeSpec> {
        self.spec_data(spec).behavior
    }

    pub fn coerce_to_register_type(&mut self, spec: TypeSpec) -> TypeSpec {
        let data = self.spec_data(spec);
        if !data.arguments.is_empty() || data.behavior.is_some() {
            return spec;
        }
        match self.type_name(data.base) {
            "int8" | "int16" | "int32" | "int64" | "integer" | "sinteger" => {
                self.intern_type_spec("int")
            }
            "uint8" | "uint16" | "uint32" | "uint64" | "uinteger" => self.intern_type_spec("uint"),
            _ => spec,
        }
    }

    pub fn substitute_method_type(&mut self, spec: TypeSpec, receiver_type: TypeSpec) -> TypeSpec {
        let data = self.spec_data(spec).clone();
        if self.type_name(data.base) == "_type_" && data.arguments.is_empty() {
            return receiver_type;
        }
        let arguments: Vec<TypeSpec> = data
            .arguments
            .iter()
            .map(|argument| self.substitute_method_type(*argument, receiver_type))
            .collect();
        let behavior = data
            .behavior
            .map(|behavior| self.substitute_method_type(behavior, receiver_type));
        self.intern_compound_type_spec(data.base, arguments, behavior)
    }

    pub fn format_type_spec(&self, spec: TypeSpec) -> String {
        let data = self.spec_data(spec);
        if data.arguments.is_empty() && data.behavior.is_none() {
            return self.type_name(data.base).to_owned();
        }
        let mut members = Vec::with_capacity(data.arguments.len() + 3);
        members.push(self.type_name(data.base).to_owned());
        members.extend(
            data.arguments
                .iter()
                .map(|argument| self.format_type_spec(*argument)),
        );
        if let Some(behavior) = data.behavior {
            members.push(":behavior".to_owned());
            members.push(self.format_type_spec(behavior));
        }
        format!("({})", members.join(" "))
    }

    pub fn declare_type(
        &mut self,
        owner: FileId,
        declaration: TypeDeclaration,
        visibility: &Visibility,
    ) -> TypeResult<()> {
        self.reject_visible_duplicate(declaration.name, owner, visibility, false)?;
        self.declarations
            .entry(declaration.name)
            .or_default()
            .push(Owned {
                owner,
                value: declaration,
            });
        Ok(())
    }

    pub fn define_type(
        &mut self,
        owner: FileId,
        definition: TypeDefinition,
        visibility: &Visibility,
    ) -> TypeResult<&TypeDefinition> {
        self.reject_visible_duplicate(definition.name, owner, visibility, true)?;
        self.validate_parent(
            definition.name,
            definition.parent,
            visibility,
            definition.span,
        )?;
        let definition = self.layout_definition(definition, visibility)?;
        let name = definition.name;
        self.definitions.entry(name).or_default().push(Owned {
            owner,
            value: definition,
        });
        Ok(&self.definitions.get(&name).unwrap().last().unwrap().value)
    }

    pub fn define_enum(
        &mut self,
        owner: FileId,
        definition: EnumDefinition,
        visibility: &Visibility,
    ) -> TypeResult<&EnumDefinition> {
        self.reject_visible_duplicate(definition.name, owner, visibility, true)?;
        self.validate_parent(
            definition.name,
            Some(definition.parent),
            visibility,
            definition.span,
        )?;
        let parent_layout = self.layout(definition.parent, visibility)?;
        if parent_layout.kind.is_reference() || parent_layout.kind == TypeKind::None {
            return Err(format!(
                "enum `{}` must inherit from a value type",
                self.type_name(definition.name)
            ));
        }
        let mut entry_names = HashSet::new();
        let mut entry_values = Vec::new();
        for entry in &definition.entries {
            if !entry_names.insert(entry.name) {
                return Err(format!(
                    "enum `{}` has duplicate entry `{}`",
                    self.type_name(definition.name),
                    self.symbol_name(entry.name).unwrap_or("<unknown>")
                ));
            }
            if !definition.bitfield && entry_values.contains(&entry.value) {
                return Err(format!(
                    "enum `{}` uses value {} more than once",
                    self.type_name(definition.name),
                    entry.value
                ));
            }
            entry_values.push(entry.value);
        }
        let name = definition.name;
        self.enums.entry(name).or_default().push(Owned {
            owner,
            value: definition,
        });
        Ok(&self.enums.get(&name).unwrap().last().unwrap().value)
    }

    pub fn remove_definitions(&mut self, owner: FileId) {
        remove_owned(&mut self.declarations, owner);
        remove_owned(&mut self.definitions, owner);
        remove_owned(&mut self.enums, owner);
    }

    pub fn declaration(&self, ty: Type, visibility: &Visibility) -> TypeResult<&TypeDeclaration> {
        self.visible_record(&self.declarations, ty, visibility, "declaration")
    }

    pub fn definition(&self, ty: Type, visibility: &Visibility) -> TypeResult<&TypeDefinition> {
        self.visible_record(&self.definitions, ty, visibility, "definition")
    }

    pub fn enum_definition(
        &self,
        ty: Type,
        visibility: &Visibility,
    ) -> TypeResult<&EnumDefinition> {
        self.visible_record(&self.enums, ty, visibility, "enum definition")
    }

    pub fn is_declared(&self, ty: Type, visibility: &Visibility) -> bool {
        self.visible_count(&self.declarations, ty, visibility) != 0
            || self.visible_count(&self.definitions, ty, visibility) != 0
            || self.visible_count(&self.enums, ty, visibility) != 0
    }

    pub fn layout(&self, ty: Type, visibility: &Visibility) -> TypeResult<TypeLayout> {
        if let Ok(definition) = self.definition(ty, visibility) {
            return Ok(definition.layout);
        }
        if let Ok(definition) = self.enum_definition(ty, visibility) {
            return self.layout(definition.parent, visibility);
        }
        if self.declaration(ty, visibility).is_ok() {
            return Err(format!(
                "type `{}` is only forward declared and has no layout yet",
                self.type_name(ty)
            ));
        }
        Err(self.unknown_or_invisible(ty, visibility, "type definition"))
    }

    pub fn size_of(&self, ty: Type, visibility: &Visibility) -> TypeResult<u32> {
        Ok(self.layout(ty, visibility)?.size)
    }

    pub fn field_definition(
        &self,
        base: Type,
        name: SymbolId,
        visibility: &Visibility,
    ) -> TypeResult<&FieldDefinition> {
        Ok(self.find_field(base, name, visibility)?.1)
    }

    pub fn field_offset(
        &self,
        base: Type,
        name: SymbolId,
        visibility: &Visibility,
    ) -> TypeResult<u32> {
        Ok(self.field_definition(base, name, visibility)?.offset)
    }

    pub fn method_slot(
        &self,
        base: Type,
        name: SymbolId,
        visibility: &Visibility,
    ) -> TypeResult<u16> {
        Ok(self.lookup_method(base, name, visibility)?.slot)
    }

    pub fn method_count(&self, ty: Type, visibility: &Visibility) -> TypeResult<Option<u16>> {
        if let Ok(definition) = self.definition(ty, visibility) {
            return Ok(Some(definition.method_count));
        }
        Ok(self.declaration(ty, visibility)?.method_count)
    }

    pub fn enum_value(
        &self,
        enum_type: Type,
        name: SymbolId,
        visibility: &Visibility,
    ) -> TypeResult<i64> {
        let definition = self.enum_definition(enum_type, visibility)?;
        definition
            .entries
            .iter()
            .find(|entry| entry.name == name)
            .map(|entry| entry.value)
            .ok_or_else(|| {
                format!(
                    "enum `{}` has no entry named `{}`",
                    self.type_name(enum_type),
                    self.symbol_name(name).unwrap_or("<unknown>")
                )
            })
    }

    pub fn runtime_type(&self, spec: TypeSpec, visibility: &Visibility) -> TypeResult<RuntimeType> {
        let base = self.type_spec_base(spec);
        if let Ok(definition) = self.definition(base, visibility) {
            return Ok(definition.runtime_type);
        }
        if let Ok(definition) = self.enum_definition(base, visibility) {
            return Ok(definition.runtime_type);
        }
        if let Ok(declaration) = self.declaration(base, visibility)
            && let Some(runtime_type) = declaration.runtime_type
        {
            return Ok(runtime_type);
        }
        Err(self.unknown_or_invisible(base, visibility, "runtime type"))
    }

    pub fn typecheck(
        &self,
        expected: TypeSpec,
        actual: TypeSpec,
        visibility: &Visibility,
    ) -> TypeResult<()> {
        self.typecheck_inner(expected, actual, visibility, &mut Vec::new())
    }

    pub fn typecheck_assignment(
        &self,
        expected: TypeSpec,
        actual: TypeSpec,
        visibility: &Visibility,
    ) -> TypeResult<()> {
        let result = self.typecheck(expected, actual, visibility);
        if result.is_ok() {
            return result;
        }
        let integer = Type(SymbolId(fnv64(b"integer")));
        let expected_is_integer =
            self.is_base_subtype(integer, self.type_spec_base(expected), visibility)?;
        let actual_is_integer =
            self.is_base_subtype(integer, self.type_spec_base(actual), visibility)?;
        if expected_is_integer && actual_is_integer {
            Ok(())
        } else {
            result
        }
    }

    pub fn is_subtype(
        &self,
        expected: TypeSpec,
        actual: TypeSpec,
        visibility: &Visibility,
    ) -> TypeResult<bool> {
        match self.typecheck(expected, actual, visibility) {
            Ok(()) => Ok(true),
            Err(_) => Ok(false),
        }
    }

    pub fn integer_is_signed(&self, spec: TypeSpec, visibility: &Visibility) -> TypeResult<bool> {
        let ty = self.type_spec_base(spec);
        let integer = Type(SymbolId(fnv64(b"integer")));
        if !self.is_base_subtype(integer, ty, visibility)? {
            return Err(format!(
                "type `{}` is not an integer",
                self.format_type_spec(spec)
            ));
        }
        Ok(self.layout(ty, visibility)?.load_signed)
    }

    pub fn lowest_common_ancestor(
        &mut self,
        left: TypeSpec,
        right: TypeSpec,
        visibility: &Visibility,
    ) -> TypeResult<TypeSpec> {
        let left_data = self.spec_data(left).clone();
        let right_data = self.spec_data(right).clone();
        let base = self.lca_base(left_data.base, right_data.base, visibility)?;
        let arguments = if !left_data.arguments.is_empty()
            && left_data.arguments.len() == right_data.arguments.len()
        {
            left_data
                .arguments
                .iter()
                .zip(right_data.arguments.iter())
                .map(|(left, right)| self.lowest_common_ancestor(*left, *right, visibility))
                .collect::<TypeResult<Vec<_>>>()?
        } else {
            Vec::new()
        };
        let function = SymbolId(fnv64(b"function"));
        let varargs = Type(SymbolId(fnv64(b"_varargs_")));
        let arguments = if base.0 == function
            && arguments.len() == 2
            && (self.type_spec_base(left_data.arguments[0]) == varargs
                || self.type_spec_base(right_data.arguments[0]) == varargs)
        {
            Vec::new()
        } else {
            arguments
        };
        let behavior = match (left_data.behavior, right_data.behavior) {
            (Some(left), Some(right)) => {
                Some(self.lowest_common_ancestor(left, right, visibility)?)
            }
            _ => None,
        };
        Ok(self.intern_compound_type_spec(base, arguments, behavior))
    }

    pub fn lookup_field(
        &mut self,
        base: Type,
        name: SymbolId,
        visibility: &Visibility,
    ) -> TypeResult<FieldLookup<'_>> {
        let (owner_type, field) = self.find_field(base, name, visibility)?;
        let field = field.clone();
        let (result_type, needs_deref) = if field.inline {
            if field.array_size.is_some() {
                (self.inline_array_type(field.field_type), false)
            } else {
                (field.field_type, false)
            }
        } else if field.array_size.is_some() || field.dynamic {
            (self.pointer_type(field.field_type), false)
        } else {
            (field.field_type, true)
        };
        let stored = self.find_field(base, name, visibility)?.1;
        Ok(FieldLookup {
            field: stored,
            owner_type,
            result_type,
            needs_deref,
        })
    }

    pub fn lookup_bit_field(
        &self,
        base: Type,
        name: SymbolId,
        visibility: &Visibility,
    ) -> TypeResult<BitFieldLookup<'_>> {
        let mut current = Some(base);
        while let Some(ty) = current {
            let definition = self.definition(ty, visibility)?;
            if let Some(field) = definition
                .bit_fields
                .iter()
                .find(|field| field.name == name)
            {
                let layout = self.layout(self.type_spec_base(field.field_type), visibility)?;
                return Ok(BitFieldLookup {
                    field,
                    owner_type: ty,
                    sign_extend: layout.load_signed,
                });
            }
            current = definition.parent;
        }
        Err(format!(
            "type `{}` has no bit field named `{}`",
            self.type_name(base),
            self.symbol_name(name).unwrap_or("<unknown>")
        ))
    }

    pub fn lookup_method(
        &self,
        base: Type,
        name: SymbolId,
        visibility: &Visibility,
    ) -> TypeResult<&MethodDefinition> {
        let mut current = Some(base);
        while let Some(ty) = current {
            let definition = self.definition(ty, visibility)?;
            if let Some(method) = definition.methods.iter().find(|method| method.name == name) {
                return Ok(method);
            }
            current = definition.parent;
        }
        Err(format!(
            "type `{}` has no method named `{}`",
            self.type_name(base),
            self.symbol_name(name).unwrap_or("<unknown>")
        ))
    }

    pub fn deref_info(
        &self,
        spec: TypeSpec,
        visibility: &Visibility,
    ) -> TypeResult<Option<DerefInfo>> {
        let data = self.spec_data(spec);
        if data.arguments.len() != 1 {
            return Ok(None);
        }
        let result_type = data.arguments[0];
        let layout = self.layout(self.type_spec_base(result_type), visibility)?;
        let pointer = SymbolId(fnv64(b"pointer"));
        let inline_array = SymbolId(fnv64(b"inline-array"));
        let array = SymbolId(fnv64(b"array"));
        if data.base.0 == pointer {
            let stride = if layout.kind.is_reference() {
                POINTER_SIZE
            } else {
                layout.size
            };
            return Ok(Some(DerefInfo {
                memory_deref: true,
                sign_extend: !layout.kind.is_reference() && layout.load_signed,
                register_class: layout.register_class,
                stride,
                offset: 0,
                load_size: Some(if layout.kind.is_reference() {
                    POINTER_SIZE
                } else {
                    layout.load_size
                }),
                result_type,
            }));
        }
        if data.base.0 == array {
            let data_field = self
                .find_field(Type(array), SymbolId(fnv64(b"data")), visibility)?
                .1;
            let object_offset = self.layout(Type(array), visibility)?.offset;
            return Ok(Some(DerefInfo {
                memory_deref: true,
                sign_extend: layout.kind.is_reference() || layout.load_signed,
                register_class: layout.register_class,
                stride: if layout.kind.is_reference() {
                    POINTER_SIZE
                } else {
                    layout.size
                },
                offset: data_field
                    .offset
                    .checked_sub(object_offset)
                    .ok_or_else(|| "array data has an invalid offset".to_owned())?,
                load_size: Some(if layout.kind.is_reference() {
                    POINTER_SIZE
                } else {
                    layout.load_size
                }),
                result_type,
            }));
        }
        if data.base.0 == inline_array && layout.kind.is_reference() && !layout.dynamic {
            return Ok(Some(DerefInfo {
                memory_deref: false,
                sign_extend: false,
                register_class: RegisterClass::Gpr,
                stride: align_up(layout.size, layout.inline_array_alignment)?,
                offset: 0,
                load_size: None,
                result_type,
            }));
        }
        Ok(None)
    }

    fn layout_definition(
        &self,
        mut definition: TypeDefinition,
        visibility: &Visibility,
    ) -> TypeResult<TypeDefinition> {
        let field_declarations = std::mem::take(&mut definition.field_declarations);
        let bit_field_declarations = std::mem::take(&mut definition.bit_field_declarations);
        let method_declarations = std::mem::take(&mut definition.method_declarations);
        self.validate_layout(&definition.layout, definition.span)?;
        let parent_definition = definition
            .parent
            .map(|parent| self.definition(parent, visibility))
            .transpose()?;
        let mut layout = definition.layout;
        if let Some(parent) = parent_definition {
            if matches!(layout.kind, TypeKind::Structure | TypeKind::Basic)
                && !parent.layout.kind.is_reference()
            {
                return Err(format!(
                    "structure type `{}` cannot inherit from non-structure type `{}`",
                    self.type_name(definition.name),
                    self.type_name(parent.name)
                ));
            }
            if layout.kind == TypeKind::BitField {
                let integer = Type(SymbolId(fnv64(b"integer")));
                if !self.is_base_subtype(integer, parent.name, visibility)? {
                    return Err(format!(
                        "bit-field type `{}` must inherit from an integer type",
                        self.type_name(definition.name)
                    ));
                }
            }
            if layout.kind == TypeKind::Structure || layout.kind == TypeKind::Basic {
                layout.size = layout.size.max(parent.layout.size);
                layout.alignment = layout.alignment.max(parent.layout.alignment);
                layout.inline_array_alignment = layout
                    .inline_array_alignment
                    .max(parent.layout.inline_array_alignment);
            } else if layout.kind == TypeKind::BitField {
                layout.size = parent.layout.size;
                layout.alignment = parent.layout.alignment;
                layout.load_size = parent.layout.load_size;
                layout.load_signed = parent.layout.load_signed;
                layout.register_class = parent.layout.register_class;
            }
        }
        layout.dynamic |= definition.options.dynamic_size;

        if !bit_field_declarations.is_empty() && layout.kind != TypeKind::BitField {
            return Err("bit fields are only valid in a bit-field type".to_owned());
        }
        if !field_declarations.is_empty()
            && !matches!(layout.kind, TypeKind::Structure | TypeKind::Basic)
        {
            return Err("ordinary fields are only valid in structure and basic types".to_owned());
        }

        let fields = self.layout_fields(
            definition.name,
            definition.parent,
            &mut layout,
            field_declarations,
            visibility,
        )?;
        let bit_fields = self.layout_bit_fields(
            definition.name,
            definition.parent,
            layout,
            bit_field_declarations,
            visibility,
        )?;
        let (methods, method_count) = self.layout_methods(
            definition.name,
            definition.parent,
            method_declarations,
            visibility,
        )?;
        if layout.kind == TypeKind::BitField
            && definition.runtime_type == RuntimeType(definition.name.0)
        {
            definition.runtime_type = parent_definition
                .expect("a validated bit-field type has a parent")
                .runtime_type;
        }
        definition.layout = layout;
        definition.fields = fields;
        definition.bit_fields = bit_fields;
        definition.methods = methods;
        definition.method_count = method_count;
        Ok(definition)
    }

    fn layout_fields(
        &self,
        defining_type: Type,
        parent: Option<Type>,
        layout: &mut TypeLayout,
        declarations: Vec<FieldDeclaration>,
        visibility: &Visibility,
    ) -> TypeResult<Vec<FieldDefinition>> {
        let mut result = Vec::with_capacity(declarations.len());
        let mut cursor = layout.size;
        let mut names = HashSet::new();
        for declaration in declarations {
            if !names.insert(declaration.name) {
                return Err(self.duplicate_field(
                    defining_type,
                    declaration.name,
                    declaration.span,
                ));
            }
            let inherited = parent
                .and_then(|parent| self.find_field(parent, declaration.name, visibility).ok());
            if declaration.replace && inherited.is_none() {
                return Err(format!(
                    "field `{}` cannot replace a field because no parent defines it",
                    self.symbol_name(declaration.name).unwrap_or("<unknown>")
                ));
            }
            if !declaration.replace && inherited.is_some() {
                return Err(self.duplicate_field(
                    defining_type,
                    declaration.name,
                    declaration.span,
                ));
            }

            let field_layout =
                self.layout(self.type_spec_base(declaration.field_type), visibility)?;
            layout.dynamic |= declaration.dynamic;
            let natural_alignment = if declaration.inline {
                field_layout.alignment
            } else if field_layout.kind.is_reference() {
                POINTER_SIZE
            } else {
                field_layout.alignment
            };
            let alignment = match declaration.align_bits {
                Some(bits) if bits != 0 && bits.is_multiple_of(8) => bits / 8,
                Some(_) => {
                    return Err("field alignment must be a nonzero multiple of 8 bits".to_owned());
                }
                None => natural_alignment,
            };
            if !alignment.is_power_of_two() {
                return Err("field alignment must be a power of two".to_owned());
            }
            if alignment < natural_alignment {
                return Err(format!(
                    "field `{}` requires at least {natural_alignment}-byte alignment",
                    self.symbol_name(declaration.name).unwrap_or("<unknown>")
                ));
            }
            let size = self.field_size(&declaration, field_layout)?;
            let (offset, overlay) = if let Some((_, inherited)) = inherited {
                if inherited.size != size {
                    return Err(format!(
                        "replacement field `{}` has size {size}, but the parent field has size {}",
                        self.symbol_name(declaration.name).unwrap_or("<unknown>"),
                        inherited.size
                    ));
                }
                (inherited.offset, true)
            } else if let Some(overlay) = &declaration.overlay_at {
                (
                    self.resolve_overlay(parent, &result, overlay, visibility)?,
                    true,
                )
            } else if let Some(offset) = declaration.offset {
                (offset, false)
            } else {
                (align_up(cursor, alignment)?, false)
            };
            if offset % alignment != 0 {
                return Err(format!(
                    "field `{}` at byte {offset} is not aligned to {alignment} bytes",
                    self.symbol_name(declaration.name).unwrap_or("<unknown>")
                ));
            }
            let end = offset
                .checked_add(size)
                .ok_or_else(|| "field layout exceeds u32".to_owned())?;
            layout.size = layout.size.max(end);
            layout.alignment = layout.alignment.max(alignment);
            if !overlay {
                cursor = cursor.max(end);
            }
            result.push(FieldDefinition {
                name: declaration.name,
                field_type: declaration.field_type,
                offset,
                size,
                alignment,
                array_size: declaration.array_size,
                dynamic: declaration.dynamic,
                overlay,
                inline: declaration.inline,
                print_as: declaration.print_as,
                accessor: declaration.accessor,
                replaces_parent: declaration.replace,
                access: declaration.access,
                span: declaration.span,
            });
        }
        Ok(result)
    }

    fn layout_bit_fields(
        &self,
        defining_type: Type,
        parent: Option<Type>,
        layout: TypeLayout,
        declarations: Vec<BitFieldDeclaration>,
        visibility: &Visibility,
    ) -> TypeResult<Vec<BitFieldDefinition>> {
        let mut result = Vec::with_capacity(declarations.len());
        let mut cursor = 0u32;
        let mut names = HashSet::new();
        let capacity = layout
            .size
            .checked_mul(8)
            .ok_or_else(|| "bit-field size exceeds u32".to_owned())?;
        for declaration in declarations {
            if !names.insert(declaration.name) {
                return Err(self.duplicate_field(
                    defining_type,
                    declaration.name,
                    declaration.span,
                ));
            }
            let inherited = parent.and_then(|parent| {
                self.find_bit_field(parent, declaration.name, visibility)
                    .ok()
            });
            let field_layout =
                self.layout(self.type_spec_base(declaration.field_type), visibility)?;
            let size_bits = declaration
                .size_bits
                .unwrap_or(field_layout.size.saturating_mul(8));
            if size_bits == 0 {
                return Err("bit field cannot have size zero".to_owned());
            }
            let (offset_bits, overlay) = if declaration.replace {
                let inherited = inherited.ok_or_else(|| {
                    format!(
                        "bit field `{}` cannot replace a field because no parent defines it",
                        self.symbol_name(declaration.name).unwrap_or("<unknown>")
                    )
                })?;
                if inherited.size_bits != size_bits {
                    return Err(
                        "replacement bit field must have the same size as its parent field"
                            .to_owned(),
                    );
                }
                (inherited.offset_bits, true)
            } else if inherited.is_some() {
                return Err(self.duplicate_field(
                    defining_type,
                    declaration.name,
                    declaration.span,
                ));
            } else if let Some(offset) = declaration.overlay_at_bits {
                (offset, true)
            } else {
                (declaration.offset_bits.unwrap_or(cursor), false)
            };
            let end = offset_bits
                .checked_add(size_bits)
                .ok_or_else(|| "bit-field layout exceeds u32".to_owned())?;
            if end > capacity {
                return Err(format!(
                    "bit field ends at bit {end}, beyond its {capacity}-bit storage"
                ));
            }
            if !overlay {
                cursor = end;
            }
            result.push(BitFieldDefinition {
                name: declaration.name,
                field_type: declaration.field_type,
                offset_bits,
                size_bits,
                overlay,
                print_as: declaration.print_as,
                accessor: declaration.accessor,
                replaces_parent: declaration.replace,
                span: declaration.span,
            });
        }
        Ok(result)
    }

    fn layout_methods(
        &self,
        defining_type: Type,
        parent: Option<Type>,
        declarations: Vec<MethodDeclaration>,
        visibility: &Visibility,
    ) -> TypeResult<(Vec<MethodDefinition>, u16)> {
        let mut next_slot = parent
            .map(|parent| self.definition(parent, visibility).map(|d| d.method_count))
            .transpose()?
            .unwrap_or(0);
        let mut names = HashSet::new();
        let mut slots = HashSet::new();
        let mut result = Vec::with_capacity(declarations.len());
        for declaration in declarations {
            if !names.insert(declaration.name) {
                return Err(format!(
                    "method `{}` is declared more than once",
                    self.symbol_name(declaration.name).unwrap_or("<unknown>")
                ));
            }
            let inherited = parent
                .map(|parent| self.find_method(parent, declaration.name, visibility))
                .transpose()?
                .flatten();
            let (slot, overrides_parent) = if let Some(inherited) = inherited {
                if inherited.signature != declaration.signature {
                    return Err(format!(
                        "method `{}` has signature {}, but the inherited signature is {}",
                        self.symbol_name(declaration.name).unwrap_or("<unknown>"),
                        self.format_type_spec(declaration.signature),
                        self.format_type_spec(inherited.signature)
                    ));
                }
                if declaration.slot.is_some_and(|slot| slot != inherited.slot) {
                    return Err("overriding method must retain its inherited slot".to_owned());
                }
                (inherited.slot, true)
            } else {
                let slot = declaration.slot.unwrap_or(next_slot);
                if slot < next_slot {
                    return Err(format!(
                        "new method `{}` cannot occupy inherited slot {slot}",
                        self.symbol_name(declaration.name).unwrap_or("<unknown>")
                    ));
                }
                next_slot = next_slot.max(
                    slot.checked_add(1)
                        .ok_or_else(|| "exhausted method slots".to_owned())?,
                );
                (slot, false)
            };
            if !slots.insert(slot) {
                return Err(format!("more than one method occupies slot {slot}"));
            }
            result.push(MethodDefinition {
                name: declaration.name,
                signature: declaration.signature,
                slot,
                defined_in: defining_type,
                private: declaration.private,
                no_virtual: declaration.no_virtual,
                overrides_parent,
                span: declaration.span,
            });
        }
        Ok((result, next_slot))
    }

    fn field_size(&self, field: &FieldDeclaration, field_layout: TypeLayout) -> TypeResult<u32> {
        if field.dynamic {
            if field.array_size.is_some() {
                return Err("a dynamic field cannot also have a fixed array size".to_owned());
            }
            return Ok(0);
        }
        let element_size = if field.inline {
            if !field_layout.kind.is_reference() || field_layout.dynamic {
                return Err("inline fields require a fixed-size structure type".to_owned());
            }
            if field.array_size.is_some() {
                align_up(field_layout.size, field_layout.inline_array_alignment)?
            } else {
                field_layout.size
            }
        } else if field_layout.kind.is_reference() {
            POINTER_SIZE
        } else {
            align_up(field_layout.size, field_layout.alignment)?
        };
        match field.array_size {
            Some(count) => element_size
                .checked_mul(count)
                .ok_or_else(|| "field array size exceeds u32".to_owned()),
            None => Ok(element_size),
        }
    }

    fn resolve_overlay(
        &self,
        parent: Option<Type>,
        current_fields: &[FieldDefinition],
        overlay: &OverlaySpec,
        visibility: &Visibility,
    ) -> TypeResult<u32> {
        if let OverlaySpec::ByteOffset(offset) = overlay {
            return Ok(*offset);
        }
        let (root_name, selectors) = match overlay {
            OverlaySpec::Field(name) => (*name, &[][..]),
            OverlaySpec::Path { field, selectors } => (*field, selectors.as_slice()),
            OverlaySpec::ByteOffset(_) => unreachable!(),
        };
        let mut field =
            if let Some(field) = current_fields.iter().find(|field| field.name == root_name) {
                field.clone()
            } else if let Some(parent) = parent {
                self.find_field(parent, root_name, visibility)
                    .map(|(_, field)| field.clone())
                    .map_err(|_| {
                        format!(
                            "overlay refers to unknown field `{}`",
                            self.symbol_name(root_name).unwrap_or("<unknown>")
                        )
                    })?
            } else {
                return Err(format!(
                    "overlay refers to unknown field `{}`",
                    self.symbol_name(root_name).unwrap_or("<unknown>")
                ));
            };
        let mut offset = field.offset;
        for selector in selectors {
            match selector {
                OverlaySelector::Index(index) => {
                    let count = field
                        .array_size
                        .ok_or_else(|| "overlay index can only select an array field".to_owned())?;
                    if *index >= count {
                        return Err(format!(
                            "overlay array index {index} is outside array of length {count}"
                        ));
                    }
                    let stride = field.size / count;
                    offset = offset
                        .checked_add(
                            stride
                                .checked_mul(*index)
                                .ok_or_else(|| "overlay offset exceeds u32".to_owned())?,
                        )
                        .ok_or_else(|| "overlay offset exceeds u32".to_owned())?;
                    field.array_size = None;
                    field.size = stride;
                }
                OverlaySelector::Field(name) => {
                    if !field.inline {
                        return Err(
                            "overlay sub-field traversal requires an inline structure field"
                                .to_owned(),
                        );
                    }
                    let nested_type = self.type_spec_base(field.field_type);
                    let (_, nested) = self.find_field(nested_type, *name, visibility)?;
                    offset = offset
                        .checked_add(nested.offset)
                        .ok_or_else(|| "overlay offset exceeds u32".to_owned())?;
                    field = nested.clone();
                }
            }
        }
        Ok(offset)
    }

    fn typecheck_inner(
        &self,
        expected: TypeSpec,
        actual: TypeSpec,
        visibility: &Visibility,
        path: &mut Vec<usize>,
    ) -> TypeResult<()> {
        let expected_data = self.spec_data(expected);
        let actual_data = self.spec_data(actual);
        if !self.is_base_subtype(expected_data.base, actual_data.base, visibility)? {
            return Err(self.type_mismatch(expected, actual, path, "base types are unrelated"));
        }
        if !expected_data.arguments.is_empty() {
            if expected_data.arguments.len() != actual_data.arguments.len() {
                return Err(self.type_mismatch(
                    expected,
                    actual,
                    path,
                    "compound types have different argument counts",
                ));
            }
            for (index, (expected_argument, actual_argument)) in expected_data
                .arguments
                .iter()
                .zip(actual_data.arguments.iter())
                .enumerate()
            {
                path.push(index);
                let result =
                    self.typecheck_inner(*expected_argument, *actual_argument, visibility, path);
                path.pop();
                result?;
            }
        }
        if let Some(expected_behavior) = expected_data.behavior {
            let actual_behavior = actual_data.behavior.ok_or_else(|| {
                self.type_mismatch(
                    expected,
                    actual,
                    path,
                    "actual type has no behavior qualifier",
                )
            })?;
            path.push(usize::MAX);
            let result = self.typecheck_inner(expected_behavior, actual_behavior, visibility, path);
            path.pop();
            result?;
        }
        Ok(())
    }

    fn is_base_subtype(
        &self,
        expected: Type,
        actual: Type,
        visibility: &Visibility,
    ) -> TypeResult<bool> {
        let expected = self.normalize_unit_type(expected);
        let actual = self.normalize_unit_type(actual);
        self.ensure_known(expected, visibility)?;
        self.ensure_known(actual, visibility)?;
        let mut current = Some(actual);
        let mut visited = HashSet::new();
        while let Some(ty) = current {
            if ty == expected {
                return Ok(true);
            }
            if !visited.insert(ty) {
                return Err(format!(
                    "inheritance cycle involving `{}`",
                    self.type_name(ty)
                ));
            }
            current = self.parent_of(ty, visibility)?;
        }
        Ok(false)
    }

    fn normalize_unit_type(&self, ty: Type) -> Type {
        match self.type_name(ty) {
            "meters" | "degrees" => Type(SymbolId(fnv64(b"float"))),
            "seconds" => Type(SymbolId(fnv64(b"int64"))),
            _ => ty,
        }
    }

    fn lca_base(&self, left: Type, right: Type, visibility: &Visibility) -> TypeResult<Type> {
        let left_path = self.path_to_root(left, visibility)?;
        let right_path = self.path_to_root(right, visibility)?;
        let right_set: HashSet<Type> = right_path.into_iter().collect();
        left_path
            .into_iter()
            .find(|ty| right_set.contains(ty))
            .ok_or_else(|| {
                format!(
                    "types `{}` and `{}` have no common ancestor",
                    self.type_name(left),
                    self.type_name(right)
                )
            })
    }

    fn path_to_root(&self, ty: Type, visibility: &Visibility) -> TypeResult<Vec<Type>> {
        self.ensure_known(ty, visibility)?;
        let mut result = Vec::new();
        let mut current = Some(ty);
        let mut visited = HashSet::new();
        while let Some(ty) = current {
            if !visited.insert(ty) {
                return Err(format!(
                    "inheritance cycle involving `{}`",
                    self.type_name(ty)
                ));
            }
            result.push(ty);
            current = self.parent_of(ty, visibility)?;
        }
        Ok(result)
    }

    fn parent_of(&self, ty: Type, visibility: &Visibility) -> TypeResult<Option<Type>> {
        if let Ok(definition) = self.definition(ty, visibility) {
            return Ok(definition.parent);
        }
        if let Ok(definition) = self.enum_definition(ty, visibility) {
            return Ok(Some(definition.parent));
        }
        if let Ok(declaration) = self.declaration(ty, visibility) {
            return Ok(declaration.parent);
        }
        Err(self.unknown_or_invisible(ty, visibility, "type"))
    }

    fn find_field(
        &self,
        base: Type,
        name: SymbolId,
        visibility: &Visibility,
    ) -> TypeResult<(Type, &FieldDefinition)> {
        let mut current = Some(base);
        while let Some(ty) = current {
            let definition = self.definition(ty, visibility)?;
            if let Some(field) = definition.fields.iter().find(|field| field.name == name) {
                return Ok((ty, field));
            }
            current = definition.parent;
        }
        Err(format!(
            "type `{}` has no field named `{}`",
            self.type_name(base),
            self.symbol_name(name).unwrap_or("<unknown>")
        ))
    }

    fn find_bit_field(
        &self,
        base: Type,
        name: SymbolId,
        visibility: &Visibility,
    ) -> TypeResult<&BitFieldDefinition> {
        let mut current = Some(base);
        while let Some(ty) = current {
            let definition = self.definition(ty, visibility)?;
            if let Some(field) = definition
                .bit_fields
                .iter()
                .find(|field| field.name == name)
            {
                return Ok(field);
            }
            current = definition.parent;
        }
        Err("unknown bit field".to_owned())
    }

    fn find_method(
        &self,
        base: Type,
        name: SymbolId,
        visibility: &Visibility,
    ) -> TypeResult<Option<&MethodDefinition>> {
        let mut current = Some(base);
        while let Some(ty) = current {
            let definition = self.definition(ty, visibility)?;
            if let Some(method) = definition.methods.iter().find(|method| method.name == name) {
                return Ok(Some(method));
            }
            current = definition.parent;
        }
        Ok(None)
    }

    fn validate_parent(
        &self,
        child: Type,
        parent: Option<Type>,
        visibility: &Visibility,
        _span: Option<SpanInfo>,
    ) -> TypeResult<()> {
        let Some(parent) = parent else {
            return Ok(());
        };
        self.ensure_known(parent, visibility)?;
        let mut current = Some(parent);
        let mut visited = HashSet::new();
        while let Some(ty) = current {
            if ty == child {
                return Err(format!(
                    "defining `{}` with parent `{}` would create an inheritance cycle",
                    self.type_name(child),
                    self.type_name(parent)
                ));
            }
            if !visited.insert(ty) {
                return Err("the existing parent chain contains a cycle".to_owned());
            }
            current = self.parent_of(ty, visibility)?;
        }
        Ok(())
    }

    fn validate_layout(&self, layout: &TypeLayout, _span: Option<SpanInfo>) -> TypeResult<()> {
        if layout.alignment == 0 || !layout.alignment.is_power_of_two() {
            return Err("type alignment must be a nonzero power of two".to_owned());
        }
        if layout.inline_array_alignment == 0 || !layout.inline_array_alignment.is_power_of_two() {
            return Err("inline-array alignment must be a nonzero power of two".to_owned());
        }
        Ok(())
    }

    fn ensure_known(&self, ty: Type, visibility: &Visibility) -> TypeResult<()> {
        if self.is_declared(ty, visibility) {
            Ok(())
        } else {
            Err(self.unknown_or_invisible(ty, visibility, "type"))
        }
    }

    fn visible_record<'a, T>(
        &self,
        records: &'a IdentityMap<Type, Vec<Owned<T>>>,
        ty: Type,
        visibility: &Visibility,
        description: &str,
    ) -> TypeResult<&'a T> {
        let Some(records) = records.get(&ty) else {
            return Err(format!(
                "no {description} exists for type `{}`",
                self.type_name(ty)
            ));
        };
        let mut visible = records
            .iter()
            .filter(|record| visibility.can_see(record.owner));
        let first = visible.next().ok_or_else(|| {
            let mut error = format!(
                "{description} for type `{}` is not visible in this file",
                self.type_name(ty)
            );
            if !visibility.unrestricted {
                error.push_str(&format!(
                    "; file {} can only use definitions from itself and its declared dependencies",
                    visibility.file
                ));
            }
            error
        })?;
        if visible.next().is_some() {
            return Err(format!(
                "more than one visible {description} exists for `{}`",
                self.type_name(ty)
            ));
        }
        Ok(&first.value)
    }

    fn visible_count<T>(
        &self,
        records: &IdentityMap<Type, Vec<Owned<T>>>,
        ty: Type,
        visibility: &Visibility,
    ) -> usize {
        records.get(&ty).map_or(0, |records| {
            records
                .iter()
                .filter(|record| visibility.can_see(record.owner))
                .count()
        })
    }

    fn reject_visible_duplicate(
        &self,
        ty: Type,
        owner: FileId,
        visibility: &Visibility,
        is_definition: bool,
    ) -> TypeResult<()> {
        let same_kind = if is_definition {
            self.visible_count(&self.definitions, ty, visibility)
                + self.visible_count(&self.enums, ty, visibility)
        } else {
            self.visible_count(&self.declarations, ty, visibility)
        };
        let owned_same_kind = if is_definition {
            owner_count(&self.definitions, ty, owner) + owner_count(&self.enums, ty, owner)
        } else {
            owner_count(&self.declarations, ty, owner)
        };
        if same_kind != 0 || owned_same_kind != 0 {
            return Err(format!("type `{}` is already defined", self.type_name(ty)));
        }
        Ok(())
    }

    fn unknown_or_invisible(&self, ty: Type, visibility: &Visibility, description: &str) -> String {
        let exists = self.declarations.contains_key(&ty)
            || self.definitions.contains_key(&ty)
            || self.enums.contains_key(&ty);
        let qualifier = if exists { "not visible" } else { "unknown" };
        let mut error = format!(
            "{description} `{}` is {qualifier} in this file",
            self.type_name(ty)
        );
        if !visibility.unrestricted {
            error.push_str(&format!(
                "; file {} can only use definitions from itself and its declared dependencies",
                visibility.file
            ));
        }
        error
    }

    fn duplicate_field(&self, ty: Type, field: SymbolId, _span: Option<SpanInfo>) -> String {
        format!(
            "type `{}` already has a field named `{}`",
            self.type_name(ty),
            self.symbol_name(field).unwrap_or("<unknown>")
        )
    }

    fn type_mismatch(
        &self,
        expected: TypeSpec,
        actual: TypeSpec,
        path: &[usize],
        reason: &str,
    ) -> String {
        let location = if path.is_empty() {
            String::new()
        } else if path.last() == Some(&usize::MAX) {
            " in the behavior qualifier".to_owned()
        } else {
            format!(
                " in type argument {}",
                path.iter()
                    .filter(|index| **index != usize::MAX)
                    .map(|index| (index + 1).to_string())
                    .collect::<Vec<_>>()
                    .join(".")
            )
        };
        format!(
            "type mismatch{location}: expected `{}`, got `{}` ({reason})",
            self.format_type_spec(expected),
            self.format_type_spec(actual)
        )
    }

    fn spec_data(&self, spec: TypeSpec) -> &TypeSpecData {
        &self.specs[spec.0 as usize]
    }

    fn install_builtin_types(&mut self) {
        let object = self.intern_type("object");
        let none = self.intern_type("none");
        let structure = self.intern_type("structure");
        let basic = self.intern_type("basic");
        let integer = self.intern_type("integer");
        let number = self.intern_type("number");
        let binteger = self.intern_type("binteger");
        let sinteger = self.intern_type("sinteger");
        let uinteger = self.intern_type("uinteger");
        let int = self.intern_type("int");
        let uint = self.intern_type("uint");
        let float = self.intern_type("float");
        let pointer = self.intern_type("pointer");
        let inline_array = self.intern_type("inline-array");
        let function = self.intern_type("function");
        let symbol = self.intern_type("symbol");
        let string = self.intern_type("string");
        let type_type = self.intern_type("type");
        let pair = self.intern_type("pair");
        let array = self.intern_type("array");
        let varargs = self.intern_type("_varargs_");
        let method_type = self.intern_type("_type_");

        self.insert_builtin(none, None, TypeLayout::none(), None);
        self.insert_builtin(
            object,
            None,
            TypeLayout::value(POINTER_SIZE, POINTER_SIZE, true, RegisterClass::Gpr),
            None,
        );
        self.install_object_methods(object);
        self.insert_builtin(
            structure,
            Some(object),
            TypeLayout::structure(TypeKind::Structure, false),
            None,
        );
        self.insert_builtin(
            basic,
            Some(structure),
            TypeLayout::structure(TypeKind::Basic, true),
            None,
        );
        self.install_basic_type_field(basic, type_type);
        self.insert_builtin(
            number,
            Some(object),
            TypeLayout::value(8, 8, false, RegisterClass::Gpr),
            None,
        );
        for (name, parent, size, signed) in [
            (integer, number, 8, false),
            (binteger, integer, 8, false),
            (sinteger, integer, 8, true),
            (uinteger, integer, 8, false),
            (int, integer, 8, true),
            (uint, uinteger, 8, false),
        ] {
            self.insert_builtin(
                name,
                Some(parent),
                TypeLayout::value(size, size, signed, RegisterClass::Gpr),
                None,
            );
        }
        for (name, parent, size, signed) in [
            ("int8", sinteger, 1, true),
            ("int16", sinteger, 2, true),
            ("int32", sinteger, 4, true),
            ("int64", sinteger, 8, true),
            ("uint8", uinteger, 1, false),
            ("uint16", uinteger, 2, false),
            ("uint32", uinteger, 4, false),
            ("uint64", uinteger, 8, false),
        ] {
            let ty = self.intern_type(name);
            self.insert_builtin(
                ty,
                Some(parent),
                TypeLayout::value(size, size, signed, RegisterClass::Gpr),
                None,
            );
        }
        for (name, parent, signed) in [("int128", sinteger, true), ("uint128", uinteger, false)] {
            let ty = self.intern_type(name);
            self.insert_builtin(
                ty,
                Some(parent),
                TypeLayout::value(16, 16, signed, RegisterClass::Gpr128),
                None,
            );
        }
        self.insert_builtin(
            float,
            Some(number),
            TypeLayout::value(4, 4, false, RegisterClass::Fpr),
            None,
        );
        for (name, parent, runtime) in [
            ("meters", float, float),
            ("degrees", float, float),
            (
                "seconds",
                self.intern_type("int64"),
                self.intern_type("int64"),
            ),
        ] {
            let ty = self.intern_type(name);
            let parent_layout = self
                .definitions
                .get(&parent)
                .and_then(|records| records.first())
                .map(|record| record.value.layout)
                .expect("builtin parent exists");
            self.insert_builtin(
                ty,
                Some(parent),
                parent_layout,
                Some(RuntimeType(runtime.0)),
            );
        }
        for ty in [pointer, inline_array] {
            self.insert_builtin(
                ty,
                Some(object),
                TypeLayout::value(POINTER_SIZE, POINTER_SIZE, false, RegisterClass::Gpr),
                Some(RuntimeType(pointer.0)),
            );
        }
        for ty in [function, symbol, string, type_type, array] {
            self.insert_builtin(
                ty,
                Some(basic),
                TypeLayout::structure(TypeKind::Basic, true),
                None,
            );
        }
        self.install_array_type(array, symbol, type_type);
        self.install_type_fields(type_type, symbol, function);
        self.insert_builtin(
            pair,
            Some(object),
            TypeLayout::structure(TypeKind::Structure, true),
            None,
        );
        self.install_pair_fields(pair, object);
        self.install_pair_new_method(pair, symbol, type_type, object);
        for ty in [varargs, method_type] {
            self.insert_builtin(
                ty,
                Some(object),
                TypeLayout::value(POINTER_SIZE, POINTER_SIZE, false, RegisterClass::Gpr),
                None,
            );
        }

        let builtin_names: Vec<Type> = self.definitions.keys().copied().collect();
        for ty in builtin_names {
            self.intern_compound_type_spec(ty, [], None);
        }
    }

    fn insert_builtin(
        &mut self,
        name: Type,
        parent: Option<Type>,
        mut layout: TypeLayout,
        runtime_type: Option<RuntimeType>,
    ) {
        let parent_definition = parent
            .and_then(|parent| self.definitions.get(&parent))
            .and_then(|records| records.first())
            .map(|record| &record.value);
        let method_count = parent_definition.map_or(0, |definition| definition.method_count);
        if layout.kind.is_reference()
            && let Some(parent) = parent_definition
            && parent.layout.kind.is_reference()
        {
            layout.size = layout.size.max(parent.layout.size);
            layout.alignment = layout.alignment.max(parent.layout.alignment);
            layout.inline_array_alignment = layout
                .inline_array_alignment
                .max(parent.layout.inline_array_alignment);
        }
        self.definitions.entry(name).or_default().push(Owned {
            owner: BUILTIN_FILE_ID,
            value: TypeDefinition {
                name,
                parent,
                runtime_type: runtime_type.unwrap_or(RuntimeType(name.0)),
                layout,
                fields: Vec::new(),
                bit_fields: Vec::new(),
                methods: Vec::new(),
                method_count,
                options: TypeOptions::default(),
                span: None,
                field_declarations: Vec::new(),
                bit_field_declarations: Vec::new(),
                method_declarations: Vec::new(),
            },
        });
    }

    fn install_basic_type_field(&mut self, basic: Type, type_type: Type) {
        let field_type = self.intern_compound_type_spec(type_type, [], None);
        let name = self.intern_symbol("type");
        let definition = &mut self
            .definitions
            .get_mut(&basic)
            .expect("basic builtin exists")[0]
            .value;
        definition.layout.size = POINTER_SIZE;
        definition.fields.push(FieldDefinition {
            name,
            field_type,
            offset: 0,
            size: POINTER_SIZE,
            alignment: POINTER_SIZE,
            array_size: None,
            dynamic: false,
            overlay: false,
            inline: false,
            print_as: PrintAs::Default,
            accessor: true,
            replaces_parent: false,
            access: FieldAccess::Public,
            span: None,
        });
    }

    fn install_pair_fields(&mut self, pair: Type, object: Type) {
        let field_type = self.intern_compound_type_spec(object, [], None);
        let car = self.intern_symbol("car");
        let cdr = self.intern_symbol("cdr");
        let fields = [car, cdr]
            .into_iter()
            .enumerate()
            .map(|(index, name)| FieldDefinition {
                name,
                field_type,
                offset: index as u32 * POINTER_SIZE,
                size: POINTER_SIZE,
                alignment: POINTER_SIZE,
                array_size: None,
                dynamic: false,
                overlay: false,
                inline: false,
                print_as: PrintAs::Default,
                accessor: true,
                replaces_parent: false,
                access: FieldAccess::Public,
                span: None,
            })
            .collect();
        let definition = &mut self
            .definitions
            .get_mut(&pair)
            .expect("pair builtin exists")[0]
            .value;
        definition.layout.size = POINTER_SIZE * 2;
        definition.fields = fields;
    }

    fn install_pair_new_method(&mut self, pair: Type, symbol: Type, type_type: Type, object: Type) {
        let symbol = self.intern_compound_type_spec(symbol, [], None);
        let type_type = self.intern_compound_type_spec(type_type, [], None);
        let object = self.intern_compound_type_spec(object, [], None);
        let pair_spec = self.intern_compound_type_spec(pair, [], None);
        let signature = self.function_type([symbol, type_type, object, object], pair_spec);
        let new = self.intern_symbol("new");
        self.definitions
            .get_mut(&pair)
            .expect("pair builtin exists")[0]
            .value
            .methods
            .push(MethodDefinition {
                name: new,
                signature,
                slot: 0,
                defined_in: pair,
                private: false,
                no_virtual: false,
                overrides_parent: true,
                span: None,
            });
    }

    fn install_array_type(&mut self, array: Type, symbol: Type, type_type: Type) {
        let int32 = self.intern_type_spec("int32");
        let type_spec = self.intern_compound_type_spec(type_type, [], None);
        let uint8 = self.intern_type_spec("uint8");
        let fields = [
            ("length", int32, 4, 4, 4, false),
            ("allocated-length", int32, 8, 4, 4, false),
            ("content-type", type_spec, 12, 4, 4, false),
            ("data", uint8, 16, 0, 1, true),
        ]
        .into_iter()
        .map(
            |(name, field_type, offset, size, alignment, dynamic)| FieldDefinition {
                name: self.intern_symbol(name),
                field_type,
                offset,
                size,
                alignment,
                array_size: None,
                dynamic,
                overlay: false,
                inline: false,
                print_as: PrintAs::Default,
                accessor: true,
                replaces_parent: false,
                access: FieldAccess::Public,
                span: None,
            },
        )
        .collect();
        let symbol = self.intern_compound_type_spec(symbol, [], None);
        let int = self.intern_type_spec("int");
        let array_spec = self.intern_compound_type_spec(array, [], None);
        let signature = self.function_type([symbol, type_spec, type_spec, int], array_spec);
        let new = self.intern_symbol("new");
        let definition = &mut self
            .definitions
            .get_mut(&array)
            .expect("array builtin exists")[0]
            .value;
        definition.layout.size = 16;
        definition.layout.dynamic = true;
        definition.fields = fields;
        definition.methods.push(MethodDefinition {
            name: new,
            signature,
            slot: 0,
            defined_in: array,
            private: false,
            no_virtual: false,
            overrides_parent: true,
            span: None,
        });
    }

    fn install_type_fields(&mut self, type_type: Type, symbol: Type, function: Type) {
        let symbol_type = self.intern_compound_type_spec(symbol, [], None);
        let type_spec = self.intern_compound_type_spec(type_type, [], None);
        let uint16 = self.intern_type_spec("uint16");
        let function_type = self.intern_compound_type_spec(function, [], None);
        let names_and_types = [
            ("symbol", symbol_type, POINTER_SIZE),
            ("parent", type_spec, POINTER_SIZE + 4),
            ("size", uint16, POINTER_SIZE + 8),
            ("psize", uint16, POINTER_SIZE + 10),
            ("heap-base", uint16, POINTER_SIZE + 12),
            ("allocated-length", uint16, POINTER_SIZE + 14),
        ];
        let mut fields = names_and_types
            .into_iter()
            .map(|(name, field_type, offset)| {
                let layout = self
                    .layout(self.type_spec_base(field_type), &Visibility::unrestricted())
                    .expect("builtin field type has a layout");
                let (size, alignment) = if layout.kind.is_reference() {
                    (POINTER_SIZE, POINTER_SIZE)
                } else {
                    (layout.load_size, layout.alignment)
                };
                FieldDefinition {
                    name: self.intern_symbol(name),
                    field_type,
                    offset,
                    size,
                    alignment,
                    array_size: None,
                    dynamic: false,
                    overlay: false,
                    inline: false,
                    print_as: PrintAs::Default,
                    accessor: true,
                    replaces_parent: false,
                    access: FieldAccess::Public,
                    span: None,
                }
            })
            .collect::<Vec<_>>();
        fields.push(FieldDefinition {
            name: self.intern_symbol("method-table"),
            field_type: function_type,
            offset: POINTER_SIZE + RUNTIME_TYPE_METHODS_OFFSET,
            size: 0,
            alignment: POINTER_SIZE,
            array_size: None,
            dynamic: true,
            overlay: false,
            inline: false,
            print_as: PrintAs::Default,
            accessor: true,
            replaces_parent: false,
            access: FieldAccess::Public,
            span: None,
        });
        let definition = &mut self
            .definitions
            .get_mut(&type_type)
            .expect("type builtin exists")[0]
            .value;
        definition.layout.size = POINTER_SIZE + RUNTIME_TYPE_METHODS_OFFSET;
        definition.layout.dynamic = true;
        definition.fields = fields;
    }

    fn install_object_methods(&mut self, object: Type) {
        let object_spec = self.intern_type_spec("object");
        let symbol = self.intern_type_spec("symbol");
        let type_type = self.intern_type_spec("type");
        let int = self.intern_type_spec("int");
        let method_type = self.intern_type_spec("_type_");
        let none = self.intern_type_spec("none");
        let signatures: [(&str, Vec<TypeSpec>, TypeSpec); OBJECT_METHOD_COUNT as usize] = [
            ("new", vec![symbol, type_type, int], method_type),
            ("delete", vec![method_type], none),
            ("print", vec![method_type], method_type),
            ("inspect", vec![method_type], method_type),
            ("length", vec![method_type], int),
            ("asize-of", vec![method_type], int),
            ("copy", vec![method_type, symbol], method_type),
            ("relocate", vec![method_type, int], method_type),
            (
                "mem-usage",
                vec![method_type, object_spec, int],
                method_type,
            ),
        ];
        let methods: Vec<MethodDefinition> = signatures
            .into_iter()
            .enumerate()
            .map(|(slot, (name, parameters, result))| MethodDefinition {
                name: self.intern_symbol(name),
                signature: self.function_type(parameters, result),
                slot: slot as u16,
                defined_in: object,
                private: false,
                no_virtual: false,
                overrides_parent: false,
                span: None,
            })
            .collect();
        let definition = &mut self
            .definitions
            .get_mut(&object)
            .expect("object builtin exists")[0]
            .value;
        definition.method_count = methods.len() as u16;
        definition.methods = methods;
    }
}

fn align_up(value: u32, alignment: u32) -> TypeResult<u32> {
    debug_assert!(alignment.is_power_of_two());
    value
        .checked_add(alignment - 1)
        .map(|value| value & !(alignment - 1))
        .ok_or_else(|| "alignment exceeds u32".to_owned())
}

fn remove_owned<T>(records: &mut IdentityMap<Type, Vec<Owned<T>>>, owner: FileId) {
    records.retain(|_, records| {
        records.retain(|record| record.owner != owner);
        !records.is_empty()
    });
}

fn owner_count<T>(records: &IdentityMap<Type, Vec<Owned<T>>>, ty: Type, owner: FileId) -> usize {
    records.get(&ty).map_or(0, |records| {
        records
            .iter()
            .filter(|record| record.owner == owner)
            .count()
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn structure_layout() -> TypeLayout {
        TypeLayout::structure(TypeKind::Structure, false)
    }

    fn define_empty_structure(
        types: &mut TypeSystem,
        owner: FileId,
        name: &str,
        parent: Type,
        visibility: &Visibility,
    ) -> Type {
        let ty = types.intern_type(name);
        let definition = TypeDefinition::new(ty, Some(parent), structure_layout());
        types.define_type(owner, definition, visibility).unwrap();
        ty
    }

    #[test]
    fn visibility_uses_file_id_bits() {
        let visibility = Visibility::new(7, [0, 63, 64, 1_000]);

        for file in [0, 7, 63, 64, 1_000, BUILTIN_FILE_ID] {
            assert!(visibility.can_see(file));
        }
        for file in [1, 62, 65, 999, 1_001] {
            assert!(!visibility.can_see(file));
        }
    }

    #[test]
    fn persistent_ids_survive_definition_removal() {
        let mut types = TypeSystem::new();
        let visibility = Visibility::new(1, []);
        let structure = types.intern_type("structure");
        let widget = types.intern_type("widget");
        let widget_spec = types.intern_type_spec("widget");
        types
            .declare_type(
                1,
                TypeDeclaration {
                    name: widget,
                    parent: Some(structure),
                    runtime_type: None,
                    method_count: None,
                    span: None,
                },
                &visibility,
            )
            .unwrap();
        types
            .define_type(
                1,
                TypeDefinition::new(widget, Some(structure), structure_layout()),
                &visibility,
            )
            .unwrap();
        assert!(types.definition(widget, &visibility).is_ok());

        types.remove_definitions(1);
        assert_eq!(types.intern_type("widget"), widget);
        assert_eq!(types.intern_type_spec("widget"), widget_spec);
        assert!(types.definition(widget, &visibility).is_err());

        types
            .define_type(
                1,
                TypeDefinition::new(widget, Some(structure), structure_layout()),
                &visibility,
            )
            .unwrap();
        assert_eq!(types.definition(widget, &visibility).unwrap().name, widget);
        assert_eq!(std::mem::size_of::<RuntimeType>(), 8);
        assert_eq!(std::mem::size_of::<Type>(), 8);
        assert_eq!(std::mem::size_of::<TypeSpec>(), 4);
    }

    #[test]
    fn definitions_obey_file_visibility() {
        let mut types = TypeSystem::new();
        let structure = types.intern_type("structure");
        let owner_visibility = Visibility::new(10, []);
        let hidden = define_empty_structure(&mut types, 10, "hidden", structure, &owner_visibility);

        let unrelated = Visibility::new(20, []);
        assert!(types.definition(hidden, &unrelated).is_err());
        let dependent = Visibility::new(20, [10]);
        assert_eq!(types.definition(hidden, &dependent).unwrap().name, hidden);
    }

    #[test]
    fn lays_out_arrays_overlays_and_inline_structures() {
        let mut types = TypeSystem::new();
        let visibility = Visibility::new(1, []);
        let structure = types.intern_type("structure");
        let float = types.intern_type_spec("float");
        let uint128 = types.intern_type_spec("uint128");
        let vector = types.intern_type("vector");
        let data = types.intern_symbol("data");
        let x = types.intern_symbol("x");
        let y = types.intern_symbol("y");
        let quad = types.intern_symbol("quad");

        let mut data_field = FieldDeclaration::new(data, float);
        data_field.array_size = Some(4);
        data_field.align_bits = Some(128);
        let mut x_field = FieldDeclaration::new(x, float);
        x_field.overlay_at = Some(OverlaySpec::Path {
            field: data,
            selectors: vec![OverlaySelector::Index(0)],
        });
        let mut y_field = FieldDeclaration::new(y, float);
        y_field.overlay_at = Some(OverlaySpec::Path {
            field: data,
            selectors: vec![OverlaySelector::Index(1)],
        });
        let mut quad_field = FieldDeclaration::new(quad, uint128);
        quad_field.overlay_at = Some(OverlaySpec::Field(data));
        let mut vector_definition =
            TypeDefinition::new(vector, Some(structure), structure_layout());
        for field in [data_field, x_field, y_field, quad_field] {
            vector_definition.add_field(field);
        }
        let definition = types
            .define_type(1, vector_definition, &visibility)
            .unwrap();
        assert_eq!(definition.layout.size, 16);
        assert_eq!(definition.layout.alignment, 16);
        assert_eq!(definition.fields[0].offset, 0);
        assert_eq!(definition.fields[2].offset, 4);
        assert!(definition.fields[3].overlay);

        let matrix = types.intern_type("matrix");
        let rows = types.intern_symbol("rows");
        let vector_spec = types.intern_type_spec("vector");
        let mut rows_field = FieldDeclaration::new(rows, vector_spec);
        rows_field.inline = true;
        rows_field.array_size = Some(4);
        let mut matrix_definition =
            TypeDefinition::new(matrix, Some(structure), structure_layout());
        matrix_definition.add_field(rows_field);
        let matrix_definition = types
            .define_type(1, matrix_definition, &visibility)
            .unwrap();
        assert_eq!(matrix_definition.layout.size, 64);
        assert_eq!(matrix_definition.fields[0].alignment, 16);
    }

    #[test]
    fn lays_out_bit_fields_and_rejects_overflow() {
        let mut types = TypeSystem::new();
        let visibility = Visibility::new(1, []);
        let uint32 = types.intern_type("uint32");
        let uint8_spec = types.intern_type_spec("uint8");
        let rgba = types.intern_type("rgba");
        let r = types.intern_symbol("r");
        let g = types.intern_symbol("g");
        let b = types.intern_symbol("b");
        let mut definition =
            TypeDefinition::new(rgba, Some(uint32), TypeLayout::bit_field(4, 4, false));
        for name in [r, g, b] {
            let mut field = BitFieldDeclaration::new(name, uint8_spec);
            field.size_bits = Some(8);
            definition.add_bit_field(field);
        }
        let definition = types.define_type(1, definition, &visibility).unwrap();
        assert_eq!(definition.bit_fields[0].offset_bits, 0);
        assert_eq!(definition.bit_fields[2].offset_bits, 16);

        let bad = types.intern_type("bad-bits");
        let too_large = types.intern_symbol("too-large");
        let mut field = BitFieldDeclaration::new(too_large, uint8_spec);
        field.offset_bits = Some(31);
        field.size_bits = Some(2);
        let mut definition =
            TypeDefinition::new(bad, Some(uint32), TypeLayout::bit_field(4, 4, false));
        definition.add_bit_field(field);
        assert!(types.define_type(1, definition, &visibility).is_err());
    }

    #[test]
    fn typechecks_compound_and_behavior_types_and_computes_lca() {
        let mut types = TypeSystem::new();
        let visibility = Visibility::new(1, []);
        let structure = types.intern_type("structure");
        let animal = define_empty_structure(&mut types, 1, "animal", structure, &visibility);
        let _dog = define_empty_structure(&mut types, 1, "dog", animal, &visibility);
        let _cat = define_empty_structure(&mut types, 1, "cat", animal, &visibility);
        let animal_spec = types.intern_type_spec("animal");
        let dog_spec = types.intern_type_spec("dog");
        let cat_spec = types.intern_type_spec("cat");
        assert!(types.typecheck(animal_spec, dog_spec, &visibility).is_ok());
        assert!(types.typecheck(dog_spec, animal_spec, &visibility).is_err());

        let pointer_animal = types.pointer_type(animal_spec);
        let pointer_dog = types.pointer_type(dog_spec);
        assert!(
            types
                .typecheck(pointer_animal, pointer_dog, &visibility)
                .is_ok()
        );

        let object_spec = types.intern_type_spec("object");
        let animal_behavior = types.with_behavior(object_spec, animal_spec);
        let dog_behavior = types.with_behavior(object_spec, dog_spec);
        assert!(
            types
                .typecheck(animal_behavior, dog_behavior, &visibility)
                .is_ok()
        );

        let common = types
            .lowest_common_ancestor(dog_spec, cat_spec, &visibility)
            .unwrap();
        assert_eq!(types.type_spec_base(common), animal);
    }

    #[test]
    fn assigns_and_inherits_method_slots() {
        let mut types = TypeSystem::new();
        let visibility = Visibility::new(1, []);
        let structure = types.intern_type("structure");
        let base = types.intern_type("drawable");
        let draw = types.intern_symbol("draw");
        let object_spec = types.intern_type_spec("object");
        let none_spec = types.intern_type_spec("none");
        let signature = types.function_type([object_spec], none_spec);
        let mut definition = TypeDefinition::new(base, Some(structure), structure_layout());
        definition.add_method(MethodDeclaration::new(draw, signature));
        types.define_type(1, definition, &visibility).unwrap();

        let child = types.intern_type("sprite");
        let update = types.intern_symbol("update");
        let mut definition = TypeDefinition::new(child, Some(base), structure_layout());
        definition.add_method(MethodDeclaration::new(draw, signature));
        definition.add_method(MethodDeclaration::new(update, signature));
        let definition = types.define_type(1, definition, &visibility).unwrap();
        assert_eq!(definition.methods[0].slot, 9);
        assert!(definition.methods[0].overrides_parent);
        assert_eq!(definition.methods[1].slot, 10);
        assert_eq!(definition.method_count, 11);
    }

    #[test]
    fn replacement_fields_keep_parent_offsets_and_sizes() {
        let mut types = TypeSystem::new();
        let visibility = Visibility::new(1, []);
        let structure = types.intern_type("structure");
        let uint32 = types.intern_type_spec("uint32");
        let int32 = types.intern_type_spec("int32");
        let value = types.intern_symbol("value");
        let parent = types.intern_type("container");
        let mut definition = TypeDefinition::new(parent, Some(structure), structure_layout());
        definition.add_field(FieldDeclaration::new(value, uint32));
        types.define_type(1, definition, &visibility).unwrap();

        let child = types.intern_type("signed-container");
        let mut replacement = FieldDeclaration::new(value, int32);
        replacement.replace = true;
        let mut definition = TypeDefinition::new(child, Some(parent), structure_layout());
        definition.add_field(replacement);
        let definition = types.define_type(1, definition, &visibility).unwrap();
        assert_eq!(definition.fields[0].offset, 0);
        assert_eq!(definition.fields[0].size, 4);
        assert!(definition.fields[0].replaces_parent);
    }

    #[test]
    fn builtin_layout_methods_and_dereference_queries_are_ready_for_frontend_use() {
        let mut types = TypeSystem::new();
        let visibility = Visibility::unrestricted();
        let symbol = types.intern_type("symbol");
        let pair = types.intern_type("pair");
        let type_field = types.intern_symbol("type");
        let cdr = types.intern_symbol("cdr");
        let copy = types.intern_symbol("copy");
        assert_eq!(types.size_of(symbol, &visibility).unwrap(), POINTER_SIZE);
        assert_eq!(
            types.field_offset(symbol, type_field, &visibility).unwrap(),
            0
        );
        assert_eq!(types.field_offset(pair, cdr, &visibility).unwrap(), 4);
        assert_eq!(types.method_slot(symbol, copy, &visibility).unwrap(), 6);

        let int32 = types.intern_type_spec("int32");
        let pointer = types.pointer_type(int32);
        let deref = types.deref_info(pointer, &visibility).unwrap().unwrap();
        assert!(deref.memory_deref);
        assert!(deref.sign_extend);
        assert_eq!(deref.stride, 4);
        assert_eq!(deref.load_size, Some(4));
    }

    #[test]
    fn object_method_layout_matches_jak1_kscheme() {
        assert_eq!(RUNTIME_TYPE_METHODS_OFFSET, 16);
        assert_eq!(POINTER_SIZE, 4);
        assert_eq!(OBJECT_METHOD_COUNT, 9);

        let mut types = TypeSystem::new();
        let visibility = Visibility::unrestricted();
        let object = types.intern_type("object");
        assert_eq!(
            types.definition(object, &visibility).unwrap().method_count,
            OBJECT_METHOD_COUNT
        );
        for (name, slot) in [
            ("new", 0),
            ("delete", 1),
            ("print", 2),
            ("inspect", 3),
            ("length", 4),
            ("asize-of", 5),
            ("copy", 6),
            ("relocate", 7),
            ("mem-usage", 8),
        ] {
            let method = types.intern_symbol(name);
            assert_eq!(
                types.method_slot(object, method, &visibility).unwrap(),
                slot
            );
        }

        let type_type = types.intern_type("type");
        let method_table = types.intern_symbol("method-table");
        let type_offset = types.layout(type_type, &visibility).unwrap().offset;
        let field = types
            .lookup_field(type_type, method_table, &visibility)
            .unwrap();
        assert_eq!(
            field.field.offset - type_offset,
            RUNTIME_TYPE_METHODS_OFFSET
        );
        assert!(field.field.dynamic);
        let method_table_type = field.result_type;
        let deref = types
            .deref_info(method_table_type, &visibility)
            .unwrap()
            .unwrap();
        assert!(deref.memory_deref);
        assert_eq!(deref.stride, POINTER_SIZE);
    }

    #[test]
    fn enums_and_method_type_substitution_use_interned_specs() {
        let mut types = TypeSystem::new();
        let visibility = Visibility::new(1, []);
        let uint32 = types.intern_type("uint32");
        let mode = types.intern_type("mode");
        let idle = types.intern_symbol("idle");
        types
            .define_enum(
                1,
                EnumDefinition {
                    name: mode,
                    parent: uint32,
                    runtime_type: RuntimeType(uint32.0),
                    entries: vec![EnumEntry {
                        name: idle,
                        value: 7,
                        span: None,
                    }],
                    bitfield: false,
                    documentation: None,
                    span: None,
                },
                &visibility,
            )
            .unwrap();
        assert_eq!(types.enum_value(mode, idle, &visibility).unwrap(), 7);

        let method_type = types.intern_type_spec("_type_");
        let none = types.intern_type_spec("none");
        let signature = types.function_type([method_type], method_type);
        let receiver = types.intern_type_spec("string");
        let substituted = types.substitute_method_type(signature, receiver);
        assert_eq!(
            types.format_type_spec(substituted),
            "(function string string)"
        );
        assert_ne!(substituted, none);
    }
}
