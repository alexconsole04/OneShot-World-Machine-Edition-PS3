// wm.cpp - WindowManager: the World Machine desktop. Windows (front to
// back), the taskbar, the system dialog, dragging files, fades, the
// OneShot window, unlocks, and desktop.dat / fs.dat. The desktop:: calls
// (desktop.h) are what main.cpp uses.
#include "wm_internal.h"
#include "apps.h"
#include "customize.h"
#include "../desktop.h"
#include "../loc.h"
#include "../json_file.h"
#include "../modal.h"
#include "../oneshot_window.h"
#include "../save.h"
#include "../settings.h"
#include "../../engine/audio.h"
#include "../../engine/gm.h"
#include "../../engine/util.h"
#include "../../platform/input.h"
#include "../../platform/sys.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

namespace twm {

// ===================================================================== state

// m14b: startup steps logged (and the log flushed) until the desktop has
// drawn a few frames, to find where some saves stop it on the PS3.

static std::vector<Theme> g_themes;
static std::vector<std::pair<Color, Color>> g_themeOriginals;   // rainbow: colours before the hue cycle
static float g_rainbowOffset;
static std::string g_themeId = "purple";
static Theme g_fallbackTheme;
static Filesystem g_fs;
static UnlockManager *g_unlocks;
static ToastManager g_toasts;
static std::unique_ptr<DesktopGrid> g_desktop;
static std::vector<std::unique_ptr<Window>> g_windows;   // [0] = front
static std::vector<Window *> g_pendingRemoval, g_previousActive;
static bool g_hasPreviousActive, g_createdThisFrame;
static std::unique_ptr<ModalWindow> g_systemModal;
static std::unique_ptr<DraggedItem> g_drag;
static bool g_ready, g_quit, g_fsDirty, g_desktopDirty;
static int g_gamma = 100, g_bgmVol = 100, g_sfxVol = 100;   // Customize (desktop.dat)
static bool g_hideUI;                                        // HideAllUI (View Wallpaper)
static int g_restartRequest;
// Taskbar
struct TaskButton { int w; Window *window; };
static std::vector<TaskButton> g_taskButtons;
static int g_taskButtonW = 100;
static Rect g_taskArea;
static std::unique_ptr<WindowButton> g_minimizeAll;
// Fades and flashes
static bool g_fading;
static int g_fadeTimer, g_fadeTotal;
static int g_fadeStart, g_fadeEnd, g_fadeOpacity;
static std::function<void()> g_onFadeDone;
static Color g_flashColor = Color::White();
static float g_flashStrength = 1;
static int g_flashTimer, g_flashDuration = 1;
// TWMNiko: Niko walking off the bottom of the screen.
struct Niko { Vec2 pos; int frame = 0, timer = 0; bool moved = false, solstice = false, entity = false; };
static std::unique_ptr<Niko> g_niko;

Filesystem &filesystem() { return g_fs; }
UnlockManager &unlocks() {
	if (!g_unlocks) g_unlocks = new UnlockManager();
	return *g_unlocks;
}
ToastManager &toasts() { return g_toasts; }
DesktopGrid &desktop_grid() { return *g_desktop; }
Vec2 screen_size() { return screen(); }
bool system_modal_open() { return g_systemModal != nullptr; }
bool desktop_ready() { return g_ready; }

// ===================================================================== themes

static void load_themes() {
	g_themes.clear();
	if (cJSON *o = parse_json_file("gamedata/twm/themes_metadata.json")) {
		const cJSON *it;
		cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(o, "themes")) {
			Theme t;
			const cJSON *id = cJSON_GetObjectItemCaseSensitive(it, "id"), *dn = cJSON_GetObjectItemCaseSensitive(it, "displayName");
			if (!cJSON_IsString(id)) continue;
			t.id = id->valuestring;
			t.displayName = cJSON_IsString(dn) ? dn->valuestring : t.id;
			auto col = [&](const char *k, Color &c) {
				const cJSON *v = cJSON_GetObjectItemCaseSensitive(it, k);
				if (!v) return;
				c = Color((int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(v, "r")),
				          (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(v, "g")),
				          (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(v, "b")));
			};
			col("primary", t.primary);
			col("primaryVariant", t.variant);
			col("background", t.background);
			bool dup = false;
			for (auto &x : g_themes) dup |= x.id == t.id;
			if (!dup) g_themes.push_back(t);
		}
		cJSON_Delete(o);
	}
	if (g_themes.empty()) sys::log("twm: gamedata/twm/themes_metadata.json missing, using the purple theme\n");
	g_themeOriginals.clear();
	for (auto &t : g_themes) g_themeOriginals.push_back({ t.primary, t.variant });
}

const Theme *theme_by_id(const std::string &id) {
	for (auto &t : g_themes)
		if (t.id == id) return &t;
	return nullptr;
}
const Theme &theme() {
	const Theme *t = theme_by_id(g_themeId);
	return t ? *t : g_fallbackTheme;
}
const std::vector<Theme> &themes() { return g_themes; }

static void hue(Color &c, Color orig, float h) {   // MathHelper.ApplyHue
	const float k = 0.57735f;
	float v[3] = { orig.r / 255.0f, orig.g / 255.0f, orig.b / 255.0f };
	float cs = cosf(h), sn = sinf(h), d = k * (v[0] + v[1] + v[2]) * (1 - cs);
	float cr[3] = { k * v[2] - k * v[1], k * v[0] - k * v[2], k * v[1] - k * v[0] };
	uint8_t out[3];
	for (int i = 0; i < 3; i++) {
		float x = v[i] * cs + cr[i] * sn + k * d;
		out[i] = (uint8_t)((x < 0 ? 0 : x > 1 ? 1 : x) * 255);
	}
	c.r = out[0]; c.g = out[1]; c.b = out[2];
}

