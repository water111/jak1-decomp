// Generate the game's Ninja build file and run the build.

mod sha256;

use std::fs;
use std::io;
use std::path::Path;
use std::process::Command;

fn write_if_changed(path: &Path, contents: &str) -> io::Result<()> {
    match fs::read(path) {
        Ok(existing) if existing == contents.as_bytes() => return Ok(()),
        Ok(_) => {}
        Err(error) if error.kind() == io::ErrorKind::NotFound => {}
        Err(error) => return Err(error),
    }

    fs::write(path, contents)
}

struct Revision {
    name: &'static str,
    description: &'static str,
    defines: &'static [&'static str],
    overlord_import_source_name: &'static str,
    overlord_sha256: &'static str,
}

// Hashes and assembler filenames from the original Jak 1 reference IRXs.
const REVISIONS: &[Revision] = &[
    Revision {
        name: "jak1/20010903/ntsc/us",
        description: "NTSC September Preview",
        defines: &["REVISION=20010903", "MEDIA_DVD", "VIDEO_NTSC", "REGION_US"],
        overlord_import_source_name: "/tmp/ccZqpG4m_ilb_stub.s",
        overlord_sha256: "308136f13a9b482c17e5a4ebad11d3516c5c968ea0667b5983433bc2f567d228",
    },
    Revision {
        name: "jak1/20010920/ntsc/jp",
        description: "NTSC Japanese Demo",
        defines: &[
            "REVISION=20010920",
            "MEDIA_CD",
            "VIDEO_NTSC",
            "REGION_JP",
            "DEMO",
        ],
        overlord_import_source_name: "/tmp/ccbEm3wW_ilb_stub.s",
        overlord_sha256: "c61f18891950cdf25bef2f5f01c9fe61971d2dbe5bd86d00bf6e8d6fc3b9d253",
    },
    Revision {
        name: "jak1/20011002/ntsc/us",
        description: "NTSC Cingular Demo",
        defines: &[
            "REVISION=20011002",
            "MEDIA_CD",
            "VIDEO_NTSC",
            "REGION_US",
            "DEMO",
            "SHARED_DEMO",
        ],
        overlord_import_source_name: "/tmp/ccBwnfyn_ilb_stub.s",
        overlord_sha256: "92c6a94204c1c260b9117161fc2745cabbaf83cd4654183e0b1415ffca531db1",
    },
    Revision {
        name: "jak1/20011019/ntsc/us",
        description: "NTSC October Prototype",
        defines: &["REVISION=20011019", "MEDIA_DVD", "VIDEO_NTSC", "REGION_US"],
        overlord_import_source_name: "/tmp/ccoZdIg7_ilb_stub.s",
        overlord_sha256: "3666828b35268d746603f6d2b0cb07e6a4a1cdee9980beb5e2326d19f792d621",
    },
    Revision {
        name: "jak1/20011031/ntsc/us",
        description: "NTSC Retail",
        defines: &["REVISION=20011031", "MEDIA_DVD", "VIDEO_NTSC", "REGION_US"],
        overlord_import_source_name: "/tmp/cc4fRwXd_ilb_stub.s",
        overlord_sha256: "fe4869449b4046aa937b08dae6138eb77bd52e9562807e02c2af9eb2a740e974",
    },
    Revision {
        name: "jak1/20011102/pal/eu",
        description: "PAL OPS2M Demo",
        defines: &[
            "REVISION=20011102",
            "MEDIA_DVD",
            "VIDEO_PAL",
            "REGION_EU",
            "DEMO",
            "SHARED_DEMO",
        ],
        overlord_import_source_name: "/tmp/cc3gKh6w_ilb_stub.s",
        overlord_sha256: "076dfe24f616f0866ea154ab983b33eacdae1eadc37e80f853a9db08901039fa",
    },
    Revision {
        name: "jak1/20011104/pal/eu",
        description: "PAL Retail",
        defines: &["REVISION=20011104", "MEDIA_DVD", "VIDEO_PAL", "REGION_EU"],
        overlord_import_source_name: "/tmp/cc0IkaSo_ilb_stub.s",
        overlord_sha256: "ad030cf8e74ebda385ad4dd32cf1feb6e1fc3e7c665f3fc90f46d5a2664dc93a",
    },
    Revision {
        name: "jak1/20011111/pal/eu",
        description: "PAL Demo",
        defines: &[
            "REVISION=20011111",
            "MEDIA_CD",
            "VIDEO_PAL",
            "REGION_EU",
            "DEMO",
        ],
        overlord_import_source_name: "/tmp/ccvWdLOm_ilb_stub.s",
        overlord_sha256: "7ea80571d692e8ae9f516f28668d37267c14bb826a7795df440318b05c6321fb",
    },
    Revision {
        name: "jak1/20011121/ntsc/jp",
        description: "NTSC Japanese Retail",
        defines: &["REVISION=20011121", "MEDIA_DVD", "VIDEO_NTSC", "REGION_JP"],
        overlord_import_source_name: "/tmp/ccz4l3Vr_ilb_stub.s",
        overlord_sha256: "265a697e65062117793f152b48c2456cd19c5537569bbbe6ea5dbcddc5813646",
    },
    Revision {
        name: "jak1/20011127/ntsc/us",
        description: "NTSC Greatest Hits",
        defines: &["REVISION=20011127", "MEDIA_DVD", "VIDEO_NTSC", "REGION_US"],
        overlord_import_source_name: "/tmp/ccz4l3Vr_ilb_stub.s",
        overlord_sha256: "265a697e65062117793f152b48c2456cd19c5537569bbbe6ea5dbcddc5813646",
    },
    Revision {
        name: "jak1/20020121/ntsc/kr",
        description: "NTSC Korean Retail",
        defines: &["REVISION=20020121", "MEDIA_DVD", "VIDEO_NTSC", "REGION_KR"],
        overlord_import_source_name: "/tmp/cc4fRwXd_ilb_stub.s",
        overlord_sha256: "fe4869449b4046aa937b08dae6138eb77bd52e9562807e02c2af9eb2a740e974",
    },
];

