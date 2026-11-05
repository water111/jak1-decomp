use std::{fs, io, path::Path};

// Select where in the link order the export stub goes.
#[allow(dead_code)]
#[derive(Clone, Copy)]
pub(super) enum ExportPosition {
    First,
    Last,
    Before(&'static str),
}

// Settings for building and linking export stub.
#[derive(Clone, Copy)]
pub(super) struct ExportTable {
    pub table: &'static str,
    pub object: &'static str,
    pub position: ExportPosition,
}

#[derive(Clone, Copy)]
pub(super) enum StripMode {
    None,
    MinimalSymbols, // normal IOP stripping
    #[allow(dead_code)]
    EmptySections, // weird stripping on 989, leaves empty sections behind.
}

impl StripMode {
    pub fn argument(self) -> &'static str {
        match self {
            Self::None => "",
            Self::MinimalSymbols => "--strip=minimal-symbols",
            Self::EmptySections => "--strip=empty-sections",
        }
    }
}

#[derive(Clone, Copy)]
pub(super) struct IopBuild {
    pub output_dir: &'static str, // relative to build/<game revision>/
    pub filename: &'static str,   // final IRX name
    pub version: &'static str,
    pub sha256: &'static str,
    pub source_dir: &'static str,
    // name map for sources with non-standard names in debug info, eg: ../common/foo.c
    pub input_names: &'static [(&'static str, &'static str)],
    pub include_dirs: &'static [&'static str],
    pub link_small_data: u8,
    pub sources: &'static [&'static str],
    // .tbl paths, in link order.
    pub imports: &'static [&'static str],
    pub defines: &'static [&'static str],
    pub entry: &'static str,
    pub optimization: &'static str,
    pub strip: StripMode,
    pub assembler_debug: bool,
    pub small_data_sources: &'static [&'static str],
    pub exports: Option<ExportTable>,
    pub linker_script: Option<&'static str>,
    pub debug_import_source: Option<&'static str>,
}

const DEFAULT_BUILD: IopBuild = IopBuild {
    output_dir: "",
    filename: "",
    version: "",
    sha256: "",
    source_dir: "",
    input_names: &[],
    include_dirs: &[".", "next"],
    link_small_data: 0,
    sources: &[],
    imports: &[],
    defines: &[],
    entry: "start",
    optimization: "-O2",
    strip: StripMode::MinimalSymbols,
    assembler_debug: false,
    small_data_sources: &[],
    exports: None,
    linker_script: None,
    debug_import_source: None,
};

pub(super) const SIO2MAN_2_4: IopBuild = IopBuild {
    output_dir: "sce/iop/sio2man",
    filename: "SIO2MAN.IRX",
    version: "2.4",
    sha256: "4e12becc4fe5c6e9cf082417ed70eb7b9b521ed9ee122f73ef2b1478f06494d6",
    source_dir: "sce/iop/sio2man",
    sources: &["sio2man.c"],
    imports: &[
        "sce/iop/loadcore/loadcore.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/stdio/stdio.tbl",
        "sce/iop/dmacman/dmacman.tbl",
        "sce/iop/thbase/thbase.tbl",
        "sce/iop/thevent/thevent.tbl",
        "sce/iop/sysclib/sysclib.tbl",
    ],
    assembler_debug: true, // needed for delay slot scheduling to match.
    exports: Some(ExportTable {
        table: "sio2man.tbl",
        object: "exports",
        position: ExportPosition::Last,
    }),
    ..DEFAULT_BUILD
};

pub(super) const MCSERV_2_12: IopBuild = IopBuild {
    output_dir: "sce/iop/mcserv",
    filename: "MCSERV.IRX",
    version: "2.12",
    sha256: "65157cc7bc2388372c244f65af5094354494794b7ac9bc0ba2242126f51c25e6",
    source_dir: "sce/iop/mcserv",
    sources: &["mcserv.c"],
    imports: &[
        "sce/iop/loadcore/loadcore.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/sysclib/sysclib.tbl",
        "sce/iop/stdio/stdio.tbl",
        "sce/iop/sifman/sifman.tbl",
        "sce/iop/sifcmd/sifcmd.tbl",
        "sce/iop/thbase/thbase.tbl",
        "sce/iop/thsemap/thsemap.tbl",
        "sce/iop/mcman/mcman.tbl",
    ],
    assembler_debug: true,
    exports: Some(ExportTable {
        table: "mcserv.tbl",
        object: "exports",
        position: ExportPosition::First,
    }),
    ..DEFAULT_BUILD
};

