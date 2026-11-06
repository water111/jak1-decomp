use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};
use std::fs;
use std::path::{Path, PathBuf};

use crate::diagnostic::Diagnostic;
pub use crate::diagnostic::{Diagnostic as CompilerMessage, DiagnosticLevel as MessageLevel};
use crate::frontend::Frontend;
use crate::goos::Interpreter;
use crate::object_file::ObjectFileOptions;
use crate::type_system::{BUILTIN_FILE_ID, TypeSystem, Visibility};

// The Driver is the top-level compiler entry point.
// The driver itself handles build ordering logic.

// The project file defines all source files and their dependencies.
// The compiler builds the project by compiling each file in dependency order.
// Incremental builds work by:
// - propagate dirty status to dependent files
// - remove all definitions from dirty files
// - compile the dirty files, clearing dirty status on success.
// This is essentially rewinding the compilation for part of the build graph and replaying it.
//
// The rewind/replay operates on the true dependency graph, not the build order.

// integer ID used to represent a file in dependency tracking.
pub type FileId = u32;

// output of compiling a single file.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct CompileOutput {
    pub messages: Vec<CompilerMessage>,
    pub object: Option<Vec<u8>>,
}

// file in the project dependency graph.
#[derive(Clone, Debug)]
struct SourceFile {
    id: FileId,
    path: PathBuf,
    dependencies: BTreeSet<FileId>,
    dirty: bool,
}

// result of parsing the project description file.
#[derive(Clone, Debug)]
struct ProjectDescription {
    files: BTreeMap<PathBuf, Vec<String>>,
    groups: BTreeMap<String, Vec<PathBuf>>,
}

impl ProjectDescription {
    // Parse the project description.
    fn parse(text: &str) -> Result<Self, String> {
        let mut files = BTreeMap::<PathBuf, Vec<String>>::new();
        let mut groups = BTreeMap::<String, Vec<PathBuf>>::new();

        for (line_index, untrimmed_line) in text.lines().enumerate() {
            let line_number = line_index + 1;
            let line = untrimmed_line
                .split_once('#')
                .map_or(untrimmed_line, |(before, _)| before)
                .trim();
            if line.is_empty() {
                continue;
            }

            let words: Vec<&str> = line.split_whitespace().collect();
            match words[0] {
                "file" => {
                    if words.len() < 2 {
                        return Err(project_line_error(
                            line_number,
                            "`file` requires a source path",
                        ));
                    }
                    let path = PathBuf::from(words[1]);
                    if files.contains_key(&path) {
                        return Err(project_line_error(
                            line_number,
                            format!("duplicate file `{}`", path.display()),
                        ));
                    }
                    files.insert(
                        path,
                        words[2..].iter().map(|word| (*word).to_owned()).collect(),
                    );
                }
                "group" => {
                    if words.len() < 2 {
                        return Err(project_line_error(line_number, "`group` requires a name"));
                    }
                    let name = words[1].to_owned();
                    if groups.contains_key(&name) {
                        return Err(project_line_error(
                            line_number,
                            format!("duplicate group `{name}`"),
                        ));
                    }
                    let members = words[2..].iter().map(PathBuf::from).collect();
                    groups.insert(name, members);
                }
                directive => {
                    return Err(project_line_error(
                        line_number,
                        format!("unknown directive `{directive}`"),
                    ));
                }
            }
        }

        if files.is_empty() {
            return Err("project: the project contains no files".to_owned());
        }

        for name in groups.keys() {
            if files.contains_key(Path::new(name)) {
                return Err(format!("project: `{name}` is both a file and a group"));
            }
        }
        for (name, members) in &groups {
            for member in members {
                if !files.contains_key(member) {
                    return Err(format!(
                        "project: group `{name}` refers to unknown file `{}`",
                        member.display()
                    ));
                }
            }
        }
        for (file, references) in &files {
            for reference in references {
                if !files.contains_key(Path::new(reference)) && !groups.contains_key(reference) {
                    return Err(format!(
                        "project: file `{}` depends on unknown file or group `{reference}`",
                        file.display()
                    ));
                }
            }
        }

        Ok(Self { files, groups })
    }

