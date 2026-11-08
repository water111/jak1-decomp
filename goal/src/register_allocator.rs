use std::cmp::Reverse;
use std::collections::{HashMap, HashSet};

use crate::frontend::{FunctionData, RegisterReservation};
use crate::ir::{
    ARGUMENT_REGISTERS, BranchCondition, BranchDelay, FPR_ALLOCATION_ORDER, GPR_ALLOCATION_ORDER,
    IrInstruction, PhysicalRegister, Register, RegisterEffects, VirtualRegister, VirtualRegisterId,
};
use crate::type_system::RegisterClass;

#[derive(Debug)]
pub struct Liveness {
    pub live_in: Vec<HashSet<Register>>,
    pub live_out: Vec<HashSet<Register>>,
    pub successors: Vec<Vec<usize>>,
    pub reachable: Vec<bool>,
}

#[derive(Debug)]
pub struct RegisterAllocation {
    /// IR after disconnected live ranges have been split into webs.
    pub instructions: Vec<IrInstruction>,
    /// Virtual-register descriptions for the split IR.
    pub registers: Vec<VirtualRegister>,
    /// Parallel to `registers`.
    pub assignments: Vec<PhysicalRegister>,
    /// In allocation order, for deterministic prologue generation.
    pub used_callee_saved: Vec<PhysicalRegister>,
}

impl RegisterAllocation {
    pub fn physical_register(&self, register: Register) -> Result<PhysicalRegister, String> {
        match register {
            Register::Physical(register) => Ok(register),
            Register::Virtual(id) => self
                .assignments
                .get(id.0 as usize)
                .copied()
                .ok_or_else(|| format!("virtual register {} was not allocated", id.0)),
        }
    }

    /// A coalesced move normally emits nothing. `must_emit` copies and every
    /// move occupying a branch delay slot remain real instructions.
    pub fn should_emit_copy(&self, instruction: usize) -> Result<bool, String> {
        let Some((destination, source, must_emit)) = self
            .instructions
            .get(instruction)
            .and_then(IrInstruction::copy_operands)
        else {
            return Err(format!("instruction {instruction} is not a copy"));
        };
        Ok(must_emit
            || instruction > 0 && self.instructions[instruction - 1].has_delay_slot()
            || self.physical_register(destination)? != self.physical_register(source)?)
    }
}

pub fn allocate_registers(function: &FunctionData) -> Result<RegisterAllocation, String> {
    validate_function(function)?;
    let split = split_live_ranges(function)?;
    color(split, &function.labels)
}

pub fn analyze_liveness(function: &FunctionData) -> Result<Liveness, String> {
    validate_function(function)?;
    let effects: Vec<_> = function
        .ir
        .iter()
        .map(IrInstruction::register_effects)
        .collect();
    analyze(&function.ir, &effects, &function.labels)
}

fn validate_function(function: &FunctionData) -> Result<(), String> {
    for register in &function.single_web_registers {
        function.registers.get(register.0 as usize).ok_or_else(|| {
            format!(
                "single-web binding refers to unknown virtual register {}",
                register.0
            )
        })?;
    }
    for reservation in &function.register_reservations {
        if reservation.start > reservation.end || reservation.end > function.ir.len() {
            return Err(format!(
                "register reservation {} has invalid instruction range {}..{}",
                reservation.register.0, reservation.start, reservation.end
            ));
        }
        function
            .registers
            .get(reservation.register.0 as usize)
            .ok_or_else(|| {
                format!(
                    "register reservation refers to unknown virtual register {}",
                    reservation.register.0
                )
            })?;
    }
    for (index, instruction) in function.ir.iter().enumerate() {
        if let IrInstruction::Call { argument_count } = instruction
            && usize::from(*argument_count) > ARGUMENT_REGISTERS.len()
        {
            return Err(format!(
                "instruction {index} passes {argument_count} arguments, but GOAL has only {} argument registers",
                ARGUMENT_REGISTERS.len()
            ));
        }
        let effects = instruction.register_effects();
        for register in effects.uses.into_iter().chain(effects.definitions) {
            let Register::Virtual(id) = register else {
                continue;
            };
            let description = function.registers.get(id.0 as usize).ok_or_else(|| {
                format!(
                    "instruction {index} refers to unknown virtual register {}",
                    id.0
                )
            })?;
            if description.class == RegisterClass::Vf {
                return Err(format!(
                    "instruction {index} asks the register allocator to allocate vf register {}",
                    id.0
                ));
            }
        }
    }
    Ok(())
}

