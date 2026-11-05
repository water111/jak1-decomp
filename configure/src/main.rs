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
}

// Some OVERLORDs are the same between game revisions and for those we just
// name the OVERLORD after the earliest revision.
const REVISIONS: &[Revision] = &[
    Revision {
        name: "jak1/20010903/ntsc/us",
        description: "NTSC September Preview",
        ee_builds: &[ee::LIBMC, ee::LIBDMA],
        iop_builds: &[
            iop::OVERLORD_20010903,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
            iop::IOPSETUP,
        ],
    },
    Revision {
        name: "jak1/20010920/ntsc/jp",
        description: "NTSC Japanese Demo",
        ee_builds: &[ee::LIBMC, ee::LIBDMA, ee::LIBSCF],
        iop_builds: &[
            iop::OVERLORD_20010920,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
        ],
    },
    Revision {
        name: "jak1/20011002/ntsc/us",
        description: "NTSC Cingular Demo",
        ee_builds: &[ee::LIBMC, ee::LIBDMA, ee::LIBSCF, ee::LIBSCEDEMO],
        iop_builds: &[
            iop::OVERLORD_20011002,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
        ],
    },
    Revision {
        name: "jak1/20011019/ntsc/us",
        description: "NTSC October Prototype",
        ee_builds: &[ee::LIBMC, ee::LIBDMA, ee::LIBSCF],
        iop_builds: &[
            iop::OVERLORD_20011019,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
            iop::IOPSETUP,
        ],
    },
    Revision {
        name: "jak1/20011031/ntsc/us",
        description: "NTSC Retail",
        ee_builds: &[ee::LIBMC, ee::LIBDMA, ee::LIBSCF],
        iop_builds: &[
            iop::OVERLORD_20011031,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
        ],
    },
    Revision {
        name: "jak1/20011102/pal/eu",
        description: "PAL OPS2M Demo",
        ee_builds: &[ee::LIBMC, ee::LIBDMA, ee::LIBSCF, ee::LIBSCEDEMO],
        iop_builds: &[
            iop::OVERLORD_20011102,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
        ],
    },
    Revision {
        name: "jak1/20011104/pal/eu",
        description: "PAL Retail",
        ee_builds: &[ee::LIBMC, ee::LIBDMA, ee::LIBSCF],
        iop_builds: &[
            iop::OVERLORD_20011104,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
        ],
    },
    Revision {
        name: "jak1/20011111/pal/eu",
        description: "PAL Demo",
        ee_builds: &[ee::LIBMC, ee::LIBDMA, ee::LIBSCF],
        iop_builds: &[
            iop::OVERLORD_20011111,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
        ],
    },
    Revision {
        name: "jak1/20011121/ntsc/jp",
        description: "NTSC Japanese Retail",
        ee_builds: &[ee::LIBMC, ee::LIBDMA, ee::LIBSCF],
        iop_builds: &[
            iop::OVERLORD_20011121,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
        ],
    },
    Revision {
        name: "jak1/20011127/ntsc/us",
        description: "NTSC Greatest Hits",
        ee_builds: &[ee::LIBMC, ee::LIBDMA, ee::LIBSCF],
        iop_builds: &[
            iop::OVERLORD_20011121,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
        ],
    },
    Revision {
        name: "jak1/20020121/ntsc/kr",
        description: "NTSC Korean Retail",
        ee_builds: &[ee::LIBMC, ee::LIBDMA, ee::LIBSCF],
        iop_builds: &[
            iop::OVERLORD_20011031,
            iop::SIO2MAN_2_4,
            iop::MCSERV_2_12,
            iop::LIBSD_1_4,
            iop::USBD_2_4,
            iop::PADMAN_4_2,
        ],
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