    // Expand dependency groups.
    fn expanded_dependencies(&self, file: &Path) -> BTreeSet<PathBuf> {
        let mut result = BTreeSet::new();
        for reference in &self.files[file] {
            if let Some(group) = self.groups.get(reference) {
                result.extend(group.iter().cloned());
            } else {
                result.insert(PathBuf::from(reference));
            }
        }
        result
    }
}

fn project_line_error(line: usize, message: impl AsRef<str>) -> String {
    format!("project line {line}: {}", message.as_ref())
}

// Main compiler struct.
#[derive(Debug)]
pub struct Driver {
    project_file: PathBuf,
    source_directory: PathBuf,
    build_directory: PathBuf,
    files: BTreeMap<FileId, SourceFile>,
    ids_by_path: BTreeMap<PathBuf, FileId>,
    reverse_dependencies: BTreeMap<FileId, BTreeSet<FileId>>,
    next_file_id: FileId,
    goos: Interpreter,
    types: TypeSystem,
    frontend: Frontend,
}

impl Driver {
    // initialize the compiler given the project file and build directory.
    pub fn load(project_file: PathBuf, build_directory: PathBuf) -> Result<Self, String> {
        // load the project file
        let source_directory = project_file
            .parent()
            .unwrap_or_else(|| Path::new("."))
            .to_path_buf();
        let text = fs::read_to_string(&project_file)
            .map_err(|error| format!("{}: {error}", project_file.display()))?;
        let description = ProjectDescription::parse(&text)?;

        for path in description.files.keys() {
            let full_path = source_directory.join(path);
            if !full_path.is_file() {
                return Err(format!(
                    "project: source file `{}` does not exist",
                    full_path.display()
                ));
            }
        }

        // create driver
        let mut types = TypeSystem::new();
        let none_type = types.intern_type_spec("none");
        let mut driver = Self {
            project_file,
            source_directory,
            build_directory,
            files: BTreeMap::new(),
            ids_by_path: BTreeMap::new(),
            reverse_dependencies: BTreeMap::new(),
            next_file_id: 0,
            goos: Interpreter::new().map_err(|diagnostic| {
                format!("could not initialize GOOS: {}", diagnostic.message)
            })?,
            types,
            frontend: Frontend::new(none_type),
        };

        // add all source files
        for path in description.files.keys() {
            let id = driver.allocate_file_id()?;
            driver.ids_by_path.insert(path.clone(), id);
            driver.files.insert(
                id,
                SourceFile {
                    id,
                    path: path.clone(),
                    dependencies: BTreeSet::new(),
                    dirty: true,
                },
            );
        }

        // add direct dependencies
        for path in description.files.keys() {
            let id = driver.ids_by_path[path];
            driver.files.get_mut(&id).unwrap().dependencies = description
                .expanded_dependencies(path)
                .iter()
                .map(|dependency| driver.ids_by_path[dependency])
                .collect();
        }

        // set up graph
        driver.rebuild_reverse_dependencies();
        driver.compilation_order()?;
        Ok(driver)
    }

    pub fn goos(&self) -> &Interpreter {
        &self.goos
    }

    pub fn goos_mut(&mut self) -> &mut Interpreter {
        &mut self.goos
    }

    pub fn type_system(&self) -> &TypeSystem {
        &self.types
    }

    pub fn type_system_mut(&mut self) -> &mut TypeSystem {
        &mut self.types
    }

    pub fn frontend(&self) -> &Frontend {
        &self.frontend
    }

    pub fn frontend_mut(&mut self) -> &mut Frontend {
        &mut self.frontend
    }

    fn allocate_file_id(&mut self) -> Result<FileId, String> {
        let id = self.next_file_id;
        if id == BUILTIN_FILE_ID {
            return Err("project: exhausted source file IDs".to_owned());
        }
        self.next_file_id = self
            .next_file_id
            .checked_add(1)
            .ok_or_else(|| "project: exhausted source file IDs".to_owned())?;
        Ok(id)
    }