fn analyze(
    instructions: &[IrInstruction],
    effects: &[RegisterEffects],
    labels: &crate::ir::LabelTable,
) -> Result<Liveness, String> {
    let count = instructions.len();
    let mut likely_delay_targets = HashMap::new();
    let mut normal_delay_successors: HashMap<usize, Vec<usize>> = HashMap::new();

    for (index, instruction) in instructions.iter().enumerate() {
        let IrInstruction::Branch {
            condition,
            target,
            delay,
        } = instruction
        else {
            continue;
        };
        if index + 1 >= count {
            return Err(format!("branch at instruction {index} has no delay slot"));
        }
        if instructions[index + 1].is_branch() {
            return Err(format!(
                "branch at instruction {} cannot occupy another branch's delay slot",
                index + 1
            ));
        }
        let target = labels
            .location(*target)?
            .ok_or_else(|| format!("branch at instruction {index} targets an unbound label"))?
            as usize;
        if target >= count {
            return Err(format!(
                "branch at instruction {index} targets instruction {target}, outside the function"
            ));
        }
        match delay {
            BranchDelay::Likely => {
                if *condition == BranchCondition::Always {
                    return Err(format!(
                        "unconditional branch at instruction {index} cannot be branch-likely"
                    ));
                }
                likely_delay_targets.insert(index + 1, target);
            }
            BranchDelay::Normal => {
                let mut successors = vec![target];
                if *condition != BranchCondition::Always && index + 2 < count {
                    successors.push(index + 2);
                }
                normal_delay_successors.insert(index + 1, successors);
            }
        }
    }

    let mut successors = vec![Vec::new(); count];
    for (index, instruction) in instructions.iter().enumerate() {
        match instruction {
            IrInstruction::Branch {
                delay: BranchDelay::Likely,
                ..
            } => {
                successors[index].push(index + 1);
                if index + 2 < count {
                    successors[index].push(index + 2);
                }
            }
            IrInstruction::Branch {
                delay: BranchDelay::Normal,
                ..
            } => successors[index].push(index + 1),
            _ if likely_delay_targets.contains_key(&index) => {
                successors[index].push(likely_delay_targets[&index]);
            }
            _ if normal_delay_successors.contains_key(&index) => {
                successors[index].extend_from_slice(&normal_delay_successors[&index]);
            }
            _ if index + 1 < count => successors[index].push(index + 1),
            _ => {}
        }
    }

    let mut reachable = vec![false; count];
    let mut pending = if count == 0 { Vec::new() } else { vec![0] };
    while let Some(index) = pending.pop() {
        if reachable[index] {
            continue;
        }
        reachable[index] = true;
        pending.extend(
            successors[index]
                .iter()
                .copied()
                .filter(|next| !reachable[*next]),
        );
    }
    for (index, instruction_successors) in successors.iter_mut().enumerate() {
        if !reachable[index] {
            instruction_successors.clear();
        }
    }

    let mut live_in = vec![HashSet::new(); count];
    let mut live_out = vec![HashSet::new(); count];
    loop {
        let mut changed = false;
        for index in (0..count).rev() {
            let mut new_out = HashSet::new();
            for &successor in &successors[index] {
                new_out.extend(live_in[successor].iter().copied());
            }
            let mut new_in = HashSet::new();
            if reachable[index] {
                new_in.extend(effects[index].uses.iter().copied());
                new_in.extend(
                    new_out
                        .iter()
                        .copied()
                        .filter(|register| !effects[index].definitions.contains(register)),
                );
            }
            if new_in != live_in[index] || new_out != live_out[index] {
                live_in[index] = new_in;
                live_out[index] = new_out;
                changed = true;
            }
        }
        if !changed {
            break;
        }
    }

    Ok(Liveness {
        live_in,
        live_out,
        successors,
        reachable,
    })
}

#[derive(Debug)]
struct DisjointSets {
    parents: Vec<usize>,
}

impl DisjointSets {
    fn new(size: usize) -> Self {
        Self {
            parents: (0..size).collect(),
        }
    }

    fn add(&mut self) -> usize {
        let id = self.parents.len();
        self.parents.push(id);
        id
    }

    fn find(&mut self, item: usize) -> usize {
        if self.parents[item] != item {
            self.parents[item] = self.find(self.parents[item]);
        }
        self.parents[item]
    }

    fn union(&mut self, left: usize, right: usize) {
        let left = self.find(left);
        let right = self.find(right);
        if left != right {
            self.parents[right] = left;
        }
    }
}

#[derive(Debug)]
struct SplitFunction {
    instructions: Vec<IrInstruction>,
    registers: Vec<VirtualRegister>,
    reservations: Vec<RegisterReservation>,
}

