use super::EeBuild;
use std::{collections::HashSet, io, path::Path};

const OBJECTS: &[&str] = &[
    "sce/ee/startup/crt0.o",
    "next/kernel/kboot.o",
    "next/kernel/ksocket.o",
    "next/kernel/kdsnetm.o",
    "next/kernel/kprint.o",
    "next/kernel/kmachine.o",
    "next/kernel/kscheme.o",
    "next/kernel/klisten.o",
    "next/kernel/ksound.o",
    "next/kernel/kdgo.o",
    "next/kernel/kmemcard.o",
    "sce/ee/libgraph/resetgraph.o",
    "sce/ee/libgraph/resetpath.o",
    "sce/ee/libgraph/putdispenv.o",
    "sce/ee/libgraph/syncv.o",
    "sce/ee/libgraph/syncpath.o",
    "sce/ee/libgraph/setdefloadimage.o",
    "sce/ee/libgraph/execloadimage.o",
    "sce/ee/libgraph/execstoreimage.o",
    "sce/ee/libgraph/putimr.o",
    "sce/ee/libgraph/getimr.o",
    "sce/ee/libdma/libdma.o",
    "sce/ee/libcdvd/eecdvd.o",
    "sce/ee/libpad/libpad.o",
    "sce/ee/libscf/libscf.o",
    "sce/ee/libmc/libmc.o",
    "sce/ee/libscedemo/libscedemo.o",
    "next/kernel/fileio.o",
    "next/kernel/kmalloc.o",
    "sce/ee/newlib/sf_modf.o",
    "sce/ee/libgcc/_main.o",
    "sce/ee/libgcc/exit.o",
    "sce/ee/libgcc/_udivdi3.o",
    "sce/ee/libgcc/_umoddi3.o",
    "sce/ee/libgcc/frame.o",
    "sce/ee/newlib/abort.o",
    "sce/ee/newlib/assert.o",
    "sce/ee/newlib/atoi.o",
    "sce/ee/newlib/calloc.o",
    "sce/ee/newlib/callocr.o",
    "sce/ee/newlib/fflush.o",
    "sce/ee/newlib/findfp.o",
    "sce/ee/newlib/fiprintf.o",
    "sce/ee/newlib/fwalk.o",
    "sce/ee/newlib/impure.o",
    "sce/ee/newlib/malloc.o",
    "sce/ee/newlib/mallocr.o",
    "sce/ee/newlib/memcpy.o",
    "sce/ee/newlib/memset.o",
    "sce/ee/newlib/mlock.o",
    "sce/ee/newlib/printf.o",
    "sce/ee/newlib/sbrkr.o",
    "sce/ee/newlib/signal.o",
    "sce/ee/newlib/signalr.o",
    "sce/ee/newlib/sprintf.o",
    "sce/ee/newlib/stdio.o",
    "sce/ee/newlib/strcat.o",
    "sce/ee/newlib/strcmp.o",
    "sce/ee/newlib/strcpy.o",
    "sce/ee/newlib/strlen.o",
    "sce/ee/newlib/strncmp.o",
    "sce/ee/newlib/strncpy.o",
    "sce/ee/newlib/strtol.o",
    "sce/ee/newlib/vfiprintf.o",
    "sce/ee/newlib/vfprintf.o",
    "sce/ee/newlib/vprintf.o",
    "sce/ee/newlib/vsprintf.o",
    "sce/ee/newlib/writer.o",
    "sce/ee/newlib/wsetup.o",
    "sce/ee/newlib/closer.o",
    "sce/ee/newlib/ctype_.o",
    "sce/ee/newlib/dtoa.o",
    "sce/ee/newlib/freer.o",
    "sce/ee/newlib/fvwrite.o",
    "sce/ee/newlib/locale.o",
    "sce/ee/newlib/lseekr.o",
    "sce/ee/newlib/makebuf.o",
    "sce/ee/newlib/mbtowc_r.o",
    "sce/ee/newlib/memchr.o",
    "sce/ee/newlib/memmove.o",
    "sce/ee/newlib/mprec.o",
    "sce/ee/newlib/readr.o",
    "sce/ee/newlib/s_isinf.o",
    "sce/ee/newlib/s_isnan.o",
    "sce/ee/newlib/fstatr.o",
    "sce/ee/libkernl/klib.o",
    "sce/ee/libkernl/glue.o",
    "sce/ee/libkernl/cache.o",
    "sce/ee/libkernl/intr.o",
    "sce/ee/libkernl/thread.o",
    "sce/ee/libkernl/deci2.o",
    "sce/ee/libkernl/tty.o",
    "sce/ee/libkernl/kprintf.o",
    "sce/ee/libkernl/sifcmd.o",
    "sce/ee/libkernl/sifrpc.o",
    "sce/ee/libkernl/filestub.o",
    "sce/ee/libkernl/eeloadfile.o",
    "sce/ee/libkernl/iopreset.o",
    "sce/ee/libkernl/tlbfunc.o",
    "sce/ee/libkernl/tlbtrap.o",
    "sce/ee/libkernl/diei.o",
    "sce/ee/libkernl/initsys.o",
    "sce/ee/libgcc/_divdi3.o",
    "sce/ee/libgcc/_fixunsdfdi.o",
    "sce/ee/libgcc/_floatdidf.o",
    "sce/ee/libgcc/_moddi3.o",
    "sce/ee/libgcc/_muldi3.o",
    "sce/ee/libgcc/dp-bit.o",
    "sce/ee/libgcc/fp-bit.o",
    "sce/ee/newlib/errno.o",
    "sce/ee/newlib/memcmp.o",
];

