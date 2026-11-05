use std::{fs, io, path::Path};

pub(super) struct EeBuild {
    pub source_dir: &'static str,
    pub output_dir: &'static str,
    pub filename: &'static str,
    pub sources: &'static [&'static str],
    pub headers: &'static [&'static str],
}

pub(super) const LIBDMA: EeBuild = EeBuild {
    source_dir: "sce/ee/libdma",
    output_dir: "sce/ee/libdma",
    filename: "libdma.a",
    sources: &["libdma.c"],
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
    filename: "libscf.a",
    sources: &["libscf.c"],
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

pub(super) fn ninja_section(build_dir: &Path, builds: &[EeBuild]) -> io::Result<String> {
    let mut ninja = String::from(
        r#"
# EE libraries
ee_tool_dir = $repo_root/toolchain/bin/ee

rule ee_preprocess
  command = cd "$source_dir" && "$ee_tool_dir/cpp" -nostdinc -I"$repo_root" "$source_name" "$build_dir/$out"
  description = EE CPP $out
rule ee_compile
  command = cd "$build_dir/$source_workdir" && "$ee_tool_dir/cc1" -quiet -O2 -G 0 "$source_name" -o "$build_dir/$out"
  description = EE CC $out
rule ee_assemble
  command = "$ee_tool_dir/ee-as" -G 0 -mabi=eabi -o "$out" "$in"
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
            let source_path = Path::new(source);
            let source_name = source_path.file_name().unwrap().to_str().unwrap();
            let parent = source_path.parent().unwrap();
            let source_dir = Path::new(build.source_dir).join(parent);
            let source_workdir = Path::new(output_dir).join("src").join(parent);
            fs::create_dir_all(build_dir.join(&source_workdir))?;
            let preprocessed = source_workdir.join(source_name);
            let assembly = Path::new(output_dir).join(source_path.with_extension("s"));
            let object = Path::new(output_dir).join(source_path.with_extension("o"));
            fs::create_dir_all(build_dir.join(object.parent().unwrap()))?;
            ninja.push_str(&format!(
                "build {preprocessed}: ee_preprocess $repo_root/{source_dir}/{source_name} | {headers} $ee_tool_dir/cpp
  source_dir = $repo_root/{source_dir}
  source_name = {source_name}
build {assembly}: ee_compile {preprocessed} | $ee_tool_dir/cc1
  source_workdir = {source_workdir}
  source_name = {source_name}
build {object}: ee_assemble {assembly} | $ee_tool_dir/ee-as
",
                preprocessed = preprocessed.display(),
                source_dir = source_dir.display(),
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
