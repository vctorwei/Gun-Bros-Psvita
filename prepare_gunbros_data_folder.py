#!/usr/bin/env python3
"""Merge verified original/patched Gun Bros data into a new Vita data folder.

The original MP3 and WAV inputs are preserved. Loose MP3/WAV files are
converted beside their sources. WVGA BIG resources reuse an exact matching XGA
OGG when one exists; only genuinely different WVGA sounds are written below
``audio/runtime``. A manifest lets the Vita runtime identify a loaded BIG-file
PCM payload and replace it without modifying the BIG archives.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib
from dataclasses import dataclass, replace
from pathlib import Path


WAV_RESOURCE_TYPE = bytes.fromhex("54778afd")
RESOURCE_FORMAT_NONE = bytes.fromhex("04000000")
RESOURCE_FORMAT_ZLIB = bytes.fromhex("04008000")
FNV64_OFFSET_BASIS = 0xCBF29CE484222325
FNV64_PRIME = 0x100000001B3
MANIFEST_HEADER = "pcm_fnv64,pcm_size,pack,resource_id,logical_id,ogg_path\n"
REQUIRED_MUSIC_TRACKS = ("1", "2", "3", "4", "5", "6", "game_0")
SUPPORTED_SO_SHA256 = "44deac65ac85faa06fad27d686d721222f1794353cc31395e1ea1175dd47a899"
LOGOS = ("classic.png", "reloaded.png", "info_logo.png",
         "choose your version.png", "selection arrow.png")


@dataclass(frozen=True)
class PackAudioResource:
    pack: Path
    resource_id: int
    logical_id: int
    wav_data: bytes
    pcm_data: bytes
    destination: Path
    reuses_xga: bool = False


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(
        description=(
            "Verify and merge original/patched Gun Bros folders, copy launcher "
            "logos, and convert MP3/WAV and BIG-contained audio for Vita."
        )
    )
    parser.add_argument(
        "--original-dir",
        type=Path,
        default=repo_root / "gunbros_real_assets",
        help="Original extracted Android data (default: %(default)s)",
    )
    parser.add_argument("--patched-dir", type=Path, default=repo_root / "gunbros_free",
                        help="Patched data overlay (default: %(default)s)")
    parser.add_argument("--logos-dir", type=Path, default=repo_root / "logos")
    parser.add_argument("--savedata-dir", type=Path,
                        default=next((repo_root / name for name in ("savedata", "savetata")
                                      if (repo_root / name).is_dir()), None),
                        help="Save contents to copy into gunbros_free (auto-detected beside script)")
    parser.add_argument("--files-dir", type=Path, help="Game files folder")
    parser.add_argument("--apk-dir", type=Path, help="Folder containing all extracted APK contents")
    parser.add_argument("--output-dir", type=Path, default=repo_root / "build/data/gunbros",
                        help="New output folder; must not exist (default: %(default)s)")
    parser.add_argument(
        "--ffmpeg",
        default="ffmpeg",
        help="ffmpeg executable or absolute path (default: %(default)s)",
    )
    parser.add_argument(
        "--quality",
        type=float,
        default=5.0,
        help="libvorbis VBR quality from -1 to 10 (default: %(default)s)",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="re-encode OGG files even when they are newer than their source",
    )
    parser.add_argument(
        "--no-pack-audio",
        action="store_true",
        help="convert only loose MP3/WAV files; do not extract WVGA BIG audio",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="show the conversion plan without writing files",
    )
    return parser.parse_args()


def find_program(program: str) -> str | None:
    candidate = Path(program).expanduser()
    if candidate.parent != Path(".") or candidate.is_absolute():
        return str(candidate.resolve()) if candidate.is_file() else None
    return shutil.which(program)


def needs_conversion(source: Path, destination: Path, force: bool) -> bool:
    if force or not valid_ogg_file(destination):
        return True
    return destination.stat().st_mtime_ns < source.stat().st_mtime_ns


def valid_ogg_file(path: Path) -> bool:
    try:
        if not path.is_file() or path.stat().st_size <= 4:
            return False
        with path.open("rb") as stream:
            return stream.read(4) == b"OggS"
    except OSError:
        return False


def temporary_ogg_path(destination: Path) -> Path:
    return destination.with_name(f".{destination.stem}.tmp.ogg")


def ffmpeg_command(
    ffmpeg: str, input_args: list[str], destination: Path, quality: float
) -> list[str]:
    return [
        ffmpeg,
        "-nostdin",
        "-loglevel",
        "error",
        "-y",
        *input_args,
        "-map",
        "0:a:0",
        "-vn",
        "-c:a",
        "libvorbis",
        "-q:a",
        f"{quality:g}",
        "-map_metadata",
        "-1",
        str(destination),
    ]


def finish_conversion(temporary: Path, destination: Path) -> None:
    if not valid_ogg_file(temporary):
        raise RuntimeError("ffmpeg produced an empty or invalid OGG output file")
    temporary.replace(destination)


def convert_file(ffmpeg: str, source: Path, destination: Path, quality: float) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = temporary_ogg_path(destination)
    temporary.unlink(missing_ok=True)
    command = ffmpeg_command(ffmpeg, ["-i", str(source)], temporary, quality)

    try:
        subprocess.run(command, check=True,
                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        finish_conversion(temporary, destination)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def convert_wav_bytes(
    ffmpeg: str, wav_data: bytes, destination: Path, quality: float
) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = temporary_ogg_path(destination)
    temporary.unlink(missing_ok=True)
    command = ffmpeg_command(
        ffmpeg, ["-f", "wav", "-i", "pipe:0"], temporary, quality
    )

    try:
        subprocess.run(command, input=wav_data, check=True,
                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        finish_conversion(temporary, destination)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def checked_slice(data: bytes, start: int, end: int, label: str) -> bytes:
    if start < 0 or end < start or end > len(data):
        raise ValueError(f"{label} is outside the file")
    return data[start:end]


def decode_resource(blob: bytes, label: str) -> bytes:
    if blob.startswith(RESOURCE_FORMAT_NONE):
        return blob[4:]
    if blob.startswith(RESOURCE_FORMAT_ZLIB):
        if len(blob) < 12:
            raise ValueError(f"{label}: truncated zlib resource header")
        raw_size, compressed_size = struct.unpack_from("<II", blob, 4)
        compressed = blob[12:]
        if len(compressed) != compressed_size:
            raise ValueError(f"{label}: compressed resource size mismatch")
        try:
            raw = zlib.decompress(compressed)
        except zlib.error as exc:
            raise ValueError(f"{label}: invalid zlib resource: {exc}") from exc
        if len(raw) != raw_size:
            raise ValueError(f"{label}: decoded resource size mismatch")
        return raw
    return blob


def wav_pcm_payload(wav_data: bytes, label: str) -> bytes:
    if len(wav_data) < 12 or wav_data[:4] != b"RIFF" or wav_data[8:12] != b"WAVE":
        raise ValueError(f"{label}: resource is not a RIFF/WAVE file")

    offset = 12
    while offset + 8 <= len(wav_data):
        chunk_type = wav_data[offset : offset + 4]
        chunk_size = struct.unpack_from("<I", wav_data, offset + 4)[0]
        content_start = offset + 8
        content_end = content_start + chunk_size
        chunk = checked_slice(wav_data, content_start, content_end, label)
        if chunk_type == b"data":
            if not chunk:
                raise ValueError(f"{label}: WAV data chunk is empty")
            return chunk
        offset = content_end + (chunk_size & 1)

    raise ValueError(f"{label}: WAV data chunk is missing")


def parse_pack_audio(pack: Path, runtime_dir: Path) -> list[PackAudioResource]:
    data = pack.read_bytes()
    label = pack.name
    if len(data) < 0x30 or data[:4] != b"FGIB":
        raise ValueError(f"{label}: invalid BIG header")

    (
        header_size,
        add_header_count,
        table_start,
        resource_count,
        table_end,
        content_size,
    ) = struct.unpack_from("<6I", data, 8)
    if header_size < 0x20 or table_start < header_size:
        raise ValueError(f"{label}: invalid BIG header layout")
    if table_start - header_size != add_header_count * 8:
        raise ValueError(f"{label}: additional-header count mismatch")
    if table_end - table_start != resource_count * 8 + 8:
        raise ValueError(f"{label}: resource table size mismatch")
    if table_end + content_size != len(data):
        raise ValueError(f"{label}: BIG content size mismatch")

    logical_ids: dict[int, int] = {}
    for index in range(add_header_count):
        first_logical, range_count, first_resource = struct.unpack_from(
            "<IHH", data, header_size + index * 8
        )
        if first_resource + range_count > resource_count:
            raise ValueError(f"{label}: logical resource range is invalid")
        for relative in range(range_count):
            logical_ids[first_resource + relative] = first_logical + relative

    resources: list[PackAudioResource] = []
    for resource_id in range(resource_count):
        toc = table_start + resource_id * 8
        resource_type = checked_slice(data, toc, toc + 4, label)
        offset = struct.unpack_from("<I", data, toc + 4)[0]
        next_offset = struct.unpack_from("<I", data, toc + 12)[0]
        if resource_type != WAV_RESOURCE_TYPE:
            continue

        resource_label = f"{label} resource {resource_id}"
        encoded = checked_slice(data, offset, next_offset, resource_label)
        wav_data = decode_resource(encoded, resource_label)
        pcm_data = wav_pcm_payload(wav_data, resource_label)
        destination = runtime_dir / f"{pack.stem}_resource{resource_id}.ogg"
        resources.append(
            PackAudioResource(
                pack=pack,
                resource_id=resource_id,
                logical_id=logical_ids.get(resource_id, -1),
                wav_data=wav_data,
                pcm_data=pcm_data,
                destination=destination,
            )
        )

    return resources


def fnv1a64(data: bytes) -> int:
    result = FNV64_OFFSET_BASIS
    for value in data:
        result ^= value
        result = (result * FNV64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return result


def reuse_exact_xga_audio(
    resources: list[PackAudioResource], data_dir: Path
) -> tuple[list[PackAudioResource], int]:
    by_payload: dict[tuple[int, int], list[Path]] = {}
    for wav in sorted((data_dir / "audio").glob("*_xga_resource*.wav")):
        pcm = wav_pcm_payload(wav.read_bytes(), str(wav))
        by_payload.setdefault((fnv1a64(pcm), len(pcm)), []).append(wav)

    remapped: list[PackAudioResource] = []
    reused = 0
    for resource in resources:
        key = (fnv1a64(resource.pcm_data), len(resource.pcm_data))
        candidates = by_payload.get(key, [])
        if not candidates:
            remapped.append(resource)
            continue

        xga_pack_stem = resource.pack.stem.replace("_wvga", "_xga")
        same_pack = [
            path
            for path in candidates
            if path.stem.startswith(f"{xga_pack_stem}_resource")
        ]
        source = (same_pack or candidates)[0]
        remapped.append(
            replace(resource, destination=source.with_suffix(".ogg"), reuses_xga=True)
        )
        reused += 1
    return remapped, reused


def manifest_text(
    resources: list[PackAudioResource], data_dir: Path
) -> str:
    lines = [MANIFEST_HEADER]
    for resource in resources:
        relative = resource.destination.relative_to(data_dir).as_posix()
        lines.append(
            f"{fnv1a64(resource.pcm_data):016x},{len(resource.pcm_data)},"
            f"{resource.pack.name},{resource.resource_id},{resource.logical_id},"
            f"{relative}\n"
        )
    return "".join(lines)


def write_text_atomic(destination: Path, content: str) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(f".{destination.name}.tmp")
    temporary.unlink(missing_ok=True)
    try:
        temporary.write_text(content, encoding="utf-8", newline="\n")
        temporary.replace(destination)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def prepare_audio(args: argparse.Namespace) -> int:
    data_dir = args.data_dir.expanduser().resolve()

    if not -1.0 <= args.quality <= 10.0:
        print("error: --quality must be between -1 and 10", file=sys.stderr)
        return 2
    if not data_dir.is_dir():
        print(f"error: Gun Bros data directory does not exist: {data_dir}", file=sys.stderr)
        return 2

    missing_music = [
        name
        for name in REQUIRED_MUSIC_TRACKS
        if not (data_dir / "files" / f"{name}.mp3").is_file()
    ]
    if missing_music:
        print(
            "error: required music MP3 files are missing: "
            + ", ".join(f"{name}.mp3" for name in missing_music),
            file=sys.stderr,
        )
        return 2

    loose_sources = sorted(
        (
            path
            for path in data_dir.rglob("*")
            if path.is_file() and path.suffix.lower() in {".mp3", ".wav"}
        ),
        key=lambda path: path.relative_to(data_dir).as_posix().lower(),
    )

    pack_resources: list[PackAudioResource] = []
    runtime_dir = data_dir / "audio" / "runtime"
    if not args.no_pack_audio:
        packs = sorted((data_dir / "files").glob("pack*_wvga.big"))
        if not packs:
            print(
                f"error: no WVGA BIG packs found below {data_dir / 'files'}",
                file=sys.stderr,
            )
            return 2
        try:
            for pack in packs:
                pack_resources.extend(parse_pack_audio(pack, runtime_dir))
            pack_resources, reused_xga = reuse_exact_xga_audio(
                pack_resources, data_dir
            )
        except (OSError, ValueError, struct.error) as exc:
            print(f"error: failed to parse BIG audio: {exc}", file=sys.stderr)
            return 1
    else:
        reused_xga = 0

    if not loose_sources and not pack_resources:
        print(f"error: no convertible audio found below {data_dir}", file=sys.stderr)
        return 2

    ffmpeg = find_program(args.ffmpeg)
    if ffmpeg is None and not args.dry_run:
        print(
            "error: ffmpeg was not found; install it or pass --ffmpeg /path/to/ffmpeg",
            file=sys.stderr,
        )
        return 2

    converted = 0
    current = 0
    for source in loose_sources:
        destination = source.with_suffix(".ogg")
        relative_source = source.relative_to(data_dir)
        relative_destination = destination.relative_to(data_dir)
        if not needs_conversion(source, destination, args.force):
            current += 1
            print(f"current  {relative_destination.as_posix()}")
            continue

        print(f"convert  {relative_source.as_posix()} -> {relative_destination.as_posix()}")
        if not args.dry_run:
            try:
                convert_file(ffmpeg, source, destination, args.quality)
            except (OSError, subprocess.CalledProcessError, RuntimeError) as exc:
                print(f"error: failed to convert {relative_source}: {exc}", file=sys.stderr)
                return 1
        converted += 1

    for resource in pack_resources:
        if resource.reuses_xga:
            continue
        relative_destination = resource.destination.relative_to(data_dir)
        if not needs_conversion(resource.pack, resource.destination, args.force):
            current += 1
            print(f"current  {relative_destination.as_posix()}")
            continue

        print(
            f"extract  {resource.pack.name} resource={resource.resource_id} "
            f"logical={resource.logical_id} -> {relative_destination.as_posix()}"
        )
        if not args.dry_run:
            try:
                convert_wav_bytes(
                    ffmpeg, resource.wav_data, resource.destination, args.quality
                )
            except (OSError, subprocess.CalledProcessError, RuntimeError) as exc:
                print(
                    f"error: failed to convert {resource.pack.name} "
                    f"resource {resource.resource_id}: {exc}",
                    file=sys.stderr,
                )
                return 1
        converted += 1

    invalid_music_ogg = [
        name
        for name in REQUIRED_MUSIC_TRACKS
        if not args.dry_run
        and not valid_ogg_file(data_dir / "files" / f"{name}.ogg")
    ]
    if invalid_music_ogg:
        print(
            "error: music OGG conversion is missing or invalid: "
            + ", ".join(f"{name}.ogg" for name in invalid_music_ogg),
            file=sys.stderr,
        )
        return 1

    print(
        "music   "
        + ", ".join(f"{name}.ogg" for name in REQUIRED_MUSIC_TRACKS)
        + " (ready)"
    )

    if pack_resources:
        manifest = runtime_dir / "audio_map.csv"
        content = manifest_text(pack_resources, data_dir)
        print(
            f"manifest {manifest.relative_to(data_dir).as_posix()} "
            f"({len(pack_resources)} BIG WAV mapping(s): "
            f"{reused_xga} exact XGA reuse, "
            f"{len(pack_resources) - reused_xga} WVGA-only)"
        )
        if not args.dry_run:
            try:
                write_text_atomic(manifest, content)
            except OSError as exc:
                print(f"error: failed to write audio manifest: {exc}", file=sys.stderr)
                return 1

    action = "would convert" if args.dry_run else "converted"
    print(
        f"done: {action} {converted} file(s), {current} already current; "
        "the original MP3, WAV, and BIG files were preserved"
    )
    return 0


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def collect_data(folder: Path, files_dir: Path | None = None) -> dict[str, Path]:
    """Normalize the repository's original dump and already patched data layout."""
    root = folder / "gunbros_free" if (folder / "gunbros_free").is_dir() else folder
    if not root.is_dir():
        raise ValueError(f"Input directory does not exist: {folder}")
    apk_roots = (root, root / "apk", root / "files/apk")
    libraries = [base / name for base in apk_roots
                 for name in ("libandroidplatformjni.so", "lib/armeabi/libandroidplatformjni.so")
                 if (base / name).is_file()]
    if not libraries:
        raise ValueError(f"Missing extracted libandroidplatformjni.so below {folder}")
    for library in libraries:
        actual = sha256(library)
        if actual != SUPPORTED_SO_SHA256:
            raise ValueError(f"Unsupported library {library}: SHA-256 {actual}; "
                             f"expected {SUPPORTED_SO_SHA256}")
    for apk in sorted(folder.glob("*.apk")):
        with zipfile.ZipFile(apk) as archive:
            actual = hashlib.sha256(archive.read("lib/armeabi/libandroidplatformjni.so")).hexdigest()
        if actual != SUPPORTED_SO_SHA256:
            raise ValueError(f"APK library hash mismatch: {apk}: {actual}")
    result = {"libandroidplatformjni.so": libraries[0]}
    for base in reversed(apk_roots):
        asset = base / "assets/gunbros.big"
        if asset.is_file():
            result["assets/gunbros.big"] = asset
    # Only ship runtime data, never Android code, user saves or debug exports.
    for pattern in ("pack*_wvga.big", "packTOC_wvga.dat", "*.mp3",
                    "events.dat", "Gman_intro.3gp", "file.big"):
        for path in sorted((files_dir if files_dir is not None else root / "files").glob(pattern)):
            if path.is_file():
                result[f"files/{path.name}"] = path
    for path in sorted((root / "audio").glob("*.wav")):
        result[f"audio/{path.name}"] = path
    return result


