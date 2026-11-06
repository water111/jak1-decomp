mod ee;
mod iop;
mod sha256;
use ee::EeBuild;
use iop::IopBuild;
use std::{fs, io, path::Path, process::Command};

struct Revision {
    name: &'static str,
    description: &'static str,
    iop_builds: &'static [IopBuild],
    ee_builds: &'static [EeBuild],
    kernel: Option<Kernel>,
}

struct Kernel {
    frame_source: &'static str,
    sha256: &'static str,
}

// Some OVERLORDs are the same between game revisions and for those we just
// name the OVERLORD after the earliest revision.
const REVISIONS: &[Revision] = &[
    Revision {
        name: "jak1/20010903/ntsc/us",
        description: "NTSC September Preview",
        kernel: Some(Kernel {
            frame_source: "/tmp/ccsJImXa.c",
            sha256: "da3eaeeae4a826bbfaa0fae7d975da29804126934b6838d248fc942fa2a327e5",
        }),
        ee_builds: &[
            EeBuild {
                defines: &[
                    "VIDEO_NTSC=1",
                    "REGION_US=1",
                    "MEDIA_DVD=2",
                    "REVISION=20010903",
                ],
                ..ee::KERNEL
            },
            ee::STARTUP,
            ee::LIBKERNL,
            ee::LIBC_EARLY,
            ee::LIBM,
            ee::LIBGCC,
            ee::LIBCDVD,
            ee::LIBPAD,
            ee::LIBGRAPH_EARLY,
            ee::LIBMC,
            ee::LIBDMA,
        ],
        iop_builds: &[
            iop::OVERLORD_20010903,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
            iop::MCMAN_2_21,
            iop::SND989_2_6_4,
            iop::IOPSETUP,
        ],
    },
    Revision {
        name: "jak1/20010920/ntsc/jp",
        description: "NTSC Japanese Demo",
        kernel: Some(Kernel {
            frame_source: "/tmp/ccsyJnJg.c",
            sha256: "3172bb7dc59e7c96ca2532f2283b8d065ff27eb730c76f7f624ec3fae4996d21",
        }),
        ee_builds: &[
            EeBuild {
                defines: &[
                    "VIDEO_NTSC=1",
                    "REGION_JP=3",
                    "MEDIA_CD=1",
                    "REVISION=20010920",
                    "DEMO",
                ],
                ..ee::KERNEL
            },
            ee::STARTUP,
            ee::LIBKERNL,
            ee::LIBC,
            ee::LIBM,
            ee::LIBGCC,
            ee::LIBCDVD,
            ee::LIBPAD,
            ee::LIBGRAPH,
            ee::LIBMC,
            ee::LIBDMA,
            ee::LIBSCF,
        ],
        iop_builds: &[
            iop::OVERLORD_20010920,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
            iop::MCMAN_2_21,
            iop::SND989_2_6_4,
        ],
    },
    Revision {
        name: "jak1/20011002/ntsc/us",
        description: "NTSC Cingular Demo",
        kernel: Some(Kernel {
            frame_source: "/tmp/ccxK3wyw.c",
            sha256: "d4ca818ecf0254534dd55a4e044beab89872feab2665fb86e550112c371d2952",
        }),
        ee_builds: &[
            EeBuild {
                defines: &[
                    "VIDEO_NTSC=1",
                    "REGION_US=1",
                    "MEDIA_CD=1",
                    "REVISION=20011002",
                    "DEMO",
                    "SHARED_DEMO",
                ],
                ..ee::KERNEL
            },
            ee::STARTUP,
            ee::LIBKERNL,
            ee::LIBC,
            ee::LIBM,
            ee::LIBGCC,
            ee::LIBCDVD,
            ee::LIBPAD,
            ee::LIBGRAPH,
            ee::LIBMC,
            ee::LIBDMA,
            ee::LIBSCF,
            ee::LIBSCEDEMO,
        ],
        iop_builds: &[
            iop::OVERLORD_20011002,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
            iop::MCMAN_2_21,
            iop::SND989_2_6_4,
        ],
    },
    Revision {
        name: "jak1/20011019/ntsc/us",
        description: "NTSC October Prototype",
        kernel: Some(Kernel {
            frame_source: "/tmp/cch7zhoJ.c",
            sha256: "9c711b8d7a30c862e152007189d33a1679982adaa5ce5612fba16bf35178a88b",
        }),
        ee_builds: &[
            EeBuild {
                defines: &[
                    "VIDEO_NTSC=1",
                    "REGION_US=1",
                    "MEDIA_DVD=2",
                    "REVISION=20011019",
                ],
                ..ee::KERNEL
            },
            ee::STARTUP,
            ee::LIBKERNL,
            ee::LIBC,
            ee::LIBM,
            ee::LIBGCC,
            ee::LIBCDVD,
            ee::LIBPAD,
            ee::LIBGRAPH,
            ee::LIBMC,
            ee::LIBDMA,
            ee::LIBSCF,
        ],
        iop_builds: &[
            iop::OVERLORD_20011019,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
            iop::MCMAN_2_21,
            iop::SND989_2_6_8,
            iop::IOPSETUP,
        ],
    },
    Revision {
        name: "jak1/20011031/ntsc/us",
        description: "NTSC Retail",
        kernel: Some(Kernel {
            frame_source: "/tmp/ccX4drDa.c",
            sha256: "b3051491a4f41bd90b345bbd91fe60b4c85f3060bacd0552903645f8d27268d5",
        }),
        ee_builds: &[
            EeBuild {
                defines: &[
                    "VIDEO_NTSC=1",
                    "REGION_US=1",
                    "MEDIA_DVD=2",
                    "REVISION=20011031",
                ],
                ..ee::KERNEL
            },
            ee::STARTUP,
            ee::LIBKERNL,
            ee::LIBC,
            ee::LIBM,
            ee::LIBGCC,
            ee::LIBCDVD,
            ee::LIBPAD,
            ee::LIBGRAPH,
            ee::LIBMC,
            ee::LIBDMA,
            ee::LIBSCF,
        ],
        iop_builds: &[
            iop::OVERLORD_20011031,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
            iop::MCMAN_2_21,
            iop::SND989_2_6_8,
        ],
    },
    Revision {
        name: "jak1/20011102/pal/eu",
        description: "PAL OPS2M Demo",
        kernel: Some(Kernel {
            frame_source: "/tmp/ccKPi7V4.c",
            sha256: "61996c5241897b57c34d77409b3019c15036c410c4e10744c81da7889d9a214e",
        }),
        ee_builds: &[
            EeBuild {
                defines: &[
                    "VIDEO_PAL=2",
                    "REGION_EU=2",
                    "MEDIA_DVD=2",
                    "REVISION=20011102",
                    "DEMO",
                    "SHARED_DEMO",
                ],
                ..ee::KERNEL
            },
            ee::STARTUP,
            ee::LIBKERNL,
            ee::LIBC,
            ee::LIBM,
            ee::LIBGCC,
            ee::LIBCDVD,
            ee::LIBPAD,
            ee::LIBGRAPH,
            ee::LIBMC,
            ee::LIBDMA,
            ee::LIBSCF,
            ee::LIBSCEDEMO,
        ],
        iop_builds: &[
            iop::OVERLORD_20011102,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
            iop::MCMAN_2_21,
            iop::SND989_2_6_8,
        ],
    },
    Revision {
        name: "jak1/20011104/pal/eu",
        description: "PAL Retail",
        kernel: Some(Kernel {
            frame_source: "/tmp/ccn1liKu.c",
            sha256: "ad5b47891e4fdfd95d349e81b59df48eed5fa195053c49520276f09ce54aa7ba",
        }),
        ee_builds: &[
            EeBuild {
                defines: &[
                    "VIDEO_PAL=2",
                    "REGION_EU=2",
                    "MEDIA_DVD=2",
                    "REVISION=20011104",
                ],
                ..ee::KERNEL
            },
            ee::STARTUP,
            ee::LIBKERNL,
            ee::LIBC,
            ee::LIBM,
            ee::LIBGCC,
            ee::LIBCDVD,
            ee::LIBPAD,
            ee::LIBGRAPH,
            ee::LIBMC,
            ee::LIBDMA,
            ee::LIBSCF,
        ],
        iop_builds: &[
            iop::OVERLORD_20011104,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
            iop::MCMAN_2_21,
            iop::SND989_2_6_8,
        ],
    },
    Revision {
        name: "jak1/20011111/pal/eu",
        description: "PAL Demo",
        kernel: Some(Kernel {
            frame_source: "/tmp/ccvMt3Na.c",
            sha256: "a69ef4282d3a81c7189781a25464942f38a3345ddfaf347e3831fef1ceb4a258",
        }),
        ee_builds: &[
            EeBuild {
                defines: &[
                    "VIDEO_PAL=2",
                    "REGION_EU=2",
                    "MEDIA_CD=1",
                    "REVISION=20011111",
                    "DEMO",
                ],
                ..ee::KERNEL
            },
            ee::STARTUP,
            ee::LIBKERNL,
            ee::LIBC,
            ee::LIBM,
            ee::LIBGCC,
            ee::LIBCDVD,
            ee::LIBPAD,
            ee::LIBGRAPH,
            ee::LIBMC,
            ee::LIBDMA,
            ee::LIBSCF,
        ],
        iop_builds: &[
            iop::OVERLORD_20011111,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
            iop::MCMAN_2_21,
            iop::SND989_2_6_8,
        ],
    },
    Revision {
        name: "jak1/20011121/ntsc/jp",
        description: "NTSC Japanese Retail",
        kernel: Some(Kernel {
            frame_source: "/tmp/ccRTKVAZ.c",
            sha256: "7ad110ee363574135dbd648d0c459f96b0000c9df04539a156d3792a366ffa07",
        }),
        ee_builds: &[
            EeBuild {
                defines: &[
                    "VIDEO_NTSC=1",
                    "REGION_JP=3",
                    "MEDIA_DVD=2",
                    "REVISION=20011121",
                ],
                ..ee::KERNEL
            },
            ee::STARTUP,
            ee::LIBKERNL,
            ee::LIBC,
            ee::LIBM,
            ee::LIBGCC,
            ee::LIBCDVD,
            ee::LIBPAD,
            ee::LIBGRAPH,
            ee::LIBMC,
            ee::LIBDMA,
            ee::LIBSCF,
        ],
        iop_builds: &[
            iop::OVERLORD_20011121,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
            iop::MCMAN_2_21,
            iop::SND989_2_6_8,
        ],
    },
    Revision {
        name: "jak1/20011127/ntsc/us",
        description: "NTSC Greatest Hits",
        kernel: Some(Kernel {
            frame_source: "/tmp/ccDjQrOc.c",
            sha256: "d0769f007060b952abf25d170726d0b361c3f37cfc133af3afe269a3f7bb0525",
        }),
        ee_builds: &[
            EeBuild {
                defines: &[
                    "VIDEO_NTSC=1",
                    "REGION_US=1",
                    "MEDIA_DVD=2",
                    "REVISION=20011127",
                ],
                ..ee::KERNEL
            },
            ee::STARTUP,
            ee::LIBKERNL,
            ee::LIBC,
            ee::LIBM,
            ee::LIBGCC,
            ee::LIBCDVD,
            ee::LIBPAD,
            ee::LIBGRAPH,
            ee::LIBMC,
            ee::LIBDMA,
            ee::LIBSCF,
        ],
        iop_builds: &[
            iop::OVERLORD_20011121,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
            iop::MCMAN_2_21,
            iop::SND989_2_6_8,
        ],
    },
    Revision {
        name: "jak1/20020121/ntsc/kr",
        description: "NTSC Korean Retail",
        kernel: Some(Kernel {
            frame_source: "/tmp/ccX4drDa.c",
            sha256: "b3051491a4f41bd90b345bbd91fe60b4c85f3060bacd0552903645f8d27268d5",
        }),
        ee_builds: &[
            EeBuild {
                defines: &[
                    "VIDEO_NTSC=1",
                    "REGION_KR=4",
                    "MEDIA_DVD=2",
                    "REVISION=20020121",
                ],
                ..ee::KERNEL
            },
            ee::STARTUP,
            ee::LIBKERNL,
            ee::LIBC,
            ee::LIBM,
            ee::LIBGCC,
            ee::LIBCDVD,
            ee::LIBPAD,
            ee::LIBGRAPH,
            ee::LIBMC,
            ee::LIBDMA,
            ee::LIBSCF,
        ],
        iop_builds: &[
            iop::OVERLORD_20011031,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
            iop::MCMAN_2_21,
            iop::SND989_2_6_8,
        ],
    },
    Revision {
        name: "jak2/20030405/ntsc/us",
        description: "NTSC Early Demo",
        kernel: None,
        ee_builds: &[],
        iop_builds: &[iop::SIO2MAN_2_5, iop::SDRDRV_4_1_2400],
    },
    Revision {
        name: "jak2/20030713/pal/eu",
        description: "PAL Preview",
        kernel: None,
        ee_builds: &[],
        iop_builds: &[iop::SCRTCHPD_1_1, iop::SIO2MAN_3_0, iop::SDRDRV_4_1_2700],
    },
    Revision {
        name: "jak2/20030720/pal/eu",
        description: "PAL Demo",
        kernel: None,
        ee_builds: &[],
        iop_builds: &[iop::SCRTCHPD_1_1, iop::SIO2MAN_3_0, iop::SDRDRV_4_1_2700],
    },
    Revision {
        name: "jak2/20030818/pal/eu",
        description: "PAL Review",
        kernel: None,
        ee_builds: &[],
        iop_builds: &[iop::SCRTCHPD_1_1, iop::SIO2MAN_3_0, iop::SDRDRV_4_1_2700],
    },
    Revision {
        name: "jak2/20030826/ntsc/us",
        description: "NTSC Retail",
        kernel: None,
        ee_builds: &[],
        iop_builds: &[iop::SCRTCHPD_1_1, iop::SIO2MAN_3_0, iop::SDRDRV_4_1_2700],
    },
    Revision {
        name: "jak2/20030916/pal/eu",
        description: "PAL Retail",
        kernel: None,
        ee_builds: &[],
        iop_builds: &[iop::SCRTCHPD_1_1, iop::SIO2MAN_3_0, iop::SDRDRV_4_1_2700],
    },
    Revision {
        name: "jak2/20031107/ntsc/us",
        description: "NTSC Retail v2.01",
        kernel: None,
        ee_builds: &[],
        iop_builds: &[iop::SCRTCHPD_1_1, iop::SIO2MAN_3_0, iop::SDRDRV_4_1_2700],
    },
    Revision {
        name: "jak2/20040108/ntsc/jp",
        description: "NTSC Japanese Retail",
        kernel: None,
        ee_builds: &[],
        iop_builds: &[iop::SCRTCHPD_1_1, iop::SIO2MAN_3_0, iop::SDRDRV_4_1_2700],
    },
];