static void update_themes() {   // TWMTheme.Update: the rainbow theme cycles its hue
	g_rainbowOffset += 0.02f;
	if (g_rainbowOffset > 6.2831853f) g_rainbowOffset -= 6.2831853f;
	for (size_t i = 0; i < g_themes.size(); i++)
		if (g_themes[i].id == "rainbow") {
			hue(g_themes[i].primary, g_themeOriginals[i].first, g_rainbowOffset);
			hue(g_themes[i].variant, g_themeOriginals[i].second, g_rainbowOffset);
		}
}

// ===================================================================== the OneShot window

static bool g_windowedDraw;
static Rect g_gameRect;   // the game's picture this frame, desktop draw-space pixels (w 0: none)

// OneshotWindow.PreDraw + DrawOneshotTexture: the 640x480 game picture
// with its top-left at TV (x, y), scale TV pixels per window pixel.
static void draw_game_at(float x, float y, float scale) {
	prof::mark("desktop draw");
	end();
	gm::set_window_placement(x, y, scale);
	// In a window the game is drawn at 1x (one TV pixel per window pixel on
	// a 720p or 1080p desktop); maximized follows the Scaling setting.
	gm::set_scaling(g_windowedDraw ? gm::SCALE_SMOOTH : settings().scaling);
	gm::begin_window();
	gm::clip_to_window();
	window().Draw();
	gm::clip_off();
	gm::set_window_glitch(window().GlitchSegments());
	gm::end_window();
	begin();
	prof::mark("game draw");
}

class GameWindow : public Window {
public:
	GameWindow() {
		icon = "oneshot";
		SetTitle(loc::twm("oneshot_appname"));
		SetContents(Vec2(320, 240));
		AddButton(WinButtonType::Close, [] { window().CloseGame(); });
		AddButton(WinButtonType::Maximize, [this] {
			if (!IsModalOpen() && !window().State().flag(123)) ToggleMaximize();   // flag 123: not during that scene
		});
		AddButton(WinButtonType::Minimize);
	}
	void ToggleMaximize() override {
		Window::ToggleMaximize();
		WindowButton *b = FindButton(WinButtonType::Maximize);
		if (maximized) oldMaxPos = b->Position();
		else b->SetPosition(oldMaxPos);
		settings().fullscreen = maximized;
	}
	bool Update(bool consumed) override {
		if (++borderTimer >= 6) {
			borderTimer = 0;
			borderFrame = (borderFrame + 1) % 2;
		}
		// GameLogic, unless a dialog is up (the game waits for it).
		if (!IsModalOpen() && !system_modal_open() && window().IsOpen()) {
			prof::mark("desktop ui");
			window().Update();
			prof::mark("game logic");
		}
		if (!maximized) return Window::Update(consumed);
		// Maximized: only the maximize button, in the screen's top-right corner.
		WindowButton *b = FindButton(WinButtonType::Maximize);
		b->SetPosition(Vec2(space().x / 2 - 2 - 16, 2));
		return b->Update(Vec2(0, 0), !system_modal_open());
	}
	// GetFullscreenRect, in desktop draw-space pixels.
	Rect FullscreenRect() const {
		Vec2 d = space();
		Rect r;
		if (d.x * 3 >= d.y * 4) r = Rect(0, 0, 320 * d.y / 240, d.y);
		else r = Rect(0, 0, d.x, 240 * d.x / 320);
		r.x = (d.x - r.w) / 2;
		r.y = (d.y - r.h) / 2;
		return r;
	}
	void Draw(const Theme &th) override {
		if (!maximized) {
			Window::Draw(th);
			return;
		}
		box(Rect(0, 0, screen().x, screen().y), Color::Black());
		Rect fr = FullscreenRect();
		g_windowedDraw = false;
		float k;
		Vec2 tv = gm::space_to_tv(space(), (float)fr.x, (float)fr.y, &k);
		draw_game_at((float)tv.x, (float)tv.y, k * fr.h / 480.0f);
		if (settings().fullscreenBorder) DrawBorder(fr);
		FindButton(WinButtonType::Maximize)->Draw(Vec2(0, 0), th, 255);
	}
	void DrawContents(const Theme &, Vec2 sp, int) override {
		g_windowedDraw = true;
		float k;
		Vec2 tv = gm::space_to_tv(space(), (float)sp.x * 2, (float)sp.y * 2, &k);
		draw_game_at((float)tv.x, (float)tv.y, k);
	}
	bool IsSameContent(const Window *other) const override { return other->Kind() == KIND_GAME; }
	int Kind() const override { return KIND_GAME; }

private:
	// FullscreenBorderManager's art beside (or above and below) the picture,
	// in the colour of the current area.
	void DrawBorder(Rect fr) {
		uint32_t c = window().BorderColor();
		float r = (c >> 16 & 255) / 255.0f, g = (c >> 8 & 255) / 255.0f, b = (c & 255) / 255.0f;
		Vec2 d = space();
		bool wide = d.x * 3 >= d.y * 4;
		const char *names[2] = { wide ? "the_world_machine/fullscreen_border_left" : "the_world_machine/fullscreen_border_up",
		                         wide ? "the_world_machine/fullscreen_border_right" : "the_world_machine/fullscreen_border_down" };
		for (int side = 0; side < 2; side++) {
			const gfx::Texture *t = gm::texture(names[side], gm::TheWorldMachine);
			if (!t) continue;
			if (wide) {
				int fw = (int)t->w / 2, fh = (int)t->h;
				float s = (float)d.y / fh;
				int w = (int)lroundf(fw * s);
				Vec2 p(side == 0 ? fr.x - w : fr.x + fr.w, 0);
				gm::blit_scaled(t, p, Rect(borderFrame * fw, 0, fw, fh), s, s, 1, 0, gfx::Blend::Normal, GameTone(), r, g, b);
			} else {
				int fw = (int)t->w, fh = (int)t->h / 2;
				float s = (float)d.x / fw;
				int h = (int)lroundf(fh * s);
				Vec2 p(0, side == 0 ? fr.y - h : fr.y + fr.h);
				gm::blit_scaled(t, p, Rect(0, borderFrame * fh, fw, fh), s, s, 1, 0, gfx::Blend::Normal, GameTone(), r, g, b);
			}
		}
	}
	Vec2 oldMaxPos;
	int borderTimer = 0, borderFrame = 0;
};