pub(super) const IOPSETUP: IopBuild = IopBuild {
    output_dir: "sce/iop/iopsetup",
    filename: "IOPSETUP.IRX",
    version: "module-0x0000",
    sha256: "90cd8eb3a86b63e64bd1e5de8c13830f6e133ec557f1c006d913d12c030ac2e3",
    source_dir: "sce/iop/iopsetup",
    sources: &["dongle.c"],
    imports: &[
        "sce/iop/usbd/usbd.tbl",
        "sce/iop/stdio/stdio.tbl",
        "sce/iop/sysclib/sysclib.tbl",
        "sce/iop/thbase/thbase.tbl",
    ],
    debug_import_source: Some("/tmp/ccR4mDML_ilb_stub.s"),
    optimization: "-O0",
    strip: StripMode::None,
    ..DEFAULT_BUILD
};

pub(super) const LIBSD_1_4: IopBuild = IopBuild {
    output_dir: "sce/iop/libsd",
    filename: "LIBSD.IRX",
    version: "1.4",
    sha256: "0b580282b6f54fac2ae130318e378a197ff8a086ea077006cecb1b532a229876",
    source_dir: "sce/iop/libsd",
    sources: &[
        "state.s",
        "libsd.c",
        "data.c",
        "effect_registers.c",
        "effect_clear.c",
        "effect.c",
        "init.c",
        "init_state.c",
        "voice_transfer.c",
        "voice_status.c",
        "block_transfer.c",
        "block_status.c",
        "transfers.c",
        "batch.c",
        "registers.c",
        "pitch.c",
        "attributes.c",
        "transfer_callbacks.c",
        "irq_callbacks.c",
        "voices_init.c",
    ],
    imports: &[
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/loadcore/loadcore.tbl",
        "sce/iop/sifman/sifman.tbl",
        "sce/iop/stdio/stdio.tbl",
        "sce/iop/thbase/thbase.tbl",
        "sce/iop/thevent/thevent.tbl",
    ],
    exports: Some(ExportTable {
        table: "libsd.tbl",
        object: "exports",
        position: ExportPosition::First,
    }),
    ..DEFAULT_BUILD
};

pub(super) const USBD_2_4: IopBuild = IopBuild {
    output_dir: "sce/iop/usbd",
    filename: "USBD.IRX",
    version: "2.4",
    sha256: "b369c4383e58167c15dd6fd80d90f36de4aa293e4b4fb18ca4c6ca9d2ec5a1da",
    source_dir: "sce/iop/usbd",
    sources: &[
        "hub.c",
        "controller.c",
        "version.c",
        "api.c",
        "device.c",
        "init.c",
    ],
    imports: &[
        "sce/iop/sysmem/sysmem.tbl",
        "sce/iop/loadcore/loadcore.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/stdio/stdio.tbl",
        "sce/iop/thbase/thbase.tbl",
        "sce/iop/thevent/thevent.tbl",
        "sce/iop/thsemap/thsemap.tbl",
        "sce/iop/sysclib/sysclib.tbl",
    ],
    assembler_debug: true,
    small_data_sources: &["version.c"],
    exports: Some(ExportTable {
        table: "usbd.tbl",
        object: "exports",
        position: ExportPosition::First,
    }),
    ..DEFAULT_BUILD
};

pub(super) const PADMAN_4_2: IopBuild = IopBuild {
    output_dir: "sce/iop/padman",
    filename: "PADMAN.IRX",
    version: "4.2",
    sha256: "10120230a66017c200c5ce8e4b9f11c42ffb240bc2df9d194e5d2624625df5d7",
    source_dir: "sce/iop/padman",
    sources: &[
        "core.c",
        "state.s",
        "transfer.c",
        "commands.c",
        "command_find.c",
        "command_mouse.c",
        "command_digital.c",
        "command_analog.c",
        "command_negicon.c",
        "command_joystick.c",
        "command_konami_gun.c",
        "command_namco_gun.c",
        "command_jogcon.c",
        "command_config.c",
        "rpc.c",
    ],
    imports: &[
        "sce/iop/sysmem/sysmem.tbl",
        "sce/iop/loadcore/loadcore.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/stdio/stdio.tbl",
        "sce/iop/sio2man/sio2man.tbl",
        "sce/iop/thbase/thbase.tbl",
        "sce/iop/thevent/thevent.tbl",
        "sce/iop/sifman/sifman.tbl",
        "sce/iop/sifcmd/sifcmd.tbl",
        "sce/iop/vblank/vblank.tbl",
        "sce/iop/sysclib/sysclib.tbl",
    ],
    assembler_debug: true,
    exports: Some(ExportTable {
        table: "padman.tbl",
        object: "exports",
        position: ExportPosition::Last,
    }),

    ..DEFAULT_BUILD
};