fn print_known_revisions() {
    eprintln!("<revision> must be `all` or one of the known revisions:");
    for revision in REVISIONS {
        eprintln!("  {} - {}", revision.name, revision.description);
    }
}

const OVERLORD_FILES: &[&str] = &[
    "ramdisk",
    "fakeiso",
    "deviso",
    "iso_cd",
    "dma",
    "iso",
    "stream",
    "isocommon",
    "isodesc",
    "iso_queue",
    "srpc",
    "sbank",
    "ssound",
    "soundcommon",
    "overlord",
    "iso_api",
];

// Import-library order affects the linked layout.
const OVERLORD_LIBRARIES: &[&str] = &[
    "cdvdman", "libsd", "snd989", "sysmem", "intrman", "ioman", "loadcore", "sifcmd", "sifman",
    "stdio", "sysclib", "thbase", "thsemap", "thmsgbx", "vblank",
];

enum BuildResult {
    Failed(String),
    Succeeded { overlord_hash: io::Result<String> },
}

fn main() {
    let mut args = std::env::args().skip(1);
    let (Some(name), None) = (args.next(), args.next()) else {
        eprintln!("Usage: configure <revision|all>");
        print_known_revisions();
        std::process::exit(1);
    };

    let revisions = if name == "all" {
        REVISIONS
    } else if let Some(revision) = REVISIONS.iter().find(|x| x.name == name) {
        std::slice::from_ref(revision)
    } else {
        eprintln!("Unknown revision {name}");
        print_known_revisions();
        std::process::exit(1);
    };

    let results: Vec<_> = revisions
        .iter()
        .map(|revision| {
            let result = build_revision(revision)
                .unwrap_or_else(|error| BuildResult::Failed(error.to_string()));
            (revision, result)
        })
        .collect();

    let mut failed = false;
    for (revision, result) in results {
        println!("\nRevision {}", revision.name);
        match result {
            BuildResult::Failed(error) => {
                println!("Build failed: {error}");
                println!("[FAIL] OVERLORD.IRX (not checked: build failed)");
                failed = true;
            }
            BuildResult::Succeeded { overlord_hash } => {
                println!("Build succeeded");
                match overlord_hash {
                    Ok(actual) if actual == revision.overlord_sha256 => {
                        println!("[OK] OVERLORD.IRX")
                    }
                    Ok(actual) => {
                        println!(
                            "[FAIL] OVERLORD.IRX\n  expected: {}\n  actual:   {actual}",
                            revision.overlord_sha256
                        );
                        failed = true;
                    }
                    Err(error) => {
                        println!("[FAIL] OVERLORD.IRX: {error}");
                        failed = true;
                    }
                }
            }
        }
    }
    if failed {
        std::process::exit(1);
    }
}

