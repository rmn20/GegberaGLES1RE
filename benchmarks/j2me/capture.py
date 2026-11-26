#!/usr/bin/env python3
"""Capture bounded GLES frames from the original benchmark JAR classes."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import build

JARS = {"custombench": "CustomBench.jar", "vorkuta": "VorkutaME.jar", "l2d": "l2d_m3g 20260821.jar"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workload", choices=JARS, required=True)
    parser.add_argument("--level", type=int, default=1)
    parser.add_argument("--times", default="0")
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--height", type=int, default=240)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=90)
    parser.add_argument("--java", type=Path, help="Java 8 executable (CLDC resource lookup semantics)")
    args = parser.parse_args()
    if not 1 <= args.warmup <= 20:
        parser.error("--warmup must be between 1 and 20")
    if not 1 <= args.width <= 1024 or not 1 <= args.height <= 1024:
        parser.error("Viewport dimensions must be between 1 and 1024")
    try:
        times = [int(value) for value in args.times.split(",")]
    except ValueError:
        parser.error("--times must contain comma-separated integer milliseconds")
    if not 1 <= len(times) <= 30 or any(t < 0 or t >= 30000 for t in times):
        parser.error("Use 1..30 timestamps between 0 and 29999 ms")
    if args.workload == "l2d" and args.level not in (1, 2, 5):
        parser.error("Supported l2d scenes are levels 1, 2 and 5")
    ext = ".dylib" if platform.system() == "Darwin" else ".so"
    if not (build.BUILD / ("libjavam3g" + ext)).exists():
        subprocess.run([os.sys.executable, str(build.HERE / "build.py")], check=True)
    args.output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, BENCH_TRACE_EVENTS=str((args.output / "trace.jsonl").resolve()),
               BENCH_TRACE_PAYLOAD=str((args.output / "trace.bin").resolve()))
    jar = build.ROOT / "bench_jars" / JARS[args.workload]
    java = args.java
    if java is None and os.environ.get("JAVA8_HOME"):
        java = Path(os.environ["JAVA8_HOME"]) / "bin/java"
    if java is None and platform.system() == "Darwin":
        java = Path(subprocess.check_output(["/usr/libexec/java_home", "-v", "1.8", "-a", platform.machine()], text=True).strip()) / "bin/java"
    if java is None:
        java = Path(shutil.which("java8") or shutil.which("java") or "java")
    version = subprocess.check_output([str(java), "-version"], stderr=subprocess.STDOUT, text=True)
    if 'version "1.8.' not in version:
        parser.error("Java 8 is required for CLDC Object.class resource lookup; use --java or JAVA8_HOME")
    command = [str(java), "-Djava.awt.headless=true",
               "-Dbench.library=" + str(build.BUILD / ("libjavam3g" + ext)),
               "-Dbench.width=" + str(args.width), "-Dbench.height=" + str(args.height),
               "-cp", str(build.BUILD / "classes") + os.pathsep + str(jar),
               "BenchMain", args.workload, str(args.level), ",".join(map(str, times)),
               str(args.warmup), str(args.output.resolve())]
    subprocess.run(command, env=env, check=True, timeout=args.timeout)
    trace = args.output / "trace.jsonl"
    events = [json.loads(line) for line in trace.read_text().splitlines()]
    annotated = []
    for event in events:
        if event["fn"] == "frame_end":
            index = event["frame"] - args.warmup
            event.update(width=args.width, height=args.height, workload=args.workload,
                         level=args.level, warmup=index < 0,
                         time_ms=times[index] if index >= 0 else 0)
            if index >= 0:
                annotated.append(dict(event, fn="capture"))
        annotated.append(event)
    trace.write_text("".join(json.dumps(event, separators=(",", ":")) + "\n" for event in annotated))


if __name__ == "__main__":
    main()