static GameWindow *game_window() {
	for (auto &w : g_windows)
		if (w->Kind() == KIND_GAME) return static_cast<GameWindow *>(w.get());
	return nullptr;
}

bool game_maximized() {
	GameWindow *g = game_window();
	return g && g->Maximized();
}
void set_game_maximized(bool on) {
	GameWindow *g = game_window();
	if (g && g->Maximized() != on) g->ToggleMaximize();
}
Vec2 game_window_pos() {
	GameWindow *g = game_window();
	return g ? g->pos : Vec2(0, 0);
}
void set_game_window_pos(Vec2 p) {
	if (GameWindow *g = game_window()) g->pos = p;
}
bool game_window_modal_open() {
	GameWindow *g = game_window();
	return g && g->IsModalOpen();
}
bool clover_equipped() { return game_window() && window().CloverEquipped(); }

// ===================================================================== windows

Window *find_window(bool (*pred)(Window *)) {
	for (auto &w : g_windows)
		if (pred(w.get())) return w.get();
	return nullptr;
}

bool is_window_focused(const Window *w) { return !g_windows.empty() && g_windows[0].get() == w; }

static void bring_to_front(Window *w) {
	for (size_t i = 0; i < g_windows.size(); i++)
		if (g_windows[i].get() == w && i != 0) {
			std::unique_ptr<Window> p = std::move(g_windows[i]);
			g_windows.erase(g_windows.begin() + i);
			g_windows.insert(g_windows.begin(), std::move(p));
			return;
		}
}

static void toggle_minimize(Window *w) {   // OnToggleMinimize
	w->ToggleMinimize();
	if (!w->Minimized()) bring_to_front(w);
}

static void place_window(Window *w) {
	Vec2 p((screen().x - w->Contents().x) / 2 - 2, (screen().y - w->Contents().y - 26 - 30) / 2);
	p.x = std::max(p.x, 0);
	p.y = std::max(p.y, 0);
	for (bool again = true; again;) {
		again = false;
		for (auto &o : g_windows)
			if (iabs(p.x - o->pos.x) < 12 || iabs(p.y - o->pos.y) < 12) {
				p.x += 13;
				p.y += 13;
				again = true;
				break;
			}
	}
	w->pos = p;
}

static bool add_window_(Window *raw) {
	std::unique_ptr<Window> w(raw);
	for (auto &o : g_windows)
		if (o->IsSameContent(raw)) {   // already open: bring that one up
			if (o->Minimized()) toggle_minimize(o.get());
			else bring_to_front(o.get());
			g_createdThisFrame = true;
			return false;
		}
	if (g_taskButtons.size() >= 6) {   // the taskbar holds six
		show_system_modal(MODAL_ERROR, "error_too_many_windows");
		return false;
	}
	place_window(raw);
	raw->onMinimize = [](Window *x) { toggle_minimize(x); };
	raw->onClose = [](Window *x) {
		if (std::find(g_pendingRemoval.begin(), g_pendingRemoval.end(), x) == g_pendingRemoval.end()) g_pendingRemoval.push_back(x);
	};
	raw->grabFocus = [](Window *x) { if (!g_createdThisFrame) bring_to_front(x); };
	g_taskButtons.push_back({ g_taskButtonW, raw });
	g_windows.insert(g_windows.begin(), std::move(w));
	g_createdThisFrame = true;
	return true;
}

void add_window(Window *w) { add_window_(w); }

static void remove_window(Window *w) {
	for (auto &b : g_taskButtons)
		if (b.window == w) b.window = nullptr;   // its button shrinks away
	g_previousActive.erase(std::remove(g_previousActive.begin(), g_previousActive.end(), w), g_previousActive.end());
	for (size_t i = 0; i < g_windows.size(); i++)
		if (g_windows[i].get() == w) {
			defer_delete(g_windows[i].release());   // it may still be running (its own close button)
			g_windows.erase(g_windows.begin() + i);
			break;
		}
}

static void minimize_all() {
	std::vector<Window *> open;
	for (auto &w : g_windows)
		if (!w->Minimized()) open.push_back(w.get());
	if (!open.empty()) {
		for (Window *w : open) toggle_minimize(w);
		g_previousActive = open;
		g_hasPreviousActive = true;
		return;
	}
	if (g_hasPreviousActive) {
		std::reverse(g_previousActive.begin(), g_previousActive.end());
		for (Window *w : g_previousActive) toggle_minimize(w);
	}
	g_previousActive.clear();
	g_hasPreviousActive = false;
}

static bool unminimize_enabled() {
	for (auto &w : g_windows)
		if (!w->Minimized()) return false;
	return g_hasPreviousActive;
}

void show_system_modal(int type, const std::string &msg, std::function<void(int)> callback, bool sound, bool canAutomash) {
	if (g_systemModal) defer_delete(g_systemModal.release());   // replaced (it may be answering right now)
	ModalWindow *m = new ModalWindow(type, msg, sound, canAutomash);
	g_systemModal.reset(m);
	m->callback = callback;
	m->onClose = [](Window *w) {
		if (g_systemModal.get() == w) defer_delete(g_systemModal.release());
	};
	m->grabFocus = [](Window *) {};
	m->pos = Vec2(screen().x / 2 - m->Contents().x / 2 - 2, screen().y / 2 - m->Contents().y / 2);
}

void flash(Color c, int duration, float strength) {
	g_flashColor = c;
	g_flashDuration = duration;
	g_flashTimer = duration;
	g_flashStrength = std::min(std::max(strength, 0.0f), 1.0f);
}

static void fade(int from, int to, int frames, std::function<void()> done) {
	g_fading = true;
	g_fadeTimer = 0;
	g_fadeTotal = frames;
	g_fadeOpacity = g_fadeStart = from;
	g_fadeEnd = to;
	g_onFadeDone = done;
}

