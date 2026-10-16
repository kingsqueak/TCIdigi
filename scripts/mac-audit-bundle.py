#!/usr/bin/env python3
"""Check that a TCIdigi .app bundle loads on an older macOS.

Usage: mac-audit-bundle.py <App.app> [--min 26.0] [--sdk <MacOSX26.x.sdk>]

Two checks for every Mach-O file in the bundle:
  1. LC_BUILD_VERSION minos above --min is listed as a warning.
  2. Every non-weak symbol imported from a system library or framework
     must be exported by that library's .tbd stub in --sdk. A symbol that
     is missing there is what killed 0.28 on macOS 26.5 (_pipe2 in glib).
Weak imports that are missing are listed but do not fail the check.
"""
import argparse
import os
import re
import subprocess
import sys

DEFAULT_SDK = "/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk"


def run(*args):
    return subprocess.run(args, capture_output=True, text=True).stdout


def is_macho(path):
    try:
        with open(path, "rb") as f:
            magic = f.read(4)
    except OSError:
        return False
    return magic in (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe")


def minos(path):
    seen = False
    for line in run("otool", "-l", path).splitlines():
        if "LC_BUILD_VERSION" in line:
            seen = True
        elif seen and line.strip().startswith("minos"):
            return line.split()[1]
    return None


def vtuple(v):
    return tuple(int(x) for x in v.split("."))


def load_paths(path):
    """Install names of the libraries path links, keyed by short name."""
    out = {}
    for line in run("otool", "-L", path).splitlines()[1:]:
        lib = line.strip().split(" (")[0]
        base = os.path.basename(lib)
        out[base] = lib
        # nm -m names a framework by its bare name, a dylib without
        # lib/.dylib and version suffix (libSystem.B.dylib -> libSystem).
        m = re.match(r"(lib[^.]+)", base)
        if m:
            out[m.group(1)[3:]] = lib
            out[m.group(1)] = lib
    return out


def tbd_for(install_name, sdk):
    if not install_name.startswith(("/usr/lib/", "/System/")):
        return None
    stub = os.path.splitext(sdk + install_name)[0] + ".tbd"
    if os.path.exists(stub):
        return stub
    # Frameworks: /System/Library/Frameworks/X.framework/Versions/A/X
    stub = sdk + install_name + ".tbd"
    if os.path.exists(stub):
        return stub
    d = os.path.dirname(sdk + install_name)
    alt = os.path.join(re.sub(r"/Versions/[^/]+$", "", d),
                       os.path.basename(install_name) + ".tbd")
    return alt if os.path.exists(alt) else None


_exports = {}

LIST_KEYS = {"symbols": "", "weak-symbols": "", "thread-local-symbols": "",
             "objc-classes": ("_OBJC_CLASS_$_", "_OBJC_METACLASS_$_"),
             "objc-eh-types": ("_OBJC_EHTYPE_$_",),
             "objc-ivars": ("_OBJC_IVAR_$_",),
             "libraries": None}


def parse_tbd(stub):
    """Exported symbols and re-exported install names of a tbd v4 stub.

    A stub holds several YAML documents (libSystem lists each libsystem_*
    it re-exports). Only the exports/reexports sections count; the
    undefineds section lists what the library imports.
    """
    syms, reexp = set(), set()
    section = key = None
    buf = ""
    for line in open(stub, encoding="utf-8", errors="replace"):
        if re.match(r"^[A-Za-z-]+:", line):
            section = line.split(":")[0]
        m = re.match(r"^\s*(?:- )?\s*([a-z-]+):\s*\[(.*)", line)
        if m and section in ("exports", "reexports", "reexported-libraries"):
            key, buf = m.group(1), m.group(2)
        elif key:
            buf += line
        else:
            continue
        if "]" not in buf:
            continue
        body = buf.split("]")[0]
        names = [t.strip().strip("'\"") for t in body.split(",") if t.strip()]
        if key in LIST_KEYS:
            pre = LIST_KEYS[key]
            for n in names:
                if pre is None:
                    reexp.add(n)
                elif pre == "":
                    syms.add(n)
                else:
                    syms.update(x + n for x in pre)
        key, buf = None, ""
    return syms, reexp


def exports(stub, sdk, depth=0):
    if stub not in _exports:
        syms, reexp = parse_tbd(stub)
        _exports[stub] = syms
        if depth < 8:
            for lib in reexp:
                sub = tbd_for(lib, sdk)
                if sub and sub != stub:
                    syms |= exports(sub, sdk, depth + 1)
    return _exports[stub]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("app")
    ap.add_argument("--min", default="26.0")
    ap.add_argument("--sdk", default=DEFAULT_SDK)
    a = ap.parse_args()

    files = []
    for root, _, names in os.walk(a.app):
        for n in names:
            p = os.path.join(root, n)
            if not os.path.islink(p) and is_macho(p):
                files.append(p)

    bad = newer = 0
    for p in sorted(files):
        rel = os.path.relpath(p, a.app)
        m = minos(p)
        if m and vtuple(m) > vtuple(a.min):
            # dyld loads a dylib stamped for a newer macOS; only a
            # missing symbol stops it, so this is a warning.
            print(f"minos   {m:6} {rel}")
            newer += 1
        libs = load_paths(p)
        for line in run("nm", "-m", "-arch", "arm64", p).splitlines():
            if "(undefined)" not in line:
                continue
            mm = re.search(r"external (\S+) \(from ([^)]+)\)", line)
            if not mm:
                continue
            sym, frm = mm.groups()
            lib = libs.get(frm)
            if not lib:
                continue
            stub = tbd_for(lib, a.sdk)
            if not stub or sym in exports(stub, a.sdk):
                continue
            if "weak" in line:
                print(f"weak    {sym} from {frm} in {rel}")
            else:
                print(f"MISSING {sym} from {frm} in {rel}")
                bad += 1

    print(f"{len(files)} Mach-O files, {bad} missing symbols, {newer} "
          f"stamped above {a.min} ({os.path.basename(a.sdk)})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
