use std::collections::HashMap;
use std::fmt;
use std::hash::{BuildHasherDefault, Hasher};

use crate::span::SpanInfo;

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct SymbolId(pub u64);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct ConsId(pub u32);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct ProcedureId(pub u32);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct EnvironmentId(pub u32);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct StringHashTableId(pub u32);

#[derive(Clone, Debug, PartialEq)]
pub struct Object {
    pub span: Option<SpanInfo>,
    pub value: Value,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Value {
    Pair(ConsId),
    String(String),
    Symbol(SymbolId),
    Float(f64),
    Integer(i64),
    Array(Vec<Object>),
    Lambda(ProcedureId),
    Macro(ProcedureId),
    Environment(EnvironmentId),
    StringHashTable(StringHashTableId),
    EmptyList,
}

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum ObjectKind {
    Pair,
    String,
    Symbol,
    Float,
    Integer,
    Array,
    Lambda,
    Macro,
    Environment,
    StringHashTable,
    EmptyList,
}

impl fmt::Display for ObjectKind {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::Pair => "pair",
            Self::String => "string",
            Self::Symbol => "symbol",
            Self::Float => "float",
            Self::Integer => "integer",
            Self::Array => "array",
            Self::Lambda => "lambda",
            Self::Macro => "macro",
            Self::Environment => "environment",
            Self::StringHashTable => "string hash table",
            Self::EmptyList => "empty list",
        })
    }
}

impl Object {
    pub fn new(value: Value, span: Option<SpanInfo>) -> Self {
        Self { span, value }
    }

    pub fn integer(value: i64, span: Option<SpanInfo>) -> Self {
        Self::new(Value::Integer(value), span)
    }

    pub fn float(value: f64, span: Option<SpanInfo>) -> Self {
        Self::new(Value::Float(value), span)
    }

    pub fn string(value: String, span: Option<SpanInfo>) -> Self {
        Self::new(Value::String(value), span)
    }

    pub fn symbol(value: SymbolId, span: Option<SpanInfo>) -> Self {
        Self::new(Value::Symbol(value), span)
    }

    pub fn array(value: Vec<Object>, span: Option<SpanInfo>) -> Self {
        Self::new(Value::Array(value), span)
    }

    pub fn empty_list(span: Option<SpanInfo>) -> Self {
        Self::new(Value::EmptyList, span)
    }

    pub fn pair(value: ConsId, span: Option<SpanInfo>) -> Self {
        Self::new(Value::Pair(value), span)
    }

    pub fn lambda(value: ProcedureId, span: Option<SpanInfo>) -> Self {
        Self::new(Value::Lambda(value), span)
    }

    pub fn macro_object(value: ProcedureId, span: Option<SpanInfo>) -> Self {
        Self::new(Value::Macro(value), span)
    }

    pub fn environment(value: EnvironmentId, span: Option<SpanInfo>) -> Self {
        Self::new(Value::Environment(value), span)
    }

    pub fn string_hash_table(value: StringHashTableId, span: Option<SpanInfo>) -> Self {
        Self::new(Value::StringHashTable(value), span)
    }

