#!/usr/bin/env python3
"""Expand the autotools fldigi link set for the Qt shell."""

import os
import re
import sys


def read_make_vars(path):
    values = {}
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    index = 0
    while index < len(lines):
        line = lines[index]
        if not line or line[0] in " \t#":
            index += 1
            continue
        match = re.match(r"^([A-Za-z0-9_]+) *= *(.*)$", line)
        if not match:
            index += 1
            continue
        name, value = match.group(1), match.group(2)
        while value.endswith("\\"):
            index += 1
            if index >= len(lines):
                break
            value = value[:-1] + " " + lines[index].strip()
        values[name] = value
        index += 1
    return values


def expand(text, values):
    for _ in range(40):
        updated = re.sub(
            r"\$\(([A-Za-z0-9_]+)\)",
            lambda match: values.get(match.group(1), ""),
            text,
        )
        updated = re.sub(
            r"\$\{([A-Za-z0-9_]+)\}",
            lambda match: values.get(match.group(1), ""),
            updated,
        )
        if updated == text:
            return updated
        text = updated
    return text


def cmake_quote(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def fix_include(flag, src):
    if not flag.startswith("-I"):
        return flag
    path = flag[2:]
    if path.startswith("./"):
        path = path[2:]
    if not os.path.isabs(path):
        path = os.path.join(src, path)
    return "-I" + os.path.normpath(path)


def shell_split(text):
    flags = []
    token = []
    quote = ""
    escaped = False
    for char in text:
        if escaped:
            token.append(char)
            escaped = False
            continue
        if char == "\\":
            escaped = True
            continue
        if quote:
            if char == quote:
                quote = ""
            else:
                token.append(char)
            continue
        if char in "\"'":
            quote = char
            continue
        if char.isspace():
            if token:
                flags.append("".join(token))
                token = []
            continue
        token.append(char)
    if token:
        flags.append("".join(token))
    return flags


def main():
    src = os.path.abspath(sys.argv[1])
    out_path = sys.argv[2]
    values = read_make_vars(os.path.join(src, "Makefile"))
    objects = []
    for name in shell_split(expand(values.get("fldigi_OBJECTS", ""), values)):
        if name.endswith("fldigi-main.o") or "flarq" in name:
            continue
        path = name if os.path.isabs(name) else os.path.join(src, name)
        if not os.path.isfile(path):
            sys.stderr.write("missing fldigi object: %s\n" % path)
            return 1
        objects.append(os.path.normpath(path))
    if len(objects) < 200:
        sys.stderr.write("only %d fldigi objects; expected the modem link set\n" % len(objects))
        return 1

    compile_text = " ".join(
        (
            expand(values.get("DEFS", ""), values),
            expand(values.get("fldigi_CPPFLAGS", ""), values),
            expand(values.get("fldigi_CXXFLAGS", ""), values),
        )
    ).replace('\\"', '"')
    compile_flags = []
    for flag in shell_split(compile_text):
        flag = fix_include(flag, src)
        if flag.startswith("-D") and "=" in flag:
            name, value = flag[2:].split("=", 1)
            if value and "/" in value and not (value.startswith('"') and value.endswith('"')):
                flag = '-D%s="%s"' % (name, value)
        compile_flags.append(flag)

    link_text = " ".join(
        (
            expand(values.get("fldigi_LDFLAGS", ""), values),
            expand(values.get("fldigi_LDADD", ""), values),
        )
    )
    link_text = " ".join(link_text.split())
    link_flags = shell_split(link_text)

    with open(out_path, "w", encoding="utf-8") as out:
        out.write("set(FLDIGI_MODEM_OBJECTS\n")
        for path in objects:
            out.write("  %s\n" % cmake_quote(path))
        out.write(")\n")
        out.write("set(FLDIGI_ENGINE_COMPILE_OPTIONS\n")
        for flag in compile_flags:
            out.write("  %s\n" % cmake_quote(flag))
        out.write(")\n")
        out.write("set(FLDIGI_ENGINE_LINK_OPTIONS\n")
        for flag in link_flags:
            out.write("  %s\n" % cmake_quote(flag))
        out.write(")\n")
        out.write("set(FLDIGI_ENGINE_LINK_LINE %s)\n" % cmake_quote(link_text))
    sys.stderr.write("fldigi objects: %d\n" % len(objects))
    return 0


if __name__ == "__main__":
    sys.exit(main())
