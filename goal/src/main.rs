use std::env;
use std::io::{self, Read};
use std::path::PathBuf;
use std::process::ExitCode;

use goal::driver::Driver;

fn usage(program: &str) {
    eprintln!("usage: {program} <project-file> <build-directory> [--repl]");
}

fn run() -> Result<(), String> {
    let mut arguments = env::args();
    let program = arguments.next().unwrap_or_else(|| "goal".to_owned());
    let Some(project_file) = arguments.next() else {
        usage(&program);
        return Err("missing project file".to_owned());
    };
    let Some(build_directory) = arguments.next() else {
        usage(&program);
        return Err("missing build directory".to_owned());
    };

    let repl = match arguments.next().as_deref() {
        None => false,
        Some("--repl") => true,
        Some(argument) => {
            usage(&program);
            return Err(format!("unknown argument `{argument}`"));
        }
    };
    if arguments.next().is_some() {
        usage(&program);
        return Err("too many arguments".to_owned());
    }

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
