use std::env;
use std::fs;
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use goal::driver::Driver;

fn usage(program: &str) {
    eprintln!(
        "usage:\n  {program} <project-file> <build-directory> [--repl]\n  {program} unpack <archive.CGO|DGO> <output-directory>\n  {program} disassemble <object.o> <output.asm>"
    );
}

fn run() -> Result<(), String> {
    let arguments: Vec<_> = env::args().collect();
    let program = arguments.first().map_or("goal", String::as_str);
    match arguments.get(1).map(String::as_str) {
        Some("unpack") => match arguments.as_slice() {
            [_, _, archive, output] => {
                let paths = goal::dgo::unpack_file(Path::new(archive), Path::new(output))?;
                println!("unpacked {} objects", paths.len());
                Ok(())
            }
            _ => {
                usage(program);
                Err("unpack expects an archive and output directory".to_owned())
            }
        },
        Some("disassemble") => match arguments.as_slice() {
            [_, _, object, output] => {
                let bytes = fs::read(object)
                    .map_err(|error| format!("could not read `{object}`: {error}"))?;
                let assembly = goal::disassemble::disassemble_object(&bytes, object)?;
                fs::write(output, assembly)
                    .map_err(|error| format!("could not write `{output}`: {error}"))
            }
            _ => {
                usage(program);
                Err("disassemble expects an object and output file".to_owned())
            }
        },
        _ => run_compiler(program, &arguments[1..]),
    }
}

fn run_compiler(program: &str, arguments: &[String]) -> Result<(), String> {
    let ([project_file, build_directory] | [project_file, build_directory, _]) = arguments else {
        usage(program);
        return Err("missing project file or build directory".to_owned());
    };
    let repl = match arguments.get(2).map(String::as_str) {
        None => false,
        Some("--repl") => true,
        Some(argument) => {
            usage(program);
            return Err(format!("unknown argument `{argument}`"));
        }
    };
    let mut driver = Driver::load(PathBuf::from(project_file), PathBuf::from(build_directory))?;
    driver.rebuild_dirty()?;
    if repl {
        let mut input = String::new();
        io::stdin()
            .read_to_string(&mut input)
            .map_err(|error| format!("could not read REPL input: {error}"))?;
        driver.repl_compile(&input)?;
    }
    Ok(())
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("goal: {error}");
            ExitCode::FAILURE
        }
    }
}
