#!/usr/bin/env python3
"""Regression for the original music EOF mixer hang; no download or deployment.

Requires Clang or GCC, exact old SoLoud upstream sources, and the seven local
original OGG files. Tests the repo's actual WavStream against the unmodified old
core/decoder. No proprietary assets are copied into this repository.
"""
import argparse
import hashlib
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import time
import zipfile

COMMIT = "7b6cb7185d12b0d3283a9bf30e6cc3295e57a77c"
ARCHIVE_SHA256 = "324cfa200e847341c0b07b6fac4f320f09515ce2e44a915a430868a11ced3127"
# Sorted paths + NUL + binary SHA256 of all consumed upstream core .cpp files,
# soloud*.h headers, old WavStream, stb header/decoder and the null backend.
SOURCE_SHA256 = "327a01ab636789d209427d878216633ce76d0089f8102e445150d433fa6878f1"
REPO = Path(__file__).resolve().parents[1]
BUILD = REPO / "build/music-eof-tests"
NAMES = ["game_0.ogg", *(str(n) + ".ogg" for n in range(1, 7))]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_source(source):
    files = sorted(set(source.glob("src/core/*.cpp")) |
                   set(source.glob("include/soloud*.h")) |
                   {source / "src/audiosource/wav/stb_vorbis.c",
                    source / "src/audiosource/wav/stb_vorbis.h",
                    source / "src/audiosource/wav/soloud_wavstream.cpp",
                    source / "src/backend/null/soloud_null.cpp"})
    digest = hashlib.sha256()
    for path in files:
        if not path.is_file():
            raise RuntimeError(f"Missing upstream file: {path}")
        digest.update(path.relative_to(source).as_posix().encode() + b"\0" +
                      hashlib.sha256(path.read_bytes()).digest())
    if digest.hexdigest() != SOURCE_SHA256:
        raise RuntimeError(f"Upstream files differ from exact SoLoud {COMMIT}; "
                           "provide the unmodified old source directory/archive")


def load_source(args):
    if args.soloud_source:
        source = args.soloud_source.resolve()
    else:
        archive_path = args.upstream_archive.resolve()
        if sha256(archive_path) != ARCHIVE_SHA256:
            raise RuntimeError("Upstream ZIP SHA256 mismatch; no extraction performed")
        source = BUILD / ("soloud-" + COMMIT)
        if not source.exists():
            with zipfile.ZipFile(archive_path) as archive:
                for member in archive.infolist():
                    name = PurePosixPath(member.filename)
                    if name.is_absolute() or ".." in name.parts:
                        raise RuntimeError("Unsafe upstream ZIP path")
                archive.extractall(BUILD)
    verify_source(source)
    return source


def run_command(command, report):
    result = subprocess.run([str(part) for part in command], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.stdout:
        report.write(result.stdout)
        report.flush()
    if result.returncode:
        raise RuntimeError(f"Build command failed ({result.returncode}): "
                           f"{' '.join(map(str, command))}\n{result.stdout}")


def build(source, args, report):
    include = ["-I", source / "include", "-I", source / "src/audiosource/wav"]
    decoder = BUILD / "stb_vorbis.o"
    run_command([args.cc, "-O2", "-w", *include, "-c",
                 source / "src/audiosource/wav/stb_vorbis.c", "-o", decoder], report)
    common = [args.cxx, "-std=c++11", "-O2", "-w", "-fno-rtti", "-DWITH_NULL",
              *include, REPO / "tests/music_eof_harness.cpp",
              *sorted(source.glob("src/core/*.cpp")),
              source / "src/backend/null/soloud_null.cpp", decoder]
    # Fixed uses this repository's real shipping source, never a generated copy.
    # Original uses the untouched upstream file as the negative control.
    for kind, cpp in (("fixed", REPO / "lib/soloud/soloud_wavstream.cpp"),
                      ("original", source / "src/audiosource/wav/soloud_wavstream.cpp")):
        run_command([*common, cpp, "-o", BUILD / ("music_eof_" + kind)], report)
        report.write(f"BUILT {kind} source={cpp}\n")
    report.flush()


def check_case(kind, path, channels, looping, report):
    command = [str(BUILD / ("music_eof_" + kind)), str(path), str(channels), str(looping)]
    started = time.monotonic()
    try:
        result = subprocess.run(command, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True,
                                timeout=1.5 if kind == "original" else 10)
        output = result.stdout
        passed = kind == "fixed" and result.returncode == 0
        outcome = f"exit={result.returncode}"
    except subprocess.TimeoutExpired as error:
        output = error.stdout or b""
        if isinstance(output, bytes):
            output = output.decode(errors="replace")
        passed = kind == "original" and "EOF_BOUNDARY" in output
        outcome = "TIMEOUT"
    summary = (f"{'PASS' if passed else 'FAIL'} kind={kind} file={path.name} "
               f"channels={channels} loop={looping} {outcome} "
               f"seconds={time.monotonic() - started:.3f}")
    print(summary, flush=True)
    report.write(summary + "\n" + output + "\n")
    report.flush()
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    upstream = parser.add_mutually_exclusive_group(required=True)
    upstream.add_argument("--soloud-source", type=Path, help="Exact unmodified upstream checkout")
    upstream.add_argument("--upstream-archive", type=Path, help="Pinned upstream ZIP, SHA checked")
    parser.add_argument("--data", type=Path, required=True,
                        help="Directory with original game_0.ogg and 1.ogg through 6.ogg")
    parser.add_argument("--cc", default=shutil.which("clang") or "gcc")
    parser.add_argument("--cxx", default=shutil.which("clang++") or "g++")
    args = parser.parse_args()
    data = args.data.resolve()
    for name in NAMES:
        if not (data / name).is_file():
            parser.error(f"Missing local game asset: {data / name}")
    BUILD.mkdir(parents=True, exist_ok=True)
    report_path = BUILD / "report.log"
    failures = total = 0
    with report_path.open("w") as report:
        source = load_source(args)
        report.write(f"UPSTREAM commit={COMMIT} source_digest={SOURCE_SHA256}\n")
        report.write(f"FIXED_SOURCE sha256={sha256(REPO / 'lib/soloud/soloud_wavstream.cpp')}\n")
        for name in NAMES:
            report.write(f"ASSET {name} sha256={sha256(data / name)}\n")
        build(source, args, report)
        for kind in ("fixed", "original"):
            names = NAMES if kind == "fixed" else ["game_0.ogg"]
            for name in names:
                for channels in (1, 2):
                    for looping in (0, 1):
                        total += 1
                        failures += not check_case(kind, data / name, channels, looping, report)
        summary = f"TOTAL CASES {total}; FAILED {failures}\n"
        report.write(summary)
    print(summary.strip())
    print(f"Report: {report_path}")
    return bool(failures)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, zipfile.BadZipFile) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(2)
