#!/usr/bin/env python3
"""Replay the captured ClassiCube GLES frame on both ARM software drivers."""

from __future__ import annotations

import argparse
from collections import defaultdict
import csv
import json
from pathlib import Path
import struct

import numpy as np
from PIL import Image

from run import COUNT_FIELDS, CPU_MODELS, Machine, counter_library, words


HERE = Path(__file__).resolve().parent


class Replay:
    def __init__(self, machine: Machine, data: bytes, events: list[dict] = ()):
        self.m = machine
        self.data = data
        self.payload = machine.put(data)
        self.payload_size = len(data)
        self.names = {"buffer": {0: 0}, "texture": {0: 0}}
        self.generated = machine.alloc(4096)
        self.output = machine.alloc(max((event["output"][1] for event in events
                                         if "output" in event), default=0))
        array_sizes = {}
        for event in events:
            if "array_id" in event:
                array_sizes.setdefault(event["array_id"], 0)
            for array_id, offset, size in event.get("arrays", []):
                if offset < 0 or size < 0 or offset + size > len(data):
                    raise ValueError(f"Array payload outside capture: {array_id}")
                array_sizes[array_id] = max(array_sizes.get(array_id, 0), size)
        self.arrays = {key: machine.alloc(size) for key, size in array_sizes.items()}

    def execute(self, event: dict) -> None:
        fn, args = event["fn"], list(event["args"])
        if fn in ("capture", "frame_end"):
            return
        mapping = self.names["buffer" if "Buffer" in fn else "texture"]
        if "array_id" in event:
            args[-1] = self.arrays[event["array_id"]]
        for array_id, offset, size in event.get("arrays", []):
            self.m.uc.mem_write(self.arrays[array_id], self.data[offset:offset + size])
        if fn in ("glBindTexture", "glBindBuffer"):
            if args[1] not in mapping:
                # Binding an unused name creates the object. Preserve that
                # single API call even if generated guest IDs differ.
                guest = args[1]
                used = set(mapping.values())
                if guest in used:
                    guest = 1
                    while guest in used:
                        guest += 1
                mapping[args[1]] = guest
            args[1] = mapping[args[1]]
        blobs = event.get("blobs", [])
        if "blob" in event:
            blobs = [event["blob"], *blobs]
        for index, offset, size in blobs:
            if offset < 0 or size < 0 or offset + size > self.payload_size:
                raise ValueError(f"Payload outside capture: {fn}, offset={offset}, size={size}")
            args[index] = self.payload + offset
            if fn in ("glDeleteTextures", "glDeleteBuffers"):
                ids = struct.unpack("<" + "I" * args[0], self.m.uc.mem_read(args[index], size))
                args[index] = self.m.put(words([mapping.get(x, 0) for x in ids]))
        if "output" in event:
            args[event["output"][0]] = self.output
        if "generated" in event:
            if args[0] > 1024:
                raise RuntimeError("Generation scratch buffer too small")
            args[1] = self.generated
        self.m.call(fn, *args)
        if fn in ("glDeleteTextures", "glDeleteBuffers"):
            for name in ids:
                if name:
                    mapping.pop(name, None)
        if "generated" in event:
            actual = struct.unpack("<" + "I" * args[0], self.m.uc.mem_read(args[1], args[0] * 4))
            mapping.update(zip(event["generated"], actual))


def category(name: str, stage: str | None = None) -> str:
    if name == "glReadPixels":
        return "readback"
    if stage == "target-transfer":
        return "target-transfer"
    if name.startswith("glDraw"):
        return "draw"
    if name == "glClear":
        return "clear"
    if name in ("glBufferSubData", "glBufferData", "glTexImage2D", "glTexSubImage2D"):
        return "upload"
    return "state"