pub(super) const MCMAN_2_21: IopBuild = IopBuild {
    output_dir: "sce/iop/mcman",
    filename: "MCMAN.IRX",
    version: "2.21",
    sha256: "4c34f10290e1acedc36a2616ddd6df18b994239734c6b1dd9667f9aeaa255dfd",
    source_dir: "sce/iop/mcman",
    sources: &[
        "core.c",
        "protocol.c",
        "geometry.c",
        "ps2.c",
        "ps1.c",
        "storage.c",
        "driver.c",
        "state.s",
    ],
    imports: &[
        "sce/iop/loadcore/loadcore.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/sio2man/sio2man.tbl",
        "sce/iop/sysclib/sysclib.tbl",
        "sce/iop/thbase/thbase.tbl",
        "sce/iop/thsemap/thsemap.tbl",
        "sce/iop/modload/modload.tbl",
        "sce/iop/ioman/ioman.tbl",
        "sce/iop/secrman/secrman.tbl",
        "sce/iop/cdvdman/cdvdman.tbl",
    ],
    assembler_debug: true,
    // Preserve section alignment and collect the fixed workspace into BSS.
    linker_script: Some("layout.ld"),
    exports: Some(ExportTable {
        table: "mcman.tbl",
        object: "exports",
        position: ExportPosition::First,
    }),

    ..DEFAULT_BUILD
};

const OVERLORD_SOURCES: &[&str] = &[
    "ramdisk.c",
    "fakeiso.c",
    "deviso.c",
    "iso_cd.c",
    "dma.c",
    "iso.c",
    "stream.c",
    "isocommon.c",
    "isodesc.c",
    "iso_queue.c",
    "srpc.c",
    "sbank.c",
    "ssound.c",
    "soundcommon.c",
    "overlord.c",
    "iso_api.c",
];

const OVERLORD: IopBuild = IopBuild {
    output_dir: "overlord",
    filename: "OVERLORD.IRX",
    source_dir: "next/overlord",
    sources: OVERLORD_SOURCES,
    imports: &[
        "sce/iop/cdvdman/cdvdman.tbl",
        "sce/iop/libsd/libsd.tbl",
        "sce/iop/snd989/snd989.tbl",
        "sce/iop/sysmem/sysmem.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/ioman/ioman.tbl",
        "sce/iop/loadcore/loadcore.tbl",
        "sce/iop/sifcmd/sifcmd.tbl",
        "sce/iop/sifman/sifman.tbl",
        "sce/iop/stdio/stdio.tbl",
        "sce/iop/sysclib/sysclib.tbl",
        "sce/iop/thbase/thbase.tbl",
        "sce/iop/thsemap/thsemap.tbl",
        "sce/iop/thmsgbx/thmsgbx.tbl",
        "sce/iop/vblank/vblank.tbl",
    ],
    optimization: "-O0",
    strip: StripMode::None,
    link_small_data: 8,
    input_names: &[
        ("isocommon.c", "../common/isocommon.c"),
        ("isodesc.c", "../common/isodesc.c"),
        ("soundcommon.c", "../common/soundcommon.c"),
    ],
    include_dirs: &["next", "."],
    ..DEFAULT_BUILD
};

// older versions didn't have iso_api.c, remove from sources.
const OVERLORD_EMBEDDED_ISO_API: IopBuild = IopBuild {
    sources: &[
        "ramdisk.c",
        "fakeiso.c",
        "deviso.c",
        "iso_cd.c",
        "dma.c",
        "iso.c",
        "stream.c",
        "isocommon.c",
        "isodesc.c",
        "iso_queue.c",
        "srpc.c",
        "sbank.c",
        "ssound.c",
        "soundcommon.c",
        "overlord.c",
    ],
    ..OVERLORD
};

pub(super) const OVERLORD_20010903: IopBuild = IopBuild {
    version: "20010903",
    sha256: "308136f13a9b482c17e5a4ebad11d3516c5c968ea0667b5983433bc2f567d228",
    defines: &["REVISION=20010903", "MEDIA_DVD", "VIDEO_NTSC", "REGION_US"],
    debug_import_source: Some("/tmp/ccZqpG4m_ilb_stub.s"),

    ..OVERLORD_EMBEDDED_ISO_API
};