pub(crate) fn ninja_section(
    build_dir: &Path,
    builds: &[EeBuild],
    frame_source: &str,
) -> io::Result<String> {
    let available: HashSet<_> = builds
        .iter()
        .flat_map(|build| {
            build
                .sources
                .iter()
                .map(|source| format!("{}/{}.o", build.output_dir, source.name))
                .chain(
                    build
                        .extra_objects
                        .iter()
                        .map(|name| format!("{}/{name}", build.output_dir)),
                )
        })
        .collect();
    let objects: Vec<_> = OBJECTS
        .iter()
        .copied()
        .filter(|object| available.contains(*object))
        .collect();
    let mut script = String::from(
        "ENTRY(_start)\n_stack = 0xffffffff;\n_stack_size = 0x4000;\n_heap_size = 0xffffffff;\nSECTIONS {\n",
    );
    for section in [".text", ".data", ".rodata", ".sdata", ".sbss", ".bss"] {
        let address = if section == ".text" {
            "0x100000"
        } else {
            "ALIGN(128)"
        };
        script.push_str(&format!("{section} {address} : {{\n"));
        let mut ordered = objects.clone();
        if section == ".rodata" {
            ordered.retain(|&object| object != "sce/ee/newlib/ctype_.o");
            let position = ordered
                .iter()
                .position(|&object| object == "sce/ee/newlib/vprintf.o")
                .unwrap();
            ordered.insert(position, "sce/ee/newlib/ctype_.o");
        }
        for object in ordered {
            if section == ".rodata" && object == "next/kernel/fileio.o" {
                script.push_str(". = ALIGN(8) + 8;\n");
            }
            script.push_str(&format!("*/{object}({section})\n"));
            if object == "sce/ee/startup/crt0.o" {
                // collect2 inserts its generated object immediately after crt0.
                script.push_str(&format!("/tmp/cc*.o({section})\n"));
            }
        }
        match section {
            ".text" => script.push_str(". = ALIGN(16);\n"),
            ".sbss" => script.push_str("*(.scommon)\n"),
            ".bss" => script.push_str("*(COMMON)\n"),
            _ => {}
        }
        script.push_str("}\n");
        if section == ".text" {
            script.push_str(".reginfo . : { *(.reginfo) }\n");
        }
    }
    script.push_str(
        "_gp = ADDR(.sdata) + 0x7ff0;\n_fbss = ADDR(.sbss);\n_end = .; end = .;\n         .mdebug . : { *(.mdebug) }\n/DISCARD/ : { *(.comment) }\n}\n         PROVIDE (__CTOR_LIST__ = 0);\nPROVIDE (__DTOR_LIST__ = 0);\n",
    );
    crate::write_if_changed(&build_dir.join("next/kernel/kernel.ld"), &script)?;
    let inputs = objects.join(" ");
    let link_objects = objects
        .iter()
        .map(|object| format!("\"$build_dir/{object}\""))
        .collect::<Vec<_>>()
        .join(" ");
    Ok(format!(
        r#"
rule ee_kernel_link
  command = TMPDIR=/tmp "$ee_tool_dir/ee-gcc" -B"$ee_tool_dir/" -nostdlib -Wl,-T,"$build_dir/next/kernel/kernel.ld" -o "$out" $link_objects
  description = EE LINK $out
rule ee_kernel_metadata
  command = "$repo_root/build/host/release/eefixup" --frame-source "$in" "$out" "$frame_source"
  description = EE METADATA $out

build next/kernel/kernel.elf: ee_kernel_link {inputs} | next/kernel/kernel.ld $ee_tool_dir/ee-gcc $ee_tool_dir/collect2 $ee_tool_dir/cpp $ee_tool_dir/cc1 $ee_tool_dir/as $ee_tool_dir/ld $ee_tool_dir/nm
  link_objects = {link_objects}
build KERNEL.ELF: ee_kernel_metadata next/kernel/kernel.elf | $repo_root/build/host/release/eefixup
  frame_source = {frame_source}

"#
    ))
}
