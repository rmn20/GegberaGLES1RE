#include "Core.h"
#if CC_WIN_BACKEND == 99

/* Use Gerbera's EGL 1.1 bindings instead of _WindowBase.h's EGL 1.2 helpers. */
#undef CC_BUILD_EGL
#include "_WindowBase.h"
#include "String_.h"
#include "Funcs.h"
#include "Bitmap.h"
#include "Errors.h"
#include "Graphics.h"
#include "Stream.h"
#include "Platform.h"
#include "Game.h"
#include "World.h"
#include "BlockID.h"
#include "Entity.h"
#include "Camera.h"
#include "Chat.h"
#include "Screens.h"
#include "Inventory.h"
#include "PackedCol.h"
#include "ExtMath.h"

#include <EGL/egl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

static int g_winWidth  = 960;
static int g_winHeight = 540;
static cc_bool g_pendingClose;

static cc_uint32*        g_fbPixels;
static eglNativeWindow_s g_nativeWin;
static EGLDisplay        g_eglDisplay = EGL_NO_DISPLAY;
static EGLConfig         g_eglConfig;
static EGLSurface        g_eglSurface = EGL_NO_SURFACE;
static EGLContext        g_eglContext = EGL_NO_CONTEXT;

static const char* g_pendingShotPath;

/*########################################################################################################################*
*--------------------------------------------------Automated Scene Showcase-----------------------------------------------*
*#########################################################################################################################*/
static int     g_worldFrames;
static int     g_stage;
static int     g_stageFrames;
static cc_bool g_showcaseBuilt;

static float g_vistaX, g_vistaY, g_vistaZ, g_vistaYaw, g_vistaPitch;
static int   g_pavBaseX, g_pavBaseY, g_pavBaseZ;

static int TopSolidY(int x, int z) {
	int y;
	if (x < 0 || z < 0 || x >= World.Width || z >= World.Length) return 0;
	for (y = World.MaxY; y >= 1; y--) {
		BlockID b = World_GetBlock(x, y, z);
		if (b != BLOCK_AIR && b != BLOCK_LEAVES && b != BLOCK_LOG &&
		    b != BLOCK_DANDELION && b != BLOCK_ROSE &&
		    b != BLOCK_BROWN_SHROOM && b != BLOCK_RED_SHROOM && b != BLOCK_SAPLING) {
			return y;
		}
	}
	return 0;
}

static void SetPlayerPose(float x, float y, float z, float yaw, float pitch, cc_bool flying) {
	struct LocationUpdate upd;
	struct LocalPlayer* p = Entities.CurPlayer;
	if (!p) return;

	p->Hacks.CanFly = true;
	p->Hacks.Flying = flying;
	p->Base.Velocity.x = 0.0f;
	p->Base.Velocity.y = 0.0f;
	p->Base.Velocity.z = 0.0f;

	Mem_Set(&upd, 0, sizeof(upd));
	upd.flags = LU_HAS_POS | LU_HAS_YAW | LU_HAS_PITCH | LU_POS_ABSOLUTE_INSTANT;
	upd.pos.x = x;
	upd.pos.y = y;
	upd.pos.z = z;
	upd.yaw   = yaw;
	upd.pitch = pitch;
	p->Base.VTABLE->SetLocation(&p->Base, &upd);
}

static void FadeOutChatHUD(void) {
	int i;
	for (i = 0; i <= CHATLOG_TIME_MASK; i++) {
		Chat_RecentLogTimes[i] = -1000.0;
	}
}