pub(super) const OVERLORD_20010920: IopBuild = IopBuild {
    version: "20010920",
    sha256: "c61f18891950cdf25bef2f5f01c9fe61971d2dbe5bd86d00bf6e8d6fc3b9d253",
    defines: &[
        "REVISION=20010920",
        "MEDIA_CD",
        "VIDEO_NTSC",
        "REGION_JP",
        "DEMO",
    ],
    debug_import_source: Some("/tmp/ccbEm3wW_ilb_stub.s"),

    ..OVERLORD_EMBEDDED_ISO_API
};

pub(super) const OVERLORD_20011002: IopBuild = IopBuild {
    version: "20011002",
    sha256: "92c6a94204c1c260b9117161fc2745cabbaf83cd4654183e0b1415ffca531db1",
    defines: &[
        "REVISION=20011002",
        "MEDIA_CD",
        "VIDEO_NTSC",
        "REGION_US",
        "DEMO",
        "SHARED_DEMO",
    ],
    debug_import_source: Some("/tmp/ccBwnfyn_ilb_stub.s"),

    ..OVERLORD_EMBEDDED_ISO_API
};

pub(super) const OVERLORD_20011019: IopBuild = IopBuild {
    version: "20011019",
    sha256: "3666828b35268d746603f6d2b0cb07e6a4a1cdee9980beb5e2326d19f792d621",
    defines: &["REVISION=20011019", "MEDIA_DVD", "VIDEO_NTSC", "REGION_US"],
    debug_import_source: Some("/tmp/ccoZdIg7_ilb_stub.s"),

    ..OVERLORD_EMBEDDED_ISO_API
};

pub(super) const OVERLORD_20011031: IopBuild = IopBuild {
    version: "20011031",
    sha256: "fe4869449b4046aa937b08dae6138eb77bd52e9562807e02c2af9eb2a740e974",
    defines: &["REVISION=20011031", "MEDIA_DVD", "VIDEO_NTSC", "REGION_US"],
    debug_import_source: Some("/tmp/cc4fRwXd_ilb_stub.s"),
    ..OVERLORD
};

pub(super) const OVERLORD_20011102: IopBuild = IopBuild {
    version: "20011102",
    sha256: "076dfe24f616f0866ea154ab983b33eacdae1eadc37e80f853a9db08901039fa",
    defines: &[
        "REVISION=20011102",
        "MEDIA_DVD",
        "VIDEO_PAL",
        "REGION_EU",
        "DEMO",
        "SHARED_DEMO",
    ],
    debug_import_source: Some("/tmp/cc3gKh6w_ilb_stub.s"),
    ..OVERLORD
};

pub(super) const OVERLORD_20011104: IopBuild = IopBuild {
    version: "20011104",
    sha256: "ad030cf8e74ebda385ad4dd32cf1feb6e1fc3e7c665f3fc90f46d5a2664dc93a",
    defines: &["REVISION=20011104", "MEDIA_DVD", "VIDEO_PAL", "REGION_EU"],
    debug_import_source: Some("/tmp/cc0IkaSo_ilb_stub.s"),
    ..OVERLORD
};

pub(super) const OVERLORD_20011111: IopBuild = IopBuild {
    version: "20011111",
    sha256: "7ea80571d692e8ae9f516f28668d37267c14bb826a7795df440318b05c6321fb",
    defines: &[
        "REVISION=20011111",
        "MEDIA_CD",
        "VIDEO_PAL",
        "REGION_EU",
        "DEMO",
    ],
    debug_import_source: Some("/tmp/ccvWdLOm_ilb_stub.s"),
    ..OVERLORD
};

pub(super) const OVERLORD_20011121: IopBuild = IopBuild {
    version: "20011121",
    sha256: "265a697e65062117793f152b48c2456cd19c5537569bbbe6ea5dbcddc5813646",
    defines: &["REVISION=20011121", "MEDIA_DVD", "VIDEO_NTSC", "REGION_JP"],
    debug_import_source: Some("/tmp/ccz4l3Vr_ilb_stub.s"),
    ..OVERLORD
};

impl IopBuild {
    pub fn output_path(&self) -> String {
        format!("{}/{}", self.output_dir, self.filename)
    }
}