fn split_live_ranges(function: &FunctionData) -> Result<SplitFunction, String> {
    if function.ir.is_empty() {
        return Ok(SplitFunction {
            instructions: Vec::new(),
            registers: Vec::new(),
            reservations: Vec::new(),
        });
    }
    let effects: Vec<_> = function
        .ir
        .iter()
        .map(IrInstruction::register_effects)
        .collect();
    let liveness = analyze(&function.ir, &effects, &function.labels)?;

    let mut predecessors = vec![Vec::new(); function.ir.len()];
    for (index, successors) in liveness.successors.iter().enumerate() {
        for &successor in successors {
            predecessors[successor].push(index);
        }
    }

    let mut definition_ids = HashMap::new();
    let mut definition_registers = Vec::new();
    for (index, effect) in effects.iter().enumerate() {
        for &register in &effect.definitions {
            let Register::Virtual(register) = register else {
                continue;
            };
            definition_ids.entry((index, register)).or_insert_with(|| {
                let id = definition_registers.len();
                definition_registers.push(register);
                id
            });
        }
    }

    let mut reaching_in = vec![HashSet::new(); function.ir.len()];
    let mut reaching_out = vec![HashSet::new(); function.ir.len()];
    loop {
        let mut changed = false;
        for index in 0..function.ir.len() {
            let mut new_in = HashSet::new();
            for &predecessor in &predecessors[index] {
                new_in.extend(reaching_out[predecessor].iter().copied());
            }
            let defined: HashSet<_> = effects[index]
                .definitions
                .iter()
                .filter_map(|register| match register {
                    Register::Virtual(register) => Some(*register),
                    Register::Physical(_) => None,
                })
                .collect();
            let mut new_out: HashSet<_> = new_in
                .iter()
                .copied()
                .filter(|definition| !defined.contains(&definition_registers[*definition]))
                .collect();
            if liveness.reachable[index] {
                new_out.extend(
                    defined
                        .iter()
                        .map(|register| definition_ids[&(index, *register)]),
                );
            } else {
                new_in.clear();
                new_out.clear();
            }
            if new_in != reaching_in[index] || new_out != reaching_out[index] {
                reaching_in[index] = new_in;
                reaching_out[index] = new_out;
                changed = true;
            }
        }
        if !changed {
            break;
        }
    }

    let forced_registers: HashSet<_> = function
        .single_web_registers
        .iter()
        .copied()
        .chain(
            function
                .register_reservations
                .iter()
                .map(|reservation| reservation.register),
        )
        .collect();
    let mut sets = DisjointSets::new(definition_registers.len());
    let mut first_forced_definition = HashMap::new();
    for (definition, &register) in definition_registers.iter().enumerate() {
        if forced_registers.contains(&register) {
            let first = *first_forced_definition
                .entry(register)
                .or_insert(definition);
            sets.union(first, definition);
        }
    }

    let mut pseudo_definitions = HashMap::new();
    let mut use_definitions = HashMap::new();
    for (index, effect) in effects.iter().enumerate() {
        for &register in &effect.uses {
            let Register::Virtual(register) = register else {
                continue;
            };
            let mut definitions: Vec<_> = reaching_in[index]
                .iter()
                .copied()
                .filter(|definition| definition_registers[*definition] == register)
                .collect();
            definitions.sort_unstable();
            if definitions.is_empty() {
                let definition = *pseudo_definitions.entry(register).or_insert_with(|| {
                    let definition = sets.add();
                    definition_registers.push(register);
                    if let Some(first) = first_forced_definition.get(&register) {
                        sets.union(definition, *first);
                    }
                    definition
                });
                definitions.push(definition);
            }
            for &other in definitions.iter().skip(1) {
                sets.union(definitions[0], other);
            }
            use_definitions.insert((index, register), definitions[0]);

            let ties_input_to_output = match &function.ir[index] {
                IrInstruction::ConditionalMove { destination, .. }
                | IrInstruction::Unary { destination, .. } => {
                    *destination == Register::Virtual(register)
                }
                _ => false,
            };
            if ties_input_to_output && liveness.reachable[index] {
                sets.union(definitions[0], definition_ids[&(index, register)]);
            }
        }
    }

    let mut reservation_definitions = HashMap::new();
    for reservation in &function.register_reservations {
        let register = reservation.register;
        if reservation_definitions.contains_key(&register) {
            continue;
        }
        let definition = match first_forced_definition.get(&register) {
            Some(definition) => *definition,
            None => {
                let definition = sets.add();
                definition_registers.push(register);
                first_forced_definition.insert(register, definition);
                definition
            }
        };
        reservation_definitions.insert(register, definition);
    }

    let mut root_members: HashMap<usize, Vec<usize>> = HashMap::new();
    for definition in 0..definition_registers.len() {
        root_members
            .entry(sets.find(definition))
            .or_default()
            .push(definition);
    }
    let mut roots: Vec<_> = root_members.into_iter().collect();
    roots.sort_by_key(|(_, members)| {
        let first = *members.iter().min().unwrap();
        let register = function.registers[definition_registers[first].0 as usize];
        (register.class.register_file(), register.number, first)
    });

    let mut web_for_root = HashMap::new();
    let mut registers = Vec::with_capacity(roots.len());
    let mut next_number = [0_u32; 2];
    for (root, members) in roots {
        let original = function.registers[definition_registers[members[0]].0 as usize];
        let class = original.class.register_file();
        let id = VirtualRegisterId(
            u32::try_from(registers.len()).map_err(|_| "exhausted split virtual register IDs")?,
        );
        web_for_root.insert(root, id);
        registers.push(VirtualRegister {
            number: next_number[class],
            ..original
        });
        next_number[class] = next_number[class]
            .checked_add(1)
            .ok_or_else(|| "exhausted split virtual register numbers".to_owned())?;
    }

    let mut use_web = HashMap::new();
    for ((index, register), definition) in use_definitions {
        use_web.insert((index, register), web_for_root[&sets.find(definition)]);
    }
    let mut definition_web = HashMap::new();
    for ((index, register), definition) in definition_ids {
        definition_web.insert((index, register), web_for_root[&sets.find(definition)]);
    }

    let instructions = function
        .ir
        .iter()
        .enumerate()
        .map(|(index, instruction)| {
            instruction.map_registers(
                |register| match register {
                    Register::Virtual(register) => Register::Virtual(use_web[&(index, register)]),
                    Register::Physical(_) => register,
                },
                |register| match register {
                    Register::Virtual(register) => {
                        Register::Virtual(definition_web[&(index, register)])
                    }
                    Register::Physical(_) => register,
                },
            )
        })
        .collect();

    let reservations = function
        .register_reservations
        .iter()
        .map(|reservation| {
            let definition = reservation_definitions[&reservation.register];
            RegisterReservation {
                register: web_for_root[&sets.find(definition)],
                start: reservation.start,
                end: reservation.end,
            }
        })
        .collect();

    Ok(SplitFunction {
        instructions,
        registers,
        reservations,
    })
}

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
enum Node {
    Virtual(VirtualRegisterId),
    Physical(PhysicalRegister),
}