def preparation_plan(args: argparse.Namespace) -> tuple[dict[str, Path], dict]:
    output = args.output_dir.expanduser().resolve()
    simple = getattr(args, "files_dir", None) is not None or getattr(args, "apk_dir", None) is not None
    if simple and not (args.files_dir and args.apk_dir):
        raise ValueError("Select both the files folder and the extracted APK folder")
    roots = [p.expanduser().resolve() for p in
             ((args.apk_dir, args.files_dir, args.logos_dir) if simple else
              (args.original_dir, args.patched_dir, args.logos_dir))]
    savedata = getattr(args, "savedata_dir", None)
    savedata = savedata.expanduser().resolve() if savedata is not None else None
    if savedata is not None and not savedata.is_dir():
        raise ValueError(f"Savedata folder does not exist: {savedata}")
    for root in roots + ([savedata] if savedata is not None else []):
        if output == root or output.is_relative_to(root) or root.is_relative_to(output):
            raise ValueError(f"Output must be separate from input directories: {root}")
    if output.exists():
        raise ValueError(f"Output already exists: {output}; choose a new --output-dir")
    if simple:
        if not roots[1].is_dir():
            raise ValueError(f"Files folder does not exist: {roots[1]}")
        original, patched = collect_data(roots[0], roots[1]), {}
    else:
        original, patched = (collect_data(root) for root in roots[:2])
    required = ["libandroidplatformjni.so", "assets/gunbros.big", "files/packTOC_wvga.dat",
                "files/pack0_core_wvga.big"]
    required += [f"files/pack{i}_wvga.big" for i in range(1, 13)]
    required += [f"files/{track}.mp3" for track in REQUIRED_MUSIC_TRACKS]
    missing = [name for name in required if name not in original]
    if missing:
        raise ValueError("Original data is incomplete: " + ", ".join(missing))
    merged = original | patched
    plan = {f"gunbros_free/{name}": path for name, path in merged.items()}
    if savedata is not None:
        for path in sorted(savedata.rglob("*")):
            if not path.is_file():
                continue
            name = "gunbros_free/" + path.relative_to(savedata).as_posix()
            if name.casefold() in {key.casefold() for key in plan}:
                raise ValueError(f"Savedata conflicts with a game asset: {name}")
            plan[name] = path
    for name in LOGOS:
        path = roots[2] / name
        if not path.is_file() or path.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
            raise ValueError(f"Missing or invalid launcher PNG: {path}")
        plan[f"logos/{name}"] = path
    report = {"library_sha256": SUPPORTED_SO_SHA256, "files": {}}
    for name, path in plan.items():
        relative = name.removeprefix("gunbros_free/")
        entry = {"source": str(path), "sha256": sha256(path)}
        if relative in original and relative in patched:
            entry["original_sha256"] = sha256(original[relative])
            entry["changed_by_overlay"] = entry["sha256"] != entry["original_sha256"]
        report["files"][name] = entry
    return plan, report