pub(super) fn ninja_section(
    repo_root: &Path,
    build_dir: &Path,
    builds: &[IopBuild],
) -> io::Result<String> {
    let mut ninja = String::from(
        r#"
# IOP modules
# repo_root and build_dir are provided by the top-level generator.
iop_tool_dir = $repo_root/toolchain/bin/iop
iop_host_target = $repo_root/build/host-tools

rule iop_host_tools
  command = cargo build --release --locked --manifest-path "$repo_root/Cargo.toml" --target-dir "$iop_host_target" -p ioplibgen -p ioplibld -p iopfixup
  description = BUILD Rust IOP tools
  restat = 1
build $iop_host_target/release/ioplibgen $iop_host_target/release/ioplibld $iop_host_target/release/iopfixup: iop_host_tools $repo_root/Cargo.toml $repo_root/Cargo.lock $repo_root/ioplibgen/Cargo.toml $repo_root/ioplibgen/src/main.rs $repo_root/ioplibld/Cargo.toml $repo_root/ioplibld/src/main.rs $repo_root/iopfixup/Cargo.toml $repo_root/iopfixup/src/main.rs

rule iop_preprocess
  command = cd "$build_dir/$source_workdir" && : > "$build_dir/$out.d" && SUNPRO_DEPENDENCIES="$build_dir/$out.d $out" "$iop_tool_dir/cpp" -nostdinc $defines $include_dirs "$source_name" "$build_dir/$out"
  depfile = $out.d
  deps = gcc
  description = IOP CPP $out
rule iop_compile
  command = "$iop_tool_dir/cc1" -quiet $optimization -G $small_data -Werror-implicit-function-declaration "$in" -o "$out"
  description = IOP CC $out
rule iop_assemble
  command = "$iop_tool_dir/iop-as" $assembler_debug -G 0 -o "$out" "$in"
  description = IOP AS $out
rule iop_library
  command = "$iop_host_target/release/ioplibgen" -d "$out" "$in"
  description = IOP ILB $out
rule iop_exports
  command = "$iop_host_target/release/ioplibgen" -e "$out" "$in"
  description = IOP EXPORTS $out
rule iop_imports
  command = "$iop_host_target/release/ioplibld" $import_filename -s "$out" $objects : $libraries
  description = IOP IMPORTS $out
rule iop_link
  command = "$iop_tool_dir/iop-ld" -r -d -G $link_small_data $linker_flags -o "$out" $in
  description = IOP LINK $out
rule iop_irx
  command = "$iop_host_target/release/iopfixup" $strip -r "$out" -e $entry "$in"
  description = IOP IRX $out
"#,
    );
    for build in builds {
        append_module(&mut ninja, repo_root, build_dir, build)?;
    }
    Ok(ninja)
}

fn edge(
    ninja: &mut String,
    output: &str,
    rule: &str,
    inputs: &str,
    tool: &str,
    variables: &[(&str, String)],
) {
    ninja.push_str(&format!("build {output}: iop_{rule} {inputs} | {tool}\n"));
    for (name, value) in variables {
        ninja.push_str(&format!("  {name} = {value}\n"));
    }
}