void begin_drag(DraggedItem *item) {
	if (g_drag) {
		sys::log("twm: double drag\n");
		if (item->onDropComplete) item->onDropComplete(false);
		delete item;
		return;
	}
	g_drag.reset(item);
}

static void cancel_drag() {
	if (g_drag && g_drag->onDropComplete) g_drag->onDropComplete(false);
	g_drag.reset();
}

// ===================================================================== saving

void save_desktop() { g_desktopDirty = true; }
void save_desktop_and_fs() { g_desktopDirty = g_fsDirty = true; }

static void write_saves() {
	if (g_fsDirty) {
		g_fsDirty = false;
		std::string text = g_fs.Save();
		if (!sys::save_write("fs.dat", text)) sys::log("twm: couldn't write fs.dat\n");
	}
	if (!g_desktopDirty) return;
	g_desktopDirty = false;
	// FilesystemSaveManager.DesktopSaveData, the PC's field names.
	cJSON *o = cJSON_CreateObject();
	cJSON *pos = cJSON_AddObjectToObject(o, "iconPositions");
	for (auto &p : g_desktop->Layout()) {
		cJSON *v = cJSON_AddObjectToObject(pos, p.first.c_str());
		cJSON_AddNumberToObject(v, "X", p.second.x);
		cJSON_AddNumberToObject(v, "Y", p.second.y);
	}
	cJSON_AddStringToObject(o, "currentWallpaper", g_desktop->Current().imageFile.c_str());
	cJSON_AddStringToObject(o, "currentTheme", g_themeId.c_str());
	unlocks().PopulateSaveData(o);
	cJSON_AddBoolToObject(o, "tutorialCompleted", 1);
	cJSON_AddBoolToObject(o, "inSolstice", g_desktop->inSolstice);
	cJSON_AddBoolToObject(o, "isSystemScalingSmooth", settings().scaling == gm::SCALE_SMOOTH);
	cJSON_AddNumberToObject(o, "Gamma", g_gamma);
	cJSON_AddNumberToObject(o, "BGMVolume", g_bgmVol);
	cJSON_AddNumberToObject(o, "SFXVolume", g_sfxVol);
	cJSON_AddBoolToObject(o, "isVsyncEnabled", 1);
	cJSON_AddBoolToObject(o, "isBadgeToastsEnabled", unlocks().badgeToastsEnabled);
	cJSON_AddBoolToObject(o, "isFullscreen", 1);
	cJSON *dr = cJSON_AddObjectToObject(o, "DrawResolution");
	cJSON_AddNumberToObject(dr, "X", space().x);
	cJSON_AddNumberToObject(dr, "Y", space().y);
	char *s = cJSON_PrintUnformatted(o);
	if (s) {
		if (!sys::save_write("desktop.dat", s)) sys::log("twm: couldn't write desktop.dat\n");
		cJSON_free(s);
	}
	cJSON_Delete(o);
}

static bool load_desktop() {
	std::string text;
	if (!sys::save_read("desktop.dat", text)) return false;
	cJSON *o = cJSON_Parse(text.c_str());
	if (!o) {
		sys::log("twm: desktop.dat is damaged, using the default layout\n");
		return false;
	}
	std::vector<std::pair<std::string, Vec2>> layout;
	const cJSON *it;
	cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(o, "iconPositions"))
		layout.push_back({ it->string ? it->string : "", Vec2((int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(it, "X")),
		                                                      (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(it, "Y"))) });
	g_desktop->SetLayout(layout);
	const cJSON *wp = cJSON_GetObjectItemCaseSensitive(o, "currentWallpaper");
	g_desktop->SetWallpaper(cJSON_IsString(wp) && g_desktop->WallpaperMeta(wp->valuestring) ? wp->valuestring : "default");
	g_desktop->inSolstice = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "inSolstice"));
	auto num = [o](const char *k, int def, int lo, int hi) {
		const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
		int n = cJSON_IsNumber(v) ? (int)v->valuedouble : def;
		return n < lo ? lo : n > hi ? hi : n;
	};
	g_gamma = num("Gamma", 100, 50, 150);
	g_bgmVol = num("BGMVolume", 100, 0, 100);
	g_sfxVol = num("SFXVolume", 100, 0, 100);
	audio::set_volumes(g_bgmVol, g_sfxVol);
	const cJSON *bt = cJSON_GetObjectItemCaseSensitive(o, "isBadgeToastsEnabled");
	if (cJSON_IsBool(bt)) unlocks().badgeToastsEnabled = cJSON_IsTrue(bt);
	unlocks().LoadSaveData(o);
	const cJSON *th = cJSON_GetObjectItemCaseSensitive(o, "currentTheme");
	if (cJSON_IsString(th) && theme_by_id(th->valuestring)) g_themeId = th->valuestring;
	cJSON_Delete(o);
	g_fs.CreateWallpaperFiles(unlocks().wallpapers);
	g_fs.CreateThemeFiles(unlocks().themes);
	return true;
}

// ===================================================================== RunFile

static void granted(File *f, const char *what, const std::string &id) {
	std::string type = what;
	g_fs.Delete(f);
	play_sound("pc_granted", 0.8f, 1.5f);
	flash(Color(0, 255, 255, 255), 20, 0.25f);
	if (type == "profile") unlocks().UnlockProfile(id);
	else unlocks().UnlockWallpaper(id);
	save_desktop();
}

static void not_yet(const std::string &name) {
	// Apps of the next stage.
	show_system_modal(MODAL_INFO, loc::twm(name.c_str()) + ":\n" + "This app isn't in the PS3 version yet.");
}