#[derive(Debug)]
struct InterferenceGraph {
    nodes: Vec<Node>,
    indices: HashMap<Node, usize>,
    adjacency: Vec<HashSet<usize>>,
    parents: Vec<usize>,
    active: Vec<bool>,
}

impl InterferenceGraph {
    fn new(nodes: impl IntoIterator<Item = Node>) -> Self {
        let nodes: Vec<_> = nodes.into_iter().collect();
        let indices = nodes
            .iter()
            .copied()
            .enumerate()
            .map(|(index, node)| (node, index))
            .collect();
        let count = nodes.len();
        Self {
            nodes,
            indices,
            adjacency: vec![HashSet::new(); count],
            parents: (0..count).collect(),
            active: vec![true; count],
        }
    }

    fn index(&self, node: Node) -> Option<usize> {
        self.indices.get(&node).copied()
    }

    fn root(&mut self, node: usize) -> usize {
        if self.parents[node] != node {
            self.parents[node] = self.root(self.parents[node]);
        }
        self.parents[node]
    }

    fn edge(&mut self, left: usize, right: usize) {
        if left != right {
            self.adjacency[left].insert(right);
            self.adjacency[right].insert(left);
        }
    }

    fn legal_merge(&mut self, left: usize, right: usize, registers: &[VirtualRegister]) -> bool {
        let left = self.root(left);
        let right = self.root(right);
        left != right
            && node_class(self.nodes[left], registers) == node_class(self.nodes[right], registers)
            && !self.adjacency[left].contains(&right)
    }

    /// Merge `left` into `right`; the right-hand identity survives.
    fn merge(&mut self, left: usize, right: usize, registers: &[VirtualRegister]) {
        let left = self.root(left);
        let right = self.root(right);
        debug_assert!(self.legal_merge(left, right, registers));
        let right_is_physical = matches!(self.nodes[right], Node::Physical(_));
        let right_class = node_class(self.nodes[right], registers);
        let neighbors: HashSet<_> = self.adjacency[left]
            .union(&self.adjacency[right])
            .copied()
            .filter(|neighbor| *neighbor != left && *neighbor != right)
            .filter(|neighbor| {
                !right_is_physical || node_class(self.nodes[*neighbor], registers) == right_class
            })
            .collect();

        let old_neighbors: Vec<_> = self.adjacency[left]
            .union(&self.adjacency[right])
            .copied()
            .collect();
        for neighbor in old_neighbors {
            self.adjacency[neighbor].remove(&left);
            self.adjacency[neighbor].remove(&right);
            if neighbors.contains(&neighbor) {
                self.adjacency[neighbor].insert(right);
            }
        }
        self.adjacency[right] = neighbors;
        self.adjacency[left].clear();
        self.parents[left] = right;
        self.active[left] = false;
    }
}

fn node_class(node: Node, registers: &[VirtualRegister]) -> usize {
    match node {
        Node::Virtual(register) => registers[register.0 as usize].class.register_file(),
        Node::Physical(register) => register.class().register_file(),
    }
}

