#!/usr/bin/env python3
"""Make a copy of the BakkesMod SDK headers usable on a case-sensitive file system.

The SDK is written for Windows: includes like "bakkesmod/wrappers/CVarManagerWrapper.h" refer to
"cvarmanagerwrapper.h" on disk. This script copies the include tree and adds symlinks for every
spelling used by an #include directive. It also neutralises one duplicate explicit template
instantiation that MSVC accepts but clang rejects. Only used by tools/sdk_syntax_check.sh.

usage: sdk_case_mirror.py <sdk include dir> <output dir>
"""
import os
import re
import shutil
import sys


def index(root):
    m = {}
    for d, _, files in os.walk(root):
        for f in files:
            p = os.path.relpath(os.path.join(d, f), root)
            m[p.lower()] = p
    return m


def main():
    src, out = sys.argv[1], sys.argv[2]
    if os.path.exists(out):
        shutil.rmtree(out)
    shutil.copytree(src, out)
    changed = True
    while changed:
        changed = False
        m = index(out)
        for d, _, files in os.walk(out):
            for f in files:
                fp = os.path.join(d, f)
                try:
                    txt = open(fp, encoding="utf-8", errors="ignore").read()
                except OSError:
                    continue
                for inc in re.findall(r'#\s*include\s*"([^"]+)"', txt):
                    if any(os.path.exists(os.path.normpath(os.path.join(b, inc))) for b in (out, d)):
                        continue
                    for base in (out, d):
                        rel = os.path.relpath(os.path.normpath(os.path.join(base, inc)), out)
                        if rel.startswith("..") or rel.lower() not in m:
                            continue
                        dst = os.path.join(out, rel)
                        os.makedirs(os.path.dirname(dst), exist_ok=True)
                        if not os.path.exists(dst):
                            os.symlink(os.path.abspath(os.path.join(out, m[rel.lower()])), dst)
                            changed = True
                        break
    usw = os.path.join(out, "bakkesmod", "wrappers", "Engine", "UnrealStringWrapper.h")
    if os.path.exists(usw):
        txt = open(usw, encoding="utf-8").read()
        txt = txt.replace("template class BAKKESMOD_PLUGIN_IMPORT ArrayWrapper<wchar_t>;",
                          "// (syntax-check harness) duplicate instantiation removed")
        open(usw, "w", encoding="utf-8").write(txt)


if __name__ == "__main__":
    main()
