# Java ME benchmark workloads

| JAR | MIDlet entry point | MIDlet version |
| --- | --- | --- |
| `VorkutaME.jar` | `code.Midlet` | 1.0.0 |
| `CustomBench.jar` | `custombench.Midlet` | 1.0 |
| `l2d_m3g 20260821.jar` | `home.Main` | 1.0 |

All three declare CLDC 1.1 and MIDP 2.0. Original filenames are preserved.
`CustomBench.jar` contains the scene `JBenchmarkHD/map.m3g`.
They require JSR-184 (M3G); L2D also uses MIDP record stores and the media API.
The submitted device results used a **320×240** display.

M3G source reference: [toaarnio/m3gcore](https://github.com/toaarnio/m3gcore),
the Nokia M3G 1.1 (JSR-184) core engine in C.

## CustomBench

Choose **Start nearest**, the second menu entry. This is mode 1 in the JAR:
nearest filtering with the scene's vertex colors retained. **Start nearest no
lit**, the third entry, removes vertex colors and changes additional rendering
state, so it is a different workload. No separate vertex-color toggle is needed
for the second entry.
The filter change applies to meshes directly under the world; meshes nested
inside groups retain their scene-defined filters.

Let the animated scene finish its 30-second run. The result screen shows
`Score` (rendered frames) and `AFPS` (`Score / 30`). Keep the same display size
for both DLLs: the scene uses the full canvas and adjusts the camera aspect
ratio to it. For a renderer-only replay, sample the same animation timestamps
on both builds; a single frozen frame does not reproduce the complete score.

The submitted result for this case was approximately **10 FPS with the stock
DLL** and **12.5 FPS with the tested DLL** at 320×240. The report did not identify
the device or exact DLL revisions:

> Второй бенчмарк, второй режим, то есть nearest, но с включенными(!) вертексными цветами
> Сток длл выдавала примерно 10 фпс, сейчас примерно 12.5

## VorkutaME

Launch the MIDlet and leave the default view unchanged. It constructs a
deterministic town scene with camera position `(1, 1, 1)` and rotation
`(-20, 45, 0)` degrees; the key handlers do not move the camera. The overlay's
`Frame time` is the interval between paint starts, clamped to 1–200 ms. `Build`
is the scene-construction time. Record these as milliseconds, not FPS.

## Author-reported l2d_m3g results

Venus Sunev reported the following FPS for
[`e5df98d16`](https://github.com/rmn20/GegberaGLES1RE/commit/e5df98d168ffec20fbe5ea53409b8574cbf00cca)
with `SYMBIAN_DEPTH_RUNS=0`, compared with the author's “stock DLL” baseline
(no stock binary revision was supplied).

| Level | Scene | Stock DLL, FPS | e5df98d16, FPS |
| --- | --- | ---: | ---: |
| 1 | Metro | 28 | 32 |
| 2 | City | 34 | 36 |
| 5 | CS Mansion | 20 | 23 |

For the measurements, select levels 1, 2 and 5 and turn the camera 180°.
Keep the spawn position, display size and in-game display-size setting the same
between builds. At 320×240 with the default 100% display-size setting, the 3D
viewport is 320×192, centered vertically at y=24. These three level files disable enemies.
Wait for the player to settle on the floor before measuring. The displayed FPS
counts frames between checks more than one second apart, without correcting
for the exact interval; collect several readings after loading and turning.
The author's scene instructions were:

> Тут первый уровень (метро), второй уровень (город), пятый уровень (кс меншен), и повернуть камеру на 180 градусов
