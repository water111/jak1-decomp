use crate::diagnostic::Diagnostic;
use crate::ir::{FunctionId, StackSlot, VirtualRegisterId};
use crate::object::SymbolId;
use crate::static_data::StaticLabelId;
use crate::type_system::{RegisterClass, TypeSpec};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct LoadInfo {
    pub size: u32,
    pub sign_extend: bool,
    pub register_class: RegisterClass,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BitFieldInfo {
    pub offset: u8,
    pub size: u8,
    pub sign_extend: bool,
    pub use_128_bits: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PlaceBase {
    Register(VirtualRegisterId),
    Global(SymbolId),
    Memory {
        base: VirtualRegisterId,
        offset: i64,
    },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PlaceProjection {
    Whole(LoadInfo),
    BitField(BitFieldInfo),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Place {
    pub base: PlaceBase,
    pub projection: PlaceProjection,
}

#[derive(Debug, PartialEq)]
pub enum ValueKind {
    None,
    Register(VirtualRegisterId),
    Integer(i128),
    Float(f32),
    SymbolObject(SymbolId),
    Function(FunctionId),
    StaticAddress(StaticLabelId),
    StackAddress(StackSlot),
    Place(Place),
}

#[derive(Debug, PartialEq)]
pub struct Value {
    pub ty: TypeSpec,
    pub kind: ValueKind,
}

impl Value {
    pub fn new(ty: TypeSpec, kind: ValueKind) -> Self {
        Self { ty, kind }
    }

    pub fn register(ty: TypeSpec, register: VirtualRegisterId) -> Self {
        Self::new(ty, ValueKind::Register(register))
    }

    pub fn place(ty: TypeSpec, place: Place) -> Self {
        Self::new(ty, ValueKind::Place(place))
    }

    pub fn as_register(&self) -> Option<VirtualRegisterId> {
        match self.kind {
            ValueKind::Register(register) => Some(register),
            _ => None,
        }
    }

    pub fn into_place(self) -> Result<Place, Diagnostic> {
        match self.kind {
            ValueKind::Place(place) => Ok(place),
            _ => Err(Diagnostic::error("expression is not assignable")),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn places_are_flat_and_keep_address_components_once() {
        let place = Place {
            base: PlaceBase::Memory {
                base: VirtualRegisterId(4),
                offset: 12,
            },
            projection: PlaceProjection::Whole(LoadInfo {
                size: 4,
                sign_extend: false,
                register_class: RegisterClass::Gpr,
            }),
        };
        assert_eq!(
            place.base,
            PlaceBase::Memory {
                base: VirtualRegisterId(4),
                offset: 12,
            }
        );
    }
}
