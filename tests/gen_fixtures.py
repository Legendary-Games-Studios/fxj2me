#!/usr/bin/env python3
"""fxj2me test fixture generator.

Creates in the output directory:
  data_N.bin / comp_N.bin  -- raw-DEFLATE vectors (comp = deflate of data)
  stored_N.bin             -- stored-block deflate streams
  test.jar                 -- jar with Fib.class (deflate), Fib$Cell.class
                              (stored) and hello.txt (deflate)
  Fib.class, Fib$Cell.class-- extracted for direct classfile tests
  hello.jar                -- HelloMidlet MIDlet with MANIFEST.MF (needs the
                              boot class library as second argument)
"""
import os
import random
import struct
import subprocess
import sys
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))

# Long manifest value exercised by test_midp: wraps across continuation
# lines exactly like the jar tool. The literal is asserted in test_midp.c
# and must stay in sync with it.
DESC = ("fxj2me fixture manifest, written the way the jar tool writes real "
        "ones: long values are wrapped across several continuation lines "
        "at seventy-two bytes")


def wrap_manifest_line(logical: str) -> str:
    """RFC 822 / jar-tool manifest formatting: CRLF endings, physical
    lines at most 72 bytes, continuation lines start with one space."""
    data = logical.encode("ascii")
    chunks = [data[:72]]
    rest = data[72:]
    while rest:
        chunks.append(b" " + rest[:71])
        rest = rest[71:]
    return b"\r\n".join(chunks).decode("ascii") + "\r\n"


def raw_deflate(data: bytes, level: int) -> bytes:
    c = zlib.compressobj(level, zlib.DEFLATED, -15)
    return c.compress(data) + c.flush()


def stored_deflate(data: bytes) -> bytes:
    """A deflate stream made only of stored blocks (level 0)."""
    return raw_deflate(data, 0)


def make_vectors(outdir: str):
    rng = random.Random(0x5A17)
    vectors = []

    vectors.append(b"")
    vectors.append(b"a")
    vectors.append(b"hello, fxj2me!\n" * 3)
    vectors.append(bytes(range(256)) * 4)
    vectors.append(b"\x00" * 5000)                       # long runs
    vectors.append(b"ab" * 4000)                         # short repeats
    vectors.append(rng.randbytes(3000))                  # incompressible
    vectors.append(b"The quick brown fox. " * 400)
    # long-distance matches
    pattern = rng.randbytes(64)
    vectors.append(pattern * 100)
    big = bytearray()
    for i in range(200):
        big += rng.randbytes(rng.randrange(0, 40))
        big += bytes([i & 0xFF]) * rng.randrange(1, 60)
    vectors.append(bytes(big))

    for i, data in enumerate(vectors):
        for level, name in ((1, "fast"), (9, "best")):
            comp = raw_deflate(data, level)
            with open(os.path.join(outdir, f"data_{i}_{name}.bin"), "wb") as f:
                f.write(data)
            with open(os.path.join(outdir, f"comp_{i}_{name}.bin"), "wb") as f:
                f.write(comp)
        comp = stored_deflate(data)
        with open(os.path.join(outdir, f"data_{i}_stored.bin"), "wb") as f:
            f.write(data)
        with open(os.path.join(outdir, f"comp_{i}_stored.bin"), "wb") as f:
            f.write(comp)


def make_classes(outdir: str):
    srcs = [os.path.join(HERE, "fixtures", n)
            for n in ("Fib.java", "Runtime.java", "MissingSuper.java")]
    try:
        subprocess.run(
            ["javac", "--release", "8", "-g:none", "-d", outdir] + srcs,
            check=True, capture_output=True,
        )
    except (OSError, subprocess.CalledProcessError) as e:
        print("WARNING: javac failed, no class fixtures:", e, file=sys.stderr)
        return False
    return True


def make_hello(outdir: str, bootjar: str):
    """Compile HelloMidlet against the boot class library into hello.jar."""
    if not bootjar or not os.path.exists(bootjar):
        print("WARNING: no boot.jar, skipping hello.jar", file=sys.stderr)
        return False
    hdir = os.path.join(outdir, "hello-classes")
    os.makedirs(hdir, exist_ok=True)
    src = os.path.join(HERE, "fixtures", "HelloMidlet.java")
    try:
        subprocess.run(
            ["javac", "--release", "8", "-g:none", "-cp", bootjar,
             "-d", hdir, src],
            check=True, capture_output=True,
        )
    except (OSError, subprocess.CalledProcessError) as e:
        print("WARNING: HelloMidlet javac failed:", e, file=sys.stderr)
        return False

    manifest = ("".join(wrap_manifest_line(l) for l in (
        "Manifest-Version: 1.0",
        "MIDlet-Name: Hello",
        "MIDlet-Version: 1.0.0",
        "MIDlet-Vendor: Acme",
        "MIDlet-1: Hello, , HelloMidlet",
        "MIDlet-Description: " + DESC,
        "MicroEdition-Configuration: CLDC-1.1",
        "MicroEdition-Profile: MIDP-2.0",
    )) + "\r\n").encode("ascii")
    jar_path = os.path.join(outdir, "hello.jar")
    with zipfile.ZipFile(jar_path, "w") as z:
        z.writestr(zipfile.ZipInfo("META-INF/MANIFEST.MF"), manifest,
                   compress_type=zipfile.ZIP_DEFLATED)
        for name in sorted(os.listdir(hdir)):
            if name.startswith("HelloMidlet") and name.endswith(".class"):
                with open(os.path.join(hdir, name), "rb") as f:
                    z.writestr(zipfile.ZipInfo(name), f.read(),
                               compress_type=zipfile.ZIP_DEFLATED)
    return True


def make_jar(outdir: str, have_classes: bool):
    jar_path = os.path.join(outdir, "test.jar")
    with zipfile.ZipFile(jar_path, "w") as z:
        if have_classes:
            for name, method in (("Fib.class", zipfile.ZIP_DEFLATED),
                                 ("Fib$Cell.class", zipfile.ZIP_STORED),
                                 ("Runtime.class", zipfile.ZIP_DEFLATED),
                                 # Stub.class is deliberately absent: loading
                                 # HasMissingParent must name the missing super.
                                 ("HasMissingParent.class",
                                  zipfile.ZIP_DEFLATED)):
                with open(os.path.join(outdir, name), "rb") as f:
                    z.writestr(zipfile.ZipInfo(name), f.read(), compress_type=method)
        else:
            z.writestr("dummy.txt", b"no classes\n", compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("hello.txt", b"hello from the jar\n" * 10,
                   compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("plain.txt", b"stored entry\n",
                   compress_type=zipfile.ZIP_STORED)
    return jar_path


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "fixtures"
    bootjar = sys.argv[2] if len(sys.argv) > 2 else None
    os.makedirs(outdir, exist_ok=True)
    make_vectors(outdir)
    have = make_classes(outdir)
    make_jar(outdir, have)
    hello = make_hello(outdir, bootjar)
    print(f"fixtures written to {outdir} (classes: {have}, hello: {hello})")


if __name__ == "__main__":
    main()