    pub fn kind(&self) -> ObjectKind {
        match &self.value {
            Value::Pair(_) => ObjectKind::Pair,
            Value::String(_) => ObjectKind::String,
            Value::Symbol(_) => ObjectKind::Symbol,
            Value::Float(_) => ObjectKind::Float,
            Value::Integer(_) => ObjectKind::Integer,
            Value::Array(_) => ObjectKind::Array,
            Value::Lambda(_) => ObjectKind::Lambda,
            Value::Macro(_) => ObjectKind::Macro,
            Value::Environment(_) => ObjectKind::Environment,
            Value::StringHashTable(_) => ObjectKind::StringHashTable,
            Value::EmptyList => ObjectKind::EmptyList,
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct ConsCell {
    pub car: Object,
    pub cdr: Object,
}

const FNV_OFFSET_BASIS: u64 = 0xcbf29ce484222325;
const FNV_PRIME: u64 = 0x100000001b3;

pub fn fnv64(bytes: &[u8]) -> u64 {
    let mut hash = FNV_OFFSET_BASIS;
    for byte in bytes {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(FNV_PRIME);
    }
    hash
}

#[derive(Default)]
pub(crate) struct IdentityHasher(u64);

impl Hasher for IdentityHasher {
    fn finish(&self) -> u64 {
        self.0
    }

    fn write(&mut self, bytes: &[u8]) {
        self.0 = fnv64(bytes);
    }

    fn write_u64(&mut self, value: u64) {
        self.0 = value;
    }
}

type IdentityBuildHasher = BuildHasherDefault<IdentityHasher>;
pub(crate) type SymbolMap<V> = HashMap<SymbolId, V, IdentityBuildHasher>;
pub(crate) type IdentityMap<K, V> = HashMap<K, V, IdentityBuildHasher>;

#[derive(Debug, Default)]
pub struct SymbolTable {
    symbols: SymbolMap<Box<str>>,
}

impl SymbolTable {
    pub fn intern(&mut self, text: &str) -> SymbolId {
        let id = SymbolId(fnv64(text.as_bytes()));
        if let Some(existing) = self.symbols.get(&id) {
            debug_assert_eq!(&**existing, text, "FNV-64 symbol hash collision");
            return id;
        }
        self.symbols.insert(id, Box::from(text));
        id
    }

    pub fn resolve(&self, id: SymbolId) -> Option<&str> {
        self.symbols.get(&id).map(Box::as_ref)
    }

    pub fn len(&self) -> usize {
        self.symbols.len()
    }

    pub fn is_empty(&self) -> bool {
        self.symbols.is_empty()
    }
}

#[derive(Debug)]
enum ConsSlot {
    Occupied { cell: ConsCell, marked: bool },
    Free { next: Option<ConsId> },
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct CollectionStats {
    pub allocated_before: usize,
    pub freed: usize,
    pub allocated_after: usize,
}

#[derive(Debug, Default)]
pub struct ConsHeap {
    slots: Vec<ConsSlot>,
    free_head: Option<ConsId>,
    allocated: usize,
    mark_stack: Vec<ConsId>,
}

impl ConsHeap {
    pub fn allocate(&mut self, cell: ConsCell) -> Result<ConsId, String> {
        if let Some(id) = self.free_head {
            let slot = &mut self.slots[id.0 as usize];
            let ConsSlot::Free { next } = slot else {
                return Err("internal compiler error: corrupt cons free list".to_owned());
            };
            self.free_head = *next;
            *slot = ConsSlot::Occupied {
                cell,
                marked: false,
            };
            self.allocated += 1;
            return Ok(id);
        }

        let id = ConsId(
            u32::try_from(self.slots.len()).map_err(|_| "exhausted cons cell IDs".to_owned())?,
        );
        self.slots.push(ConsSlot::Occupied {
            cell,
            marked: false,
        });
        self.allocated += 1;
        Ok(id)
    }

    pub fn get(&self, id: ConsId) -> Result<&ConsCell, String> {
        match self.slots.get(id.0 as usize) {
            Some(ConsSlot::Occupied { cell, .. }) => Ok(cell),
            Some(ConsSlot::Free { .. }) => Err(format!(
                "internal compiler error: cons cell {} has been freed",
                id.0
            )),
            None => Err(format!(
                "internal compiler error: invalid cons cell {}",
                id.0
            )),
        }
    }

    pub fn get_mut(&mut self, id: ConsId) -> Result<&mut ConsCell, String> {
        match self.slots.get_mut(id.0 as usize) {
            Some(ConsSlot::Occupied { cell, .. }) => Ok(cell),
            Some(ConsSlot::Free { .. }) => Err(format!(
                "internal compiler error: cons cell {} has been freed",
                id.0
            )),
            None => Err(format!(
                "internal compiler error: invalid cons cell {}",
                id.0
            )),
        }
    }

    pub fn allocated_len(&self) -> usize {
        self.allocated
    }

    pub fn slot_len(&self) -> usize {
        self.slots.len()
    }

    pub fn collect(&mut self, roots: &[Object]) -> Result<CollectionStats, String> {
        let allocated_before = self.allocated;
        self.clear_marks();
        self.mark_stack.clear();
        for root in roots {
            enqueue_pairs(root, &mut self.mark_stack);
        }

        while let Some(id) = self.mark_stack.pop() {
            let newly_marked = self.mark(id)?;
            if newly_marked {
                let ConsSlot::Occupied { cell, .. } = &self.slots[id.0 as usize] else {
                    unreachable!();
                };
                enqueue_pairs(&cell.car, &mut self.mark_stack);
                enqueue_pairs(&cell.cdr, &mut self.mark_stack);
            }
        }

        let freed = self.sweep();
        Ok(CollectionStats {
            allocated_before,
            freed,
            allocated_after: self.allocated,
        })
    }

    pub(crate) fn clear_marks(&mut self) {
        for slot in &mut self.slots {
            if let ConsSlot::Occupied { marked, .. } = slot {
                *marked = false;
            }
        }
    }

    pub(crate) fn mark(&mut self, id: ConsId) -> Result<bool, String> {
        match self.slots.get_mut(id.0 as usize) {
            Some(ConsSlot::Occupied { marked, .. }) if !*marked => {
                *marked = true;
                Ok(true)
            }
            Some(ConsSlot::Occupied { .. }) => Ok(false),
            Some(ConsSlot::Free { .. }) => Err(format!(
                "internal compiler error: object refers to freed cons cell {}",
                id.0
            )),
            None => Err(format!(
                "internal compiler error: object refers to invalid cons cell {}",
                id.0
            )),
        }
    }

    pub(crate) fn sweep(&mut self) -> usize {
        let mut freed = 0;
        for index in 0..self.slots.len() {
            let should_free =
                matches!(&self.slots[index], ConsSlot::Occupied { marked: false, .. });
            if should_free {
                let id = ConsId(index as u32);
                self.slots[index] = ConsSlot::Free {
                    next: self.free_head,
                };
                self.free_head = Some(id);
                freed += 1;
            } else if let ConsSlot::Occupied { marked, .. } = &mut self.slots[index] {
                *marked = false;
            }
        }
        self.allocated -= freed;
        freed
    }
}

fn enqueue_pairs(object: &Object, pending: &mut Vec<ConsId>) {
    match &object.value {
        Value::Pair(id) => pending.push(*id),
        Value::Array(items) => {
            for item in items {
                enqueue_pairs(item, pending);
            }
        }
        Value::String(_)
        | Value::Symbol(_)
        | Value::Float(_)
        | Value::Integer(_)
        | Value::Lambda(_)
        | Value::Macro(_)
        | Value::Environment(_)
        | Value::StringHashTable(_)
        | Value::EmptyList => {}
    }
}

#[derive(Debug, Default)]
pub struct ObjectStore {
    pub symbols: SymbolTable,
    pub cons: ConsHeap,
}

impl ObjectStore {
    pub fn cons(
        &mut self,
        car: Object,
        cdr: Object,
        span: Option<SpanInfo>,
    ) -> Result<Object, String> {
        let id = self.cons.allocate(ConsCell { car, cdr })?;
        Ok(Object::pair(id, span))
    }

    pub fn list(
        &mut self,
        items: Vec<Object>,
        tail: Object,
        span: Option<SpanInfo>,
    ) -> Result<Object, String> {
        let mut result = tail;
        for item in items.into_iter().rev() {
            result = self.cons(item, result, span)?;
        }
        Ok(result)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fnv64_matches_the_standard_fnv1a_vector() {
        assert_eq!(fnv64(b"hello"), 0xa430_d846_80aa_bd0b);
    }

    #[test]
    fn collection_traces_pairs_inside_arrays_and_reuses_freed_slots() {
        let mut store = ObjectStore::default();
        let empty = Object::empty_list(None);
        let tail = store
            .cons(Object::integer(2, None), empty.clone(), None)
            .unwrap();
        let list = store.cons(Object::integer(1, None), tail, None).unwrap();
        let unreachable = store.cons(Object::integer(3, None), empty, None).unwrap();
        assert_eq!(store.cons.allocated_len(), 3);
        assert_eq!(store.cons.slot_len(), 3);

        let root = Object::array(vec![list], None);
        let stats = store.cons.collect(&[root]).unwrap();
        assert_eq!(
            stats,
            CollectionStats {
                allocated_before: 3,
                freed: 1,
                allocated_after: 2,
            }
        );
        let Value::Pair(unreachable_id) = unreachable.value else {
            panic!();
        };
        assert!(store.cons.get(unreachable_id).is_err());

        let reused = store
            .cons(Object::integer(4, None), Object::empty_list(None), None)
            .unwrap();
        assert_eq!(reused.value, Value::Pair(unreachable_id));
        assert_eq!(store.cons.slot_len(), 3);
    }

    #[test]
    fn collection_marks_long_lists_iteratively() {
        let mut store = ObjectStore::default();
        let mut list = Object::empty_list(None);
        for value in 0..20_000 {
            list = store
                .cons(Object::integer(value, None), list, None)
                .unwrap();
        }

        let stats = store.cons.collect(&[list]).unwrap();
        assert_eq!(stats.freed, 0);
        assert_eq!(stats.allocated_after, 20_000);
    }
}
