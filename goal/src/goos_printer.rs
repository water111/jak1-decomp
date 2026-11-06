use std::collections::HashSet;

use crate::diagnostic::Diagnostic;
use crate::object::{ConsId, Object, Value};

use super::Interpreter;

impl Interpreter {
    pub fn format_object(&self, object: &Object) -> Result<String, Diagnostic> {
        let mut active_pairs = HashSet::new();
        self.format_object_inner(object, &mut active_pairs)
    }

    fn format_object_inner(
        &self,
        object: &Object,
        active_pairs: &mut HashSet<ConsId>,
    ) -> Result<String, Diagnostic> {
        match &object.value {
            Value::Integer(value) => Ok(value.to_string()),
            Value::Float(value) => Ok(format_float(*value)),
            Value::String(value) => Ok(format!("\"{}\"", escape_string(value))),
            Value::Symbol(symbol) => Ok(self.symbol_name(*symbol)?.to_owned()),
            Value::EmptyList => Ok("()".to_owned()),
            Value::Array(items) => {
                let mut output = String::from("#(");
                for (index, item) in items.iter().enumerate() {
                    if index != 0 {
                        output.push(' ');
                    }
                    output.push_str(&self.format_object_inner(item, active_pairs)?);
                }
                output.push(')');
                Ok(output)
            }
            Value::Pair(id) => self.format_pair(*id, active_pairs),
            Value::Lambda(_) => Ok("#<lambda>".to_owned()),
            Value::Macro(_) => Ok("#<macro>".to_owned()),
            Value::Environment(_) => Ok("#<environment>".to_owned()),
            Value::StringHashTable(_) => Ok("#<string-hash-table>".to_owned()),
        }
    }

    fn format_pair(
        &self,
        id: ConsId,
        active_pairs: &mut HashSet<ConsId>,
    ) -> Result<String, Diagnostic> {
        let mut output = String::from("(");
        let mut current = Object::pair(id, None);
        let mut first = true;
        let mut inserted = Vec::new();
        loop {
            match current.value {
                Value::Pair(pair_id) => {
                    if !active_pairs.insert(pair_id) {
                        if !first {
                            output.push_str(" . ");
                        }
                        output.push_str("#<cycle>)");
                        break;
                    }
                    inserted.push(pair_id);
                    let cell = self.pair(pair_id)?;
                    if !first {
                        output.push(' ');
                    }
                    output.push_str(&self.format_object_inner(&cell.car, active_pairs)?);
                    current = cell.cdr;
                    first = false;
                }
                Value::EmptyList => {
                    output.push(')');
                    break;
                }
                _ => {
                    output.push_str(" . ");
                    output.push_str(&self.format_object_inner(&current, active_pairs)?);
                    output.push(')');
                    break;
                }
            }
        }
        for pair_id in inserted {
            active_pairs.remove(&pair_id);
        }
        Ok(output)
    }

    pub(super) fn object_fragment(&self, object: &Object) -> Result<String, Diagnostic> {
        match &object.value {
            Value::Symbol(symbol) => Ok(self.symbol_name(*symbol)?.to_owned()),
            Value::String(value) => Ok(value.clone()),
            Value::Integer(value) => Ok(value.to_string()),
            Value::Float(value) => Ok(format_float(*value)),
            _ => self.format_object(object),
        }
    }
}

fn escape_string(value: &str) -> String {
    let mut output = String::with_capacity(value.len());
    for character in value.chars() {
        match character {
            '\\' => output.push_str("\\\\"),
            '"' => output.push_str("\\\""),
            '\n' => output.push_str("\\n"),
            '\r' => output.push_str("\\r"),
            '\t' => output.push_str("\\t"),
            _ => output.push(character),
        }
    }
    output
}

fn format_float(value: f64) -> String {
    let mut output = value.to_string();
    if value.is_finite() && !output.contains(['.', 'e', 'E']) {
        output.push_str(".0");
    }
    output
}