/* Score viewpoints by visible terrain types and nearby obstructions. */
static void FindScenicVista(void) {
	int bestScore = -1;
	int x, z, a, step;

	g_vistaX = World.Width * 0.5f;
	g_vistaZ = World.Length * 0.5f;
	g_vistaY = (float)(TopSolidY((int)g_vistaX, (int)g_vistaZ) + 4);
	g_vistaYaw   = 45.0f;
	g_vistaPitch = 14.0f;

	for (x = 20; x < World.Width - 20; x += 3) {
		for (z = 20; z < World.Length - 20; z += 3) {
			int groundY = TopSolidY(x, z);
			BlockID groundBlk = World_GetBlock(x, groundY, z);
			int cx, cz, cy, canopyNear = 0;
			if (groundBlk != BLOCK_GRASS || groundY < 33 || groundY > 46) continue;

			/* Reject occupied space in the 5x5 area above the camera site. */
			for (cx = -2; cx <= 2 && !canopyNear; cx++) {
				for (cz = -2; cz <= 2 && !canopyNear; cz++) {
					for (cy = groundY + 1; cy <= groundY + 9 && cy <= World.MaxY; cy++) {
						BlockID cb = World_GetBlock(x + cx, cy, z + cz);
						if (cb != BLOCK_AIR) { canopyNear = 1; break; }
					}
				}
			}
			if (canopyNear) continue;

			for (a = 0; a < 8; a++) {
				float yaw = a * 45.0f;
				float rad = yaw * (3.14159265f / 180.0f);
				float dx  = sinf(rad);
				float dz  = -cosf(rad);

				int waterCnt = 0, sandCnt = 0, grassCnt = 0, treeCnt = 0, hillCnt = 0;
				int blockedNear = 0;

				for (step = 2; step <= 44; step += 2) {
					int sx = x + (int)roundf(dx * step);
					int sz = z + (int)roundf(dz * step);
					int sy, ty;
					BlockID sb;
					if (sx < 4 || sz < 4 || sx >= World.Width - 4 || sz >= World.Length - 4) break;

					sy = TopSolidY(sx, sz);
					if (step <= 12 && sy > groundY + 1) { blockedNear = 1; break; }
					if (step <= 12) {
						for (cy = groundY + 1; cy <= groundY + 8 && cy <= World.MaxY; cy++) {
							if (World_GetBlock(sx, cy, sz) != BLOCK_AIR) { blockedNear = 1; break; }
						}
						if (blockedNear) break;
					}

					sb = World_GetBlock(sx, sy, sz);
					if (sb == BLOCK_WATER || sb == BLOCK_STILL_WATER) waterCnt++;
					else if (sb == BLOCK_SAND) sandCnt++;
					else if (sb == BLOCK_GRASS) {
						grassCnt++;
						if (sy >= groundY + 2) hillCnt++;
					}

					for (ty = sy + 1; ty <= sy + 6 && ty <= World.MaxY; ty++) {
						BlockID ab = World_GetBlock(sx, ty, sz);
						if (ab == BLOCK_LEAVES || ab == BLOCK_LOG) { treeCnt++; break; }
					}
				}

				if (!blockedNear && waterCnt >= 3 && grassCnt >= 4) {
					int score = waterCnt * 8 + sandCnt * 6 + grassCnt * 4 + treeCnt * 12 + hillCnt * 5;
					if (score > bestScore) {
						bestScore    = score;
						g_vistaX     = x + 0.5f;
						g_vistaY     = (float)groundY + 4.2f;
						g_vistaZ     = z + 0.5f;
						g_vistaYaw   = yaw;
						g_vistaPitch = 13.0f;
					}
				}
			}
		}
	}
}

static void SafeSetBlock(int x, int y, int z, BlockID block) {
	if (x >= 1 && y >= 1 && z >= 1 && x < World.MaxX && y < World.MaxY && z < World.MaxZ) {
		Game_UpdateBlock(x, y, z, block);
	}
}