fn build_revision(revision: &Revision) -> io::Result<BuildResult> {
    println!(
        "Configuring build for revision {} - {}",
        revision.name, revision.description
    );

    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .expect("configure crate should be inside the repository");

    let source_dir = repo_root.join("next");
    let tool_dir = repo_root.join("toolchain/bin");
    let build_dir = repo_root.join("build").join(revision.name);
    fs::create_dir_all(build_dir.join("overlord"))?;
    fs::create_dir_all(build_dir.join("ilb"))?;
    fs::create_dir_all(build_dir.join("src/overlord"))?;
    fs::create_dir_all(build_dir.join("src/common"))?;

    // Preserve the original compiler input names without moving the sources.
    for file in OVERLORD_FILES {
        let directory = if matches!(*file, "isocommon" | "isodesc" | "soundcommon") {
            "common"
        } else {
            "overlord"
        };
        let link = build_dir.join(format!("src/{directory}/{file}.c"));
        let target = source_dir.join(format!("overlord/{file}.c"));
        if fs::read_link(&link).ok().as_ref() != Some(&target) {
            std::os::unix::fs::symlink(target, link)?;
        }
    }

    let defines = revision
        .defines
        .iter()
        .map(|x| format!("-D{x}"))
        .collect::<Vec<_>>()
        .join(" ");

    let mut ninja = format!(
        r#"# Generated by configure.
ninja_required_version = 1.3

repo_root = {repo_root}
build_dir = {build_dir}
source_dir = {source_dir}
tool_dir = {tool_dir}
defines = {defines}
overlord_import_source_name = {overlord_import_source_name}

host_target = $repo_root/build/host-tools
iop_libgen = $host_target/release/ioplibgen
iop_libld = $host_target/release/ioplibld
iop_cpp = $tool_dir/iop/cpp
iop_cc = $tool_dir/iop/cc1
iop_as = $tool_dir/iop/iop-as
iop_ld = $tool_dir/iop/iop-ld
iop_fixup = $host_target/release/iopfixup

iop_cppflags = -nostdinc -I"$source_dir" $defines
iop_cflags = -O0 -G 0 -Werror-implicit-function-declaration
iop_asflags = -G 0

rule host_tools
  command = cargo build --release --locked --manifest-path "$repo_root/Cargo.toml" --target-dir "$host_target" -p ioplibgen -p ioplibld -p iopfixup
  description = BUILD Rust IOP tools
  restat = 1

build $iop_libgen $iop_libld $iop_fixup: host_tools $repo_root/Cargo.toml $repo_root/Cargo.lock $repo_root/ioplibgen/Cargo.toml $repo_root/ioplibgen/src/main.rs $repo_root/ioplibld/Cargo.toml $repo_root/ioplibld/src/main.rs $repo_root/iopfixup/Cargo.toml $repo_root/iopfixup/src/main.rs

rule iop_preprocess
  command = cd "$build_dir/src/overlord" && : > "$build_dir/$out.d" && SUNPRO_DEPENDENCIES="$build_dir/$out.d $out" "$iop_cpp" $iop_cppflags "$source_name" "$build_dir/$out"
  depfile = $out.d
  deps = gcc
  description = IOP CPP $out

rule iop_compile
  command = "$iop_cc" -quiet $iop_cflags "$in" -o "$out"
  description = IOP CC $out

rule iop_assemble
  command = "$iop_as" $iop_asflags -o "$out" "$in"
  description = IOP AS $out

rule iop_library
  command = "$iop_libgen" -d "$out" "$in"
  description = IOP ILB $out

rule iop_imports
  command = "$iop_libld" -s "$out" $objects : $libraries && sed -i '1i.file 1 "$overlord_import_source_name"' "$out"
  description = IOP IMPORTS $out

rule iop_link
  command = "$iop_ld" -r -d -G 8 -o "$out" $in
  description = IOP LINK $out

rule iop_irx
  command = "$iop_fixup" -r "$out" -e start "$in"
  description = IOP IRX $out
"#,
        repo_root = repo_root.display(),
        build_dir = build_dir.display(),
        source_dir = source_dir.display(),
        tool_dir = tool_dir.display(),
        defines = defines,
        overlord_import_source_name = revision.overlord_import_source_name,
    );

    // OVERLORD compilation:
    let mut objects = Vec::new();
    for file in OVERLORD_FILES {
        let output = format!("overlord/{file}");
        let source_name = if matches!(*file, "isocommon" | "isodesc" | "soundcommon") {
            format!("../common/{file}.c")
        } else {
            format!("{file}.c")
        };
        ninja.push_str(&format!(
            "\
build {output}.i: iop_preprocess $source_dir/overlord/{file}.c | $iop_cpp
  source_name = {source_name}
build {output}.s: iop_compile {output}.i | $iop_cc
build {output}.o: iop_assemble {output}.s | $iop_as

"
        ));
        objects.push(format!("{output}.o"));
    }

    let mut libraries = Vec::new();
    for library in OVERLORD_LIBRARIES {
        let output = format!("ilb/{library}.ilb");
        ninja.push_str(&format!(
            "build {output}: iop_library $repo_root/sce/iop/{library}/{library}.tbl | $iop_libgen\n"
        ));
        libraries.push(output);
    }

    let objects = objects.join(" ");
    let libraries = libraries.join(" ");
    ninja.push_str(&format!(
        r#"
build overlord/imports.s: iop_imports {objects} {libraries} | $iop_libld
  objects = {objects}
  libraries = {libraries}

build overlord/imports.o: iop_assemble overlord/imports.s | $iop_as
build overlord/OVERLORD.o: iop_link {objects} overlord/imports.o | $iop_ld
build overlord/OVERLORD.IRX: iop_irx overlord/OVERLORD.o | $iop_fixup
"#
    ));

    write_if_changed(&build_dir.join("build.ninja"), &ninja)?;

    let status = Command::new("ninja").current_dir(&build_dir).status()?;
    if !status.success() {
        return Ok(BuildResult::Failed(format!("Ninja {status}")));
    }
    let overlord_hash =
        fs::read(build_dir.join("overlord/OVERLORD.IRX")).map(|data| sha256::hash(&data));
    Ok(BuildResult::Succeeded { overlord_hash })
}