fn color(
    split: SplitFunction,
    labels: &crate::ir::LabelTable,
) -> Result<RegisterAllocation, String> {
    let effects: Vec<_> = split
        .instructions
        .iter()
        .map(IrInstruction::register_effects)
        .collect();
    let liveness = analyze(&split.instructions, &effects, labels)?;

    let mut seen_virtual = vec![false; split.registers.len()];
    for effect in &effects {
        for register in effect.uses.iter().chain(&effect.definitions) {
            if let Register::Virtual(register) = register {
                seen_virtual[register.0 as usize] = true;
            }
        }
    }
    for reservation in &split.reservations {
        seen_virtual[reservation.register.0 as usize] = true;
    }
    let virtual_nodes = seen_virtual
        .iter()
        .enumerate()
        .filter(|(_, seen)| **seen)
        .map(|(index, _)| Node::Virtual(VirtualRegisterId(index as u32)));
    let physical_nodes = GPR_ALLOCATION_ORDER
        .into_iter()
        .chain(FPR_ALLOCATION_ORDER)
        .map(Node::Physical);
    let mut graph = InterferenceGraph::new(virtual_nodes.chain(physical_nodes));

    for pool in [&GPR_ALLOCATION_ORDER[..], &FPR_ALLOCATION_ORDER[..]] {
        for left in 0..pool.len() {
            for right in left + 1..pool.len() {
                graph.edge(
                    graph.index(Node::Physical(pool[left])).unwrap(),
                    graph.index(Node::Physical(pool[right])).unwrap(),
                );
            }
        }
    }

    for (index, effect) in effects.iter().enumerate() {
        for &definition in &effect.definitions {
            let Some(definition) = canonical_node(definition, &graph) else {
                continue;
            };
            for &live in &liveness.live_out[index] {
                let Some(live) = canonical_node(live, &graph) else {
                    continue;
                };
                let physical_edge = matches!(graph.nodes[definition], Node::Physical(_))
                    || matches!(graph.nodes[live], Node::Physical(_));
                if !physical_edge
                    || node_class(graph.nodes[definition], &split.registers)
                        == node_class(graph.nodes[live], &split.registers)
                {
                    graph.edge(definition, live);
                }
            }
        }
    }

    for reservation in &split.reservations {
        let reserved = graph
            .index(Node::Virtual(reservation.register))
            .expect("reserved virtual register is in the graph");
        let mut conflicts = HashSet::new();
        for index in reservation.start..reservation.end {
            conflicts.extend(effects[index].uses.iter().copied());
            conflicts.extend(effects[index].definitions.iter().copied());
            conflicts.extend(liveness.live_in[index].iter().copied());
            conflicts.extend(liveness.live_out[index].iter().copied());
        }
        for conflict in conflicts {
            let Some(conflict) = canonical_node(conflict, &graph) else {
                continue;
            };
            if node_class(graph.nodes[reserved], &split.registers)
                == node_class(graph.nodes[conflict], &split.registers)
            {
                graph.edge(reserved, conflict);
            }
        }
    }
    for left in 0..split.reservations.len() {
        for right in left + 1..split.reservations.len() {
            let left_reservation = split.reservations[left];
            let right_reservation = split.reservations[right];
            if left_reservation.start < right_reservation.end
                && right_reservation.start < left_reservation.end
            {
                let left = graph
                    .index(Node::Virtual(left_reservation.register))
                    .expect("reserved virtual register is in the graph");
                let right = graph
                    .index(Node::Virtual(right_reservation.register))
                    .expect("reserved virtual register is in the graph");
                if node_class(graph.nodes[left], &split.registers)
                    == node_class(graph.nodes[right], &split.registers)
                {
                    graph.edge(left, right);
                }
            }
        }
    }

    let mut conditional_initializations = HashSet::new();
    let mut latest_definition = HashMap::new();
    for (index, instruction) in split.instructions.iter().enumerate() {
        if let IrInstruction::ConditionalMove { destination, .. } = instruction
            && let Some(previous) = latest_definition.get(destination)
        {
            conditional_initializations.insert(*previous);
        }
        for &definition in &effects[index].definitions {
            latest_definition.insert(definition, index);
        }
    }
    let eligible_moves: Vec<_> = split
        .instructions
        .iter()
        .enumerate()
        .filter_map(|(index, instruction)| {
            let (destination, source, must_emit) = instruction.copy_operands()?;
            (!must_emit
                && !conditional_initializations.contains(&index)
                && (index == 0 || !split.instructions[index - 1].is_branch()))
            .then_some((index, destination, source))
        })
        .collect();

    // Pass 1: incoming parameters.
    for &(_, destination, source) in &eligible_moves {
        let (Register::Virtual(destination), Register::Physical(source)) = (destination, source)
        else {
            continue;
        };
        if !source.is_argument() {
            continue;
        }
        let destination = graph.root(graph.index(Node::Virtual(destination)).unwrap());
        let source = graph.index(Node::Physical(source)).unwrap();
        if matches!(graph.nodes[destination], Node::Virtual(_))
            && graph.legal_merge(destination, source, &split.registers)
        {
            graph.merge(destination, source, &split.registers);
        }
    }

    // Pass 2: virtual-register copies. Each original source web wins at most
    // once, and the destination identity survives.
    let mut successful_sources = HashSet::new();
    for &(_, destination, source) in &eligible_moves {
        let (Register::Virtual(destination_id), Register::Virtual(source_id)) =
            (destination, source)
        else {
            continue;
        };
        let source = graph.root(graph.index(Node::Virtual(source_id)).unwrap());
        let destination = graph.root(graph.index(Node::Virtual(destination_id)).unwrap());
        if source == destination
            || !matches!(graph.nodes[source], Node::Virtual(_))
            || !matches!(graph.nodes[destination], Node::Virtual(_))
            || successful_sources.contains(&source_id)
        {
            continue;
        }
        let source_description = split.registers[source_id.0 as usize];
        let destination_description = split.registers[destination_id.0 as usize];
        if node_class(graph.nodes[source], &split.registers)
            == node_class(graph.nodes[destination], &split.registers)
            && source_description.width() == destination_description.width()
            && graph.legal_merge(source, destination, &split.registers)
        {
            graph.merge(source, destination, &split.registers);
            successful_sources.insert(source_id);
        }
    }

    // Pass 3: outgoing calls and returns.
    for &(_, destination, source) in &eligible_moves {
        let (Register::Physical(destination), Register::Virtual(source_id)) = (destination, source)
        else {
            continue;
        };
        if successful_sources.contains(&source_id) {
            continue;
        }
        let Some(destination) = graph.index(Node::Physical(destination)) else {
            continue;
        };
        let source = graph.root(graph.index(Node::Virtual(source_id)).unwrap());
        if matches!(graph.nodes[source], Node::Virtual(_))
            && graph.legal_merge(source, destination, &split.registers)
        {
            graph.merge(source, destination, &split.registers);
        }
    }

    let mut members: Vec<Vec<VirtualRegisterId>> = vec![Vec::new(); graph.nodes.len()];
    for (index, seen) in seen_virtual.iter().enumerate() {
        if *seen {
            let id = VirtualRegisterId(index as u32);
            let node = graph.index(Node::Virtual(id)).unwrap();
            let root = graph.root(node);
            members[root].push(id);
        }
    }
    let read: HashSet<_> = effects
        .iter()
        .flat_map(|effect| effect.uses.iter())
        .filter_map(|register| match register {
            Register::Virtual(register) => Some(*register),
            Register::Physical(_) => None,
        })
        .collect();

    let mut order: Vec<_> = (0..graph.nodes.len())
        .filter(|index| graph.active[*index] && matches!(graph.nodes[*index], Node::Virtual(_)))
        .collect();
    order.sort_by_key(|index| {
        let Node::Virtual(register) = graph.nodes[*index] else {
            unreachable!()
        };
        (
            !members[*index].iter().any(|member| read.contains(member)),
            Reverse(graph.adjacency[*index].len()),
            split.registers[register.0 as usize].number,
            split.registers[register.0 as usize].class.register_file(),
        )
    });

    for virtual_node in order {
        let class = node_class(graph.nodes[virtual_node], &split.registers);
        let palette: &[PhysicalRegister] = if class == 0 {
            &GPR_ALLOCATION_ORDER
        } else {
            &FPR_ALLOCATION_ORDER
        };
        let chosen = palette.iter().find_map(|register| {
            let physical = graph.index(Node::Physical(*register)).unwrap();
            graph
                .legal_merge(virtual_node, physical, &split.registers)
                .then_some(physical)
        });
        let Some(chosen) = chosen else {
            let Node::Virtual(register) = graph.nodes[virtual_node] else {
                unreachable!()
            };
            return Err(format!(
                "no hardware register available for virtual register {}",
                register.0
            ));
        };
        graph.merge(virtual_node, chosen, &split.registers);
    }

    let mut assignments = Vec::with_capacity(split.registers.len());
    for (index, seen) in seen_virtual.iter().enumerate() {
        if !seen {
            return Err(format!(
                "split virtual register {index} does not appear in the instruction stream"
            ));
        }
        let node = graph
            .index(Node::Virtual(VirtualRegisterId(index as u32)))
            .unwrap();
        let root = graph.root(node);
        let Node::Physical(register) = graph.nodes[root] else {
            return Err(format!("virtual register {index} was not colored"));
        };
        assignments.push(register);
    }
    let assigned: HashSet<_> = assignments.iter().copied().collect();
    let used_callee_saved = GPR_ALLOCATION_ORDER
        .into_iter()
        .chain(FPR_ALLOCATION_ORDER)
        .filter(|register| register.is_callee_saved() && assigned.contains(register))
        .collect();

    Ok(RegisterAllocation {
        instructions: split.instructions,
        registers: split.registers,
        assignments,
        used_callee_saved,
    })
}