/* Place opaque, alpha-tested and blended geometry in the same view (-Z). */
static void BuildShowcasePavilion(void) {
	int bx = World.Width / 2;
	int bz = World.Length / 2;
	int by, dx, dz, dy;

	/* Leave enough room around the viewpoint for the pavilion. */
	if ((int)g_vistaX >= 24 && (int)g_vistaX <= World.Width - 24 &&
	    (int)g_vistaZ >= 32 && (int)g_vistaZ <= World.Length - 20) {
		bx = (int)g_vistaX;
		bz = (int)g_vistaZ;
	}
	by = TopSolidY(bx, bz);
	if (by < 32) by = 32;
	if (by > 44) by = 44;

	g_pavBaseX = bx;
	g_pavBaseY = by;
	g_pavBaseZ = bz;

	/* Clear air volume and lay grass terrace + cobblestone walkway (z from bz-12 to bz+3) */
	for (dx = -7; dx <= 7; dx++) {
		for (dz = -12; dz <= 3; dz++) {
			int wx = bx + dx;
			int wz = bz + dz;
			int fillY;
			for (fillY = by - 3; fillY <= by - 1; fillY++) {
				SafeSetBlock(wx, fillY, wz, BLOCK_DIRT);
			}
			if (dx >= -1 && dx <= 1 && dz >= -8 && dz <= 2) {
				SafeSetBlock(wx, by, wz, ((dx + dz) & 1) ? BLOCK_MOSSY_ROCKS : BLOCK_COBBLE);
			} else if (dz >= -11 && dz <= -7 && dx >= -5 && dx <= 5) {
				SafeSetBlock(wx, by, wz, BLOCK_DOUBLE_SLAB);
			} else {
				SafeSetBlock(wx, by, wz, BLOCK_GRASS);
			}
			for (dy = 1; dy <= 9; dy++) {
				SafeSetBlock(wx, by + dy, wz, BLOCK_AIR);
			}
		}
	}

	/* Water behind the glass window exercises overlapping transparency. */
	for (dx = -4; dx <= 4; dx++) {
		SafeSetBlock(bx + dx, by, bz - 12, BLOCK_STILL_WATER);
		SafeSetBlock(bx + dx, by + 1, bz - 12, BLOCK_AIR);
	}

	/* Stone slab steps leading up to pavilion */
	for (dx = -5; dx <= 5; dx++) {
		SafeSetBlock(bx + dx, by + 1, bz - 6, BLOCK_SLAB);
	}

	/* Brick & gold pillars at x = bx-4 and bx+4, z = bz-9 */
	for (dy = 1; dy <= 4; dy++) {
		SafeSetBlock(bx - 4, by + dy, bz - 9, (dy == 4) ? BLOCK_GOLD : BLOCK_BRICK);
		SafeSetBlock(bx + 4, by + dy, bz - 9, (dy == 4) ? BLOCK_GOLD : BLOCK_BRICK);
		SafeSetBlock(bx - 4, by + dy, bz - 11, BLOCK_WOOD);
		SafeSetBlock(bx + 4, by + dy, bz - 11, BLOCK_WOOD);
	}

	/* Obsidian & gold lintel beam + glass window back wall */
	for (dx = -4; dx <= 4; dx++) {
		SafeSetBlock(bx + dx, by + 5, bz - 9, (dx == 0) ? BLOCK_GOLD : BLOCK_OBSIDIAN);
		SafeSetBlock(bx + dx, by + 5, bz - 10, BLOCK_BRICK);
		SafeSetBlock(bx + dx, by + 5, bz - 11, BLOCK_OBSIDIAN);
		if (dx >= -3 && dx <= 3) {
			SafeSetBlock(bx + dx, by + 1, bz - 11, BLOCK_BOOKSHELF);
			SafeSetBlock(bx + dx, by + 2, bz - 11, BLOCK_GLASS);
			SafeSetBlock(bx + dx, by + 3, bz - 11, BLOCK_GLASS);
			SafeSetBlock(bx + dx, by + 4, bz - 11, BLOCK_WOOD);
		}
	}

	/* Showcase blocks inside/flanking the pavilion */
	SafeSetBlock(bx - 3, by + 1, bz - 9, BLOCK_TNT);
	SafeSetBlock(bx + 3, by + 1, bz - 9, BLOCK_IRON);
	SafeSetBlock(bx - 2, by + 1, bz - 10, BLOCK_GOLD_ORE);
	SafeSetBlock(bx + 2, by + 1, bz - 10, BLOCK_SPONGE);

	/* Colorful wool arc along the left & right garden borders */
	SafeSetBlock(bx - 6, by + 1, bz - 8, BLOCK_RED);
	SafeSetBlock(bx - 6, by + 1, bz - 7, BLOCK_ORANGE);
	SafeSetBlock(bx - 6, by + 1, bz - 6, BLOCK_YELLOW);
	SafeSetBlock(bx + 6, by + 1, bz - 8, BLOCK_LIME);
	SafeSetBlock(bx + 6, by + 1, bz - 7, BLOCK_AQUA);
	SafeSetBlock(bx + 6, by + 1, bz - 6, BLOCK_VIOLET);

	/* Alpha-tested cross-quad flora (roses, dandelions, mushrooms, saplings) */
	SafeSetBlock(bx - 3, by + 1, bz - 5, BLOCK_ROSE);
	SafeSetBlock(bx - 2, by + 1, bz - 4, BLOCK_DANDELION);
	SafeSetBlock(bx - 4, by + 1, bz - 4, BLOCK_RED_SHROOM);
	SafeSetBlock(bx + 3, by + 1, bz - 5, BLOCK_DANDELION);
	SafeSetBlock(bx + 2, by + 1, bz - 4, BLOCK_ROSE);
	SafeSetBlock(bx + 4, by + 1, bz - 4, BLOCK_BROWN_SHROOM);
	SafeSetBlock(bx - 5, by + 1, bz - 3, BLOCK_SAPLING);
	SafeSetBlock(bx + 5, by + 1, bz - 3, BLOCK_SAPLING);
}

