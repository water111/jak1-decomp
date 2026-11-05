use std::{fs, io, path::Path};

#[derive(Clone, Copy)]
pub(super) struct EeObject {
    pub source: &'static str,
    pub name: &'static str,
    pub defines: &'static [&'static str],
}

pub(super) struct EeBuild {
    pub source_dir: &'static str,
    pub output_dir: &'static str,
    pub defines: &'static [&'static str],
    pub compile_dir: &'static str,
    pub small_data: u8,
    pub filename: &'static str,
    pub sources: &'static [EeObject],
    pub headers: &'static [&'static str],
}

pub(super) const LIBDMA: EeBuild = EeBuild {
    source_dir: "sce/ee/libdma",
    output_dir: "sce/ee/libdma",
    defines: &[],
    compile_dir: "",
    small_data: 0,
    filename: "libdma.a",
    sources: &[EeObject {
        source: "libdma.c",
        name: "libdma",
        defines: &[],
    }],
    headers: &[
        "sce/ee/libdma/libdma.h",
        "sce/ee/eeregs.h",
        "sce/ee/newlib/stdio.h",
        "sce/ee/newlib/_ansi.h",
        "sce/ee/newlib/sys/config.h",
        "sce/ee/newlib/sys/reent.h",
        "sce/ee/newlib/time.h",
        "sce/ee/newlib/machine/time.h",
        "sce/ee/newlib/machine/types.h",
        "sce/ee/newlib/stddef.h",
        "sce/ee/newlib/stdarg.h",
        "sce/ee/newlib/va-mips.h",
    ],
};

pub(super) const LIBSCF: EeBuild = EeBuild {
    source_dir: "sce/ee/libscf",
    output_dir: "sce/ee/libscf",
    defines: &[],
    compile_dir: "",
    small_data: 0,
    filename: "libscf.a",
    sources: &[EeObject {
        source: "libscf.c",
        name: "libscf",
        defines: &[],
    }],
    headers: &[
        "sce/ee/libscf/libscf.h",
        "sce/ee/libcdvd/libcdvd.h",
        "sce/ee/libkernl/eekernel.h",
        "sce/ee/libkernl/sifdev.h",
        "sce/ee/newlib/assert.h",
        "sce/ee/newlib/stdio.h",
        "sce/ee/newlib/_ansi.h",
        "sce/ee/newlib/sys/config.h",
        "sce/ee/newlib/sys/reent.h",
        "sce/ee/newlib/time.h",
        "sce/ee/newlib/machine/time.h",
        "sce/ee/newlib/machine/types.h",
        "sce/ee/newlib/stddef.h",
        "sce/ee/newlib/stdarg.h",
        "sce/ee/newlib/va-mips.h",
    ],
};

pub(super) const LIBMC: EeBuild = EeBuild {
    source_dir: "sce/ee/libmc",
    output_dir: "sce/ee/libmc",
    defines: &[
        "__mips__",
        "__mips64",
        "__mips_eabi",
        "__mips_single_float",
        "__SIZE_TYPE__=unsigned int",
        "__PTRDIFF_TYPE__=int",
    ],
    compile_dir: "build",
    small_data: 0,
    filename: "libmc.a",
    sources: &[EeObject {
        source: "libmc.c",
        name: "libmc",
        defines: &[],
    }],
    headers: &[
        "sce/ee/libmc/libmc.h",
        "sce/ee/libkernl/eekernel.h",
        "sce/ee/libkernl/sif.h",
        "sce/ee/newlib/string.h",
        "sce/ee/newlib/stdio.h",
        "sce/ee/newlib/_ansi.h",
        "sce/ee/newlib/sys/config.h",
        "sce/ee/newlib/sys/reent.h",
        "sce/ee/newlib/time.h",
        "sce/ee/newlib/machine/time.h",
        "sce/ee/newlib/machine/types.h",
        "sce/ee/newlib/stddef.h",
        "sce/ee/newlib/stdarg.h",
        "sce/ee/newlib/va-mips.h",
    ],
};