    pub fn reload_project(&mut self) -> Result<(), String> {
        let text = fs::read_to_string(&self.project_file)
            .map_err(|error| format!("{}: {error}", self.project_file.display()))?;
        let description = ProjectDescription::parse(&text)?;
        self.apply_project_edit(description)
    }

    fn apply_project_edit(&mut self, description: ProjectDescription) -> Result<(), String> {
        for path in description.files.keys() {
            let full_path = self.source_directory.join(path);
            if !full_path.is_file() {
                return Err(format!(
                    "project: source file `{}` does not exist",
                    full_path.display()
                ));
            }
        }

        let old_files = self.files.clone();
        let old_ids_by_path = self.ids_by_path.clone();
        let old_reverse = self.reverse_dependencies.clone();

        let mut new_ids_by_path = BTreeMap::new();
        for path in description.files.keys() {
            let id = match old_ids_by_path.get(path) {
                Some(id) => *id,
                None => self.allocate_file_id()?,
            };
            new_ids_by_path.insert(path.clone(), id);
        }

        let mut new_files = BTreeMap::new();
        for path in description.files.keys() {
            let id = new_ids_by_path[path];
            let dependencies = description
                .expanded_dependencies(path)
                .iter()
                .map(|dependency| new_ids_by_path[dependency])
                .collect();
            let dirty = old_files.get(&id).is_none_or(|file| file.dirty);
            new_files.insert(
                id,
                SourceFile {
                    id,
                    path: path.clone(),
                    dependencies,
                    dirty,
                },
            );
        }

        let new_reverse = make_reverse_dependencies(&new_files);
        validate_acyclic(&new_files)?;

        let removed: BTreeSet<FileId> = old_files
            .keys()
            .filter(|id| !new_files.contains_key(id))
            .copied()
            .collect();
        let mut changed = removed.clone();
        for (id, file) in &new_files {
            match old_files.get(id) {
                None => {
                    changed.insert(*id);
                }
                Some(old_file) if old_file.dependencies != file.dependencies => {
                    changed.insert(*id);
                }
                _ => {}
            }
        }

        let mut invalidated = dependent_closure(&changed, &old_reverse);
        invalidated.extend(dependent_closure(&changed, &new_reverse));

        for id in &removed {
            if let Some(file) = old_files.get(id) {
                self.remove_definitions(file);
            }
        }

        self.files = new_files;
        self.ids_by_path = new_ids_by_path;
        self.reverse_dependencies = new_reverse;
        for id in invalidated {
            if let Some(file) = self.files.get_mut(&id) {
                file.dirty = true;
            }
        }
        Ok(())
    }

    pub fn mark_dirty(&mut self, path: impl AsRef<Path>) -> Result<(), String> {
        let path = path.as_ref();
        let id = self
            .ids_by_path
            .get(path)
            .copied()
            .ok_or_else(|| format!("unknown source file `{}`", path.display()))?;
        let dirty = dependent_closure(&BTreeSet::from([id]), &self.reverse_dependencies);
        for dirty_id in dirty {
            self.files.get_mut(&dirty_id).unwrap().dirty = true;
        }
        Ok(())
    }

    pub fn rebuild_dirty(&mut self) -> Result<(), String> {
        let order = self.compilation_order()?;
        let dirty: Vec<FileId> = order
            .into_iter()
            .filter(|id| self.files[id].dirty)
            .collect();

        // All stale definitions are removed before any dirty source is compiled.
        for id in &dirty {
            let file = self.files[id].clone();
            self.remove_definitions(&file);
        }

        for id in dirty {
            let file = self.files[&id].clone();
            let dependency_ids = self.transitive_dependencies(id);
            let output = match self.compile(&file, &dependency_ids) {
                Ok(output) => output,
                Err(messages) => {
                    self.print_messages(&file.path, &messages);
                    let message = messages
                        .iter()
                        .find(|message| message.level == MessageLevel::Error)
                        .or_else(|| messages.first())
                        .map_or_else(
                            || "compiler reported an error".to_owned(),
                            |m| m.message.clone(),
                        );
                    return Err(format!(
                        "could not compile {}: {message}",
                        file.path.display()
                    ));
                }
            };
            self.print_messages(&file.path, &output.messages);
            if let Some(object) = output.object {
                self.write_object(&file.path, &object)?;
            }
            self.files.get_mut(&id).unwrap().dirty = false;
        }
        Ok(())
    }

