#!/usr/bin/env python3
"""Replay a captured GLES frame on named ARM renderer builds."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
import re
import time

import numpy as np

from replay_classicube import render
from run import COUNT_FIELDS, CPU_MODELS


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, help="JSONL trace, with payload in a same-name .bin file")
    parser.add_argument("--renderer", action="append", required=True, metavar="LABEL=ELF")
    parser.add_argument("--output", type=Path, required=True, help="Results directory")
    parser.add_argument("--frame", type=int, help="Defaults to the first measured frame marker")
    parser.add_argument("--end-frame", type=int,
                        help="Replay through this frame in one context, retaining resources and the JIT cache")
    parser.add_argument("--width", type=int, help="Defaults to capture metadata")
    parser.add_argument("--height", type=int, help="Defaults to capture metadata")
    parser.add_argument("--bpp", type=int, choices=(16, 32), default=32)
    parser.add_argument("--include-16bpp", action="store_true",
                        help="Allow 16bpp RGB565 replay")
    parser.add_argument("--window", action="store_true", help="Use native window-surface locking during replay")
    parser.add_argument("--cpu", choices=CPU_MODELS, default="arm1136")
    parser.add_argument("--include-arm926", action="store_true",
                        help="Allow arm926 CPU replay")
    parser.add_argument("--counter", choices=("block", "instruction"), default="block")
    parser.add_argument("--profile", action="store_true",
                        help="Save a warm scene function profile using the native block counter")
    parser.add_argument("--profile-jit", action="store_true",
                        help="Include the hottest generated-code blocks and their disassembly")
    parser.add_argument("--warm-runs", type=int, default=2)
    parser.add_argument("--max-warm-runs", type=int, default=6)
    args = parser.parse_args()
    if args.bpp == 16 and not args.include_16bpp:
        parser.error("--bpp 16 requires --include-16bpp")
    if args.cpu == "arm926" and not args.include_arm926:
        parser.error("--cpu arm926 requires --include-arm926")
    args.profile |= args.profile_jit
    if args.warm_runs < 1 or args.max_warm_runs < args.warm_runs:
        parser.error("warm run bounds must satisfy 1 <= warm-runs <= max-warm-runs")
    if args.profile and args.counter != "block":
        parser.error("--profile requires --counter block")
    events = [json.loads(line) for line in args.trace.read_text().splitlines() if line.strip()]
    markers = [event for event in events if event["fn"] == "capture"]
    if args.frame is not None or not markers:
        markers = [event for event in events if event["fn"] in ("capture", "frame_end")]
    capture = next((event for event in markers
                    if (event["frame"] == args.frame if args.frame is not None
                        else not event.get("warmup", False))), {})
    frame = args.frame if args.frame is not None else capture.get("frame")
    width, height = args.width or capture.get("width"), args.height or capture.get("height")
    if frame is None or not width or not height or min(width, height) < 1:
        parser.error("frame, width and height must be supplied by options or capture metadata")
    if args.end_frame is not None and args.end_frame < frame:
        parser.error("--end-frame must be at least --frame")
    if args.end_frame is not None and not any(e["frame"] == args.end_frame for e in events):
        parser.error("--end-frame is absent from the capture")
    payload = args.trace.with_suffix(".bin").read_bytes()
    renderers = {}
    for item in args.renderer:
        label, separator, elf = item.partition("=")
        if not separator or not re.fullmatch(r"[A-Za-z0-9_-]+", label) or label in renderers:
            parser.error("each renderer needs a distinct LABEL=ELF, with a filename-safe label")
        renderers[label] = Path(elf)
    args.output.mkdir(parents=True, exist_ok=True)
    rows, reference, reference_label = [], None, None
    for label, elf in renderers.items():
        first_row = len(rows)
        started = time.monotonic()
        frames = {}
        render(label, args.bpp, events, payload, frame, args.output, rows, width, height,
                        elf=elf, cpu=args.cpu, counter=args.counter,
                        warm_runs=args.warm_runs, max_warm_runs=args.max_warm_runs, finish=True,
                        profile=args.profile, profile_jit=args.profile_jit, window=args.window,
                        end_frame=args.end_frame, frames=frames)
        elapsed = time.monotonic() - started
        rgb = np.concatenate([np.frombuffer(pixels, np.uint8).reshape(-1, 4)[:, :3]
                              for _, pixels in sorted(frames.items())]).astype(np.int16)
        if reference is None:
            reference, reference_label = rgb, label
        difference = np.abs(reference - rgb)
        for row in rows[first_row:]:
            row.update(workload=capture.get("workload", args.trace.stem),
                       frame=frame if args.end_frame in (None, frame) else f"{frame}-{args.end_frame}",
                       width=width, height=height,
                       cpu=args.cpu, counter=args.counter, reference=reference_label,
                       compared_frames=len(frames),
                       rgb_mean_difference=float(difference.mean()),
                       rgb_max_difference=int(difference.max()),
                       identical_rgb_pixels=int(np.count_nonzero(np.all(reference == rgb, axis=1))))
        output = args.output / f"results_{args.bpp}.csv"
        fields = ["workload", "renderer", "frame", "width", "height", "bpp", "surface", "cpu", "counter",
                  "phase", "category", "iteration", "settling_runs", *COUNT_FIELDS, "calls", "reference",
                  "compared_frames", "rgb_mean_difference", "rgb_max_difference", "identical_rgb_pixels"]
        with output.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            writer.writerows(rows)
        print(f"{label}: replay wall time {elapsed:.2f}s; RGB difference from {reference_label} "
              f"mean={difference.mean():.6f}, max={difference.max()}", flush=True)
    print(f"Saved {output}")


if __name__ == "__main__":
    main()