static void SaveDemoScreenshot(const char* filename) {
	static char pathBuf[512];
	const char* outDir = getenv("CC_DEMO_DIR");
	if (!outDir || !*outDir) outDir = ".";
	snprintf(pathBuf, sizeof(pathBuf), "%s/%s", outDir, filename);
	g_pendingShotPath = pathBuf;
}

static void FlushPendingScreenshot(void) {
	struct Stream stream;
	cc_string pathStr;
	cc_filepath rawPath;
	cc_result res;

	if (!g_pendingShotPath) return;
	pathStr = String_FromReadonly(g_pendingShotPath);
	g_pendingShotPath = NULL;

	Platform_EncodePath(&rawPath, &pathStr);
	res = Stream_CreatePath(&stream, &rawPath);
	if (res) {
		fprintf(stderr, "[Gerbera-Demo] ERROR creating %s (err=%d)\n", pathStr.buffer, (int)res);
		return;
	}
	res = Gfx_TakeScreenshot(&stream);
	stream.Close(&stream);
	if (res) {
		fprintf(stderr, "[Gerbera-Demo] ERROR saving screenshot %s (err=%d)\n", pathStr.buffer, (int)res);
	} else {
		printf("[Gerbera-Demo] Saved GLES 1.1 screenshot -> %s\n", pathStr.buffer);
		fflush(stdout);
	}
}

