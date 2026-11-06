use super::{EeBuild, EeObject};

pub(crate) const LIBKERNL: EeBuild = EeBuild {
    source_dir: "sce/ee/libkernl",
    output_dir: "sce/ee/libkernl",
    defines: &[
        "__mips__",
        "__mips64",
        "__mips_eabi",
        "__mips_single_float",
        "__SIZE_TYPE__=unsigned int",
        "__PTRDIFF_TYPE__=int",
        "ALARM_PAYLOAD=\"$build_dir/sce/ee/libkernl/alarm-payload.h\"",
    ],
    preprocessor_flags: "",
    compiler_flags: "",
    debug_dir: Some("/build"),
    debug_source_root: None,
    assembler_flags: "",
    compile_dir: "",
    small_data: 0,
    filename: "libkernl.a",
    sources: &[
        EeObject {
            source: "klib.S",
            name: "klib",
            debug_name: Some("klib.s"),
            defines: &[],
        },
        EeObject {
            source: "glue.c",
            name: "glue",
            debug_name: None,
            defines: &[],
        },
        EeObject {
            source: "cache.c",
            name: "cache",
            debug_name: None,
            defines: &[],
        },
        EeObject {
            source: "intr.c",
            name: "intr",
            debug_name: None,
            defines: &[],
        },
        EeObject {
            source: "thread.c",
            name: "thread",
            debug_name: None,
            defines: &[],
        },
        EeObject {
            source: "tlbfunc.c",
            name: "tlbfunc",
            debug_name: None,
            defines: &[],
        },
        EeObject {
            source: "deci2.c",
            name: "deci2",
            debug_name: None,
            defines: &[],
        },
        EeObject {
            source: "tty.c",
            name: "tty",
            debug_name: None,
            defines: &[],
        },
        EeObject {
            source: "kprintf.c",
            name: "kprintf",
            debug_name: None,
            defines: &[],
        },
        EeObject {
            source: "sifcmd.c",
            name: "sifcmd",
            debug_name: None,
            defines: &[],
        },
        EeObject {
            source: "sifrpc.c",
            name: "sifrpc",
            debug_name: None,
            defines: &[],
        },
        EeObject {
            source: "filestub.c",
            name: "filestub",
            debug_name: None,
            defines: &[],
        },
        EeObject {
            source: "eeloadfile.c",
            name: "eeloadfile",
            debug_name: None,
            defines: &[],
        },
        EeObject {
            source: "iopreset.c",
            name: "iopreset",
            debug_name: None,
            defines: &[],
        },
        EeObject {
            source: "diei.c",
            name: "diei",
            debug_name: None,
            defines: &[],
        },
        EeObject {
            source: "initsys.c",
            name: "initsys",
            debug_name: None,
            defines: &[],
        },
    ],
    headers: &[
        "$build_dir/sce/ee/libkernl/alarm-payload.h",
        "sce/ee/libkernl/sif.h",
        "sce/ee/newlib/string.h",
        "sce/ee/newlib/stdarg.h",
        "sce/ee/newlib/va-mips.h",
        "sce/ee/newlib/sys/stat.h",
        "sce/ee/newlib/sys/types.h",
        "sce/ee/newlib/sys/config.h",
        "sce/ee/newlib/_ansi.h",
        "sce/ee/newlib/time.h",
        "sce/ee/newlib/machine/time.h",
        "sce/ee/newlib/machine/types.h",
        "sce/ee/newlib/stddef.h",
    ],
    extra_objects: &["tlbtrap.o"],
    extra_ninja: r#"
rule ee_alarm_compile
  command = "$ee_tool_dir/cc1" -quiet -O2 -fno-common -G 0 "$in" -o "$out"
  description = EE CC $out
rule ee_alarm_link
  command = "$ee_tool_dir/ld" -T "$repo_root/sce/ee/libkernl/alarm.ld" -o "$out" "$in"
  description = EE LD $out
rule ee_binary
  command = "$ee_tool_dir/ee-objcopy" -O binary -R .reginfo "$in" "$out"
  description = EE BIN $out
rule ee_alarm_header
  command = { echo 'static unsigned char srcfile[] __attribute__((aligned(8))) = {'; od -An -v -tu1 "$in" | awk '{ for (i = 1; i <= NF; i++) printf "%s,", $$i; print "" }'; echo '};'; } > "$out"
  description = EE EMBED $out
rule ee_fixup_build
  command = cargo build --release --locked --manifest-path "$repo_root/Cargo.toml" --target-dir "$repo_root/build/host" -p eefixup
  description = Build EE metadata tool
rule ee_file_symbol
  command = "$repo_root/build/host/release/eefixup" "$in" "$out" tlbtrap.s ../kernel/kparam.h
  description = EE FILE SYMBOL $out

build sce/ee/libkernl/alarm.s: ee_alarm_compile $repo_root/sce/ee/libkernl/alarm.c | $ee_tool_dir/cc1
build sce/ee/libkernl/alarm.o: ee_assemble sce/ee/libkernl/alarm.s | $ee_tool_dir/as
  small_data = 0
build sce/ee/libkernl/alarm.elf: ee_alarm_link sce/ee/libkernl/alarm.o | $repo_root/sce/ee/libkernl/alarm.ld $ee_tool_dir/ld
build sce/ee/libkernl/alarm.bin: ee_binary sce/ee/libkernl/alarm.elf | $ee_tool_dir/ee-objcopy
build sce/ee/libkernl/alarm-payload.h: ee_alarm_header sce/ee/libkernl/alarm.bin
build $repo_root/build/host/release/eefixup: ee_fixup_build $repo_root/Cargo.toml $repo_root/Cargo.lock $repo_root/eefixup/Cargo.toml $repo_root/eefixup/src/main.rs
build sce/ee/libkernl/tlbtrap.raw.o: ee_assemble $repo_root/sce/ee/libkernl/tlbtrap.s | $ee_tool_dir/as
  small_data = 0
build sce/ee/libkernl/tlbtrap.o: ee_file_symbol sce/ee/libkernl/tlbtrap.raw.o | $repo_root/build/host/release/eefixup

"#,
};
