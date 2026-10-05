use crate::model::{CompileCommandEntry, ParsedUnit};
use std::path::{Path, PathBuf};

pub fn parse_command_entry(entry: &CompileCommandEntry) -> ParsedUnit {
    let args = if let Some(ref arguments) = entry.arguments {
        arguments.clone()
    } else if let Some(ref cmd) = entry.command {
        split_command_line(cmd)
    } else {
        Vec::new()
    };

    let mut compiler = String::new();
    let mut includes = Vec::new();
    let mut defines = Vec::new();
    let mut output = entry.output.clone();
    let mut standard = None;
    let mut flags = Vec::new();

    let mut iter = args.into_iter();
    if let Some(comp) = iter.next() {
        compiler = comp;
    }

    while let Some(arg) = iter.next() {
        if arg == "-I" {
            if let Some(inc) = iter.next() {
                includes.push(normalize_path(&inc, &entry.directory));
            }
        } else if let Some(inc) = arg.strip_prefix("-I") {
            includes.push(normalize_path(inc, &entry.directory));
        } else if arg == "-isystem" {
            if let Some(inc) = iter.next() {
                includes.push(normalize_path(&inc, &entry.directory));
            }
        } else if let Some(inc) = arg.strip_prefix("-isystem") {
            includes.push(normalize_path(inc, &entry.directory));
        } else if arg == "-D" {
            if let Some(def) = iter.next() {
                defines.push(def);
            }
        } else if let Some(def) = arg.strip_prefix("-D") {
            defines.push(def.to_string());
        } else if arg == "-o" {
            if let Some(out) = iter.next() {
                output = Some(out);
            }
        } else if let Some(out) = arg.strip_prefix("-o") {
            output = Some(out.to_string());
        } else if let Some(std) = arg.strip_prefix("-std=") {
            standard = Some(std.to_string());
        } else if arg == "-c" {
            // compile-only flag, skip or retain
        } else if arg.starts_with('-') {
            flags.push(arg);
        }
    }

    includes.sort();
    includes.dedup();
    defines.sort();
    defines.dedup();
    flags.sort();
    flags.dedup();

    ParsedUnit {
        file: normalize_path(&entry.file, &entry.directory),
        directory: entry.directory.clone(),
        compiler,
        includes,
        defines,
        output,
        standard,
        flags,
    }
}

pub fn split_command_line(cmd: &str) -> Vec<String> {
    let mut args = Vec::new();
    let mut current = String::new();
    let mut in_single_quote = false;
    let mut in_double_quote = false;
    let mut escape = false;

    for ch in cmd.chars() {
        if escape {
            current.push(ch);
            escape = false;
            continue;
        }

        if ch == '\\' {
            escape = true;
            continue;
        }

        if in_single_quote {
            if ch == '\'' {
                in_single_quote = false;
            } else {
                current.push(ch);
            }
            continue;
        }

        if in_double_quote {
            if ch == '"' {
                in_double_quote = false;
            } else {
                current.push(ch);
            }
            continue;
        }

        match ch {
            '\'' => in_single_quote = true,
            '"' => in_double_quote = true,
            c if c.is_whitespace() => {
                if !current.is_empty() {
                    args.push(std::mem::take(&mut current));
                }
            }
            other => current.push(other),
        }
    }

    if !current.is_empty() {
        args.push(current);
    }

    args
}

fn normalize_path(path: &str, base_dir: &str) -> String {
    let p = Path::new(path);
    if p.is_absolute() {
        clean_path(p)
    } else {
        let joined = PathBuf::from(base_dir).join(p);
        clean_path(&joined)
    }
}

fn clean_path(path: &Path) -> String {
    use std::path::Component;
    let mut components = Vec::new();
    for comp in path.components() {
        match comp {
            Component::CurDir => {}
            Component::ParentDir => {
                if !components.is_empty() {
                    components.pop();
                }
            }
            c => components.push(c),
        }
    }
    let mut buf = PathBuf::new();
    for c in components {
        buf.push(c);
    }
    buf.to_string_lossy().to_string()
}