def main() -> int:
    args = parse_args()
    try:
        if not -1 <= args.quality <= 10:
            raise ValueError("--quality must be between -1 and 10")
        plan, report = preparation_plan(args)
        print(f"Verified native library SHA-256: {SUPPORTED_SO_SHA256}")
        changed = sum(bool(item.get("changed_by_overlay")) for item in report["files"].values())
        print(f"Plan: {len(plan)} source files, {changed} changed by patched overlay")
        if getattr(args, "savedata_dir", None) is not None:
            print(f"Copy savedata contents: {args.savedata_dir} -> gunbros_free/")
        if args.dry_run:
            print(f"Would prepare {args.output_dir}; audio conversion was not executed")
            return 0
        if find_program(args.ffmpeg) is None:
            raise ValueError("ffmpeg was not found; install it or pass --ffmpeg /path/to/ffmpeg")
        output = args.output_dir.expanduser().resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        # Publish only a completely prepared tree; TemporaryDirectory owns cleanup.
        with tempfile.TemporaryDirectory(prefix=".gunbros-", dir=output.parent) as temporary:
            stage = Path(temporary) / "gunbros"
            for name, source in plan.items():
                destination = stage / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
                if sha256(destination) != report["files"][name]["sha256"]:
                    raise ValueError(f"Source changed during preparation: {source}")
            (stage / "gunbros_reloaded").mkdir()
            args.data_dir = stage / "gunbros_free"
            # Regenerate OGGs and manifest from the final merged inputs. Never
            # trust an old overlay's OGG timestamps or mappings against new BIGs.
            result = prepare_audio(args)
            if result:
                return result
            report["audio"] = {"quality": args.quality, "pack_audio": not args.no_pack_audio}
            write_text_atomic(stage / "preparation.json", json.dumps(report, indent=2) + "\n")
            stage.rename(output)
        print(f"Ready: copy {output} to ux0:data/gunbros/")
        return 0
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