fn print_known_revisions() {
    eprintln!("<revision> must be `all` or one of the known revisions:");
    for revision in REVISIONS {
        eprintln!("  {} - {}", revision.name, revision.description);
    }
}

fn write_if_changed(path: &Path, contents: &str) -> io::Result<()> {
    match fs::read(path) {
        Ok(existing) if existing == contents.as_bytes() => return Ok(()),
        Ok(_) => {}
        Err(error) if error.kind() == io::ErrorKind::NotFound => {}
        Err(error) => return Err(error),
    }
    fs::write(path, contents)
}

fn build_revision(repo_root: &Path, build_dir: &Path, revision: &Revision) -> io::Result<()> {
    fs::create_dir_all(build_dir)?;
    let mut ninja = format!(
        "# Generated by configure for {}.\nninja_required_version = 1.3\nrepo_root = {}\nbuild_dir = {}\n",
        revision.name,
        repo_root.display(),
        build_dir.display()
    );
    ninja.push_str(&iop::ninja_section(
        repo_root,
        build_dir,
        revision.iop_builds,
    )?);
    ninja.push_str(&ee::ninja_section(
        repo_root,
        build_dir,
        revision.ee_builds,
    )?);
    if let Some(kernel) = &revision.kernel {
        ninja.push_str(&ee::kernel_link(
            build_dir,
            revision.ee_builds,
            kernel.frame_source,
        )?);
    }
    write_if_changed(&build_dir.join("build.ninja"), &ninja)?;
    let status = Command::new("ninja").current_dir(build_dir).status()?;
    if !status.success() {
        return Err(io::Error::other(format!("Ninja {status}")));
    }
    Ok(())
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

    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR")).parent().unwrap();
    let mut failed = false;
    for revision in revisions {
        println!("\nRevision {} - {}", revision.name, revision.description);
        let build_dir = repo_root.join("build").join(revision.name);
        if let Err(error) = build_revision(repo_root, &build_dir, revision) {
            eprintln!("Build failed for {}: {error}", revision.name);
            failed = true;
            continue;
        }
        if let Some(kernel) = &revision.kernel {
            match fs::read(build_dir.join("KERNEL.ELF")).map(|data| sha256::hash(&data)) {
                Ok(actual) if actual == kernel.sha256 => println!("[OK] KERNEL.ELF"),
                Ok(actual) => {
                    println!(
                        "[FAIL] KERNEL.ELF\n  expected: {}\n  actual: {actual}",
                        kernel.sha256
                    );
                    failed = true;
                }
                Err(error) => {
                    println!("[FAIL] KERNEL.ELF: {error}");
                    failed = true;
                }
            }
        }
        for build in revision.iop_builds {
            let output = build_dir.join(build.output_path());
            match fs::read(output).map(|data| sha256::hash(&data)) {
                Ok(actual) if actual == build.sha256 => println!("[OK] {}", build.filename),
                Ok(actual) => {
                    println!(
                        "[FAIL] {}\n  expected: {}\n  actual: {actual}",
                        build.filename, build.sha256
                    );
                    failed = true;
                }
                Err(error) => {
                    println!("[FAIL] {}: {error}", build.filename);
                    failed = true;
                }
            }
        }
    }
    if failed {
        std::process::exit(1);
    }
}