pub(super) const LIBSCEDEMO: EeBuild = EeBuild {
    source_dir: "sce/ee/libscedemo",
    output_dir: "sce/ee/libscedemo",
    defines: &[],
    compile_dir: "",
    small_data: 8,
    filename: "libscedemo.a",
    sources: &[EeObject {
        source: "libscedemo.c",
        name: "libscedemo",
        defines: &[],
    }],
    headers: &[
        "sce/ee/libscedemo/libscedemo.h",
        "sce/ee/eeregs.h",
        "sce/ee/libkernl/eekernel.h",
        "sce/ee/newlib/stdio.h",
        "sce/ee/newlib/_ansi.h",
        "sce/ee/newlib/sys/config.h",
        "sce/ee/newlib/sys/reent.h",
        "sce/ee/newlib/time.h",
        "sce/ee/newlib/machine/time.h",
        "sce/ee/newlib/machine/types.h",
        "sce/ee/newlib/stddef.h",
        "sce/ee/newlib/stdarg.h",
        "sce/ee/newlib/va-mips.h",
    ],
};

const GRAPH_OBJECTS: &[EeObject] = &[
    EeObject {
        source: "graphdev.c",
        name: "resetgraph",
        defines: &["F_resetgraph"],
    },
    EeObject {
        source: "graphdev.c",
        name: "resetpath",
        defines: &["F_resetpath"],
    },
    EeObject {
        source: "graphdev.c",
        name: "putdispenv",
        defines: &["F_putdispenv"],
    },
    EeObject {
        source: "graphdev.c",
        name: "syncv",
        defines: &["F_syncv"],
    },
    EeObject {
        source: "graphdev.c",
        name: "syncpath",
        defines: &["F_syncpath"],
    },
    EeObject {
        source: "graphdev.c",
        name: "setdefloadimage",
        defines: &["F_setdefloadimage"],
    },
    EeObject {
        source: "graphdev.c",
        name: "execloadimage",
        defines: &["F_execloadimage"],
    },
    EeObject {
        source: "graphdev.c",
        name: "execstoreimage",
        defines: &["F_execstoreimage"],
    },
    EeObject {
        source: "graphdev.c",
        name: "putimr",
        defines: &["F_putimr"],
    },
    EeObject {
        source: "graphdev.c",
        name: "getimr",
        defines: &["F_getimr"],
    },
];

pub(super) const LIBGRAPH: EeBuild = EeBuild {
    source_dir: "sce/ee/libgraph",
    output_dir: "sce/ee/libgraph",
    defines: &[],
    compile_dir: "",
    small_data: 0,
    filename: "libgraph.a",
    sources: GRAPH_OBJECTS,
    headers: &[
        "sce/ee/libgraph/libgraph.h",
        "sce/ee/eeasm.h",
        "sce/ee/eeregs.h",
        "sce/ee/libkernl/eekernel.h",
        "sce/ee/newlib/stdio.h",
        "sce/ee/newlib/_ansi.h",
        "sce/ee/newlib/sys/config.h",
        "sce/ee/newlib/sys/reent.h",
        "sce/ee/newlib/time.h",
        "sce/ee/newlib/machine/time.h",
        "sce/ee/newlib/machine/types.h",
        "sce/ee/newlib/stddef.h",
        "sce/ee/newlib/stdarg.h",
        "sce/ee/newlib/va-mips.h",
    ],
};

pub(super) const LIBGRAPH_EARLY: EeBuild = EeBuild {
    sources: &[
        GRAPH_OBJECTS[0],
        GRAPH_OBJECTS[1],
        GRAPH_OBJECTS[2],
        GRAPH_OBJECTS[3],
        GRAPH_OBJECTS[4],
        GRAPH_OBJECTS[7],
        GRAPH_OBJECTS[8],
        GRAPH_OBJECTS[9],
    ],
    ..LIBGRAPH
};