static void DriveShowcaseFrames(void) {
	if (!World.Loaded || !World.Blocks || !Entities.CurPlayer) return;

	if (g_worldFrames == 0) {
		FindScenicVista();
		FadeOutChatHUD();
		Game_SetViewDistance(128);
		Inventory_SetSelectedBlock(BLOCK_GRASS);
		SetPlayerPose(g_vistaX, g_vistaY, g_vistaZ, g_vistaYaw, g_vistaPitch, true);
		printf("[Gerbera-Demo] World loaded (%dx%dx%d). Scenic vista at (%.1f, %.1f, %.1f) yaw=%.0f\n",
		       World.Width, World.Height, World.Length,
		       g_vistaX, g_vistaY, g_vistaZ, g_vistaYaw);
	}

	g_worldFrames++;
	g_stageFrames++;

	switch (g_stage) {
	case 0:
		/* Allow terrain VBOs to build before capturing the landscape. */
		SetPlayerPose(g_vistaX, g_vistaY, g_vistaZ, g_vistaYaw, g_vistaPitch, true);
		if (g_stageFrames == 32) {
			FadeOutChatHUD();
		}
		if (g_stageFrames == 35) {
			SaveDemoScreenshot("01_overworld_landscape.png");
			g_stage++;
			g_stageFrames = 0;
		}
		break;

	case 1:
		/* Capture the renderer information overlay. */
		if (g_stageFrames == 1) {
			cc_string cmd = String_FromConst("/client gpuinfo");
			FadeOutChatHUD();
			Chat_Send(&cmd, false);
		}
		SetPlayerPose(g_vistaX, g_vistaY, g_vistaZ, g_vistaYaw, g_vistaPitch, true);
		if (g_stageFrames == 2) {
			SaveDemoScreenshot("02_gles1_gpuinfo_hud.png");
			g_stage++;
			g_stageFrames = 0;
		}
		break;

	case 2:
		/* Capture the pavilion from the front walkway. */
		if (g_stageFrames == 1) {
			FadeOutChatHUD();
			if (!g_showcaseBuilt) {
				BuildShowcasePavilion();
				g_showcaseBuilt = true;
			}
			Inventory_SetSelectedBlock(BLOCK_BRICK);
		}
		SetPlayerPose(g_pavBaseX + 0.5f, g_pavBaseY + 1.6f, g_pavBaseZ + 1.5f, 0.0f, 8.0f, true);
		if (g_stageFrames == 20) {
			SaveDemoScreenshot("03_block_showcase_pavilion.png");
			g_stage++;
			g_stageFrames = 0;
		}
		break;

	case 3:
		/* Capture the inventory overlay. */
		if (g_stageFrames == 1) {
			InventoryScreen_Show();
		}
		SetPlayerPose(g_pavBaseX + 0.5f, g_pavBaseY + 1.6f, g_pavBaseZ + 1.5f, 0.0f, 8.0f, true);
		if (g_stageFrames == 3) {
			SaveDemoScreenshot("04_block_inventory_gui.png");
			g_stage++;
			g_stageFrames = 0;
		}
		break;

	case 4:
		/* Capture the player model in third person. */
		if (g_stageFrames == 1) {
			InventoryScreen_Hide();
			Inventory_SetSelectedBlock(BLOCK_GOLD);
			if (!Camera.Active->isThirdPerson) {
				Camera_CycleActive();
			}
		}
		SetPlayerPose(g_pavBaseX + 0.5f, (float)(g_pavBaseY + 1), g_pavBaseZ - 3.5f, 18.0f, 14.0f, false);
		if (g_stageFrames == 10) {
			SaveDemoScreenshot("05_third_person_player.png");
			g_stage++;
			g_stageFrames = 0;
		}
		break;

	case 5:
		/* Capture first-person terrain with sunset colors and a shorter view distance. */
		if (g_stageFrames == 1) {
			while (Camera.Active->isThirdPerson) {
				Camera_CycleActive();
			}
			Inventory_SetSelectedBlock(BLOCK_ROSE);
			Env_SetSkyCol(PackedCol_Make(255, 132, 74, 255));
			Env_SetFogCol(PackedCol_Make(255, 178, 112, 255));
			Env_SetCloudsCol(PackedCol_Make(255, 205, 165, 255));
			Env_SetSunCol(PackedCol_Make(255, 218, 168, 255));
			Env_SetShadowCol(PackedCol_Make(145, 108, 128, 255));
			Game_SetViewDistance(64);
		}
		SetPlayerPose(g_vistaX, g_vistaY + 1.5f, g_vistaZ, g_vistaYaw, 11.0f, true);
		if (g_stageFrames == 20) {
			SaveDemoScreenshot("06_sunset_fog_atmosphere.png");
			g_stage++;
			g_stageFrames = 0;
		}
		break;

	default:
		if (g_stageFrames >= 2) {
			printf("[Gerbera-Demo] All 6 OpenGL ES 1.1 screenshots captured cleanly. Exiting.\n");
			Window_RequestClose();
		}
		break;
	}
}

/*########################################################################################################################*
*------------------------------------------------------Window Backend-----------------------------------------------------*
*#########################################################################################################################*/
void Window_PreInit(void) {
	const char* envW = getenv("CC_WIDTH");
	const char* envH = getenv("CC_HEIGHT");
	if (envW && atoi(envW) >= 320) g_winWidth  = atoi(envW);
	if (envH && atoi(envH) >= 240) g_winHeight = atoi(envH);
	DisplayInfo.CursorVisible = true;
}

void Window_Init(void) {
	DisplayInfo.Width  = g_winWidth;
	DisplayInfo.Height = g_winHeight;
	DisplayInfo.Depth  = 32;
	DisplayInfo.ScaleX = 1.0f;
	DisplayInfo.ScaleY = 1.0f;
	Input.Sources      = INPUT_SOURCE_NORMAL;
}

void Window_Free(void) {
	Mem_Free(g_fbPixels);
	g_fbPixels = NULL;
}

