"""Positive option policy for shell-free GCC/Clang include tracing."""

from __future__ import annotations

import re

COMPILER_NAME = re.compile(
    r"^(?:(?:[A-Za-z0-9_+.]+-)+)?(?:gcc|g\+\+|clang|clang\+\+|cc|c\+\+)"
    r"(?:-[0-9][A-Za-z0-9_.-]*)?(?:\.exe)?$",
    re.IGNORECASE,
)

DROP_EXACT = frozenset(
    {
        "-c",
        "-S",
        "-E",
        "-fsyntax-only",
        "-M",
        "-MM",
        "-MD",
        "-MMD",
        "-MP",
        "-MG",
        "-H",
        "-pipe",
        "-v",
        "--verbose",
    }
)
DROP_VALUE = frozenset(
    {
        "-o",
        "--output",
        "-MF",
        "-MT",
        "-MQ",
        "-MJ",
        "-dumpdir",
        "-dumpbase",
        "-serialize-diagnostics",
        "--serialize-diagnostics",
        "-dependency-file",
        "--dependency-file",
    }
)
DROP_JOINED = (
    "--output=",
    "-MF",
    "-MT",
    "-MQ",
    "-MJ",
    "-save-temps=",
    "--save-temps=",
    "-ftime-trace=",
    "-fdiagnostics-color=",
    "-dependency-file=",
    "--dependency-file=",
)
PRESERVE_VALUE = frozenset(
    {
        "-D",
        "-U",
        "-I",
        "-F",
        "-x",
        "--language",
        "-std",
        "-include",
        "-imacros",
        "-isystem",
        "-iquote",
        "-idirafter",
        "-isysroot",
        "--sysroot",
        "-target",
        "--target",
        "-arch",
        "-march",
        "-mcpu",
        "-mtune",
        "-mabi",
        "-resource-dir",
    }
)
SAFE_LANGUAGES = frozenset({"c", "c-header", "c++", "c++-header", "objective-c", "objective-c++"})
SAFE_EXACT = frozenset(
    {
        "-ansi",
        "-pedantic",
        "-pedantic-errors",
        "-pthread",
        "-nostdinc",
        "-nostdinc++",
        "-undef",
        "-trigraphs",
        "-Qunused-arguments",
        "-fPIC",
        "-fPIE",
        "-fpic",
        "-fpie",
        "-fexceptions",
        "-fno-exceptions",
        "-frtti",
        "-fno-rtti",
        "-fpermissive",
        "-ffreestanding",
        "-fhosted",
        "-fshort-enums",
        "-fshort-wchar",
        "-fsigned-char",
        "-funsigned-char",
        "-fvisibility-inlines-hidden",
        "-fno-visibility-inlines-hidden",
        "-fconcepts",
        "-fconcepts-ts",
        "-fcoroutines",
        "-fcoroutines-ts",
        "-fms-extensions",
        "-fstrict-aliasing",
        "-fno-strict-aliasing",
        "-fcommon",
        "-fno-common",
    }
)
SAFE_PREFIXES = (
    "-D",
    "-U",
    "-I",
    "-F",
    "-W",
    "-std=",
    "--std=",
    "-isystem",
    "-iquote",
    "-idirafter",
    "-imacros",
    "--sysroot=",
    "-isysroot=",
    "-target=",
    "--target=",
    "-arch=",
    "-march=",
    "-mcpu=",
    "-mtune=",
    "-mabi=",
    "-resource-dir=",
    "-stdlib=",
    "-fabi-version=",
    "-fconstexpr-",
    "-fmacro-prefix-map=",
    "-ffile-prefix-map=",
    "-fdebug-prefix-map=",
    "-fms-compatibility-version=",
    "-fno-builtin-",
)
REJECT_EXACT = frozenset(
    {
        "-Xclang",
        "-Xpreprocessor",
        "-Xassembler",
        "-Xlinker",
        "-mllvm",
        "-load",
        "-load-plugin",
        "-plugin",
        "-cc1",
        "-wrapper",
        "--config",
        "-B",
        "-specs",
    }
)
REJECT_PREFIXES = (
    "-Xclang=",
    "-Xpreprocessor=",
    "-Xassembler=",
    "-Xlinker=",
    "-mllvm=",
    "-Wa,",
    "-Wl,",
    "-Wp,",
    "-fplugin",
    "-fpass-plugin",
    "-fmodules",
    "-fmodule-",
    "-fdump-",
    "-fopt-info",
    "-fprofile-",
    "-ftest-coverage",
    "-fpath-coverage",
    "-fsanitize=",
    "-fno-sanitize=",
    "-save-temps",
    "--save-temps",
    "-ftime-",
    "--config=",
    "--gcc-toolchain",
    "--vfsoverlay",
    "-vfsoverlay",
    "--coverage",
    "-coverage",
    "-B",
    "-specs=",
)


def should_drop(token: str) -> bool:
    return (
        token in DROP_EXACT
        or token.startswith(DROP_JOINED)
        or (token.startswith("-g") and token not in {"-Winvalid-pch"})
    )


def is_rejected(token: str) -> bool:
    return token in REJECT_EXACT or token.startswith(REJECT_PREFIXES)


def is_safe(token: str) -> bool:
    return token in SAFE_EXACT or token.startswith(SAFE_PREFIXES)