    fn compilation_order(&self) -> Result<Vec<FileId>, String> {
        validate_acyclic(&self.files)?;
        let mut remaining_dependencies: BTreeMap<FileId, usize> = self
            .files
            .iter()
            .map(|(id, file)| (*id, file.dependencies.len()))
            .collect();
        let mut ready: BTreeSet<(PathBuf, FileId)> = remaining_dependencies
            .iter()
            .filter(|(_, count)| **count == 0)
            .map(|(id, _)| (self.files[id].path.clone(), *id))
            .collect();
        let mut order = Vec::with_capacity(self.files.len());

        while let Some((_, id)) = ready.pop_first() {
            order.push(id);
            if let Some(dependents) = self.reverse_dependencies.get(&id) {
                for dependent in dependents {
                    let count = remaining_dependencies.get_mut(dependent).unwrap();
                    *count -= 1;
                    if *count == 0 {
                        ready.insert((self.files[dependent].path.clone(), *dependent));
                    }
                }
            }
        }
        Ok(order)
    }

    fn transitive_dependencies(&self, id: FileId) -> Vec<FileId> {
        fn visit(id: FileId, files: &BTreeMap<FileId, SourceFile>, found: &mut HashSet<FileId>) {
            for dependency in &files[&id].dependencies {
                if found.insert(*dependency) {
                    visit(*dependency, files, found);
                }
            }
        }

        let mut found = HashSet::new();
        visit(id, &self.files, &mut found);
        self.compilation_order()
            .expect("the installed project is acyclic")
            .into_iter()
            .filter(|dependency| found.contains(dependency))
            .collect()
    }

    fn rebuild_reverse_dependencies(&mut self) {
        self.reverse_dependencies = make_reverse_dependencies(&self.files);
    }

    // Compiler boundary methods. The driver owns invalidation and output;
    // compiler subsystems own the definitions and generated object.
    fn remove_definitions(&mut self, source_file: &SourceFile) {
        self.types.remove_definitions(source_file.id);
    }

    fn compile(
        &mut self,
        source_file: &SourceFile,
        dependency_ids: &[FileId],
    ) -> Result<CompileOutput, Vec<CompilerMessage>> {
        debug_assert_eq!(self.files[&source_file.id].path, source_file.path);
        let path = self.source_directory.join(&source_file.path);
        let text = fs::read_to_string(&path)
            .map_err(|error| vec![Diagnostic::error(format!("{}: {error}", path.display()))])?;
        let parsed = self
            .goos
            .parser
            .parse(path, text)
            .map_err(|diagnostic| vec![diagnostic])?;

        let visibility = Visibility::new(source_file.id, dependency_ids.iter().copied());
        let file = self
            .frontend
            .compile_file(
                source_file.id,
                parsed.source_id,
                visibility,
                &parsed.objects,
            )
            .map_err(|diagnostic| vec![diagnostic])?;
        let functions =
            crate::codegen::generate_file(&file).map_err(|error| vec![Diagnostic::error(error)])?;
        let name = source_file
            .path
            .file_stem()
            .and_then(|name| name.to_str())
            .ok_or_else(|| vec![Diagnostic::error("source file has no UTF-8 object name")])?;
        let object = crate::object_file::write_v3(
            name,
            &functions,
            &file.static_data,
            &self.goos.parser.objects.symbols,
            ObjectFileOptions::default(),
        )
        .map_err(|error| vec![Diagnostic::error(error)])?;
        Ok(CompileOutput {
            messages: Vec::new(),
            object: Some(object),
        })
    }

    pub fn repl_compile(&mut self, _input: &str) -> Result<(), String> {
        Err("GOAL REPL compilation is not implemented yet".to_owned())
    }