bool run_file(Node *node) {
	if (!node) return false;
	if (node->IsFolder()) {
		add_window_(new BrowserWindow(&g_fs, static_cast<Folder *>(node)->Path()));
		return true;
	}
	File *f = static_cast<File *>(node);
	auto arg = [f](size_t i) { return i < f->args.size() ? f->args[i] : std::string(); };
	sys::log("twm: run %s (%s)\n", f->name.c_str(), program_name(f->program));
	switch (f->program) {
	case PROG_ONESHOT:
		if (find_window([](Window *w) { return w->Kind() == KIND_JUKEBOX; })) {
			show_system_modal(MODAL_ERROR, "error_launch_oneshot_musicbox_open");
		} else if (!game_window()) {
			if (add_window_(new GameWindow())) {
				sys::log("desktop: launching OneShot\n");
				window().Launch(true);
				if (settings().fullscreen) set_game_maximized(true);   // the saved Fullscreen setting
			}
		} else {
			add_window_(new GameWindow());   // brings the open one up
		}
		break;
	case PROG_PHOTO:
		add_window_(new PhotoWindow(arg(0), f->name, f->args.size() > 1 ? atoi(arg(1).c_str()) : 1));
		break;
	case PROG_JOURNAL: add_window_(new JournalWindow()); break;
	case PROG_DOCUMENT: add_window_(new DocumentWindow(f->name, arg(0), arg(1))); break;
	case PROG_JUKEBOX:
		if (game_window()) show_system_modal(MODAL_ERROR, "error_launch_musicbox_oneshot_open");
		else not_yet(f->name);
		break;
	case PROG_CONTACTS:
		if (unlocks().profiles.empty()) show_system_modal(MODAL_ERROR, "error_no_friends_unlocked");
		else not_yet(f->name);
		break;
	case PROG_AUTHOR_PROFILE: granted(f, "profile", "author"); break;
	case PROG_NIKO_PROFILE: granted(f, "profile", "niko"); break;
	case PROG_FROMNIKO_WALLPAPER: granted(f, "wallpaper", "from_niko"); break;
	case PROG_WALLPAPER:
		if (!g_desktop->IsOverridden()) {
			g_desktop->SetWallpaper(arg(0));
			save_desktop();
		}
		break;
	case PROG_THEME:
		if (theme_by_id(arg(0))) g_themeId = arg(0);
		save_desktop();
		break;
	case PROG_SHUTDOWN:
		if (game_window()) {
			show_system_modal(MODAL_ERROR, "cant_shutdown_while_oneshot_running");
			break;
		}
		show_system_modal(MODAL_YESNO, "do_you_really_want_to_shut_down", [](int r) {
			if (r != RESP_YES) return;
			save_desktop_and_fs();
			write_saves();
			fade(128, 255, 120, [] {
				for (auto &w : g_windows)
					if (w->onClose) w->onClose(w.get());
				g_quit = true;
			});
		});
		break;
	case PROG_CUSTOMIZE: add_window_(new CustomizeWindow()); break;
	case PROG_DUMMY_FILE_FOR_TUTORIALS: break;
	default: not_yet(f->name); break;
	}
	return true;
}

void unlock_app(Program p) {
	if (g_fs.FileTypeExists(p)) return;
	std::shared_ptr<File> f;
	switch (p) {
	case PROG_CONTACTS: f = make_file("contacts", "contacts_app", p); break;
	case PROG_JUKEBOX: f = make_file("jukebox", "musicbox_appname", p); break;
	case PROG_GALLERY: f = make_file("gallery", "gallery_app", p); break;
	case PROG_ACHIEVEMENT: f = make_file("achievements", "achievements_app_name", p); break;
	default: return;
	}
	f->deleteRestricted = true;
	g_fs.WriteFile(Filesystem::DESKTOP, f);
}

// ===================================================================== the game's calls (wm.h)

static bool is_journal(Window *w) { return w->Kind() == KIND_JOURNAL; }
bool journal_open() { return find_window(is_journal) != nullptr; }
void set_journal_page(const std::string &page) {
	if (Window *w = find_window(is_journal)) static_cast<JournalWindow *>(w)->SetPicture(page);
}

void deploy_niko(bool solstice, bool entity) {
	GameWindow *g = game_window();
	if (!g) return;
	g_niko.reset(new Niko());
	g_niko->pos = g->pos + Vec2(150 + (solstice ? 2 : 0), 234);
	g_niko->solstice = solstice;
	g_niko->entity = entity;
}

void override_wallpaper(const std::string &image, int mode, uint8_t r, uint8_t g, uint8_t b) {
	WallpaperInfo w;
	w.imageFile = image;
	w.displayName = "asdf";
	w.mode = mode;
	w.bg = Color(r, g, b);
	if (g_desktop) g_desktop->OverrideWallpaper(w);
}
void restore_wallpaper() { if (g_desktop) g_desktop->RestoreWallpaper(); }
void set_wallpaper(const std::string &id) { if (g_desktop) g_desktop->SetWallpaper(id); }
bool wallpaper_overridden() { return g_desktop && g_desktop->IsOverridden(); }
bool overriding_wallpaper(std::string &image, std::string &name, int &mode, uint8_t &r, uint8_t &g, uint8_t &b) {
	if (!wallpaper_overridden()) return false;
	const WallpaperInfo &w = g_desktop->Showing();   // the override itself
	image = w.imageFile;
	name = w.displayName;
	mode = w.mode;
	r = w.bg.r; g = w.bg.g; b = w.bg.b;
	return true;
}
void set_in_solstice(bool on) { if (g_desktop) g_desktop->inSolstice = on; }

// The game's own dialogs (EdText, "can't close during cutscenes", the
// language question): ShowModalWindow on the OneShot window, or a system
// dialog when it isn't open.
static int g_gameModalResponse = RESP_NONE;
static bool g_gameModalSystem;

void game_modal_show(int type, const std::string &text, bool canAutomash) {
	g_gameModalResponse = RESP_NONE;
	auto cb = [](int r) { g_gameModalResponse = r; };
	GameWindow *g = g_ready ? game_window() : nullptr;
	g_gameModalSystem = !g;
	if (g) g->ShowModal(type, text, cb, true, canAutomash);
	else show_system_modal(type, text, cb, true, canAutomash);
}
bool game_modal_open() {
	if (g_gameModalSystem) return g_systemModal != nullptr;
	GameWindow *g = game_window();
	return g && g->IsModalOpen();
}
int game_modal_response() { return g_gameModalResponse; }