fn canonical_node(register: Register, graph: &InterferenceGraph) -> Option<usize> {
    match register {
        Register::Virtual(register) => graph.index(Node::Virtual(register)),
        Register::Physical(register) => graph.index(Node::Physical(register)),
    }
}

#[cfg(test)]
mod tests {
    use crate::frontend::FunctionIdentity;
    use crate::ir::{Gpr, LabelTable};
    use crate::static_data::Segment;
    use crate::type_system::TypeSystem;

    use super::*;

    fn function() -> (FunctionData, crate::type_system::TypeSpec) {
        let mut types = TypeSystem::new();
        let integer = types.intern_type_spec("integer");
        (
            FunctionData::new(FunctionIdentity::TopLevel, None, Segment::Main, integer),
            integer,
        )
    }

    fn vreg(function: &mut FunctionData, ty: crate::type_system::TypeSpec) -> Register {
        Register::Virtual(function.allocate_register(ty, RegisterClass::Gpr).unwrap())
    }

    fn operation(uses: &[Register], definitions: &[Register]) -> IrInstruction {
        IrInstruction::Operation {
            uses: uses.to_vec(),
            definitions: definitions.to_vec(),
        }
    }

    #[test]
    fn normal_branch_executes_delay_slot_on_both_paths() {
        let (mut function, ty) = function();
        let condition = vreg(&mut function, ty);
        let delay_value = vreg(&mut function, ty);
        let label = function.allocate_label().unwrap();
        function.ir.push(operation(&[], &[condition]));
        function.ir.push(IrInstruction::Branch {
            condition: BranchCondition::NonZero(condition),
            target: label,
            delay: BranchDelay::Normal,
        });
        function.ir.push(operation(&[], &[delay_value]));
        function.ir.push(IrInstruction::Nop);
        function.bind_label(label).unwrap();
        function.ir.push(IrInstruction::Nop);

        let live = analyze_liveness(&function).unwrap();
        assert_eq!(live.successors[1], vec![2]);
        assert_eq!(live.successors[2], vec![4, 3]);
    }

    #[test]
    fn branch_likely_annuls_delay_slot_on_fallthrough() {
        let (mut function, ty) = function();
        let condition = vreg(&mut function, ty);
        let label = function.allocate_label().unwrap();
        function.ir.push(operation(&[], &[condition]));
        function.ir.push(IrInstruction::Branch {
            condition: BranchCondition::NonZero(condition),
            target: label,
            delay: BranchDelay::Likely,
        });
        function.ir.push(IrInstruction::Nop);
        function.ir.push(IrInstruction::Nop);
        function.bind_label(label).unwrap();
        function.ir.push(IrInstruction::Nop);

        let live = analyze_liveness(&function).unwrap();
        assert_eq!(live.successors[1], vec![2, 3]);
        assert_eq!(live.successors[2], vec![4]);
    }

