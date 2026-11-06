use std::{fs, io, path::Path};

mod newlib;
pub(super) use newlib::{LIBC, LIBC_EARLY, LIBM};

#[derive(Clone, Copy)]
pub(super) struct EeObject {
    pub source: &'static str,
    pub name: &'static str,
    pub debug_name: Option<&'static str>,
    pub defines: &'static [&'static str],
}

pub(super) struct EeBuild {
    pub source_dir: &'static str,
    pub output_dir: &'static str,
    pub defines: &'static [&'static str],
    pub compiler_flags: &'static str,
    pub debug_dir: Option<&'static str>,
    pub debug_source_root: Option<&'static str>,
    pub assembler_flags: &'static str,
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
    compiler_flags: "",
    debug_dir: None,
    debug_source_root: None,
    assembler_flags: "",
    compile_dir: "",
    small_data: 0,
    filename: "libdma.a",
    sources: &[EeObject {
        source: "libdma.c",
        name: "libdma",
        debug_name: None,
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
    compiler_flags: "",
    debug_dir: None,
    debug_source_root: None,
    assembler_flags: "",
    compile_dir: "",
    small_data: 0,
    filename: "libscf.a",
    sources: &[EeObject {
        source: "libscf.c",
        name: "libscf",
        debug_name: None,
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
    compiler_flags: "",
    debug_dir: None,
    debug_source_root: None,
    assembler_flags: "",
    compile_dir: "build",
    small_data: 0,
    filename: "libmc.a",
    sources: &[EeObject {
        source: "libmc.c",
        name: "libmc",
        debug_name: None,
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

pub(super) const LIBCDVD: EeBuild = EeBuild {
    source_dir: "sce/ee/libcdvd",
    output_dir: "sce/ee/libcdvd",
    defines: &[
        "__mips__",
        "__mips64",
        "__mips_eabi",
        "__mips_single_float",
        "__SIZE_TYPE__=unsigned int",
        "__PTRDIFF_TYPE__=int",
    ],
    compiler_flags: "-fno-strict-aliasing",
    debug_dir: None,
    debug_source_root: None,
    assembler_flags: "",
    compile_dir: "build",
    small_data: 0,
    filename: "libcdvd.a",
    sources: &[EeObject {
        source: "eecdvd.c",
        name: "eecdvd",
        debug_name: None,
        defines: &[],
    }],
    headers: &[
        "sce/ee/libcdvd/libcdvd.h",
        "sce/common/libcdvd/rpc.h",
        "sce/ee/memory.h",
        "sce/ee/eeasm.h",
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

pub(super) const LIBPAD: EeBuild = EeBuild {
    source_dir: "sce/ee/libpad",
    output_dir: "sce/ee/libpad",
    defines: &[
        "__mips__",
        "__mips64",
        "__mips_eabi",
        "__mips_single_float",
        "__SIZE_TYPE__=unsigned int",
        "__PTRDIFF_TYPE__=int",
    ],
    compiler_flags: "",
    debug_dir: None,
    debug_source_root: None,
    assembler_flags: "",
    compile_dir: "",
    small_data: 0,
    filename: "libpad.a",
    sources: &[EeObject {
        source: "libpad.c",
        name: "libpad",
        debug_name: None,
        defines: &["PAD_OPEN_ASM"],
    }],
    headers: &[
        "sce/ee/libpad/libpad.h",
        "sce/ee/libpad/port-open.inc",
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
    compiler_flags: "",
    debug_dir: None,
    debug_source_root: None,
    assembler_flags: "",
    compile_dir: "",
    small_data: 8,
    filename: "libscedemo.a",
    sources: &[EeObject {
        source: "libscedemo.c",
        name: "libscedemo",
        debug_name: None,
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
        debug_name: None,
        defines: &["F_resetgraph"],
    },
    EeObject {
        source: "graphdev.c",
        name: "resetpath",
        debug_name: None,
        defines: &["F_resetpath"],
    },
    EeObject {
        source: "graphdev.c",
        name: "putdispenv",
        debug_name: None,
        defines: &["F_putdispenv"],
    },
    EeObject {
        source: "graphdev.c",
        name: "syncv",
        debug_name: None,
        defines: &["F_syncv"],
    },
    EeObject {
        source: "graphdev.c",
        name: "syncpath",
        debug_name: None,
        defines: &["F_syncpath"],
    },
    EeObject {
        source: "graphdev.c",
        name: "setdefloadimage",
        debug_name: None,
        defines: &["F_setdefloadimage"],
    },
    EeObject {
        source: "graphdev.c",
        name: "execloadimage",
        debug_name: None,
        defines: &["F_execloadimage"],
    },
    EeObject {
        source: "graphdev.c",
        name: "execstoreimage",
        debug_name: None,
        defines: &["F_execstoreimage"],
    },
    EeObject {
        source: "graphdev.c",
        name: "putimr",
        debug_name: None,
        defines: &["F_putimr"],
    },
    EeObject {
        source: "graphdev.c",
        name: "getimr",
        debug_name: None,
        defines: &["F_getimr"],
    },
];

pub(super) const LIBGRAPH: EeBuild = EeBuild {
    source_dir: "sce/ee/libgraph",
    output_dir: "sce/ee/libgraph",
    defines: &[],
    compiler_flags: "",
    debug_dir: None,
    debug_source_root: None,
    assembler_flags: "",
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

pub(super) const LIBGCC: EeBuild = EeBuild {
    source_dir: "sce/ee/libgcc/src",
    output_dir: "sce/ee/libgcc",
    defines: &[
        "__SIZE_TYPE__=unsigned int",
        "__PTRDIFF_TYPE__=int",
        "__LONG_MAX__=9223372036854775807L",
        "__GNUC__=2",
        "__GNUC_MINOR__=9",
        "__mips=3",
        "__mips64",
        "__mips_eabi",
        "__mips_single_float",
        "__R5900__",
        "__MIPSEL__",
        "__mips__",
        "_MIPSEL",
        "__OPTIMIZE__",
        "IN_LIBGCC2",
        "__GCC_FLOAT_NOT_NEEDED",
        "CROSS_COMPILE",
        "inhibit_libc",
    ],
    compiler_flags: "-g1",
    debug_dir: Some("/usr/local/sce/ee/gcc/build/gcc"),
    debug_source_root: None,
    assembler_flags: "-g",
    compile_dir: "",
    small_data: 0,
    filename: "libgcc.a",
    sources: &[
        EeObject {
            source: "libgcc2.c",
            name: "_main",
            debug_name: Some("../../src/gcc/libgcc2.c"),
            defines: &["L__main"],
        },
        EeObject {
            source: "libgcc2.c",
            name: "exit",
            debug_name: Some("../../src/gcc/libgcc2.c"),
            defines: &["L_exit"],
        },
        EeObject {
            source: "libgcc2.c",
            name: "_udivdi3",
            debug_name: Some("../../src/gcc/libgcc2.c"),
            defines: &["L_udivdi3"],
        },
        EeObject {
            source: "libgcc2.c",
            name: "_umoddi3",
            debug_name: Some("../../src/gcc/libgcc2.c"),
            defines: &["L_umoddi3"],
        },
        EeObject {
            source: "frame.c",
            name: "frame",
            debug_name: Some("../../src/gcc/frame.c"),
            defines: &[],
        },
        EeObject {
            source: "libgcc2.c",
            name: "_divdi3",
            debug_name: Some("../../src/gcc/libgcc2.c"),
            defines: &["L_divdi3"],
        },
        EeObject {
            source: "libgcc2.c",
            name: "_fixunsdfdi",
            debug_name: Some("../../src/gcc/libgcc2.c"),
            defines: &["L_fixunsdfdi"],
        },
        EeObject {
            source: "libgcc2.c",
            name: "_floatdidf",
            debug_name: Some("../../src/gcc/libgcc2.c"),
            defines: &["L_floatdidf"],
        },
        EeObject {
            source: "libgcc2.c",
            name: "_moddi3",
            debug_name: Some("../../src/gcc/libgcc2.c"),
            defines: &["L_moddi3"],
        },
        EeObject {
            source: "libgcc2.c",
            name: "_muldi3",
            debug_name: Some("../../src/gcc/libgcc2.c"),
            defines: &["L_muldi3"],
        },
        EeObject {
            source: "config/fp-bit.c",
            name: "dp-bit",
            debug_name: Some("dp-bit.c"),
            defines: &[
                "FLOAT_BIT_ORDER_MISMATCH",
                "US_SOFTWARE_GOFAST",
                "NO_DENORMALS",
            ],
        },
        EeObject {
            source: "config/fp-bit.c",
            name: "fp-bit",
            debug_name: Some("fp-bit.c"),
            defines: &[
                "FLOAT_BIT_ORDER_MISMATCH",
                "US_SOFTWARE_GOFAST",
                "NO_DENORMALS",
                "FLOAT",
            ],
        },
    ],
    headers: &[
        "sce/ee/libgcc/config/tconfig.h",
        "sce/ee/libgcc/config/tm.h",
        "sce/ee/libgcc/include/ansidecl.h",
        "sce/ee/libgcc/src/config/gofast.h",
        "sce/ee/libgcc/src/config/libgloss.h",
        "sce/ee/libgcc/src/config/mips/abi64.h",
        "sce/ee/libgcc/src/config/mips/elf5900.h",
        "sce/ee/libgcc/src/config/mips/elf64.h",
        "sce/ee/libgcc/src/config/mips/elfl64.h",
        "sce/ee/libgcc/src/config/mips/mips.h",
        "sce/ee/libgcc/src/config/mips/xm-mips.h",
        "sce/ee/libgcc/src/defaults.h",
        "sce/ee/libgcc/src/dwarf2.h",
        "sce/ee/libgcc/src/frame.h",
        "sce/ee/libgcc/src/gansidecl.h",
        "sce/ee/libgcc/src/gbl-ctors.h",
        "sce/ee/libgcc/src/gthr-single.h",
        "sce/ee/libgcc/src/gthr.h",
        "sce/ee/libgcc/src/longlong.h",
        "sce/ee/libgcc/src/machmode.def",
        "sce/ee/libgcc/src/machmode.h",
        "sce/ee/newlib/stddef.h",
    ],
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
  command = cd "$build_dir/$source_workdir" && "$ee_tool_dir/cc1" -quiet -O2 $compiler_flags -G $small_data "$source_name" -o "$build_dir/$out" $debug_paths
  description = EE CC $out
rule ee_assemble
  command = "$ee_tool_dir/ee-as" $assembler_flags -G $small_data -mabi=eabi -o "$out" "$in"
  description = EE AS $out
rule ee_assembly_source
  command = cp "$in" "$out" $debug_paths
  description = EE ASM SOURCE $out
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
            let mut debug_paths = String::new();
            if let Some(directory) = build.debug_dir {
                let actual = build_dir.join(&source_workdir);
                let (directory, name) = if let Some(prefix) = build.debug_source_root {
                    (
                        Path::new(directory).join(parent),
                        format!(
                            "{}{prefix}/{}",
                            "../".repeat(parent.components().count()),
                            source.source
                        ),
                    )
                } else {
                    (
                        Path::new(directory).to_path_buf(),
                        source.debug_name.unwrap_or(&compile_name).to_string(),
                    )
                };
                debug_paths = format!(
                    "&& sed -i -e '/^[[:space:]]*\\.\\(file\\|stabs\\)/ {{ s|{actual}/|{directory}/|g; s|\"{compile_name}\"|\"{name}\"|g; }}' \"$build_dir/{assembly}\"",
                    actual = actual.display().to_string().trim_end_matches('/'),
                    assembly = assembly.display(),
                    directory = directory.display(),
                );
            }
            let is_assembly = source_path.extension().is_some_and(|ext| ext == "S");
            let compile_rule = if is_assembly {
                "ee_assembly_source"
            } else {
                "ee_compile"
            };
            let compile_tool = if is_assembly {
                ""
            } else {
                " | $ee_tool_dir/cc1"
            };
            if is_assembly {
                debug_paths = debug_paths.replace("/^[[:space:]]*\\.\\(file\\|stabs\\)/", "/^#/");
            }
            ninja.push_str(&format!(
                "build {preprocessed}: ee_preprocess {source_dir}/{source_name} | {headers} $ee_tool_dir/cpp
  defines = {defines}
  source_dir = $build_dir/{preprocess_dir}
  source_name = {compile_name}
build {assembly}: {compile_rule} {preprocessed}{compile_tool}
  compiler_flags = {compiler_flags}
  debug_paths = {debug_paths}
  small_data = {small_data}
  source_workdir = {source_workdir}
  source_name = {compile_name}
build {object}: ee_assemble {assembly} | $ee_tool_dir/ee-as
  assembler_flags = {assembler_flags}
  small_data = {small_data}
",
                compiler_flags = build.compiler_flags,
                assembler_flags = build.assembler_flags,
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
