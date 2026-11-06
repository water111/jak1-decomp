use super::{EeBuild, EeObject};

const CRT0: EeObject = EeObject {
    source: "crt0.S",
    name: "crt0",
    debug_name: None,
    defines: &[],
};

pub(crate) const STARTUP: EeBuild = EeBuild {
    source_dir: "sce/ee/startup",
    output_dir: "sce/ee/startup",
    defines: &[],
    preprocessor_flags: "-P",
    compiler_flags: "-O0",
    debug_dir: None,
    debug_source_root: None,
    assembler_flags: "",
    compile_dir: "",
    small_data: 0,
    filename: "startup.a",
    sources: &[CRT0],
    extra_ninja: "",
    extra_objects: &[],
    headers: &[],
};