// ===================================================================== taskbar

static bool taskbar_update(bool consumed) {
	Vec2 m = cursor().MousePos();
	if (!consumed) {
		Rect r(2, g_taskArea.y + 2, 0, 24);
		for (auto &b : g_taskButtons) {
			r.w = b.w;
			if (in_rect(r, m)) {
				cursor().SetState(MouseCursor::Clickable);
				if (cursor().Clicked() && b.window) {   // OnTaskbarSelect
					Window *w = b.window, *active = nullptr;
					for (auto &x : g_windows)
						if (!x->Minimized()) { active = x.get(); break; }
					if (w->Minimized()) { w->ToggleMinimize(); bring_to_front(w); }
					else if (active == w) w->ToggleMinimize();
					else bring_to_front(w);
				}
			}
			r.x += r.w + 2;
		}
		g_minimizeAll->type = unminimize_enabled() ? WinButtonType::UnminimizeAll : WinButtonType::MinimizeAll;
	}
	g_minimizeAll->Update(Vec2(0, 0), !consumed);
	for (auto it = g_taskButtons.begin(); it != g_taskButtons.end();) {
		if (!it->window) {
			it->w -= 6;
			if (it->w < 6) { it = g_taskButtons.erase(it); continue; }
		}
		++it;
	}
	return m.y >= g_taskArea.y;
}

static void draw_taskbar(Window *active) {
	const Theme &th = theme();
	box(g_taskArea, th.Primary());
	Rect a = g_taskArea;
	a.y += 2;
	a.h -= 2;
	box(a, th.Background());
	Vec2 p(2, a.y + 2);
	for (auto &b : g_taskButtons) {
		Color frame, fill, fg;
		if (b.window) {
			bool on = b.window == active && !b.window->Minimized();
			frame = b.window->Minimized() ? th.Variant() : th.Primary();
			fill = on ? th.Primary() : th.Background();
			fg = on ? th.Background() : th.Primary();
		} else {
			frame = th.Variant();
			fill = th.Background();
			fg = th.Primary();
		}
		Rect r(p.x, p.y, b.w, 24);
		box(r, frame);
		box(shrink(r, 2), fill);
		if (b.window) {
			Vec2 q(r.x + 4, r.y + 4);
			std::string t = b.window->Title();
			if (b.window->Kind() == KIND_BROWSER) t = t.substr(t.find_last_of('/') + 1);
			if (!b.window->icon.empty()) tex(("the_world_machine/window_icons/" + b.window->icon).c_str(), q, Rect(), fg);
			text(Vec2(q.x + 20, q.y), truncate(t, b.w - 4 - 24), fg);
		}
		p.x += b.w + 2;
	}
	// The clock (DateTime.Now.ToShortTimeString), in the console's format,
	// read once a second.
	static char clock[16];
	static uint64_t nextRead, nextFormat;
	static bool h24;
	uint64_t now = sys::time_us();
	if (now >= nextRead) {
		int h, m, s, yd;
		sys::local_time(&h, &m, &s, &yd);
		if (now >= nextFormat) { h24 = sys::clock_24h(); nextFormat = now + 30000000; }
		if (h24) snprintf(clock, sizeof(clock), "%d:%02d", h, m);
		else snprintf(clock, sizeof(clock), "%d:%02d %s", h % 12 == 0 ? 12 : h % 12, m, h < 12 ? "AM" : "PM");
		nextRead = now + 1000000;
	}
	text(Vec2(a.x + a.w - text_w(clock) - 26, a.y + 4), clock, th.Primary());
	g_minimizeAll->Draw(Vec2(0, 0), th, 255);
}

// ===================================================================== init

static void init() {
	if (g_ready) return;
	uint64_t t0 = sys::time_us();
	// The desktop's draw size (DrawScreenSize): the TV's own pixels on a
	// 1080p TV (as the PC at 1920x1080), else the 720-line boot space.
	if (gfx::screen_h() >= 1080) set_space(Vec2(gfx::screen_w(), gfx::screen_h()));
	else set_space(gm::boot_space());
	set_desktop_space(space());
	load_themes();
	unlocks();
	// InitFs
	bool freshFs = true;
	std::string fsText;
	if (sys::save_read("fs.dat", fsText) && g_fs.Load(fsText)) {
		freshFs = false;
		g_fs.CreateExtraSystemFiles();
	} else {
		if (!fsText.empty()) sys::log("twm: fs.dat is damaged, making a new file system\n");
		g_fs.CreateDefaultFiles();
	}
	g_fs.onAnyWriteOrDelete = [] { g_fsDirty = true; };
	audio::set_song_hook([](const char *name) {   // SoundManager.PlaySong / QueueSong: UnlockTrack
		if (unlocks().IsTrack(name)) unlocks().UnlockTrack(name);
	});
	// Taskbar (SetTaskbarSizeFromScreenSize)
	g_taskArea = Rect(0, screen().y - 30, screen().x, 30);
	g_taskButtonW = (g_taskArea.w - 26 - text_w("00:00 PM")) / 6 - 4;
	g_minimizeAll.reset(new WindowButton(WinButtonType::MinimizeAll, Vec2(screen().x - 16 - 6, screen().y - 30 + 2 + 6), [] { minimize_all(); }));
	// InitDesktop
	g_desktop.reset(new DesktopGrid(&g_fs, Vec2(screen().x, screen().y - 30)));
	g_desktop->onBeginDrag = [](DraggedItem *d) { begin_drag(d); };
	g_desktop->createModal = [](int type, const std::string &msg) { show_system_modal(type, msg); };
	g_desktop->onDesktopChange = [] { save_desktop_and_fs(); };
	if (freshFs) {
		// No file system yet. The PC's desktop tutorial would build it; that
		// isn't in, so it starts as the tutorial leaves it.
		g_fs.CreateDocumentsFolder();
		g_fs.CreateOneshotFile();
		g_fs.CreateTutorialCompleteFiles(unlocks().wallpapers, unlocks().themes);
		g_desktop->MoveDocumentsToTopLeft();
		// A playthrough saved before the file system existed (builds before
		// m14) keeps its save: the save's file goes where MakeSave puts it.
		bool haveSave = sys::save_exists("save.dat");
		if (haveSave) {
			auto f = make_file("save", "fakesave_filename", PROG_DOCUMENT, { "save_progress_text" });
			f->moveRestricted = true;
			g_fs.WriteFile(Filesystem::MYGAMESONESHOT, f);
		}
	}
	bool haveDesktop = load_desktop();   // the icons' places, unlocks, theme
	if (freshFs) sys::log("twm: new file system%s\n", haveDesktop ? " (desktop.dat kept)" : "");
	if (!haveDesktop) g_desktop->SetWallpaper("default");
	save_desktop_and_fs();
	cursor().SetMousePos(Vec2(screen().x / 2, screen().y / 2));
	window().desktopMode = true;
	window().onClosed = [] {
		if (GameWindow *g = game_window())
			if (g->onClose) g->onClose(g);
	};
	window().onOpenedForDebug = [] {
		if (!game_window()) add_window_(new GameWindow());
	};
	g_ready = true;
	if (const char *o = getenv("OSPS3_OPEN"))   // host testing: open an app at start
		if (strcmp(o, "customize") == 0) add_window_(new CustomizeWindow());
	if (const char *cp = getenv("OSPS3_CURSOR")) {   // host testing: "x,y" in desktop pixels
		int x = 0, y = 0;
		if (sscanf(cp, "%d,%d", &x, &y) == 2) cursor().SetMousePos(Vec2(x, y));
	}
	sys::log("desktop: ready (%dx%d desktop drawn at %dx%d, theme %s) in %.0f ms\n", screen().x, screen().y, space().x,
	         space().y, g_themeId.c_str(), (sys::time_us() - t0) / 1000.0);
}

