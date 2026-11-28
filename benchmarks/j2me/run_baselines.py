#!/usr/bin/env python3
"""Capture the benchmark JARs and compare their GLES frames on ARM1136."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ARM = ROOT / "benchmarks/arm"
CASES = {
    "custombench": ("custombench", 1),
    "vorkuta": ("vorkuta", 1),
    "l2d-metro": ("l2d", 1),
    "l2d-city": ("l2d", 2),
    "l2d-mansion": ("l2d", 5),
}


def run(command: list[str | Path], log: Path, timeout: int = 180) -> None:
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w") as output:
        result = subprocess.run(list(map(str, command)), stdout=output,
                                stderr=subprocess.STDOUT, timeout=timeout)
    if result.returncode:
        tail = "\n".join(log.read_text(errors="replace").splitlines()[-30:])
        raise RuntimeError(f"Command failed ({result.returncode}): {log}\n{tail}")


def parallel(jobs: int, tasks: list[tuple[str, list, Path]]) -> None:
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        running = {pool.submit(run, command, log): label for label, command, log in tasks}
        for future in as_completed(running):
            future.result()
            print(f"Completed {running[future]}", flush=True)


def revision(source: Path) -> str:
    return subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"],
                                   text=True).strip()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempt-source", type=Path,
                        default=ARM / "build/attempt-3-source")
    parser.add_argument("--output", type=Path, default=HERE / "baselines")
    parser.add_argument("--cases", nargs="+", choices=CASES, default=list(CASES))
    parser.add_argument("--bpp", type=int, choices=(16, 32), default=32)
    parser.add_argument("--include-16bpp", action="store_true",
                        help="Allow 16bpp RGB565 replay")
    parser.add_argument("--include-armv5", action="store_true",
                        help="Build current-worktree with armv5t instead of armv6j")
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--height", type=int, default=240)
    parser.add_argument("--times", default="0,5000,10000,15000,20000,25000",
                        help="CustomBench animation timestamps in milliseconds")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--skip-capture", action="store_true")
    parser.add_argument("--renderer", action="append", metavar="LABEL=ELF",
                        help="Compare prebuilt renderers without building or capturing")
    parser.add_argument("--captures", type=Path,
                        help="Read existing captures from this directory")
    parser.add_argument("--profile", action="store_true",
                        help="Save native function profiles for each measured frame")
    args = parser.parse_args()
    if args.bpp == 16 and not args.include_16bpp:
        parser.error("--bpp 16 requires --include-16bpp")
    if args.jobs < 1 or args.jobs > 8:
        parser.error("--jobs must be between 1 and 8")
    if not 1 <= args.width <= 1024 or not 1 <= args.height <= 1024:
        parser.error("dimensions must be between 1 and 1024")
    output = args.output.resolve()
    captures = args.captures.resolve() if args.captures else output / "captures"
    if args.renderer:
        if output == (HERE / "baselines").resolve():
            parser.error("prebuilt comparisons require a separate --output directory")
        renderers = {}
        for item in args.renderer:
            label, separator, path = item.partition("=")
            if (not separator or not re.fullmatch(r"[A-Za-z0-9_-]+", label)
                    or label in renderers or not Path(path).is_file()):
                parser.error("--renderer needs a distinct filename-safe LABEL and an existing ELF")
            renderers[label] = Path(path).resolve()
        args.skip_build = args.skip_capture = True
        if not args.captures:
            captures = HERE / "baselines/captures"
        revisions = {label: "" for label in renderers}
        architectures = {label: "" for label in renderers}
    else:
        attempt = args.attempt_source.resolve()
        if not (attempt / "src/gerbera").is_dir():
            parser.error("--attempt-source must point to a checkout of attempt-3")
        sources = {"current-worktree": ROOT, "attempt-3": attempt}
        revisions = {label: revision(source) for label, source in sources.items()}
        architectures = {
            "current-worktree": "armv5t" if args.include_armv5 else "armv6j",
            "attempt-3": "armv6j",
        }
        builds = {label: ARM / "build" / ("j2me-current" if label == "current-worktree"
                                          else "j2me-attempt-3") for label in sources}
        renderers = {label: build / "gerbera.elf" for label, build in builds.items()}
    output.mkdir(parents=True, exist_ok=True)
    scratch = HERE / "build" / output.name
    logs = scratch / "logs"
    if not args.skip_build:
        tasks = []
        for label, source in sources.items():
            build_cmd = [sys.executable, ARM / "build_gerbera.py",
                         "--source", source, "--build", builds[label],
                         "--march", architectures[label]]
            if architectures[label] in ("armv5t", "armv5te"):
                build_cmd.append("--include-armv5")
            tasks.append((label, build_cmd, logs / (label + ".log")))
        parallel(args.jobs, tasks)
        run([sys.executable, HERE / "build.py"], logs / "host-build.log")
    if not args.skip_capture and not args.captures:
        tasks = []
        for case in args.cases:
            workload, level = CASES[case]
            tasks.append(("capture " + case,
                          [sys.executable, HERE / "capture.py", "--workload", workload,
                           "--level", str(level), "--times",
                           args.times if case == "custombench" else "0",
                           "--width", str(args.width), "--height", str(args.height),
                           "--output", captures / case],
                          logs / ("capture-" + case + ".log")))
        parallel(args.jobs, tasks)
    tasks, frames = [], []
    for case in args.cases:
        trace = captures / case / "trace.jsonl"
        events = [json.loads(line) for line in trace.read_text().splitlines()]
        markers = [event for event in events if event["fn"] == "capture"]
        if not markers:
            raise ValueError(f"No measured frame markers in {trace}")
        for capture in markers:
            frame = capture["frame"]
            directory = scratch / "replays" / case / str(frame)
            command = [sys.executable, ARM / "replay_gles.py", trace,
                       "--frame", str(frame), "--width", str(args.width),
                       "--height", str(args.height), "--cpu", "arm1136",
                       "--counter", "block", "--warm-runs", "2", "--bpp", str(args.bpp),
                       "--output", directory]
            if args.bpp == 16:
                command.append("--include-16bpp")
            if args.profile:
                command.append("--profile")
            for label, elf in renderers.items():
                command += ["--renderer", label + "=" + str(elf)]
            tasks.append((f"replay {case} frame {frame}", command,
                          logs / f"replay-{case}-{frame}.log"))
            frames.append((case, capture, directory))
    parallel(args.jobs, tasks)
    rows = []
    for case, capture, directory in frames:
        with (directory / f"results_{args.bpp}.csv").open(newline="") as stream:
            for row in csv.DictReader(stream):
                label = row["renderer"]
                row.update(workload=case, time_ms=capture.get("time_ms", ""),
                           revision=revisions[label], march=architectures[label],
                           source_state=("prebuilt" if args.renderer else
                                         "working-tree" if label == "current-worktree" else "commit"))
                rows.append(row)
        images = output / "images" / case / str(capture["frame"])
        images.mkdir(parents=True, exist_ok=True)
        for label in renderers:
            shutil.copyfile(directory / f"{label}_{args.bpp}.png", images / (label + ".png"))
            if args.profile:
                shutil.copyfile(directory / f"{label}_{args.bpp}_profile.csv",
                                images / (label + "-profile.csv"))
    with (output / "results.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    for case in args.cases:
        counts = {}
        for label in renderers:
            counts[label] = sum(int(row["instructions"]) for row in rows
                                if row["workload"] == case and row["renderer"] == label
                                and row["phase"] == "warm" and row["category"] not in
                                ("total", "target-transfer", "readback"))
        reference = next(iter(counts.values()))
        summary = ", ".join(f"{label} {count:,} ({(count / reference - 1) * 100:+.2f}%)"
                            for label, count in counts.items())
        print(f"{case}: sampled warm scene instructions: {summary}")
    print(f"Saved {output / 'results.csv'}")
    if any(int(row["rgb_max_difference"]) for row in rows):
        raise SystemExit("Renderer images differ; inspect the recorded pixel differences and images")
    if len(renderers) > 1:
        print(f"All {len(renderers)} renderers match pixel for pixel on all {len(frames)} sampled frames")
    else:
        print(f"Stable cold/warm rendering on all {len(frames)} sampled frames")


if __name__ == "__main__":
    main()