pub(super) fn ninja_section(
    repo_root: &Path,
    build_dir: &Path,
    builds: &[EeBuild],
) -> io::Result<String> {
    let mut ninja = String::from(
        r#"
# EE libraries
ee_tool_dir = $repo_root/toolchain/bin/ee

rule ee_preprocess
  command = cd "$source_dir" && "$ee_tool_dir/cpp" -nostdinc -I"$repo_root" $defines "$source_name" "$build_dir/$out"
  description = EE CPP $out
rule ee_compile
  command = cd "$build_dir/$source_workdir" && "$ee_tool_dir/cc1" -quiet -O2 -G $small_data "$source_name" -o "$build_dir/$out"
  description = EE CC $out
rule ee_assemble
  command = "$ee_tool_dir/ee-as" -G $small_data -mabi=eabi -o "$out" "$in"
  description = EE AS $out
rule ee_archive
  command = rm -f "$out" && "$ee_tool_dir/ee-ar" rcs "$out" $in
  description = EE AR $out

"#,
    );
    for build in builds {
        let output_dir = build.output_dir;
        let headers = build
            .headers
            .iter()
            .map(|header| format!("$repo_root/{header}"))
            .collect::<Vec<_>>()
            .join(" ");
        let mut objects = Vec::new();
        for source in build.sources {
            let defines = build
                .defines
                .iter()
                .chain(source.defines.iter())
                .map(|define| format!("'-D{define}'"))
                .collect::<Vec<_>>()
                .join(" ");
            let source_path = Path::new(source.source);
            let source_name = source_path.file_name().unwrap().to_str().unwrap();
            let parent = source_path.parent().unwrap();
            let source_dir = repo_root.join(build.source_dir).join(parent);
            // Preserve relative source names in the compiler metadata.
            let input_dir = Path::new(output_dir).join("input").join(parent);
            let preprocess_dir = input_dir.join(build.compile_dir);
            fs::create_dir_all(build_dir.join(&preprocess_dir))?;
            let input = build_dir.join(input_dir).join(source_name);
            if input.symlink_metadata().is_err() {
                std::os::unix::fs::symlink(source_dir.join(source_name), &input)?;
            }
            let source_workdir = Path::new(output_dir)
                .join("src")
                .join(source.name)
                .join(parent);
            fs::create_dir_all(build_dir.join(&source_workdir))?;
            let preprocessed = source_workdir.join(source_name);
            let source_workdir = source_workdir.join(build.compile_dir);
            fs::create_dir_all(build_dir.join(&source_workdir))?;
            let compile_name = format!(
                "{}{source_name}",
                "../".repeat(Path::new(build.compile_dir).components().count())
            );
            let assembly = Path::new(output_dir).join(format!("{}.s", source.name));
            let object = Path::new(output_dir).join(format!("{}.o", source.name));
            fs::create_dir_all(build_dir.join(object.parent().unwrap()))?;
            ninja.push_str(&format!(
                "build {preprocessed}: ee_preprocess {source_dir}/{source_name} | {headers} $ee_tool_dir/cpp
  defines = {defines}
  source_dir = $build_dir/{preprocess_dir}
  source_name = {compile_name}
build {assembly}: ee_compile {preprocessed} | $ee_tool_dir/cc1
  small_data = {small_data}
  source_workdir = {source_workdir}
  source_name = {compile_name}
build {object}: ee_assemble {assembly} | $ee_tool_dir/ee-as
  small_data = {small_data}
",
                small_data = build.small_data,
                preprocessed = preprocessed.display(),
                source_dir = source_dir.display(),
                preprocess_dir = preprocess_dir.display(),
                source_workdir = source_workdir.display(),
                assembly = assembly.display(),
                object = object.display(),
            ));
            objects.push(object.display().to_string());
        }
        ninja.push_str(&format!(
            "build {output_dir}/{}: ee_archive {} | $ee_tool_dir/ee-ar\n",
            build.filename,
            objects.join(" "),
        ));
    }
    Ok(ninja)
}