    fn write_object(&self, source_path: &Path, contents: &[u8]) -> Result<(), String> {
        let mut output_path = self.build_directory.join(source_path);
        output_path.set_extension("o");
        if let Some(parent) = output_path.parent() {
            fs::create_dir_all(parent).map_err(|error| format!("{}: {error}", parent.display()))?;
        }
        fs::write(&output_path, contents)
            .map_err(|error| format!("{}: {error}", output_path.display()))
    }

    fn print_messages(&self, source_path: &Path, messages: &[CompilerMessage]) {
        for message in messages {
            if message.span.is_some() {
                match self.goos.parser.sources.render_diagnostic(message) {
                    Ok(rendered) => {
                        eprintln!("{rendered}");
                        continue;
                    }
                    Err(error) => eprintln!("{error}"),
                }
            }
            let level = match message.level {
                MessageLevel::Warning => "warning",
                MessageLevel::Error => "error",
            };
            eprintln!("{}: {level}: {}", source_path.display(), message.message);
        }
    }
}

fn make_reverse_dependencies(
    files: &BTreeMap<FileId, SourceFile>,
) -> BTreeMap<FileId, BTreeSet<FileId>> {
    let mut reverse: BTreeMap<FileId, BTreeSet<FileId>> =
        files.keys().map(|id| (*id, BTreeSet::new())).collect();
    for (id, file) in files {
        for dependency in &file.dependencies {
            reverse.entry(*dependency).or_default().insert(*id);
        }
    }
    reverse
}

fn dependent_closure(
    roots: &BTreeSet<FileId>,
    reverse_dependencies: &BTreeMap<FileId, BTreeSet<FileId>>,
) -> BTreeSet<FileId> {
    let mut result = roots.clone();
    let mut pending: Vec<FileId> = roots.iter().copied().collect();
    while let Some(id) = pending.pop() {
        if let Some(dependents) = reverse_dependencies.get(&id) {
            for dependent in dependents {
                if result.insert(*dependent) {
                    pending.push(*dependent);
                }
            }
        }
    }
    result
}