    #[test]
    fn branch_requires_a_delay_slot() {
        let (mut function, _) = function();
        let label = function.allocate_label().unwrap();
        function.bind_label(label).unwrap();
        function.ir.push(IrInstruction::Branch {
            condition: BranchCondition::Always,
            target: label,
            delay: BranchDelay::Normal,
        });
        assert!(
            analyze_liveness(&function)
                .unwrap_err()
                .contains("no delay slot")
        );
    }

    #[test]
    fn copies_coalesce_and_delay_slot_copies_do_not_disappear() {
        let (mut function, ty) = function();
        let source = vreg(&mut function, ty);
        let copy = vreg(&mut function, ty);
        let delay_copy = vreg(&mut function, ty);
        let end = function.allocate_label().unwrap();
        function.ir.push(operation(&[], &[source]));
        function.ir.push(IrInstruction::Move {
            destination: copy,
            source,
            must_emit: false,
        });
        function.ir.push(IrInstruction::Branch {
            condition: BranchCondition::Always,
            target: end,
            delay: BranchDelay::Normal,
        });
        function.ir.push(IrInstruction::Move {
            destination: delay_copy,
            source: copy,
            must_emit: false,
        });
        function.bind_label(end).unwrap();
        function.ir.push(operation(&[delay_copy], &[]));

        let allocation = allocate_registers(&function).unwrap();
        assert!(!allocation.should_emit_copy(1).unwrap());
        assert!(allocation.should_emit_copy(3).unwrap());
    }

    #[test]
    fn value_live_across_call_uses_a_callee_saved_register() {
        let (mut function, ty) = function();
        let value = vreg(&mut function, ty);
        function.ir.push(operation(&[], &[value]));
        function.ir.push(IrInstruction::Call { argument_count: 0 });
        function.ir.push(operation(&[value], &[]));

        let allocation = allocate_registers(&function).unwrap();
        let assigned = allocation
            .physical_register(allocation.instructions[2].register_effects().uses[0])
            .unwrap();
        assert!(assigned.is_callee_saved());
        assert!(allocation.used_callee_saved.contains(&assigned));
    }

    #[test]
    fn parameter_precolors_unless_it_crosses_a_call() {
        let (mut simple, ty) = function();
        let parameter = vreg(&mut simple, ty);
        simple.ir.push(IrInstruction::Move {
            destination: parameter,
            source: Register::Physical(ARGUMENT_REGISTERS[0]),
            must_emit: false,
        });
        simple.ir.push(operation(&[parameter], &[]));
        let allocation = allocate_registers(&simple).unwrap();
        assert_eq!(allocation.assignments[0], ARGUMENT_REGISTERS[0]);

        let (mut crossing, ty) = function();
        let parameter = vreg(&mut crossing, ty);
        crossing.ir.push(IrInstruction::Move {
            destination: parameter,
            source: Register::Physical(ARGUMENT_REGISTERS[0]),
            must_emit: false,
        });
        crossing.ir.push(IrInstruction::Call { argument_count: 0 });
        crossing.ir.push(operation(&[parameter], &[]));
        let allocation = allocate_registers(&crossing).unwrap();
        assert_ne!(allocation.assignments[0], ARGUMENT_REGISTERS[0]);
        assert!(allocation.assignments[0].is_callee_saved());
    }

    #[test]
    fn call_clobbers_all_temporary_float_registers() {
        let effects = IrInstruction::Call { argument_count: 0 }.register_effects();
        assert!(
            effects
                .definitions
                .contains(&Register::Physical(PhysicalRegister::Fpr(19)))
        );
        assert!(
            !effects
                .definitions
                .contains(&Register::Physical(PhysicalRegister::Fpr(30)))
        );
        assert!(
            effects
                .definitions
                .contains(&Register::Physical(PhysicalRegister::Gpr(Gpr::T8)))
        );
    }

    #[test]
    fn empty_function_allocates_nothing() {
        let (function, _) = function();
        let allocation = allocate_registers(&function).unwrap();
        assert!(allocation.assignments.is_empty());
        assert!(allocation.instructions.is_empty());
        assert_eq!(function.labels, LabelTable::default());
    }

    #[test]
    fn disconnected_definitions_become_separate_webs() {
        let (mut function, ty) = function();
        let value = vreg(&mut function, ty);
        function.ir.push(operation(&[], &[value]));
        function.ir.push(operation(&[], &[value]));
        function.ir.push(operation(&[value], &[]));

        let allocation = allocate_registers(&function).unwrap();
        assert_eq!(allocation.registers.len(), 2);
        let Register::Virtual(first) = allocation.instructions[0].register_effects().definitions[0]
        else {
            unreachable!()
        };
        let Register::Virtual(second) =
            allocation.instructions[1].register_effects().definitions[0]
        else {
            unreachable!()
        };
        let Register::Virtual(used) = allocation.instructions[2].register_effects().uses[0] else {
            unreachable!()
        };
        assert_ne!(first, second);
        assert_eq!(second, used);
    }

