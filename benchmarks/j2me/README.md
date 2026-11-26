# Java ME renderer baselines

The original benchmark JAR classes run in a headless Java 8 host with Nokia's
M3G implementation from J2ME-Loader. Their GLES commands, textures and client
arrays are captured and replayed through the two ARM renderer builds, including
their generated ARM pixel pipelines. The target is ARM1136 at **320×240**.

`baselines/results.csv` contains the measurements for both branches.
`baselines/captures/` contains the replay inputs and native capture images;
`baselines/images/` contains each branch's ARM-rendered images.

## Recorded comparison

| Renderer | Source | Build |
| --- | --- | --- |
| `current-worktree` | `SOMETHINKWORKS`, `7b4a04abff2b741398b375a997e9c46558312dbf`, with the accompanying local source edits | ARMv5T, tuned for ARM1136JF-S |
| `attempt-3` | `rmn20/attempt-3`, `e5df98d168ffec20fbe5ea53409b8574cbf00cca` | ARMv6J, tuned for ARM1136JF-S |

The current working-tree edits disable assertion-only analysis in release
builds and avoid touching unused scan-conversion edges. They are in
`grPixelPipeline.c`, `grRasterizer.c` and `hcCompiler.c`; the baseline includes
them. Both builds use Arm GNU GCC 15.3.1, `-O2`, soft-float, and the Symbian unity
build's inlining/loop flags. The `attempt-3` depth-run experiment is disabled,
matching `SYMBIAN_DEPTH_RUNS=0` in the submitted device results.

All **10 sampled RGB565/Z16 frames match pixel for pixel** between branches.
Cold, settling and stable warm outputs also match within each branch. The
current branch is the reference baseline; `attempt-3` is a performance
comparison with matching output on these scenes.

Stable warm **scene** instruction counts exclude the memory-target transfer
and readback categories described below. CustomBench's row sums six samples;
the other rows contain one settled scene each.

| Workload | Samples | Current | attempt-3 | Fewer instructions |
| --- | ---: | ---: | ---: | ---: |
| CustomBench, Start nearest with vertex colors | 6 | 178,814,814 | 170,990,070 | 4.38% |
| VorkutaME, default view | 1 | 16,989,852 | 15,332,688 | 9.75% |
| L2D Metro, 180° | 1 | 7,192,102 | 6,414,856 | 10.81% |
| L2D City, 180° | 1 | 5,256,168 | 4,875,210 | 7.25% |
| L2D CS Mansion, 180° | 1 | 11,450,962 | 10,546,230 | 7.90% |

These are dynamic ARM instruction slots, including condition-failed ARM
instructions. They are not clock cycles or FPS: Unicorn does not model ARM1136
caches, memory bandwidth or instruction latency. The CSV also retains complete
GLES totals, generated-code instructions, modeled OS calls and copied bytes.
OS bodies, Java/M3G frontend execution, 2D canvas overlays, audio and phone
presentation are outside the ARM counts.

## E72 optimization