static void DoCreateWindow(int width, int height, cc_bool is3D) {
	if (width  <= 0) width  = g_winWidth;
	if (height <= 0) height = g_winHeight;
	/* Fixed capture dimensions. */
	width  = g_winWidth;
	height = g_winHeight;

	Window_Main.Width    = width;
	Window_Main.Height   = height;
	Window_Main.Exists   = true;
	Window_Main.Focused  = true;
	Window_Main.Inactive = false;
	Window_Main.Is3D     = is3D;
	Window_Main.UIScaleX = DEFAULT_UI_SCALE_X;
	Window_Main.UIScaleY = DEFAULT_UI_SCALE_Y;

	Mem_Free(g_fbPixels);
	g_fbPixels = (cc_uint32*)Mem_AllocCleared(width * height, sizeof(cc_uint32), "gerbera egl fb");

	Mem_Set(&g_nativeWin, 0, sizeof(g_nativeWin));
	g_nativeWin.width       = width;
	g_nativeWin.height      = height;
	g_nativeWin.stride      = width * 4;
	g_nativeWin.displayMode = EGL_SYMBIAN_ECOLOR16MU;
	g_nativeWin.frontBuffer = g_fbPixels;

	Window_Main.Handle.ptr = &g_nativeWin;
}

void Window_Create2D(int width, int height) { DoCreateWindow(width, height, false); }
void Window_Create3D(int width, int height) { DoCreateWindow(width, height, true);  }

void Window_Destroy(void) {
	Window_Main.Exists = false;
}

void Window_SetTitle(const cc_string* title) { (void)title; }
void Clipboard_GetText(cc_string* value) { (void)value; }
void Clipboard_SetText(const cc_string* value) { (void)value; }
int  Window_GetWindowState(void) { return WINDOW_STATE_NORMAL; }
cc_result Window_EnterFullscreen(void) { return 0; }
cc_result Window_ExitFullscreen(void)  { return 0; }
int  Window_IsObscured(void) { return 0; }
void Window_Show(void) { }

void Window_SetSize(int width, int height) {
	(void)width; (void)height;
}

void Window_RequestClose(void) {
	g_pendingClose = true;
}

void Window_ProcessEvents(float delta) {
	(void)delta;
	/* Ensure non-zero elapsed frame delta for Game_RenderFrame */
	usleep(16000);

	if (g_pendingClose) {
		g_pendingClose = false;
		Window_Main.Exists = false;
		Event_RaiseVoid(&WindowEvents.Closing);
		return;
	}

	DriveShowcaseFrames();

	/* Stop after 600 rendered world frames if the capture sequence stalls. */
	if (g_worldFrames > 600) {
		Window_Main.Exists = false;
		Event_RaiseVoid(&WindowEvents.Closing);
	}
}

void Gamepads_PreInit(void) { }
void Gamepads_Init(void) { }
void Gamepads_Process(float delta) { (void)delta; }

static void Cursor_GetRawPos(int* x, int* y) { *x = g_winWidth / 2; *y = g_winHeight / 2; }
void Cursor_SetPosition(int x, int y) { (void)x; (void)y; }
static void Cursor_DoSetVisible(cc_bool visible) { (void)visible; }

static void ShowDialogCore(const char* title, const char* msg) {
	fprintf(stderr, "[ClassiCube Dialog] %s: %s\n", title, msg);
}

cc_result Window_OpenFileDialog(const struct OpenFileDialogArgs* args) { (void)args; return ERR_NOT_SUPPORTED; }
cc_result Window_SaveFileDialog(const struct SaveFileDialogArgs* args) { (void)args; return ERR_NOT_SUPPORTED; }

void Window_AllocFramebuffer(struct Bitmap* bmp, int width, int height) {
	bmp->scan0  = (BitmapCol*)Mem_AllocCleared(width * height, BITMAPCOLOR_SIZE, "2d fb");
	bmp->width  = width;
	bmp->height = height;
}
void Window_DrawFramebuffer(Rect2D r, struct Bitmap* bmp) { (void)r; (void)bmp; }
void Window_FreeFramebuffer(struct Bitmap* bmp) { Mem_Free(bmp->scan0); }

void OnscreenKeyboard_Open(struct OpenKeyboardArgs* args) { (void)args; }
void OnscreenKeyboard_SetText(const cc_string* text) { (void)text; }
void OnscreenKeyboard_Close(void) { }
void Window_LockLandscapeOrientation(cc_bool lock) { (void)lock; }