// ===================================================================== Customize services

bool game_window_open() { return game_window() != nullptr; }
int brightness() { return g_gamma; }
void set_brightness(int v) { g_gamma = v < 50 ? 50 : v > 150 ? 150 : v; save_desktop(); }
int bgm_volume() { return g_bgmVol; }
int sfx_volume() { return g_sfxVol; }
void set_volumes(int bgm, int sfx) {
	g_bgmVol = bgm;
	g_sfxVol = sfx;
	audio::set_volumes(bgm, sfx);
	save_desktop();
}
void hide_all_ui() { g_hideUI = true; }

void request_restart(int kind) {
	// WindowManager.ShowLanguageSelect: fade out, then restart.
	fade(128, 255, 120, [kind] {
		for (auto &w : g_windows)
			if (w->onClose) w->onClose(w.get());
		g_restartRequest = kind;
	});
}

// HueShader's "gamma": every colour times Brightness/100, done here as one
// pass over the finished frame.
static void draw_brightness() {
	if (g_gamma == 100) return;
	gfx::Shade sh;
	gfx::Blend b = gfx::Blend::Multiply;
	float f = g_gamma / 100.0f;
	if (f > 1.0f) { b = gfx::Blend::Brighten; f -= 1.0f; }
	sh.r = sh.g = sh.b = f;
	gfx::clip_off();
	gfx::draw(gm::white(), 0, 0, (float)gfx::screen_w(), (float)gfx::screen_h(), 0, 0, 1, 1, sh, b);
}

// ===================================================================== update

static void niko_update() {
	if (!g_niko) return;
	Niko &n = *g_niko;
	if (!n.moved) { n.pos.y++; n.moved = true; }
	else n.moved = false;
	if (++n.timer >= 16) {
		n.timer = 0;
		n.frame = (n.frame + 1) % 4;
	}
	if (n.pos.y > screen().y) g_niko.reset();
}

static void niko_draw() {
	if (!g_niko) return;
	static const int frames[4] = { 1, 2, 3, 2 };
	Niko &n = *g_niko;
	char name[64];
	int f = frames[n.frame];
	if (n.solstice) {
		snprintf(name, sizeof(name), "the_world_machine/journal/niko/niko_finale%d", f);
		tex(name, n.pos, Rect(), Color::White());
		return;
	}
	snprintf(name, sizeof(name), "the_world_machine/journal/niko/%s%d", n.entity ? "en" : "niko", f);
	tex(name, n.pos, Rect(), Color::White());
	snprintf(name, sizeof(name), "the_world_machine/journal/niko/niko_lightmap%d", f);
	tex_add(name, Vec2(n.pos.x * 2, n.pos.y * 2), Rect(), Color::White(), 1);
}