def render(renderer: str, bpp: int, events: list[dict], payload: bytes, capture_frame: int,
           directory: Path, rows: list[dict], width: int, height: int, *,
           elf: Path | None = None, cpu: str = "arm1136", counter: str = "instruction",
           warm_runs: int = 1, max_warm_runs: int = 6, finish: bool = False,
           profile: bool = False, profile_jit: bool = False, window: bool = False,
           end_frame: int | None = None, frames: dict[int, bytes] | None = None) -> bytes:
    profile |= profile_jit
    if warm_runs < 1 or max_warm_runs < warm_runs:
        raise ValueError("Warm run bounds must satisfy 1 <= warm_runs <= max_warm_runs")
    end_frame = capture_frame if end_frame is None else end_frame
    if end_frame < capture_frame:
        raise ValueError("End frame precedes start frame")
    for frame in (capture_frame, end_frame):
        if not any(e["frame"] == frame for e in events):
            raise ValueError(f"Frame {frame} is absent from the capture")
    sequence = end_frame != capture_frame
    first_row = len(rows)
    m = Machine(elf or HERE / "build" / renderer / f"{renderer}.elf",
                counter_library(), width, height, bpp, cpu=cpu, counter=counter,
                profile=profile, profile_jit=profile_jit)
    m.verify_counter()
    m.reset_counts()
    m.initialize(bpp, window)
    m.call("glEnable", 0x0BD0)  # Capture used GLES's default enabled dithering.
    rows.append(dict(renderer=renderer, bpp=bpp, phase="context_setup", category="total",
                     iteration=0, settling_runs=0, **m.counts(), calls=m.api_calls))
    replay = Replay(m, payload, events)
    setup_totals = defaultdict(lambda: defaultdict(int))

    def record(event, totals):
        kind = category(event["fn"], event.get("stage"))
        m.reset_counts()
        replay.execute(event)
        for key, value in m.counts().items():
            totals[kind][key] += value
            totals["total"][key] += value
        totals[kind]["calls"] += 1
        totals["total"]["calls"] += 1

    print(f"{renderer}: replaying resource/state setup", flush=True)
    for index, event in enumerate(events):
        if event["frame"] >= capture_frame:
            break
        if event["fn"] in ("capture", "frame_end", "glClear") or event["fn"].startswith("glDraw"):
            continue
        record(event, setup_totals)
        error = m.call("glGetError")
        if error:
            raise RuntimeError(f"{renderer}: setup event {index} {event}: GL error {error:#x}")
    for kind, counts in setup_totals.items():
        rows.append(dict(renderer=renderer, bpp=bpp, phase="resource_setup", category=kind,
                         iteration=0, settling_runs=0, **counts))
    print(f"{renderer}: resource setup {setup_totals['total']['instructions']:,} instructions "
          f"(uploads {setup_totals['upload']['instructions']:,})", flush=True)
    target = [e for e in events if capture_frame <= e["frame"] <= end_frame and
              e["fn"] != "frame_end" and (sequence or e["fn"] != "capture")]
    if not target:
        raise ValueError(f"No GLES calls in frame {capture_frame}")
    if finish and target[-1]["fn"] not in ("glFinish", "capture"):
        target.append(dict(fn="glFinish", args=[], frame=capture_frame))
    previous_pixels = None
    previous_frames = None
    previous_totals = None
    samples = []
    stable_runs = 0
    for iteration in range(max_warm_runs + 1):
        m.reset_profile()
        phase = "cold" if iteration == 0 else f"warm{iteration}"
        totals: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))
        frame_images = []
        frame_label = f"{capture_frame}-{end_frame}" if sequence else str(capture_frame)
        print(f"{renderer}: {phase} frame {frame_label}, "
              f"{sum(event['fn'] != 'capture' for event in target)} GLES calls", flush=True)
        for index, event in enumerate(target):
            if event["fn"] == "capture":
                # Charge deferred rendering to its frame before diagnostic readback.
                if finish:
                    record(dict(fn="glFinish", args=[], frame=event["frame"]), totals)
                frame_images.append((event["frame"], m.pixels()))
                continue
            kind = category(event["fn"], event.get("stage"))
            m.profile_enable(kind not in ("target-transfer", "readback"))
            record(event, totals)
            m.profile_enable(False)
            error = m.call("glGetError")
            if error:
                raise RuntimeError(f"{renderer}: frame event {index} {event}: GL error {error:#x}")
        samples.append(totals)
        pixels = m.pixels()
        transfer = totals.get("target-transfer", {}).get("instructions", 0)
        readback = totals.get("readback", {}).get("instructions", 0)
        scene = totals["total"]["instructions"] - transfer - readback
        setup = sum(counts["pipeline_setup_instructions"] for kind, counts in totals.items()
                    if kind not in ("total", "target-transfer", "readback"))
        pixel = sum(counts["jit_instructions"] for kind, counts in totals.items()
                    if kind not in ("total", "target-transfer", "readback"))
        setup_text = f"{setup:,}" if m.counter.setup_entry_count else "unavailable"
        print(f"{renderer} {phase}: scene {scene:,}; pipeline setup {setup_text}, "
              f"generated pixels {pixel:,}, remaining renderer work {scene - setup - pixel:,}; "
              f"transfer {transfer:,}, readback {readback:,}, "
              f"JIT cache insertions {totals['total']['jit_flushes']}", flush=True)
        if previous_pixels is not None and pixels != previous_pixels:
            diff = np.abs(np.frombuffer(pixels, np.uint8).astype(int) -
                          np.frombuffer(previous_pixels, np.uint8).astype(int))
            raise RuntimeError(f"{renderer}: cold/warm image mismatch: mean channel difference {diff.mean()}")
        previous_pixels = pixels
        if previous_frames is not None and frame_images != previous_frames:
            raise RuntimeError(f"{renderer}: an intermediate sequence frame changed between repeats")
        previous_frames = frame_images
        if iteration:
            # Recreated bridge targets can change allocator costs between
            # sequence repeats. Scene convergence is independent of that work.
            stable_totals = ({kind: counts for kind, counts in totals.items()
                              if kind not in ("total", "target-transfer", "readback")}
                             if sequence else totals)
            stable_runs = stable_runs + 1 if stable_totals == previous_totals else 1
            previous_totals = stable_totals
            if stable_runs >= warm_runs:
                break
    else:
        raise RuntimeError(f"{renderer}: frame counts did not stabilize within {max_warm_runs} warm runs")
    settling_runs = len(samples) - 1 - warm_runs
    for iteration, totals in enumerate(samples):
        if iteration == 0:
            phase = "cold"
        elif iteration <= settling_runs:
            phase = f"settling{iteration}"
        else:
            stable_index = iteration - settling_runs
            phase = "warm" if stable_index == 1 else f"warm{stable_index}"
        for kind, counts in totals.items():
            rows.append(dict(renderer=renderer, bpp=bpp, phase=phase, category=kind,
                             iteration=iteration, settling_runs=settling_runs, **counts))
    if settling_runs:
        print(f"{renderer}: {settling_runs} settling frames before {warm_runs} identical warm runs", flush=True)
    if profile:
        profile_rows = m.profile_rows()
        scene_count = sum(value["instructions"] for kind, value in totals.items()
                          if kind not in ("total", "target-transfer", "readback"))
        if sum(row["instructions"] for row in profile_rows) != scene_count:
            raise RuntimeError("Function profile does not account for every scene instruction")
        with (directory / f"{renderer}_{bpp}_profile.csv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=("function", "instructions"), lineterminator="\n")
            writer.writeheader()
            writer.writerows(profile_rows)
        print(f"{renderer}: largest warm scene functions", flush=True)
        for row in profile_rows[:12]:
            print(f"  {row['instructions']:>12,} {row['instructions'] / scene_count:6.1%} "
                  f"{row['function']}", flush=True)
        if profile_jit:
            if totals["total"]["jit_flushes"]:
                raise RuntimeError("JIT block profiling requires a stable generated-code cache")
            (directory / f"{renderer}_{bpp}_jit.txt").write_text(m.jit_profile_text())
    image = Image.frombytes("RGBA", (width, height), pixels).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    image.save(directory / f"{renderer}_{bpp}.png")
    if frames is not None:
        frames.update(frame_images)
        frames[end_frame] = pixels
    for row in rows[first_row:]:
        row["surface"] = "window" if window else "pbuffer"
        if not m.counter.setup_entry_count:
            row["pipeline_setup_instructions"] = None
    return pixels


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path, default=HERE / "classicube")
    parser.add_argument("--bpp", type=int, choices=(16, 32), default=32)
    parser.add_argument("--include-16bpp", action="store_true",
                        help="Allow 16bpp RGB565 replay")
    parser.add_argument("--cpu", choices=CPU_MODELS, default="arm1136")
    parser.add_argument("--include-arm926", action="store_true",
                        help="Allow arm926 CPU replay")
    parser.add_argument("--renderer", choices=("gerbera", "pixelflinger", "both"), default="both")
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--height", type=int, default=240)
    parser.add_argument("--window", action="store_true", help="Use native window-surface locking during replay")
    args = parser.parse_args()
    if args.bpp == 16 and not args.include_16bpp:
        parser.error("--bpp 16 requires --include-16bpp")
    if args.cpu == "arm926" and not args.include_arm926:
        parser.error("--cpu arm926 requires --include-arm926")
    events = [json.loads(line) for line in (args.trace / "landscape.jsonl").read_text().splitlines()]
    payload = (args.trace / "landscape.bin").read_bytes()
    capture_frame = next(e["frame"] for e in events if e["fn"] == "capture")
    renderers = ("gerbera", "pixelflinger") if args.renderer == "both" else (args.renderer,)
    rows, images = [], {}
    for renderer in renderers:
        images[renderer] = render(renderer, args.bpp, events, payload, capture_frame,
                                  args.trace, rows, args.width, args.height, cpu=args.cpu,
                                  window=args.window)
        output = args.trace / f"results_{args.bpp}_{args.renderer}.csv"
        fields = ["renderer", "bpp", "surface", "phase", "category", "iteration", "settling_runs",
                  *COUNT_FIELDS, "calls"]
        with output.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            writer.writerows(rows)
    if len(images) == 2:
        a = np.frombuffer(images["gerbera"], np.uint8).reshape(-1, 4)[:, :3].astype(int)
        b = np.frombuffer(images["pixelflinger"], np.uint8).reshape(-1, 4)[:, :3].astype(int)
        diff = np.abs(a - b)
        print(f"Cross-renderer RGB mean absolute difference {diff.mean():.6f}/255; "
              f"identical RGB pixels {np.mean(np.all(a == b, axis=1)):.3%}", flush=True)


if __name__ == "__main__":
    main()