`optimizations/e72/results.csv` compares `attempt-3` at
`e5df98d168ffec20fbe5ea53409b8574cbf00cca` with `optimize/e72` at
`5bca84ddc558a0cc9fdadd90aef68249a2663b1e` (renderer `e72-5bca84d`).
Both use [CC3D Symbian GCCE 14.2.0](https://github.com/klaxons1/gcc-symbian),
ARMv6J, ARM1136JF-S tuning, soft-float, and the Symbian unity build flags.
The optimized renderer enables `O3` for four raster/gradient/tessellation
functions and specializes three-component short vertices. Other functions
retain `O2`; the vertex transforms disable pre-allocation scheduling to reduce
register spills.

All ten RGB565/Z16 images in `optimizations/e72/images/` match attempt-3
pixel for pixel. Cold and stable warm rendering also agree, and no warm frame
flushes generated code from the cache.

| Workload | attempt-3 scene instructions | Optimized | Reduction |
| --- | ---: | ---: | ---: |
| CustomBench, six samples | 173,327,317 | 148,084,333 | 14.56% |
| VorkutaME | 15,524,833 | 14,266,507 | 8.11% |
| L2D Metro | 6,477,301 | 5,771,246 | 10.90% |
| L2D City | 4,968,799 | 4,528,152 | 8.87% |
| L2D CS Mansion | 10,643,412 | 9,440,408 | 11.30% |

Float vertices reuse exponent-alignment plans through a two-entry cache, with
a four-miss cutoff for varied inputs. Repeated triangle slopes use cached exact
reciprocals. These caches add 592 bytes per context. The ARMv6 JIT combines packed
color arithmetic while retaining each blend term's truncation, reuses pure
expressions and depth addresses, and folds independent shifts into consumers.

`../arm/optimizations/e72/general-gles.csv` covers 25 scenes in RGB565 and RGB888
in 32-bit storage at 320x240, using the same compiler and revisions. All 50 RGB
images match attempt-3 exactly, including cold and repeated warm rendering.
Warm instruction counts decrease in 47 cases. RGB888 fog and EXP2 fog increase
2.14%, and textured blending increases 1.12%, from an extra instruction per
shaded pixel caused by scheduling/register allocation. Scenes include lighting,
short and float vertices, changing exponents, points, clipped lines, palettes,
alpha rejection, stencil operations, multitexturing and mixed depth rejection.

`../arm/optimizations/e72/classicube.csv` records a ClassiCube landscape frame:
revision `c34b15782965e32a75a788ffcbe5c5a5a316e161`, seed 20261122, world 128x64x128,
view distance 64, camera (104.5, 40.2, 41.5), yaw 315, frame 35 at 320x240.
The frame submits 45,628 triangles through 439 indexed draws. Both output formats
match attempt-3 exactly, with stable warm counts and no warm JIT flushes.

| ClassiCube output | attempt-3 warm instructions | Optimized | Reduction |
| --- | ---: | ---: | ---: |
| RGB565 | 181,079,556 | 156,822,476 | 13.40% |
| RGB888 in 32-bit storage | 175,772,283 | 153,093,189 | 12.90% |

These counts cover the captured GLES frame, excluding game logic and presentation.
The optimized ELF's `.text` is 238,520 bytes versus attempt-3's 236,792 (+0.73%).

Attempt-3's checked-in Symbian script uses `-O2`, with
`-finline-functions -fgcse-after-reload -fpredictive-commoning -funswitch-loops
-fwhole-program`. Both default renderer builds use these flags, together with
`-march=armv6j -mtune=arm1136jf-s -marm -mfloat-abi=soft`. Attempt-3's optional
`SYMBIAN_DEPTH_RUNS` experiment is disabled. Neither build enables fast math.

The CSV also retains the compiler comparison at
`61a5e044f972a311bdc952e0d50fae6436f36413`: `e72` uses the default flags, while
`attempt-3-o3` and `e72-o3` are built with `--cflag=-O3`.
This changes both C translation units to `O3`; assembly is unchanged. All
four builds match the stored reference images, so the O3 images are not
duplicated. The runtime uses `-ffreestanding -fno-tree-loop-distribute-patterns`
in every benchmark build. Adding these guards to attempt-3's literal runtime
flags produces identical code and relocations with GCC 14.2.0.

| Workload | attempt-3 O3 | Optimized O3 | Reduction versus attempt-3 O3 | O3 gain over optimized default |
| --- | ---: | ---: | ---: | ---: |
| CustomBench, six samples | 168,503,203 | 149,332,055 | 11.38% | 0.77% |
| VorkutaME | 15,159,750 | 14,682,012 | 3.15% | 0.57% |
| L2D Metro | 6,334,638 | 5,942,622 | 6.19% | 0.52% |
| L2D City | 4,829,252 | 4,496,533 | 6.89% | 0.77% |
| L2D CS Mansion | 10,466,478 | 9,563,494 | 8.63% | 0.47% |

At that revision, full `O3` increases the optimized benchmark ELF's `.text` from 231,704 to
348,868 bytes (+50.57%); attempt-3 grows from 236,792 to 364,476 bytes
(+53.92%). The default retains scoped `O3`: full `O3` saves less than 0.8%
of scene instructions while substantially increasing code size. ARM1136
instruction-cache costs require device timing and are not modeled here.

The ARM JIT advances only depth through consecutive rejected fragments,
then reconstructs texture coordinates and the dither phase when shading
resumes. This path is selected for eligible nearest MODULATE pipelines with
16-bit color/depth. It preserves wrapped gradients, depth-write masks and
LESS/LEQUAL behavior. Other changes reduce texel unpack/repack work, gradient
register pressure, vertex processing, surface-state lookups and color conversion.

The compiler can run on x86-64 Linux directly or through a container command
prefix. Its packaged assembler requires glibc 2.38 or newer. Point
`SYMBIAN_GCC_DIR` at the unpacked `gcc` directory. For an OrbStack Docker host,
build the optimized renderer with:

```sh
python3 benchmarks/arm/build_gerbera.py --march armv6j --build benchmarks/arm/build/e72 --compiler "$SYMBIAN_GCC_DIR/bin/arm-none-symbianelf-gcc" --runner "docker run --rm --platform linux/amd64 --network none -v \"$PWD:$PWD\" -w \"$PWD\" debian:trixie-slim"
```

Use the same command with `--source benchmarks/arm/build/attempt-3-source`
and `--build benchmarks/arm/build/attempt-3` for the comparison renderer.
Omit `--runner` when the compiler runs natively. Replay the saved workloads:

```sh
python3 benchmarks/j2me/run_baselines.py --renderer attempt-3=benchmarks/arm/build/attempt-3/gerbera.elf --renderer e72=benchmarks/arm/build/e72/gerbera.elf --output benchmarks/j2me/build/e72-check --jobs 2
```

Run the broader GLES scenes with the same builds:

```sh
python3 benchmarks/arm/run.py --renderer attempt-3=benchmarks/arm/build/attempt-3/gerbera.elf --renderer e72=benchmarks/arm/build/e72/gerbera.elf --width 320 --height 240 --require-identical --jobs 2 --output benchmarks/arm/build/e72-check.csv
```

`--profile` adds function profiles; `replay_gles.py --profile-jit` also
disassembles the hottest generated blocks. Both use the native block hook.
`make arm-jit-check arm-division-check` exercises ARMv5/ARMv6 instruction
selection, register preservation, gradient reconstruction, depth-rejection
runs and exact division.

The Symbian build script defaults to ARMv6J for this branch. Set
`SYMBIAN_ARM_ARCH=armv5t` for older ARM targets; the ARMv5 JIT remains available.
The script fetches the pinned CC3D compiler package when needed;
`SYMBIAN_GCC_DIR` and `SYMBIAN_ELF2E32` select existing tools.

## Workload and measurement details

- CustomBench uses its original mode 1, **Start nearest**, with vertex colors
  retained. Animation timestamps are 0, 5000, 10000, 15000, 20000 and 25000 ms.
  The JAR changes filters only on direct world children; nested meshes retain
  their original filters. Sampling is not the JAR's full 30-second AFPS run.
- VorkutaME uses its deterministic town and default camera.
- L2D uses levels 1, 2 and 5, the spawn position, a 180° turn and 100% display
  size. Its 3D viewport is **320×192 at y=24**. Two original paint calls settle
  the scene before capture; there are no enemies in these levels.
- The host implements the MIDP subset needed for scene construction and
  rendering. Image loading and image-pixel operations run; canvas text/HUD
  drawing and the UI event loop are omitted. Networking is disabled.
- J2ME-Loader binds a memory target. Its backing-image uploads and two copy
  quads are tagged `target-transfer`; `glReadPixels` is `readback`. Both remain
  in the complete `total`. The scene subtotal is `total - target-transfer -
  readback`; it excludes this bridge's main transfer costs while retaining
  the scene calls and their state changes. A Symbian canvas may use a different
  target/presentation path.
- Prior-frame resources/state are replayed before measurement while draws
  are skipped. `cold` therefore measures the selected frame with an empty JIT
  cache. Warm repeats must produce two consecutive identical per-category
  counts and identical pixels, with a six-repeat limit. Intermediate runs are
  retained as `settlingN`. One final `glFinish` flushes deferred work if the
  trace does not already end with it. Error checks and verification reads are
  outside the counters.

The recorder preserves array strides, copies client data at each draw, remaps
texture names and captures floating-point argument bits. A CustomBench native
32-bit frame matched its ARM 32-bit replay at every pixel. The fast C block
counter matched the instruction counter for nearest, bilinear and blend scenes;
the measured speedup was about 2.5–3.5× on those checks.

The [submitted FPS and device instructions](../../bench_jars/README.md) remain
separate from these measurements. Their stock-DLL revision was not supplied,
so `current-worktree` is not identified as that stock baseline. The reported
CustomBench 10→12.5 FPS and L2D improvements are consistent in direction with
the instruction reductions, but these measurements do not establish the same
device speedup.

## Reproduction

Requirements: a native C/C++ compiler, Make, Java 8 for execution, a JDK for
compilation, `arm-none-eabi-gcc`, and Python with Unicorn, pyelftools, NumPy and
Pillow. The capture/build scripts support macOS and Linux on ARM64/x86-64.
Use `JAVA8_HOME` if Java 8 is not the default; `JAVA_HOME` selects the compiler
JDK. Runtime Java 8 preserves these CLDC JARs' `Object.class` resource lookup.

From the repository root, prepare the pinned M3G source and branch checkout:

```sh
git clone https://github.com/nikita36078/J2ME-Loader.git benchmarks/arm/vendor/j2me-loader
git -C benchmarks/arm/vendor/j2me-loader checkout --detach e270ed91e67b5cafc18c1991b92ba5249cf41c50
git worktree add --detach benchmarks/arm/build/attempt-3-source e5df98d168ffec20fbe5ea53409b8574cbf00cca
```

The copied M3G build sources receive an LP64 compatibility correction: GL
texture-name arrays use 32-bit `GLuint`, not pointer-sized elements. The
dependency checkout and supplied JARs are unchanged.

To capture and measure the currently checked-out source against attempt-3,
write a separate result directory:

```sh
python3 benchmarks/j2me/run_baselines.py --output benchmarks/j2me/build/branch-comparison --jobs 2
```

`--skip-build` reuses already-built binaries; use it only while renderer and
capture sources are unchanged. `--skip-capture` replays the saved inputs.
`--cases l2d-city` selects one workload; `--times 0,15000` selects CustomBench
samples. Logs and compiled artifacts stay under ignored `build/` directories.
The script saves counts and images and fails on rendering differences or
unstable measurements.

To replay a saved frame without running Java:

```sh
python3 benchmarks/arm/replay_gles.py benchmarks/j2me/baselines/captures/l2d-city/trace.jsonl --renderer current-worktree=benchmarks/arm/build/j2me-current/gerbera.elf --renderer attempt-3=benchmarks/arm/build/j2me-attempt-3/gerbera.elf --output benchmarks/j2me/build/city-check --cpu arm1136 --counter block --bpp 16 --warm-runs 2
```
