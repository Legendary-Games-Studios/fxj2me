#!/usr/bin/env python3
"""fxj2me: build the boot class library (lib/java) into boot.jar.

Steps:
  1. javac --release 8 -g:none compiles the library (our own java.lang etc.;
     javac accepts user java.lang classes when they are in the same run).
  2. jar the class files (stored, not deflated: tools/gen_bootimg.c reads
     this jar and nothing inflates on the boot path).

usage: build_boot.py <lib-src-dir> <out-dir>
"""
import os
import shutil
import subprocess
import sys
import zipfile


def main():
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    srcdir = os.path.abspath(sys.argv[1])
    outdir = os.path.abspath(sys.argv[2])

    classes = os.path.join(outdir, "boot-classes")
    jar_path = os.path.join(outdir, "boot.jar")
    os.makedirs(classes, exist_ok=True)

    sources = []
    for root, _dirs, files in os.walk(srcdir):
        for name in sorted(files):
            if name.endswith(".java"):
                sources.append(os.path.join(root, name))
    if not sources:
        print("no java sources under", srcdir, file=sys.stderr)
        return 1

    cmd = ["javac", "--release", "8", "-g:none", "-Xlint:-options",
           "-d", classes] + sources
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout)
        sys.stderr.write(r.stderr)
        return r.returncode
    if r.stderr.strip():
        print(r.stderr.strip(), file=sys.stderr)

    with zipfile.ZipFile(jar_path, "w") as z:
        for root, _dirs, files in os.walk(classes):
            for name in sorted(files):
                if not name.endswith(".class"):
                    continue
                full = os.path.join(root, name)
                arc = os.path.relpath(full, classes).replace(os.sep, "/")
                with open(full, "rb") as f:
                    z.writestr(zipfile.ZipInfo(arc), f.read(),
                               compress_type=zipfile.ZIP_STORED)

    size = os.path.getsize(jar_path)
    nclass = sum(1 for _r, _d, fs in os.walk(classes)
                 for f in fs if f.endswith(".class"))
    print(f"boot.jar: {nclass} classes, {size} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
