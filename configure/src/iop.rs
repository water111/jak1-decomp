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
    pub compiler: &'static str,
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
    compiler: "$iop_tool_dir/cc1",
    optimization: "-O2",
    strip: StripMode::MinimalSymbols,
    assembler_debug: false,
    small_data_sources: &[],
    exports: None,
    linker_script: None,
    debug_import_source: None,
};

// Both SDRDRV revisions report module version 4.1; the embedded SDK build IDs differ.
pub(super) const SDRDRV_4_1_2400: IopBuild = IopBuild {
    output_dir: "sce/iop/sdrdrv",
    filename: "SDRDRV.IRX",
    version: "4.1 (SDK 2400)",
    sha256: "e82385cf40f89dd905a58230da05c410146bf27f2421322f353e83eb920b13b2",
    source_dir: "sce/iop/sdrdrv",
    sources: &["main.c", "commands.c", "callbacks.c"],
    defines: &["SDRDRV_SDK=2400"],
    imports: &[
        "sce/iop/libsd/libsd.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/loadcore/loadcore-1.3.tbl",
        "sce/iop/sifcmd/sifcmd.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/sysclib/sysclib-1.3.tbl",
        "sce/iop/thbase/thbase.tbl",
    ],
    exports: Some(ExportTable {
        table: "sdrdrv.tbl",
        object: "exports",
        position: ExportPosition::Last,
    }),
    ..DEFAULT_BUILD
};

pub(super) const SDRDRV_4_1_2700: IopBuild = IopBuild {
    version: "4.1 (SDK 2700)",
    sha256: "37b615869fed71ec90760543d83170dc7b7608142a4117d665a0cfbe78c3cc52",
    defines: &["SDRDRV_SDK=2700"],
    imports: &[
        "sce/iop/libsd/libsd-1.5.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/loadcore/loadcore-1.3.tbl",
        "sce/iop/sifcmd/sifcmd.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/sysclib/sysclib-1.3.tbl",
        "sce/iop/thbase/thbase-1.2.tbl",
    ],
    ..SDRDRV_4_1_2400
};

pub(super) const SCRTCHPD_1_1: IopBuild = IopBuild {
    output_dir: "sce/iop/scrtchpd",
    filename: "SCRTCHPD.IRX",
    version: "1.1",
    sha256: "47027ff57a1f1bb47d4bf8ffc16803195864e73776e1f63eecae2e904e237318",
    source_dir: "sce/iop/scrtchpd",
    sources: &["scrtchpd.c"],
    link_small_data: 8, // Place the eight-byte common state in .sbss.
    imports: &[
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/loadcore/loadcore-1.3.tbl",
    ],
    exports: Some(ExportTable {
        table: "scrtchpd.tbl",
        object: "exports",
        position: ExportPosition::Last,
    }),
    ..DEFAULT_BUILD
};

