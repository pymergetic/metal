//! builtinhelp — short help text (no REPL UI yet).

pub fn help_text(topic: Option<&str>) -> &'static str {
    match topic {
        None | Some("") => "Metal upy: builtins/sys/errno; import Metal via reg.",
        Some("import") => "import_module(name): sys|errno|builtins|pymergetic.metal.*",
        Some("sys") => "sys.modules, sys.path, sys.version",
        _ => "no help for topic",
    }
}