fn validate_acyclic(files: &BTreeMap<FileId, SourceFile>) -> Result<(), String> {
    #[derive(Clone, Copy, Eq, PartialEq)]
    enum Visit {
        Active,
        Complete,
    }

    fn visit(
        id: FileId,
        files: &BTreeMap<FileId, SourceFile>,
        visits: &mut HashMap<FileId, Visit>,
        stack: &mut Vec<FileId>,
    ) -> Result<(), String> {
        if visits.get(&id) == Some(&Visit::Complete) {
            return Ok(());
        }
        if visits.get(&id) == Some(&Visit::Active) {
            let beginning = stack.iter().position(|candidate| *candidate == id).unwrap();
            let mut cycle: Vec<String> = stack[beginning..]
                .iter()
                .map(|cycle_id| files[cycle_id].path.display().to_string())
                .collect();
            cycle.push(files[&id].path.display().to_string());
            return Err(format!("dependency cycle -> {}", cycle.join(" -> ")));
        }

        visits.insert(id, Visit::Active);
        stack.push(id);
        for dependency in &files[&id].dependencies {
            visit(*dependency, files, visits, stack)?;
        }
        stack.pop();
        visits.insert(id, Visit::Complete);
        Ok(())
    }

    let mut visits = HashMap::new();
    let mut stack = Vec::new();
    for id in files.keys() {
        visit(*id, files, &mut visits, &mut stack)?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    use crate::static_data::Segment;
    use crate::type_system::{TypeDefinition, TypeKind, TypeLayout, Visibility};

    struct TestDirectory(PathBuf);

    impl TestDirectory {
        fn new() -> Self {
            let nonce = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_nanos();
            let path =
                std::env::temp_dir().join(format!("goal-driver-{}-{nonce}", std::process::id()));
            fs::create_dir(&path).unwrap();
            Self(path)
        }

        fn write(&self, path: &str, contents: &str) {
            let path = self.0.join(path);
            if let Some(parent) = path.parent() {
                fs::create_dir_all(parent).unwrap();
            }
            fs::write(path, contents).unwrap();
        }

        fn driver(&self, project: &str) -> Driver {
            self.write("project.goalproj", project);
            Driver::load(self.0.join("project.goalproj"), self.0.join("build")).unwrap()
        }
    }

    impl Drop for TestDirectory {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    fn dirty_paths(driver: &Driver) -> BTreeSet<PathBuf> {
        driver
            .files
            .values()
            .filter(|file| file.dirty)
            .map(|file| file.path.clone())
            .collect()
    }

    #[test]
    fn parses_groups_and_orders_dependencies_first() {
        let directory = TestDirectory::new();
        directory.write("math/vector.gc", "; test");
        directory.write("math/matrix.gc", "; test");
        directory.write("main.gc", "; test");
        let driver = directory.driver(
            "\
             file math/vector.gc\n\
             file math/matrix.gc math/vector.gc\n\
             group math math/vector.gc math/matrix.gc\n\
             file main.gc math\n",
        );

        let order: Vec<PathBuf> = driver
            .compilation_order()
            .unwrap()
            .iter()
            .map(|id| driver.files[id].path.clone())
            .collect();
        assert_eq!(
            order,
            ["math/vector.gc", "math/matrix.gc", "main.gc"].map(PathBuf::from)
        );
        let main_id = driver.ids_by_path[Path::new("main.gc")];
        assert_eq!(driver.transitive_dependencies(main_id).len(), 2);
    }

    #[test]
    fn rejects_unknown_references_and_cycles() {
        let unknown = ProjectDescription::parse("file a.gc missing.gc").unwrap_err();
        assert!(unknown.to_string().contains("unknown file or group"));

        let directory = TestDirectory::new();
        directory.write("a.gc", "; test");
        directory.write("b.gc", "; test");
        directory.write("project.goalproj", "file a.gc b.gc\nfile b.gc a.gc\n");
        let cycle = Driver::load(
            directory.0.join("project.goalproj"),
            directory.0.join("build"),
        )
        .unwrap_err();
        assert!(cycle.to_string().contains("a.gc -> b.gc -> a.gc"));
    }

    #[test]
    fn accepts_paths_as_written() {
        let description =
            ProjectDescription::parse("file ../shared.gc\nfile /absolute/main.gc ../shared.gc\n")
                .unwrap();

        assert!(description.files.contains_key(Path::new("../shared.gc")));
        assert!(
            description
                .files
                .contains_key(Path::new("/absolute/main.gc"))
        );
    }

    #[test]
    fn dirtiness_propagates_and_successful_rebuild_clears_it() {
        let directory = TestDirectory::new();
        for name in ["a.gc", "b.gc", "c.gc", "unrelated.gc"] {
            directory.write(name, "; test");
        }
        let mut driver = directory.driver(
            "\
             file a.gc\n\
             file b.gc a.gc\n\
             file c.gc b.gc\n\
             file unrelated.gc\n",
        );
        driver.rebuild_dirty().unwrap();
        assert!(dirty_paths(&driver).is_empty());

        driver.mark_dirty("a.gc").unwrap();
        assert_eq!(
            dirty_paths(&driver),
            ["a.gc", "b.gc", "c.gc"].map(PathBuf::from).into()
        );
        driver.rebuild_dirty().unwrap();
        assert!(dirty_paths(&driver).is_empty());
    }

    #[test]
    fn failed_file_and_later_files_stay_dirty() {
        let directory = TestDirectory::new();
        directory.write("a.gc", "; test");
        directory.write("b.gc", "; test");
        let mut driver = directory.driver("file a.gc\nfile b.gc a.gc\n");
        fs::remove_file(directory.0.join("a.gc")).unwrap();

        assert!(driver.rebuild_dirty().is_err());
        assert_eq!(
            dirty_paths(&driver),
            ["a.gc", "b.gc"].map(PathBuf::from).into()
        );
    }

    #[test]
    fn unsupported_nonempty_file_is_not_reported_as_compiled() {
        let directory = TestDirectory::new();
        directory.write("a.gc", "(unsupported)");
        let mut driver = directory.driver("file a.gc\n");

        let error = driver.rebuild_dirty().unwrap_err();
        assert!(error.contains("IR generation is not implemented"));
        assert_eq!(dirty_paths(&driver), [PathBuf::from("a.gc")].into());
        assert!(!directory.0.join("build/a.o").exists());
    }

    #[test]
    fn invalidation_removes_file_owned_type_records_but_keeps_type_ids() {
        let directory = TestDirectory::new();
        directory.write("a.gc", "; test");
        let mut driver = directory.driver("file a.gc\n");
        driver.rebuild_dirty().unwrap();
        let file = driver.ids_by_path[Path::new("a.gc")];
        let visibility = Visibility::new(file, []);
        let structure = driver.types.intern_type("structure");
        let transient = driver.types.intern_type("transient");
        let spec = driver.types.intern_type_spec("transient");
        driver
            .types
            .define_type(
                file,
                TypeDefinition::new(
                    transient,
                    Some(structure),
                    TypeLayout::structure(TypeKind::Structure, false),
                ),
                &visibility,
            )
            .unwrap();

        driver.mark_dirty("a.gc").unwrap();
        driver.rebuild_dirty().unwrap();
        assert!(driver.types.definition(transient, &visibility).is_err());
        assert_eq!(driver.types.intern_type("transient"), transient);
        assert_eq!(driver.types.intern_type_spec("transient"), spec);
    }

    #[test]
    fn project_edits_use_old_and_new_reverse_graphs_and_preserve_ids() {
        let directory = TestDirectory::new();
        for name in ["a.gc", "b.gc", "c.gc", "d.gc"] {
            directory.write(name, "; test");
        }
        let mut driver = directory.driver(
            "\
             file a.gc\n\
             file b.gc a.gc\n\
             file c.gc b.gc\n",
        );
        driver.rebuild_dirty().unwrap();
        let a_id = driver.ids_by_path[Path::new("a.gc")];

        directory.write(
            "project.goalproj",
            "\
             file a.gc\n\
             file b.gc\n\
             file c.gc b.gc\n\
             file d.gc b.gc\n",
        );
        driver.reload_project().unwrap();

        assert_eq!(driver.ids_by_path[Path::new("a.gc")], a_id);
        assert_eq!(
            dirty_paths(&driver),
            ["b.gc", "c.gc", "d.gc"].map(PathBuf::from).into()
        );
    }

    #[test]
    fn removing_a_file_invalidates_its_old_dependents() {
        let directory = TestDirectory::new();
        directory.write("a.gc", "; test");
        directory.write("b.gc", "; test");
        let mut driver = directory.driver("file a.gc\nfile b.gc a.gc\n");
        driver.rebuild_dirty().unwrap();

        directory.write("project.goalproj", "file b.gc\n");
        driver.reload_project().unwrap();
        assert_eq!(dirty_paths(&driver), [PathBuf::from("b.gc")].into());
        assert!(!driver.ids_by_path.contains_key(Path::new("a.gc")));
    }

    #[test]
    fn writes_objects_to_a_mirrored_build_path() {
        let directory = TestDirectory::new();
        directory.write("game/main.gc", "main");
        let driver = directory.driver("file game/main.gc\n");
        driver
            .write_object(Path::new("game/main.gc"), b"object")
            .unwrap();
        assert_eq!(
            fs::read(directory.0.join("build/game/main.o")).unwrap(),
            b"object"
        );
    }

    #[test]
    fn compiles_an_empty_file_through_the_backend() {
        let directory = TestDirectory::new();
        directory.write("gstring-h.gc", "");
        let mut driver = directory.driver("file gstring-h.gc\n");
        driver.rebuild_dirty().unwrap();
        let object = fs::read(directory.0.join("build/gstring-h.o")).unwrap();
        assert_eq!(object.len(), 192);
        let object = crate::goal_object::GoalObject::read(&object).unwrap();
        assert_eq!(object.name, "gstring-h");
        assert_eq!(object.segments[Segment::TopLevel.index()].len(), 24);
    }
}