pub(super) const SIO2MAN_2_4: IopBuild = IopBuild {
    defines: &["SIO2MAN_VERSION=0x0204"],
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

pub(super) const SIO2MAN_2_5: IopBuild = IopBuild {
    version: "2.5",
    sha256: "d02dd7bcf83a802c388b6ce1fdd447b4d5c29dcc7ba11000ccd66f8f14714fd6",
    defines: &["SIO2MAN_VERSION=0x0205"],
    imports: &[
        "sce/iop/loadcore/loadcore-1.3.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/dmacman/dmacman.tbl",
        "sce/iop/thbase/thbase.tbl",
        "sce/iop/thevent/thevent.tbl",
        "sce/iop/thsemap/thsemap.tbl",
        "sce/iop/sysclib/sysclib-1.3.tbl",
    ],
    exports: Some(ExportTable {
        table: "sio2man-2.5.tbl",
        object: "exports",
        position: ExportPosition::Last,
    }),
    ..SIO2MAN_2_4
};

pub(super) const SIO2MAN_3_0: IopBuild = IopBuild {
    version: "3.0",
    sha256: "0facd5a832d3114c3477ded714f8766d443705a1440deeb84c59927cf0b6968e",
    defines: &["SIO2MAN_VERSION=0x0300"],
    imports: &[
        "sce/iop/loadcore/loadcore-1.3.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/dmacman/dmacman.tbl",
        "sce/iop/thbase/thbase-1.2.tbl",
        "sce/iop/thevent/thevent.tbl",
        "sce/iop/thsemap/thsemap.tbl",
        "sce/iop/sysclib/sysclib-1.3.tbl",
    ],
    exports: Some(ExportTable {
        table: "sio2man-3.0.tbl",
        object: "exports",
        position: ExportPosition::Last,
    }),
    ..SIO2MAN_2_4
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

pub(super) const MCSERV_2_15: IopBuild = IopBuild {
    version: "2.15",
    sha256: "1195d3cb96f46a13f0342dc14c7bc4f55240aad2bf85427f7f38a0bea57331e8",
    defines: &["MCSERV_VERSION=0x020f"],
    imports: &[
        "sce/iop/loadcore/loadcore-1.3.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/sysclib/sysclib-1.3.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/sifman/sifman.tbl",
        "sce/iop/sifcmd/sifcmd.tbl",
        "sce/iop/thbase/thbase.tbl",
        "sce/iop/thsemap/thsemap.tbl",
        "sce/iop/mcman/mcman-2.6.tbl",
    ],
    ..MCSERV_2_12
};

pub(super) const MCSERV_2_16: IopBuild = IopBuild {
    version: "2.16",
    sha256: "42790c4999c9deb17bb0fe2ceb155d3b141adf059c3055e11039070be2a8a7a6",
    defines: &["MCSERV_VERSION=0x0210"],
    imports: &[
        "sce/iop/loadcore/loadcore-1.3.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/sysclib/sysclib-1.3.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/sifman/sifman.tbl",
        "sce/iop/sifcmd/sifcmd.tbl",
        "sce/iop/thbase/thbase-1.2.tbl",
        "sce/iop/thsemap/thsemap.tbl",
        "sce/iop/mcman/mcman-2.7.tbl",
    ],
    ..MCSERV_2_12
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

pub(super) const IOPSETUP_20030405: IopBuild = IopBuild {
    version: "20030405",
    sha256: "5cb6cd1444dd3f0aa6f645320e6fa74fac58907da86bbd8b3eb99aae7aecfc41",
    debug_import_source: Some("/tmp/ccXXQttB_ilb_stub.s"),
    defines: &["IOPSETUP_REVISION=20030405"],
    optimization: "-O2",
    imports: &[
        "sce/iop/usbd/usbd.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/sysclib/sysclib-1.3.tbl",
        "sce/iop/thbase/thbase.tbl",
    ],
    ..IOPSETUP
};

pub(super) const IOPSETUP_20030713: IopBuild = IopBuild {
    version: "20030713",
    sha256: "40ce16f522a9167628eae0fa1acba72c9ee779e1aa3ba3fe3a38c3538b05a64b",
    debug_import_source: Some("/tmp/ccWTkR5G_ilb_stub.s"),
    defines: &["IOPSETUP_REVISION=20030713"],
    optimization: "-O3",
    imports: &[
        "sce/iop/usbd/usbd.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/sysclib/sysclib-1.3.tbl",
        "sce/iop/thbase/thbase-1.2.tbl",
    ],
    ..IOPSETUP_20030405
};

// The later Jak 2 binaries differ only in the temporary import filename in .mdebug.
pub(super) const IOPSETUP_20030720: IopBuild = IopBuild {
    version: "20030720",
    sha256: "6bf65a87e6b7960afd28aa9e132791ccd9af46bb8045b2e169cc49784b19d8f4",
    debug_import_source: Some("/tmp/ccwT7gF9_ilb_stub.s"),
    ..IOPSETUP_20030713
};

pub(super) const IOPSETUP_20030818: IopBuild = IopBuild {
    version: "20030818",
    sha256: "66da696a7ce358923be5f3b7036bbe01e8d0d435c0a690bf7a85e92c2b1308a7",
    debug_import_source: Some("/tmp/cc1gT13t_ilb_stub.s"),
    ..IOPSETUP_20030713
};

pub(super) const IOPSETUP_20030826: IopBuild = IopBuild {
    version: "20030826",
    sha256: "468a7c4489043152da3e9fb0c8fcc8de443149bf454d6dd4452544b8c2779083",
    debug_import_source: Some("/tmp/ccJL5BgU_ilb_stub.s"),
    ..IOPSETUP_20030713
};

pub(super) const USBMOUSE_20030405: IopBuild = IopBuild {
    version: "2.2 (20030405)",
    sha256: "c51c3786b45c5dec88037165d9af51b63c804bf4f7643c0f0251a5e5b79ccabf",
    debug_import_source: Some("/tmp/ccGsA6Of_ilb_stub.s"),
    output_dir: "sce/iop/usbmouse",
    filename: "USBMOUSE.IRX",
    source_dir: "sce/iop/usbmouse",
    sources: &["usbmouse.c"],
    strip: StripMode::None,
    optimization: "-O2",
    imports: &[
        "sce/iop/usbd/usbd.tbl",
        "sce/iop/sysmem/sysmem.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/sifcmd/sifcmd.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/sysclib/sysclib-1.3.tbl",
        "sce/iop/thbase/thbase.tbl",
    ],
    ..DEFAULT_BUILD
};

pub(super) const USBMOUSE_20030713: IopBuild = IopBuild {
    version: "2.2 (20030713)",
    sha256: "5ccc422d5dc305466414bcfa07c7acf0d679b70408b24636d552d4723fa55333",
    debug_import_source: Some("/tmp/cc9IIAHp_ilb_stub.s"),
    optimization: "-O3",
    defines: &["USBMOUSE_ORDER_RPC_SYMBOLS"],
    imports: &[
        "sce/iop/usbd/usbd.tbl",
        "sce/iop/sysmem/sysmem.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/sifcmd/sifcmd.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/sysclib/sysclib-1.3.tbl",
        "sce/iop/thbase/thbase-1.2.tbl",
    ],
    ..USBMOUSE_20030405
};

// These later files differ only in the temporary import filename retained in .mdebug.
pub(super) const USBMOUSE_20030720: IopBuild = IopBuild {
    version: "2.2 (20030720)",
    sha256: "52472730c48caaf05623238268617d6c75920d4095086c5f3bd67f62cc643748",
    debug_import_source: Some("/tmp/ccbXNjLP_ilb_stub.s"),
    ..USBMOUSE_20030713
};

pub(super) const USBMOUSE_20030818: IopBuild = IopBuild {
    version: "2.2 (20030818)",
    sha256: "cfa0e94e3334e738b2d7b07f716c6473331378090a7463d65c005ab1096d0ae6",
    debug_import_source: Some("/tmp/ccOp7BOd_ilb_stub.s"),
    ..USBMOUSE_20030713
};

pub(super) const USBMOUSE_20030826: IopBuild = IopBuild {
    version: "2.2 (20030826)",
    sha256: "729572ffc23a1421b89a4db7c87b7458c470d8435b78d05341cbdb5a205ba494",
    debug_import_source: Some("/tmp/cch6BnLJ_ilb_stub.s"),
    ..USBMOUSE_20030713
};

pub(super) const CDVDSTM_2_1: IopBuild = IopBuild {
    output_dir: "sce/iop/cdvdstm",
    filename: "CDVDSTM.IRX",
    version: "2.1",
    sha256: "39a38b90283da2fd2e6ab15339ff9a177fca31360f60a7c8182a6ebc69e2eee2",
    source_dir: "sce/iop/cdvdstm",
    sources: &["main.c", "ee.c", "copy.s"],
    link_small_data: 8,
    assembler_debug: true,
    imports: &[
        "sce/iop/sysmem/sysmem.tbl",
        "sce/iop/loadcore/loadcore-1.3.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/thbase/thbase-1.2.tbl",
        "sce/iop/thevent/thevent.tbl",
        "sce/iop/thsemap/thsemap.tbl",
        "sce/iop/sifman/sifman.tbl",
        "sce/iop/ioman/ioman.tbl",
        "sce/iop/cdvdman/cdvdman.tbl",
    ],
    exports: Some(ExportTable {
        table: "cdvdstm.tbl",
        object: "exports",
        position: ExportPosition::Last,
    }),
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

pub(super) const LIBSD_3_1: IopBuild = IopBuild {
    version: "3.1",
    sha256: "dd593d288fe2f801a991a5822382e3f756f5078b72a5008e85fbd6d37fd73300",
    sources: &[
        "state_v3.s",
        "libsd.c",
        "data.c",
        "effect_registers.c",
        "effect_clear.c",
        "effect.c",
        "init.c",
        "voice_transfer.c",
        "voice_status.c",
        "block_transfer.c",
        "transfers_v3.c",
        "batch.c",
        "registers.c",
        "pitch.c",
        "attributes.c",
        "transfer_callbacks.c",
        "irq_callbacks.c",
        "init_v3.c",
    ],
    imports: &[
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/loadcore/loadcore-1.3.tbl",
        "sce/iop/sifman/sifman.tbl",
        "sce/iop/sysclib/sysclib-1.3.tbl",
        "sce/iop/thevent/thevent.tbl",
    ],
    defines: &["LIBSD_VERSION=0x0301"],
    exports: Some(ExportTable {
        table: "libsd-3.1.tbl",
        object: "exports",
        position: ExportPosition::First,
    }),
    ..LIBSD_1_4
};

pub(super) const LIBSD_3_3: IopBuild = IopBuild {
    version: "3.3",
    sha256: "57cbd80b7c2f966985e14ca18c03e849e3b3eb1216f06a6f4c1e6ca85084ea37",
    sources: &[
        "state_clear.s",
        "state_v3.s",
        "libsd.c",
        "data.c",
        "effect_registers.c",
        "effect_clear.c",
        "effect.c",
        "init.c",
        "voice_transfer.c",
        "voice_status.c",
        "block_transfer.c",
        "transfers_v3.c",
        "batch.c",
        "registers.c",
        "pitch.c",
        "attributes.c",
        "transfer_callbacks.c",
        "irq_callbacks.c",
        "init_v3.c",
    ],
    defines: &["LIBSD_VERSION=0x0303"],
    exports: Some(ExportTable {
        table: "libsd-1.5.tbl",
        object: "exports",
        position: ExportPosition::First,
    }),
    ..LIBSD_3_1
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

// The descriptor remains 2.4; use the embedded driver version to distinguish builds.
pub(super) const USBD_1_0_0: IopBuild = IopBuild {
    version: "2.4 (driver 1.0.0)",
    sha256: "3d46f4b3e3ee7b9120f7bd0926b852792f205208e3b4eefc3d9fc4002223b472",
    defines: &["USBD_DRIVER_VERSION=0x010000"],
    sources: &["hub.c", "controller.c", "version.c", "api.c", "device.c"],
    assembler_debug: true,
    imports: &[
        "sce/iop/sysmem/sysmem.tbl",
        "sce/iop/loadcore/loadcore-1.3.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/thbase/thbase.tbl",
        "sce/iop/thevent/thevent.tbl",
        "sce/iop/thsemap/thsemap.tbl",
        "sce/iop/sysclib/sysclib-1.3.tbl",
    ],
    exports: Some(ExportTable {
        table: "usbd-1.0.0.tbl",
        object: "exports",
        position: ExportPosition::First,
    }),
    ..USBD_2_4
};

pub(super) const USBD_1_1_0: IopBuild = IopBuild {
    version: "2.4 (driver 1.1.0)",
    sha256: "f0e882517547bd689051950eb323f861b70eef8662ed2cac50533ae3cc93c564",
    defines: &["USBD_DRIVER_VERSION=0x010100"],
    imports: &[
        "sce/iop/sysmem/sysmem.tbl",
        "sce/iop/loadcore/loadcore-1.3.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/thbase/thbase-1.2.tbl",
        "sce/iop/thevent/thevent.tbl",
        "sce/iop/thsemap/thsemap.tbl",
        "sce/iop/sysclib/sysclib-1.3.tbl",
    ],
    ..USBD_1_0_0
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

pub(super) const PADMAN_4_3: IopBuild = IopBuild {
    version: "4.3",
    sha256: "8879a8ffb9d67a34c902c80e7d566eb964c02e23b1ee1f70900af065c217f5b2",
    defines: &["PADMAN_VERSION=0x0403"],
    sources: &[
        "core.c",
        "state_v43.s",
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
        "sce/iop/loadcore/loadcore-1.3.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/sio2man/sio2man-2.5.tbl",
        "sce/iop/thbase/thbase.tbl",
        "sce/iop/thevent/thevent.tbl",
        "sce/iop/sifman/sifman.tbl",
        "sce/iop/sifcmd/sifcmd.tbl",
        "sce/iop/vblank/vblank.tbl",
        "sce/iop/sysclib/sysclib-1.3.tbl",
    ],
    ..PADMAN_4_2
};

pub(super) const PADMAN_4_5: IopBuild = IopBuild {
    version: "4.5",
    sha256: "c11d3fffbca7f99f571299eddbbea3a77fa2d0614dbb22ee8f5a4f9a1c22d67f",
    defines: &["PADMAN_VERSION=0x0405"],
    sources: &[
        "core.c",
        "state_v45.s",
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
        "sce/iop/loadcore/loadcore-1.3.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/sio2man/sio2man-3.0.tbl",
        "sce/iop/thbase/thbase-1.2.tbl",
        "sce/iop/thevent/thevent.tbl",
        "sce/iop/sifman/sifman.tbl",
        "sce/iop/sifcmd/sifcmd.tbl",
        "sce/iop/vblank/vblank.tbl",
        "sce/iop/sysclib/sysclib-1.3.tbl",
    ],
    ..PADMAN_4_2
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
        "backup.c",
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

pub(super) const MCMAN_2_32: IopBuild = IopBuild {
    version: "2.32",
    sha256: "804ee51670407c3d71fe56155e76b4a40a1d8f9d3077e12890efcdf6826e542e",
    defines: &["MCMAN_VERSION=0x0220"],
    sources: &[
        "core.c",
        "protocol.c",
        "geometry.c",
        "ps2.c",
        "ps1.c",
        "storage.c",
        "backup.c",
        "driver.c",
        "state_v32.s",
    ],
    imports: &[
        "sce/iop/loadcore/loadcore-1.3.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/sio2man/sio2man-2.5.tbl",
        "sce/iop/sysclib/sysclib-1.3.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/thbase/thbase.tbl",
        "sce/iop/thsemap/thsemap.tbl",
        "sce/iop/modload/modload-1.5.tbl",
        "sce/iop/ioman/ioman.tbl",
        "sce/iop/secrman/secrman.tbl",
        "sce/iop/cdvdman/cdvdman.tbl",
    ],
    exports: Some(ExportTable {
        table: "mcman-2.6.tbl",
        object: "exports",
        position: ExportPosition::First,
    }),
    ..MCMAN_2_21
};

pub(super) const MCMAN_2_34: IopBuild = IopBuild {
    version: "2.34",
    sha256: "cdb1dfd12cb71f60d4834eb888dbbffeb6f6382e3bdffc1a50122034f7969b6b",
    defines: &["MCMAN_VERSION=0x0222"],
    sources: &[
        "core.c",
        "protocol.c",
        "geometry.c",
        "ps2.c",
        "ps1.c",
        "storage.c",
        "backup.c",
        "driver.c",
        "raw_protocol.c",
        "state_v34.s",
    ],
    imports: &[
        "sce/iop/loadcore/loadcore-1.3.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/sio2man/sio2man-3.0.tbl",
        "sce/iop/sysclib/sysclib-1.3.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/thbase/thbase-1.2.tbl",
        "sce/iop/thsemap/thsemap.tbl",
        "sce/iop/modload/modload-1.6.tbl",
        "sce/iop/ioman/ioman.tbl",
        "sce/iop/secrman/secrman.tbl",
        "sce/iop/cdvdman/cdvdman.tbl",
    ],
    exports: Some(ExportTable {
        table: "mcman-2.7.tbl",
        object: "exports",
        position: ExportPosition::First,
    }),
    ..MCMAN_2_21
};

pub(super) const SND989_2_6_8: IopBuild = IopBuild {
    output_dir: "sce/iop/989snd",
    filename: "989SND.IRX",
    version: "2.6.8",
    sha256: "fc81724f7b62062e786c9e461a707342c0c9186580526e3802f91834bfd0e48d",
    source_dir: "sce/iop/989snd",
    sources: &[
        "frontend.c",
        "valloc.c",
        "loader.c",
        "init.c",
        "sram.c",
        "tick.c",
        "playback.c",
        "handlers.c",
        "stream_sound.c",
        "volume.c",
        "pantable.c",
        "reverb.c",
        "midi.c",
        "ame.c",
        "autovol.c",
        "autopan.c",
        "autopitch.c",
        "stream.c",
        "autopitchbend.c",
        "movie_control.c",
        "exports.c",
        "autoverb.c",
        "block.c",
        "random.c",
        "lfo.c",
        "interrupts.c",
        "state.c",
        "small_state.s",
    ],
    imports: &[
        "sce/iop/libsd/libsd.tbl",
        "sce/iop/cdvdman/cdvdman.tbl",
        "sce/iop/sysmem/sysmem.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/ioman/ioman.tbl",
        "sce/iop/loadcore/loadcore-1.2.tbl",
        "sce/iop/sifcmd/sifcmd.tbl",
        "sce/iop/sifman/sifman.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/sysclib/sysclib-1.2.tbl",
        "sce/iop/thbase/thbase.tbl",
        "sce/iop/thsemap/thsemap.tbl",
        "sce/iop/xtimrman/xtimrman.tbl",
    ],
    exports: Some(ExportTable {
        table: "989snd-2.6.8.tbl",
        object: "export_table",
        position: ExportPosition::Before("exports.o"),
    }),
    entry: "_start",
    strip: StripMode::EmptySections,
    defines: &["SND989_VERSION=0x020608"],
    ..DEFAULT_BUILD
};

pub(super) const SND989_2_6_4: IopBuild = IopBuild {
    version: "2.6.4",
    sha256: "c1c856244d8cecee2ffa5be4d0317456d36fff766dd731d1a6de2efef31ffcab",
    defines: &["SND989_VERSION=0x020604"],
    // The -finline-function options changes the layout of functions significantly.
    optimization: "-O2 -finline-functions",
    imports: &[
        "sce/iop/libsd/libsd.tbl",
        "sce/iop/cdvdman/cdvdman.tbl",
        "sce/iop/sysmem/sysmem.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/loadcore/loadcore-1.2.tbl",
        "sce/iop/sifcmd/sifcmd.tbl",
        "sce/iop/sifman/sifman.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/sysclib/sysclib-1.2.tbl",
        "sce/iop/thbase/thbase.tbl",
        "sce/iop/thsemap/thsemap.tbl",
        "sce/iop/xtimrman/xtimrman.tbl",
    ],
    exports: Some(ExportTable {
        table: "989snd-2.6.4.tbl",
        object: "export_table",
        position: ExportPosition::Before("exports.o"),
    }),
    ..SND989_2_6_8
};

pub(super) const SND989_3_0_0: IopBuild = IopBuild {
    version: "3.0.0",
    sha256: "558cb177c4d5de8b53a42b1cd1180047fa2fc81f5f1b299c8f823e70e6dd216a",
    defines: &["SND989_VERSION=0x030000"],
    compiler: "$repo_root/toolchain/bin/iop-2.95.3/cc1",
    optimization: "-O2 -mgas -msplit-addresses",
    ..SND989_2_6_8
};

pub(super) const OVERLORD_JAK2_20030826: IopBuild = IopBuild {
    output_dir: "overlord",
    filename: "OVERLORD.IRX",
    version: "2003-08-26",
    sha256: "ce42292f33798369c263361b63ce0f16c8cd72b1bf4baff8473c8d10addc61bf",
    source_dir: "jak2/overlord",
    defines: &["OVERLORD_REVISION=20030826"],
    sources: &[
        "overlord.c",
        "iso_cd.c",
        "ramdisk.c",
        "fakeiso.c",
        "deviso.c",
        "isocommon.c",
        "isodesc.c",
        "dma.c",
        "iso.c",
        "iso_queue.c",
        "stream.c",
        "srpc.c",
        "vag.c",
        "sbank.c",
        "ssound.c",
        "soundcommon.c",
        "iso_api.c",
        "minilzo.c",
        "plugin.c",
        "streamlfo.c",
        "streamlist.c",
        "spustreams.c",
        "pages.c",
        "list.c",
    ],
    imports: &[
        "sce/iop/cdvdman/cdvdman.tbl",
        "sce/iop/libsd/libsd-1.5.tbl",
        "sce/iop/scrtchpd/scrtchpd.tbl",
        "sce/iop/989snd/snd989-jak2.tbl",
        "sce/iop/sysmem/sysmem.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/loadcore/loadcore-1.3.tbl",
        "sce/iop/sifcmd/sifcmd.tbl",
        "sce/iop/sifman/sifman.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/sysclib/sysclib-1.3.tbl",
        "sce/iop/thbase/thbase-1.2.tbl",
        "sce/iop/thsemap/thsemap.tbl",
        "sce/iop/thmsgbx/thmsgbx.tbl",
        "sce/iop/vblank/vblank.tbl",
    ],
    input_names: &[
        (
            "isocommon.c",
            "/usr/home/agavin/src/jak2/libs/common/isocommon.c",
        ),
        (
            "isodesc.c",
            "/usr/home/agavin/src/jak2/libs/common/isodesc.c",
        ),
        (
            "soundcommon.c",
            "/usr/home/agavin/src/jak2/libs/common/soundcommon.c",
        ),
        (
            "minilzo.c",
            "/usr/home/agavin/src/jak2/libs/common/minilzo.c",
        ),
    ],
    debug_import_source: Some("/tmp/ccgggVxU_ilb_stub.s"),
    strip: StripMode::None,
    link_small_data: 8,
    ..DEFAULT_BUILD
};

pub(super) const OVERLORD_JAK2_20030405: IopBuild = IopBuild {
    version: "2003-04-05",
    sha256: "4f1ec1726e0b6678ecc1f04efb59bf01248c712ffc79a1db2662c75e6f15397c",
    sources: &[
        "overlord.c",
        "iso_cd.c",
        "ramdisk.c",
        "fakeiso.c",
        "deviso.c",
        "isocommon.c",
        "isodesc.c",
        "dma.c",
        "iso.c",
        "iso_queue.c",
        "stream.c",
        "srpc.c",
        "vag.c",
        "sbank.c",
        "ssound.c",
        "soundcommon.c",
        "iso_api.c",
        "minilzo.c",
        "plugin.c",
        "streamlfo.c",
        "streamlist.c",
        "spustreams.c",
    ],
    imports: &[
        "sce/iop/cdvdman/cdvdman.tbl",
        "sce/iop/libsd/libsd-1.5.tbl",
        "sce/iop/scrtchpd/scrtchpd.tbl",
        "sce/iop/989snd/snd989-jak2.tbl",
        "sce/iop/sysmem/sysmem.tbl",
        "sce/iop/intrman/intrman.tbl",
        "sce/iop/loadcore/loadcore-1.3.tbl",
        "sce/iop/sifcmd/sifcmd.tbl",
        "sce/iop/sifman/sifman.tbl",
        "sce/iop/stdio/stdio-1.3.tbl",
        "sce/iop/sysclib/sysclib-1.3.tbl",
        "sce/iop/thbase/thbase.tbl",
        "sce/iop/thsemap/thsemap.tbl",
        "sce/iop/thmsgbx/thmsgbx.tbl",
        "sce/iop/vblank/vblank.tbl",
    ],
    defines: &["OVERLORD_REVISION=20030405"],
    debug_import_source: Some("/tmp/cc5yyieP_ilb_stub.s"),
    ..OVERLORD_JAK2_20030826
};

pub(super) const OVERLORD_JAK2_20030713: IopBuild = IopBuild {
    version: "2003-07-13",
    sha256: "356c377b6df3e59581c73a7034e0c9f266ab8c2f34edbd9818d7f257e7a62456",
    defines: &["OVERLORD_REVISION=20030713"],
    debug_import_source: Some("/tmp/ccnxKPTh_ilb_stub.s"),
    ..OVERLORD_JAK2_20030826
};

pub(super) const OVERLORD_JAK2_20030720: IopBuild = IopBuild {
    version: "2003-07-20",
    sha256: "6d537fda8d1abeabe120b076240a42f3479a9cf55569cfb4ceb18c2d0d69d016",
    defines: &["OVERLORD_REVISION=20030720"],
    debug_import_source: Some("/tmp/ccjDJN3f_ilb_stub.s"),
    ..OVERLORD_JAK2_20030826
};

pub(super) const OVERLORD_JAK2_20030818: IopBuild = IopBuild {
    version: "2003-08-18",
    defines: &["OVERLORD_REVISION=20030818"],
    sha256: "e98d91c2a9829814269b3a8d4ba3fd451be0c00a231f7dc7dc61c2815e7eb14e",
    debug_import_source: Some("/tmp/cc7yc0Ov_ilb_stub.s"),
    ..OVERLORD_JAK2_20030826
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
  command = cd "$build_dir/$source_workdir" && : > "$build_dir/$out.d" && SUNPRO_DEPENDENCIES="$build_dir/$out.d $out" $source_name_command "$iop_tool_dir/cpp" -nostdinc $defines $include_dirs "$source_name" "$build_dir/$out"
  depfile = $out.d
  deps = gcc
  description = IOP CPP $out
rule iop_compile
  command = "$compiler" -quiet $optimization -G $small_data -Werror-implicit-function-declaration "$in" -o "$out"
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
        // Absolute build-machine paths belong in debug metadata, not on the host
        // filesystem. Rewrite only preprocessor line markers for these sources.
        let (source_name, source_name_command, preprocess_tools) =
            if Path::new(source_name).is_absolute() {
                let local_name = Path::new(source).file_name().unwrap().to_str().unwrap();
                let configure = std::env::current_exe()?;
                (
                    local_name,
                    format!(
                        "\"{}\" --iop-source-name \"{local_name}\" \"{source_name}\"",
                        configure.display()
                    ),
                    format!("$iop_tool_dir/cpp {}", configure.display()),
                )
            } else {
                (source_name, String::new(), "$iop_tool_dir/cpp".into())
            };
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
                    &preprocess_tools,
                    &[
                        ("source_name", source_name.into()),
                        ("source_name_command", source_name_command),
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
                    build.compiler,
                    &[
                        ("compiler", build.compiler.into()),
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

// Invoke cpp normally, then preserve the original absolute source name in its
// line markers. This affects compiler metadata only; source text is untouched.
pub(super) fn preprocess_named_source(args: Vec<String>) -> io::Result<()> {
    if args.len() < 4 {
        return Err(io::Error::other(
            "Expected source name, debug name, and cpp command",
        ));
    }
    let status = std::process::Command::new(&args[2])
        .args(&args[3..])
        .status()?;
    if !status.success() {
        return Err(io::Error::other(format!("cpp {status}")));
    }
    let output = args.last().unwrap();
    let text = fs::read_to_string(output)?;
    let old = format!("\"{}\"", args[0]);
    let new = format!("\"{}\"", args[1]);
    let mut mapped = String::with_capacity(text.len());
    for line in text.split_inclusive('\n') {
        if line.starts_with("# ") {
            mapped.push_str(&line.replace(&old, &new));
        } else {
            mapped.push_str(line);
        }
    }
    fs::write(output, mapped)
}