static void update() {
	if (!g_ready) return;
	cursor().Update();
	if (g_hideUI) {
		// HideAllUI: any button or a click brings the desktop back.
		using namespace input;
		input::suppress_game(false);   // read the buttons, then keep them from the game
		if (pressed(BTN_OK) || pressed(BTN_CANCEL) || pressed(BTN_INVENTORY) || pressed(BTN_MAINMENU) || pressed(BTN_RUN) ||
		    cursor().Clicked())
			g_hideUI = false;
		input::suppress_game(true);
		g_toasts.Update();
		write_saves();
		return;
	}
	if (g_flashTimer > 0) g_flashTimer--;
	GameWindow *gw = game_window();
	// SuppressOneshotwindowInputs: the game ignores the pad while its window
	// is minimized, a system dialog is up or the screen is fading.
	input::suppress_game(gw && (gw->Minimized() || g_systemModal || g_fading));
	// The dialog the pad answers: a system one, else the game's (the game
	// has the pad even when its window isn't in front), else the front
	// window's.
	ModalWindow::padOwner = nullptr;
	if (g_systemModal) ModalWindow::padOwner = g_systemModal.get();
	else if (gw && gw->IsModalOpen()) ModalWindow::padOwner = static_cast<ModalWindow *>(gw->Modal());
	else if (!g_windows.empty() && g_windows[0]->IsModalOpen()) ModalWindow::padOwner = static_cast<ModalWindow *>(g_windows[0]->Modal());
	bool dropping = g_drag && !cursor().Held();
	bool anyMax = false;
	for (auto &w : g_windows) anyMax |= w->Maximized();
	if (g_systemModal) g_systemModal->Update(false);
	if (g_fading) {
		g_fadeTimer++;
		float t = (float)g_fadeTimer / g_fadeTotal;
		int d = g_fadeEnd - g_fadeStart, step = (int)(d * t);
		if (step == 0) step = d >= 0 ? 1 : -1;
		g_fadeOpacity = d == 0 ? g_fadeEnd : g_fadeStart + step;
		if (g_fadeTimer >= g_fadeTotal) {
			g_fading = false;
			if (g_onFadeDone) g_onFadeDone();
		}
	}
	bool consumed = g_fading || anyMax || g_systemModal;
	consumed |= taskbar_update(consumed);
	if (consumed && dropping) { cancel_drag(); dropping = false; }
	std::vector<Window *> order;
	for (auto &w : g_windows) order.push_back(w.get());
	for (Window *w : order) {
		if (std::find(g_pendingRemoval.begin(), g_pendingRemoval.end(), w) != g_pendingRemoval.end()) continue;
		consumed |= w->Update(consumed);
		if (consumed && dropping) {
			w->OnDragDropped(g_drag.get(), cursor().MousePos());
			g_drag.reset();
			dropping = false;
		}
	}
	g_desktop->Update(Vec2(0, 0), consumed);
	if (dropping) {
		g_desktop->OnDragDropped(g_drag.get(), cursor().MousePos());
		g_drag.reset();
	}
	for (Window *w : g_pendingRemoval) remove_window(w);
	g_pendingRemoval.clear();
	g_createdThisFrame = false;
	niko_update();
	if (g_drag) cursor().SetState(MouseCursor::Holding);
	g_toasts.Update();
	update_themes();
	flush_deletes();
	write_saves();
}

// ===================================================================== draw

static void draw() {
	if (!g_ready) return;
	begin();
	const Theme &th = theme();
	if (g_hideUI) {
		g_desktop->DrawDesktop(th, Rect(), false);
		end();
		return;
	}
	Window *maxed = nullptr;
	for (auto &w : g_windows)
		if (w->Maximized()) { maxed = w.get(); break; }
	if (!maxed) {
		g_gameRect = Rect();
		if (GameWindow *gw = game_window())
			if (!gw->Minimized() && gw->MinimizedFrames() == 0)
				g_gameRect = Rect((gw->pos.x + 2) * 2, (gw->pos.y + 26) * 2, 640, 480);
		g_desktop->DrawDesktop(th, g_gameRect);
		for (int i = (int)g_windows.size() - 1; i >= 0; i--) g_windows[i]->Draw(th);
		g_toasts.Draw(th, screen());
		Window *active = nullptr;
		for (auto &w : g_windows)
			if (!w->Minimized()) { active = w.get(); break; }
		draw_taskbar(active);
		if (g_fadeOpacity) box(Rect(0, 0, screen().x, screen().y), Color(0, 0, 0, g_fadeOpacity));
		if (g_drag) g_drag->Draw(th, cursor().MousePos());
	} else {
		maxed->Draw(th);
		g_toasts.Draw(th, screen());
	}
	if (g_systemModal) {
		box(Rect(0, 0, screen().x, screen().y), Color(0, 0, 0, 128));
		g_systemModal->Draw(th);
	}
	niko_draw();
	if (g_flashTimer > 0 && g_flashDuration > 0) {
		Color c = g_flashColor;
		c.a = (uint8_t)(g_flashStrength * ((float)g_flashTimer / g_flashDuration) * 255);
		box(Rect(0, 0, screen().x, screen().y), c);
	}
	end();
	prof::mark("desktop draw");
}

} // namespace twm

// ===================================================================== desktop.h

namespace desktop {

void init() { twm::init(); }
bool ready() { return twm::g_ready; }
void update() {
	twm::update();
}
void draw() { twm::draw(); }
void draw_cursor() {
	if (twm::g_ready && !twm::g_hideUI) twm::cursor().Draw();
	twm::draw_brightness();
}

int take_restart_request() {
	int r = twm::g_restartRequest;
	twm::g_restartRequest = 0;
	return r;
}

void erase_all_data() {
	using namespace twm;
	static const char *files[] = { "save.dat", "p-settings.dat", "desktop.dat", "fs.dat" };
	for (const char *f : files)
		if (sys::save_exists(f) && !sys::save_delete(f)) sys::log("erase: couldn't delete %s\n", f);
	g_fsDirty = g_desktopDirty = false;
	g_fs = Filesystem();
	delete g_unlocks;
	g_unlocks = nullptr;
	g_desktop.reset();
	g_themeId = "purple";
	g_hideUI = false;
	g_fadeOpacity = 0;
	g_fading = false;
	g_ready = false;
	sys::log("erase: all data erased (language and settings kept)\n");
}
bool quit_requested() { return twm::g_quit; }
bool game_window_open() { return twm::game_window() != nullptr; }

void ensure_game_window(bool maximized) {
	if (!twm::g_ready) twm::init();
	twm::GameWindow *g = twm::game_window();
	if (!g) {
		twm::add_window_(new twm::GameWindow());
		g = twm::game_window();
	}
	if (!g) return;
	if (!window().IsOpen()) window().Launch(false);
	if (maximized && !g->Maximized()) g->ToggleMaximize();
}

void close_all() {
	using namespace twm;
	cancel_drag();
	if (g_systemModal) defer_delete(g_systemModal.release());
	for (auto &w : g_windows) g_pendingRemoval.push_back(w.get());
	for (Window *w : g_pendingRemoval) remove_window(w);
	g_pendingRemoval.clear();
	g_taskButtons.clear();
	g_previousActive.clear();
	g_hasPreviousActive = false;
	g_niko.reset();
	DocumentWindow::ClearRandomLetters();
	flush_deletes();
}

} // namespace desktop