    #[test]
    fn lexical_reservation_keeps_one_web_and_blocks_scope_reuse() {
        let (mut function, ty) = function();
        let reserved = vreg(&mut function, ty);
        let temporary = vreg(&mut function, ty);

        function.ir.push(operation(&[], &[reserved]));
        function.ir.push(operation(&[reserved], &[]));
        function.ir.push(operation(&[], &[temporary]));
        function.ir.push(operation(&[temporary], &[]));
        function.ir.push(operation(&[], &[reserved]));
        function.ir.push(operation(&[reserved], &[]));
        function.register_reservations.push(RegisterReservation {
            register: match reserved {
                Register::Virtual(register) => register,
                Register::Physical(_) => unreachable!(),
            },
            start: 0,
            end: function.ir.len(),
        });

        let allocation = allocate_registers(&function).unwrap();
        let Register::Virtual(first_reserved) =
            allocation.instructions[0].register_effects().definitions[0]
        else {
            unreachable!()
        };
        let Register::Virtual(second_reserved) =
            allocation.instructions[4].register_effects().definitions[0]
        else {
            unreachable!()
        };
        let Register::Virtual(temporary) =
            allocation.instructions[2].register_effects().definitions[0]
        else {
            unreachable!()
        };

        assert_eq!(first_reserved, second_reserved);
        assert_ne!(
            allocation.assignments[first_reserved.0 as usize],
            allocation.assignments[temporary.0 as usize]
        );
    }

    #[test]
    fn definitions_reaching_a_common_use_share_a_web() {
        let (mut function, ty) = function();
        let condition = vreg(&mut function, ty);
        let result = vreg(&mut function, ty);
        let alternate = function.allocate_label().unwrap();
        let join = function.allocate_label().unwrap();
        function.ir.push(operation(&[], &[condition]));
        function.ir.push(IrInstruction::Branch {
            condition: BranchCondition::NonZero(condition),
            target: alternate,
            delay: BranchDelay::Normal,
        });
        function.ir.push(IrInstruction::Nop);
        function.ir.push(operation(&[], &[result]));
        function.ir.push(IrInstruction::Branch {
            condition: BranchCondition::Always,
            target: join,
            delay: BranchDelay::Normal,
        });
        function.ir.push(IrInstruction::Nop);
        function.bind_label(alternate).unwrap();
        function.ir.push(operation(&[], &[result]));
        function.bind_label(join).unwrap();
        function.ir.push(operation(&[result], &[]));

        let allocation = allocate_registers(&function).unwrap();
        assert_eq!(allocation.registers.len(), 2);
        let first = allocation.instructions[3].register_effects().definitions[0];
        let second = allocation.instructions[6].register_effects().definitions[0];
        let used = allocation.instructions[7].register_effects().uses[0];
        assert_eq!(first, second);
        assert_eq!(second, used);
    }

    #[test]
    fn unreachable_definition_does_not_reach_an_unreachable_use() {
        let (mut function, ty) = function();
        let value = vreg(&mut function, ty);
        let end = function.allocate_label().unwrap();
        function.ir.push(IrInstruction::Branch {
            condition: BranchCondition::Always,
            target: end,
            delay: BranchDelay::Normal,
        });
        function.ir.push(IrInstruction::Nop);
        function.ir.push(operation(&[], &[value]));
        function.ir.push(operation(&[value], &[]));
        function.bind_label(end).unwrap();
        function.ir.push(IrInstruction::Nop);

        let allocation = allocate_registers(&function).unwrap();
        let definition = allocation.instructions[2].register_effects().definitions[0];
        let used = allocation.instructions[3].register_effects().uses[0];
        assert_ne!(definition, used);
        assert_eq!(allocation.registers.len(), 2);
    }

    #[test]
    fn conditional_move_joins_its_incoming_and_outgoing_destination() {
        let (mut function, ty) = function();
        let initial = vreg(&mut function, ty);
        let alternate = vreg(&mut function, ty);
        let condition = vreg(&mut function, ty);
        let result = vreg(&mut function, ty);
        function
            .ir
            .push(operation(&[], &[initial, alternate, condition]));
        function.ir.push(IrInstruction::Move {
            destination: result,
            source: initial,
            must_emit: false,
        });
        function.ir.push(IrInstruction::ConditionalMove {
            operation: crate::ir::ConditionalMoveOperation::NonZero,
            destination: result,
            source: alternate,
            condition,
        });
        function.ir.push(operation(&[result], &[]));

        let allocation = allocate_registers(&function).unwrap();
        let incoming = allocation.instructions[1].register_effects().definitions[0];
        let effects = allocation.instructions[2].register_effects();
        assert_eq!(incoming, effects.uses[0]);
        assert_eq!(effects.uses[0], effects.definitions[0]);
    }

    #[test]
    fn reports_register_pressure_instead_of_spilling() {
        let (mut function, ty) = function();
        let values: Vec<_> = (0..GPR_ALLOCATION_ORDER.len() + 1)
            .map(|_| vreg(&mut function, ty))
            .collect();
        for &value in &values {
            function.ir.push(operation(&[], &[value]));
        }
        function.ir.push(operation(&values, &[]));

        assert!(
            allocate_registers(&function)
                .unwrap_err()
                .contains("no hardware register available")
        );
    }
}