fn append_module(
    ninja: &mut String,
    repo_root: &Path,
    build_dir: &Path,
    build: &IopBuild,
) -> io::Result<()> {
    let out = build.output_dir;
    let source_dir = repo_root.join(build.source_dir);
    // Preprocessing needs a faked build tree with the right source layout.
    // These folder names can contain usernames, randomly generated /tmp subdirectories,
    // and can't be recreated without this hack.
    let source_workdir = format!("{out}/src");
    let source_workdir_path = build_dir.join(&source_workdir);
    fs::create_dir_all(&source_workdir_path)?;
    let include_dirs = std::iter::once(source_dir.clone())
        .chain(build.include_dirs.iter().map(|p| repo_root.join(p)))
        .map(|p| format!("-I\"{}\"", p.display()))
        .collect::<Vec<_>>()
        .join(" ");
    let defines = build
        .defines
        .iter()
        .map(|d| format!("-D{d}"))
        .collect::<Vec<_>>()
        .join(" ");
    let assemble = |ninja: &mut String, output: &str, input: &str| {
        edge(
            ninja,
            output,
            "assemble",
            input,
            "$iop_tool_dir/iop-as",
            &[(
                "assembler_debug",
                if build.assembler_debug { "-g" } else { "" }.into(),
            )],
        );
    };
    ninja.push_str(&format!("\n# {} {}\n", build.filename, build.version));
    let mut objects = Vec::new();
    for source in build.sources {
        let source_name = build
            .input_names
            .iter()
            .find(|(name, _)| name == source)
            .map(|(_, alias)| *alias)
            .unwrap_or(source);
        let link = source_workdir_path.join(source_name);
        let target = source_dir.join(source);
        fs::create_dir_all(link.parent().unwrap())?;
        if fs::read_link(&link).ok().as_ref() != Some(&target) {
            if fs::symlink_metadata(&link).is_ok() {
                fs::remove_file(&link)?;
            }
            std::os::unix::fs::symlink(&target, link)?;
        }
        let stem = Path::new(source).file_stem().unwrap().to_str().unwrap();
        let object = format!("{out}/{stem}");
        let input = format!("$repo_root/{}/{source}", build.source_dir);
        objects.push(format!("{object}.o"));
        match Path::new(source).extension().and_then(|ext| ext.to_str()) {
            Some("c") => {
                edge(
                    ninja,
                    &format!("{object}.i"),
                    "preprocess",
                    &input,
                    "$iop_tool_dir/cpp",
                    &[
                        ("source_name", source_name.into()),
                        ("source_workdir", source_workdir.clone()),
                        ("defines", defines.clone()),
                        ("include_dirs", include_dirs.clone()),
                    ],
                );
                edge(
                    ninja,
                    &format!("{object}.s"),
                    "compile",
                    &format!("{object}.i"),
                    "$iop_tool_dir/cc1",
                    &[
                        ("optimization", build.optimization.into()),
                        (
                            "small_data",
                            if build.small_data_sources.contains(source) {
                                "8"
                            } else {
                                "0"
                            }
                            .into(),
                        ),
                    ],
                );
                assemble(ninja, &format!("{object}.o"), &format!("{object}.s"));
            }
            Some("s") => assemble(ninja, &format!("{object}.o"), &input),
            _ => {
                return Err(io::Error::other(format!(
                    "Unsupported IOP source: {source}"
                )));
            }
        }
    }
    if let Some(exports) = build.exports {
        let index = match exports.position {
            ExportPosition::First => 0,
            ExportPosition::Last => objects.len(),
            ExportPosition::Before(name) => objects
                .iter()
                .position(|o| o == &format!("{out}/{name}"))
                .ok_or_else(|| {
                    io::Error::other(format!("Export position references missing object: {name}"))
                })?,
        };
        let object = format!("{out}/{}", exports.object);
        objects.insert(index, format!("{object}.o"));
        edge(
            ninja,
            &format!("{object}.s"),
            "exports",
            &format!("$repo_root/{}/{}", build.source_dir, exports.table),
            "$iop_host_target/release/ioplibgen",
            &[],
        );
        assemble(ninja, &format!("{object}.o"), &format!("{object}.s"));
    }
    let mut libraries = Vec::new();
    for import in build.imports {
        let library = Path::new(import).file_stem().unwrap().to_str().unwrap();
        let table = format!("$repo_root/{import}");
        let output = format!("{out}/{library}.ilb");
        edge(
            ninja,
            &output,
            "library",
            &table,
            "$iop_host_target/release/ioplibgen",
            &[],
        );
        libraries.push(output);
    }
    let objects = objects.join(" ");
    let libraries = libraries.join(" ");
    edge(
        ninja,
        &format!("{out}/imports.s"),
        "imports",
        &format!("{objects} {libraries}"),
        "$iop_host_target/release/ioplibld",
        &[
            ("objects", objects.clone()),
            ("libraries", libraries),
            (
                "import_filename",
                build
                    .debug_import_source
                    .map(|name| format!("--source-name \"{name}\""))
                    .unwrap_or_default(),
            ),
        ],
    );
    assemble(
        ninja,
        &format!("{out}/imports.o"),
        &format!("{out}/imports.s"),
    );
    let linker_script = build
        .linker_script
        .map(|p| format!("$repo_root/{}/{p}", build.source_dir))
        .unwrap_or_default();
    let linker_flags = if linker_script.is_empty() {
        String::new()
    } else {
        format!("-T \"{linker_script}\"")
    };
    edge(
        ninja,
        &format!("{out}/module.o"),
        "link",
        &format!("{objects} {out}/imports.o"),
        &format!("$iop_tool_dir/iop-ld {linker_script}"),
        &[
            ("linker_flags", linker_flags),
            ("link_small_data", build.link_small_data.to_string()),
        ],
    );
    edge(
        ninja,
        &build.output_path(),
        "irx",
        &format!("{out}/module.o"),
        "$iop_host_target/release/iopfixup",
        &[
            ("strip", build.strip.argument().into()),
            ("entry", build.entry.into()),
        ],
    );
    Ok(())
}