def launch_gui() -> int:
    import queue
    import threading
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    from tkinter.scrolledtext import ScrolledText

    repo = Path(__file__).resolve().parent
    window = tk.Tk()
    window.title("Gun Bros — Prepare Vita data")
    window.geometry("760x520")
    window.minsize(660, 460)
    frame = ttk.Frame(window, padding=20)
    frame.pack(fill="both", expand=True)
    frame.columnconfigure(1, weight=1)
    ttk.Label(frame, text="Prepare Gun Bros for PS Vita", font=("Segoe UI", 17, "bold")).grid(
        row=0, column=0, columnspan=3, sticky="w", pady=(0, 8))
    ttk.Label(frame, text="Choose the two folders, then click Generate folder.").grid(
        row=1, column=0, columnspan=3, sticky="w", pady=(0, 16))
    files = tk.StringVar()
    apk = tk.StringVar()
    output = tk.StringVar(value=str(repo / "output/gunbros"))
    ffmpeg = tk.StringVar(value=find_program("ffmpeg") or "")
    widgets = []

    def row(index, label, variable, executable=False):
        ttk.Label(frame, text=label).grid(row=index, column=0, sticky="w", padx=(0, 12), pady=6)
        entry = ttk.Entry(frame, textvariable=variable)
        entry.grid(row=index, column=1, sticky="ew", pady=6)
        def browse():
            if executable:
                path = filedialog.askopenfilename(parent=window, title="Select FFmpeg executable")
            else:
                path = filedialog.askdirectory(parent=window, title=label)
            if path:
                variable.set(str(Path(path) / "gunbros") if variable is output else path)
        button = ttk.Button(frame, text="Browse…", command=browse)
        button.grid(row=index, column=2, padx=(8, 0))
        widgets.extend((entry, button))

    row(2, "Files folder here", files)
    row(3, "All extracted APK contents here", apk)
    row(4, "Generate folder at", output)
    row(5, "FFmpeg (detected automatically)", ffmpeg, True)
    status = tk.StringVar(value="Ready")
    ttk.Label(frame, textvariable=status, wraplength=700).grid(
        row=7, column=0, columnspan=3, sticky="w", pady=(10, 4))
    progress = ttk.Progressbar(frame, mode="indeterminate")
    progress.grid(row=8, column=0, columnspan=3, sticky="ew")
    log = ScrolledText(frame, height=10, state="disabled", wrap="word")
    log.grid(row=9, column=0, columnspan=3, sticky="nsew", pady=(10, 0))
    frame.rowconfigure(9, weight=1)
    messages = queue.Queue()
    running = False

    def generate():
        nonlocal running
        if not all(v.get().strip() for v in (files, apk, output, ffmpeg)):
            messagebox.showerror("Missing folder or FFmpeg", "Select both input folders, an output location and FFmpeg.", parent=window)
            return
        command = [sys.executable, "-u", str(Path(__file__).resolve()),
                   "--files-dir", files.get(), "--apk-dir", apk.get(),
                   "--output-dir", output.get(), "--ffmpeg", ffmpeg.get()]
        destination = output.get()
        running = True
        for widget in widgets:
            widget.configure(state="disabled")
        log.configure(state="normal")
        log.delete("1.0", "end")
        log.configure(state="disabled")
        status.set("Preparing files and converting sounds…")
        progress.start(12)

        def worker():
            try:
                with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                      text=True, encoding="utf-8", errors="replace",
                                      env={**os.environ, "PYTHONIOENCODING": "utf-8"},
                                      creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)) as process:
                    for line in process.stdout:
                        messages.put(("log", line))
                    messages.put(("done", (process.wait(), destination)))
            except Exception as exc:
                messages.put(("log", f"{exc}\n"))
                messages.put(("done", (1, destination)))
        threading.Thread(target=worker, daemon=True).start()

    generate_button = ttk.Button(frame, text="Generate folder", command=generate)
    generate_button.grid(row=6, column=0, columnspan=3, sticky="ew", pady=(12, 0))
    widgets.append(generate_button)

    def poll():
        nonlocal running
        for _ in range(100):
            try:
                kind, value = messages.get_nowait()
            except queue.Empty:
                break
            if kind == "log":
                log.configure(state="normal")
                log.insert("end", value)
                log.see("end")
                log.configure(state="disabled")
            else:
                running = False
                progress.stop()
                for widget in widgets:
                    widget.configure(state="normal")
                code, destination = value
                if code == 0:
                    status.set("Done — copy the generated gunbros folder to ux0:data/.")
                    messagebox.showinfo("Folder ready", f"Generated: {destination}\n\nCopy this gunbros folder to ux0:data/ on your Vita.", parent=window)
                else:
                    status.set("Preparation failed. See the details below, correct the input, and try again.")
                    messagebox.showerror("Preparation failed", "See the details in the log below.", parent=window)
        window.after(100, poll)

    def close():
        if running:
            messagebox.showinfo("Preparing data", "Please wait for preparation to finish before closing.", parent=window)
        else:
            window.destroy()
    window.protocol("WM_DELETE_WINDOW", close)
    window.after(100, poll)
    window.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(launch_gui() if len(sys.argv) == 1 or sys.argv[1:] == ["--gui"] else main())