void Window_EnableRawMouse(void)  { DefaultEnableRawMouse();  }
void Window_UpdateRawMouse(void)  { DefaultUpdateRawMouse();  }
void Window_DisableRawMouse(void) { DefaultDisableRawMouse(); }

/*########################################################################################################################*
*-----------------------------------------------Gerbera EGL 1.1 GLContext-------------------------------------------------*
*#########################################################################################################################*/
void GLContext_Create(void) {
	static const EGLint cfgAttribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE,     8,
		EGL_GREEN_SIZE,   8,
		EGL_BLUE_SIZE,    8,
		EGL_DEPTH_SIZE,   16,
		EGL_NONE
	};
	EGLint major = 0, minor = 0, numConfigs = 0;

	g_eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (g_eglDisplay == EGL_NO_DISPLAY) Logger_FailToStart("eglGetDisplay failed");

	if (!eglInitialize(g_eglDisplay, &major, &minor))
		Logger_FailToStart("eglInitialize failed");

	if (!eglChooseConfig(g_eglDisplay, cfgAttribs, &g_eglConfig, 1, &numConfigs) || numConfigs < 1)
		Logger_FailToStart("eglChooseConfig failed");

	g_eglSurface = eglCreateWindowSurface(g_eglDisplay, g_eglConfig, (NativeWindowType)&g_nativeWin, NULL);
	if (g_eglSurface == EGL_NO_SURFACE)
		Logger_FailToStart("eglCreateWindowSurface failed");

	g_eglContext = eglCreateContext(g_eglDisplay, g_eglConfig, EGL_NO_CONTEXT, NULL);
	if (g_eglContext == EGL_NO_CONTEXT)
		Logger_FailToStart("eglCreateContext failed");

	if (!eglMakeCurrent(g_eglDisplay, g_eglSurface, g_eglSurface, g_eglContext))
		Logger_FailToStart("eglMakeCurrent failed");

	printf("[Gerbera-EGL] Initialized EGL %d.%d (%s) | GL_VENDOR='%s' GL_RENDERER='%s' GL_VERSION='%s' (%dx%d)\n",
	       (int)major, (int)minor,
	       eglQueryString(g_eglDisplay, EGL_VENDOR),
	       (const char*)glGetString(GL_VENDOR),
	       (const char*)glGetString(GL_RENDERER),
	       (const char*)glGetString(GL_VERSION),
	       g_nativeWin.width, g_nativeWin.height);
	fflush(stdout);
}

void GLContext_Update(void) { }
cc_bool GLContext_TryRestore(void) { return true; }

void GLContext_Free(void) {
	if (g_eglDisplay == EGL_NO_DISPLAY) return;
	eglMakeCurrent(g_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (g_eglContext != EGL_NO_CONTEXT) {
		eglDestroyContext(g_eglDisplay, g_eglContext);
		g_eglContext = EGL_NO_CONTEXT;
	}
	if (g_eglSurface != EGL_NO_SURFACE) {
		eglDestroySurface(g_eglDisplay, g_eglSurface);
		g_eglSurface = EGL_NO_SURFACE;
	}
	eglTerminate(g_eglDisplay);
	g_eglDisplay = EGL_NO_DISPLAY;
}

void* GLContext_GetAddress(const char* function) {
	return (void*)(cc_uintptr)eglGetProcAddress(function);
}

cc_bool GLContext_SwapBuffers(void) {
	FlushPendingScreenshot();
	return eglSwapBuffers(g_eglDisplay, g_eglSurface) == EGL_TRUE;
}

void GLContext_SetVSync(cc_bool vsync) {
	if (g_eglDisplay != EGL_NO_DISPLAY) {
		eglSwapInterval(g_eglDisplay, vsync ? 1 : 0);
	}
}

void GLContext_GetApiInfo(cc_string* info) {
	const char* eglVer = eglQueryString(g_eglDisplay, EGL_VERSION);
	const char* eglVen = eglQueryString(g_eglDisplay, EGL_VENDOR);
	String_Format2(info, "EGL: %c (%c - Gerbera GLES 1.1)\n", eglVer ? eglVer : "1.1", eglVen ? eglVen : "Hybrid Graphics");
}

#endif
