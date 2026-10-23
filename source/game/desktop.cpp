#include "desktop.h"
#include "loc.h"
#include "modal.h"
#include "mouse_cursor.h"
#include "oneshot_window.h"
#include "settings.h"
#include "../engine/audio.h"
#include "../engine/font.h"
#include "../engine/gm.h"
#include "../engine/util.h"
#include "../platform/input.h"
#include "../platform/sys.h"
#include "json_file.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace desktop {

// ===================================================================== basics

struct Color {
	uint8_t r = 255, g = 255, b = 255, a = 255;
	Color() = default;
	Color(int r_, int g_, int b_, int a_ = 255) : r((uint8_t)r_), g((uint8_t)g_), b((uint8_t)b_), a((uint8_t)a_) {}
	Color withA(int alpha) const { Color c = *this; c.a = (uint8_t)alpha; return c; }
};

// TWMTheme (gamedata/twm/themes_metadata.json).
struct Theme {
	std::string id = "purple";
	Color primary = Color(150, 100, 255), variant = Color(100, 66, 165), background = Color(0, 0, 0);
	Color Primary(int a = 255) const { return primary.withA(a); }
	Color Variant(int a = 255) const { return variant.withA(a); }
	Color Background(int a = 255) const { return background.withA(a); }
};

static Vec2 g_space;      // desktop draw space (1280x720)
static Vec2 g_screen;     // WindowManager.ScreenSize: desktop pixels (640x360)
static Theme g_theme;
static std::vector<Theme> g_themes;
static MouseCursor g_cursor;
static bool g_ready, g_quit;
static bool g_systemModal;   // a system dialog (Shut Down, errors) is up

static bool in_rect(const Rect &r, Vec2 v) { return v.x >= r.x && v.x < r.x + r.w && v.y >= r.y && v.y < r.y + r.h; }

// ColorBoxBlit at scale 2 (desktop pixels).
static void box(Rect r, Color c) { gm::color_box(r, c.r, c.g, c.b, c.a, gfx::Blend::Normal, 2); }

// MainBlit at scale 2 with a tint.
static void tex(const char *name, Vec2 pos, Rect src, Color c, int scale = 2) {
	const gfx::Texture *t = gm::texture(name, gm::TheWorldMachine);
	if (!t) return;
	if (src.w <= 0) src = Rect(0, 0, (int)t->w, (int)t->h);
	gm::blit(t, pos, src, c.a / 255.0f, 0, gfx::Blend::Normal, scale, GameTone(), c.r / 255.0f, c.g / 255.0f, c.b / 255.0f);
}

static unsigned rgb(Color c) { return (unsigned)(c.r << 16 | c.g << 8 | c.b); }

// TextBlit(OS, pos, text, colour) at scale 2.
static void text(Vec2 pos, const std::string &s, Color c) {
	font::draw(font::OS, pos, s, rgb(c), c.a / 255.0f, 2, font::Mode::OnlyText);
}
static int text_w(const std::string &s) { return font::width(font::OS, s); }

// MathHelper.Truncate: cut to fit maxLen, ending in "..".
static std::string truncate(const std::string &s, int maxLen) {
	if (text_w(s) < maxLen) return s;
	std::vector<size_t> starts;   // character boundaries
	for (size_t i = 0; i < s.size(); i += font::utf8_len((unsigned char)s[i])) starts.push_back(i);
	for (size_t n = starts.size(); n > 0; n--) {
		std::string t = s.substr(0, starts[n - 1]) + "..";
		if (text_w(t) <= maxLen) return t;
	}
	return "";
}

// MathHelper.EaseOut
static int ease_out(int start, int target, int t, int total) {
	float f = (float)t / total;
	return start + (int)lroundf((target - start) * f * f * f);
}

// ===================================================================== buttons

enum ButtonType { Minimize, Maximize, Close, MinimizeAll, UnminimizeAll };

// TWMWindowButton (IconButton): 16x16, acts on release while still over it.
struct WinButton {
	ButtonType type;
	Vec2 pos;
	std::function<void()> action;
	bool hovering = false, pressed = false;
	WinButton(ButtonType t, std::function<void()> a) : type(t), action(a) {}
	bool Update(Vec2 parent, bool canInteract) {
		if (!canInteract) { hovering = false; return false; }
		Vec2 v = g_cursor.MousePos() - parent;
		hovering = in_rect(Rect(pos.x, pos.y, 16, 16), v);
		if (hovering) g_cursor.SetState(MouseCursor::Clickable);
		if (hovering && g_cursor.Clicked()) pressed = true;
		else if (pressed && !g_cursor.Held()) {
			if (hovering && action) action();
			pressed = false;
		}
		return hovering;
	}
	void Draw(Vec2 parent, const Theme &th, int alpha) const {
		Color c = hovering ? th.Variant(alpha) : th.Primary(alpha);
		Vec2 p(parent.x + pos.x, parent.y + pos.y);
		box(Rect(p.x, p.y, 16, 16), th.Background(alpha));
		tex("the_world_machine/window_buttons", p, Rect(16 * (int)type, 0, 16, 16), c);
	}
};

// ===================================================================== windows

class Window {
public:
	virtual ~Window() {}
	Vec2 pos;
	Vec2 contents;
	std::string icon, title;   // title: a TWM string id
	bool minimized = false, maximized = false;
	int minimizedFrames = 0;
	std::vector<WinButton> buttons;
	std::function<void(Window *)> onClose, onMinimize, grabFocus;

	void AddButton(ButtonType t, std::function<void()> action = nullptr) {
		if (!action) {
			if (t == Close) action = [this] { if (onClose) onClose(this); };
			else if (t == Minimize) action = [this] { if (onMinimize) onMinimize(this); };
		}
		buttons.emplace_back(t, action);
		Recalc();
	}
	void Recalc() {
		int x = contents.x + 2 - 16 - 2;
		for (size_t i = 0; i < buttons.size(); i++) buttons[i].pos = Vec2(x - 18 * (int)i, 4);
	}
	WinButton *Find(ButtonType t) {
		for (auto &b : buttons)
			if (b.type == t) return &b;
		return nullptr;
	}
	void ToggleMinimize() {
		minimized = !minimized;
		if (!minimized && minimizedFrames >= 30) {
			int maxX = g_screen.x - contents.x, maxY = g_screen.y - 26 - 30;
			pos.y = std::max(0, std::min(pos.y, maxY));
			pos.x = std::max(0, std::min(pos.x, maxX));
		}
	}
	virtual void ToggleMaximize() { maximized = !maximized; }

	// TWMWindow.Update: returns true when the cursor is used up by this window.
	virtual bool Update(bool consumed) {
		if (minimized) {
			if (minimizedFrames < 30) minimizedFrames++;
			held = false;
			return false;
		}
		if (minimizedFrames > 0) minimizedFrames--;
		bool canInteract = !consumed;
		bool onButton = false;
		for (auto &b : buttons) onButton |= b.Update(pos, canInteract);
		if (!canInteract) { held = false; return false; }
		if (onButton) return true;
		Rect bar(pos.x + 2, pos.y + 2, contents.x, 20);
		if (held) {
			if (g_cursor.Held()) {
				g_cursor.SetState(MouseCursor::Holding);
				Vec2 m = g_cursor.MousePos();
				pos = pos + (m - heldMouse);
				heldMouse = m;
				return true;
			}
			held = false;
		} else if (!maximized && in_rect(bar, g_cursor.MousePos())) {
			g_cursor.SetState(MouseCursor::Grabbable);
			if (g_cursor.Clicked()) {
				held = true;
				heldMouse = g_cursor.MousePos();
				if (grabFocus) grabFocus(this);
			}
			return true;
		}
		if (in_rect(Rect(pos.x, pos.y, contents.x + 4, contents.y + 28), g_cursor.MousePos())) {
			if (g_cursor.Clicked() && grabFocus) grabFocus(this);
			return true;
		}
		return false;
	}

	virtual void Draw(const Theme &th) {
		if (minimizedFrames >= 30) return;
		Vec2 p = pos;
		if (minimizedFrames > 0) {   // sliding down to the taskbar
			p.y = ease_out(pos.y, g_screen.y, minimizedFrames, 30);
		}
		box(Rect(p.x, p.y, contents.x + 4, contents.y + 28), th.Primary());
		box(Rect(p.x + 2, p.y + 2, contents.x, 20), th.Background());
		// Title: the icon, then the name (GenerateTitleTexture), in the
		// theme's colour.
		int maxLen = contents.x - 24 - (int)buttons.size() * 18;
		if (!icon.empty()) tex(("the_world_machine/window_icons/" + icon).c_str(), Vec2(p.x + 4, p.y + 4), Rect(), th.Primary());
		text(Vec2(p.x + 24, p.y + 3), truncate(loc::twm(title.c_str()), maxLen), th.Primary());
		for (auto &b : buttons) b.Draw(p, th, 255);
		DrawContents(th, Vec2(p.x + 2, p.y + 26));
	}
	virtual void DrawContents(const Theme &th, Vec2 screenPos) = 0;
	virtual bool IsGame() const { return false; }

protected:
	bool held = false;
	Vec2 heldMouse;
};

// Draws the 640x480 game window with its top-left at TV (x, y), scale TV
// pixels per window pixel (OneshotWindow.PreDraw + DrawOneshotTexture).
static void draw_game_at(float x, float y, float scale) {
	gm::end_space();
	gm::set_window_placement(x, y, scale);
	gm::set_scaling(settings().scaling);
	gm::begin_window();
	gm::clip_to_window();
	window().Draw();
	gm::clip_off();
	gm::end_window();
	gm::begin_space(g_space);
}

// OneshotWindow (the TWM window around the game).
class GameWindow : public Window {
public:
	GameWindow() {
		icon = "oneshot";
		title = "oneshot_appname";
		contents = Vec2(320, 240);
		AddButton(Close, [] { window().CloseGame(); });
		AddButton(Maximize, [this] {
			if (!window().State().flag(123)) ToggleMaximize();   // flag 123: no fullscreen during that scene
		});
		AddButton(Minimize);
	}
	bool IsGame() const override { return true; }
	void ToggleMaximize() override {
		Window::ToggleMaximize();
		WinButton *b = Find(Maximize);
		if (maximized) oldMaxPos = b->pos;
		else b->pos = oldMaxPos;
	}
	bool Update(bool consumed) override {
		if (!maximized) return Window::Update(consumed);
		// Maximized: only the maximize button, in the screen's top-right corner.
		WinButton *b = Find(Maximize);
		b->pos = Vec2(g_screen.x - 2 - 16, 2);
		(void)consumed;   // (every other window is blocked while one is maximized; this button isn't)
		return b->Update(Vec2(0, 0), !g_systemModal);
	}
	// GetFullscreenRect, in desktop draw-space pixels.
	Rect FullscreenRect() const {
		int W = g_space.x, H = g_space.y;
		Rect r;
		if (W * 3 >= H * 4) r = Rect(0, 0, 320 * H / 240, H);
		else r = Rect(0, 0, W, 240 * W / 320);
		r.x = (W - r.w) / 2;
		r.y = (H - r.h) / 2;
		return r;
	}
	void Draw(const Theme &th) override {
		if (!maximized) { Window::Draw(th); return; }
		box(Rect(0, 0, g_screen.x, g_screen.y), Color(0, 0, 0));
		Rect fr = FullscreenRect();
		float k;
		Vec2 tv = gm::space_to_tv(g_space, (float)fr.x, (float)fr.y, &k);
		draw_game_at((float)tv.x, (float)tv.y, k * fr.h / 480.0f);
		Find(Maximize)->Draw(Vec2(0, 0), th, 255);
	}
	void DrawContents(const Theme &, Vec2 screenPos) override {
		float k;
		Vec2 tv = gm::space_to_tv(g_space, (float)screenPos.x * 2, (float)screenPos.y * 2, &k);
		draw_game_at((float)tv.x, (float)tv.y, k);
	}
private:
	Vec2 oldMaxPos;
};

// ===================================================================== desktop icons

enum Program { PROG_ONESHOT, PROG_SHUTDOWN };

struct FileNode {
	std::string icon, name;   // name: a TWM string id
	Program program;
};

static std::vector<FileNode> g_files;
// TWMDesktopManager / IconGrid: icon slots, column by column.
static const int GRID_W = 84, GRID_H = 72;
static std::vector<int> g_grid;   // cols * rows, index into g_files or -1
static int g_cols, g_rows;
static int g_framesSinceClick = 30, g_clickedIcon = -1;   // clickedIcon: grid slot being pressed
static Vec2 g_clickGridPos(-1, -1), g_clickPos;
static int g_heldSlot = -1;     // slot of the icon being dragged
static bool g_cursorWasFree;

static Vec2 grid_position(int gx, int gy) { return Vec2(gx * GRID_W + 4, gy * GRID_H + 4); }
static int &slot(int gx, int gy) { return g_grid[gx * g_rows + gy]; }

struct IconText { std::string l1, l2; Rect r1, r2; int height; };

// FileIcon.processFileName: one or two lines under the icon, centred.
static IconText icon_text(const FileNode &f) {
	IconText t;
	std::string name = loc::twm(f.name.c_str());
	std::vector<std::string> lines = font::word_wrap(font::OS, name, 76);
	if (lines.size() <= 1) {
		t.l1 = truncate(name, 76);
		int w = text_w(t.l1);
		t.r1 = Rect((76 - w) / 2 + 4, 36, w + 2, 12);
		t.height = 12;
	} else {
		t.l1 = truncate(lines[0], 76);
		while (!t.l1.empty() && t.l1.back() == ' ') t.l1.pop_back();
		int w = text_w(t.l1);
		t.r1 = Rect((76 - w) / 2 + 4, 36, w + 2, 12);
		t.l2 = truncate(lines[1], 76);
		w = text_w(t.l2);
		t.r2 = Rect((76 - w) / 2 + 4, 48, w + 2, 12);
		t.height = 24;
	}
	return t;
}

static Rect click_area(const FileNode &f, Vec2 p) { return Rect(p.x + 2, p.y + 2, 80, 36 + icon_text(f).height + 2 + 4); }

static void draw_icon(const FileNode &f, Vec2 p, bool focus, bool canHover, float alpha) {
	const Theme &th = g_theme;
	Color bg = th.Background(), frame = focus ? th.Primary() : th.Variant(), fg = th.Primary(), shadow = th.Background();
	if (alpha != 1.0f) fg.a = frame.a = bg.a = (uint8_t)(255 * alpha);
	Rect area = click_area(f, p);
	if (canHover && in_rect(area, g_cursor.MousePos())) {
		focus = true;
		g_cursor.SetState(MouseCursor::Clickable);
	}
	if (focus) {
		frame.a /= 2;
		bg.a /= 2;
		box(area, frame);
		box(Rect(area.x + 2, area.y + 2, area.w - 4, area.h - 4), bg);
	}
	std::string path = "the_world_machine/desktop_icons/" + f.icon;
	tex(path.c_str(), Vec2(p.x + 27, p.y + 7), Rect(), shadow);
	tex(path.c_str(), Vec2(p.x + 26, p.y + 6), Rect(), fg);
	IconText t = icon_text(f);
	Rect r1(t.r1.x + p.x - 1, t.r1.y + p.y + 4, t.r1.w, t.r1.h);
	box(r1, Color(0, 0, 0));
	// GetSingleLineTexture(OS) puts the text 2 right and 4 up in its texture.
	text(Vec2(r1.x - 1 + 2, r1.y - 4), t.l1, fg);
	if (!t.l2.empty()) {
		Rect r2(t.r2.x + p.x - 1, t.r2.y + p.y + 4, t.r2.w, t.r2.h);
		box(r2, Color(0, 0, 0));
		text(Vec2(r2.x - 1 + 2, r2.y - 4), t.l2, fg);
	}
}

static Vec2 point_to_grid(Vec2 p) {
	return Vec2(std::min(std::max(p.x, 0) / GRID_W, g_cols - 1), std::min(std::max(p.y, 0) / GRID_H, g_rows - 1));
}

// ===================================================================== window manager state

static std::vector<std::unique_ptr<Window>> g_windows;   // [0] = front
static std::vector<Window *> g_pendingRemoval;
static bool g_createdThisFrame;
static std::vector<Window *> g_previousActive;

// Taskbar
struct TaskButton { int w; Window *window; };
static std::vector<TaskButton> g_taskButtons;
static int g_taskButtonW = 100;
static Rect g_taskArea;
static std::unique_ptr<WinButton> g_minimizeAll;

// Fades (shutdown)
static bool g_fading;
static int g_fadeTimer, g_fadeTotal;
static uint8_t g_fadeStart, g_fadeEnd, g_fadeOpacity;
static std::function<void()> g_onFadeDone;

// System dialogs (shutdown question / errors) use the shared modal.
static std::function<void(modal::Response)> g_modalCallback;

static void show_system_modal(modal::Type t, const char *key, std::function<void(modal::Response)> cb = nullptr) {
	modal::show(t, loc::twm(key));
	g_systemModal = true;
	g_modalCallback = cb;
}

static void fade(uint8_t from, uint8_t to, int frames, std::function<void()> done) {
	g_fading = true;
	g_fadeTimer = 0;
	g_fadeTotal = frames;
	g_fadeOpacity = g_fadeStart = from;
	g_fadeEnd = to;
	g_onFadeDone = done;
}

static GameWindow *game_window() {
	for (auto &w : g_windows)
		if (w->IsGame()) return static_cast<GameWindow *>(w.get());
	return nullptr;
}

static void bring_to_front(Window *w) {
	for (size_t i = 0; i < g_windows.size(); i++)
		if (g_windows[i].get() == w && i != 0) {
			std::unique_ptr<Window> p = std::move(g_windows[i]);
			g_windows.erase(g_windows.begin() + i);
			g_windows.insert(g_windows.begin(), std::move(p));
			return;
		}
}

static void toggle_minimize(Window *w) {
	w->ToggleMinimize();
	if (!w->minimized) bring_to_front(w);
}

static void place_window(Window *w) {
	Vec2 p((g_screen.x - w->contents.x) / 2 - 2, (g_screen.y - w->contents.y - 26 - 30) / 2);
	p.x = std::max(p.x, 0);
	p.y = std::max(p.y, 0);
	for (bool again = true; again;) {
		again = false;
		for (auto &o : g_windows)
			if (iabs(p.x - o->pos.x) < 12 || iabs(p.y - o->pos.y) < 12) { p.x += 13; p.y += 13; again = true; break; }
	}
	w->pos = p;
}

static void add_window(Window *raw) {
	std::unique_ptr<Window> w(raw);
	if (g_taskButtons.size() >= 6) {
		show_system_modal(modal::Type::Error, "error_too_many_windows");
		return;
	}
	place_window(raw);
	raw->onMinimize = [](Window *x) { toggle_minimize(x); };
	auto prevClose = raw->onClose;
	raw->onClose = [prevClose](Window *x) {
		if (prevClose) prevClose(x);
		g_pendingRemoval.push_back(x);
	};
	raw->grabFocus = [](Window *x) { if (!g_createdThisFrame) bring_to_front(x); };
	g_taskButtons.push_back({ g_taskButtonW, raw });
	g_windows.insert(g_windows.begin(), std::move(w));
	g_createdThisFrame = true;
}

static void remove_window(Window *w) {
	for (auto &b : g_taskButtons)
		if (b.window == w) b.window = nullptr;   // its button shrinks away
	for (size_t i = 0; i < g_windows.size(); i++)
		if (g_windows[i].get() == w) { g_windows.erase(g_windows.begin() + i); break; }
	g_previousActive.erase(std::remove(g_previousActive.begin(), g_previousActive.end(), w), g_previousActive.end());
}

static void minimize_all() {
	std::vector<Window *> open;
	for (auto &w : g_windows)
		if (!w->minimized) open.push_back(w.get());
	if (!open.empty()) {
		for (Window *w : open) toggle_minimize(w);
		g_previousActive = open;
		return;
	}
	std::reverse(g_previousActive.begin(), g_previousActive.end());
	for (Window *w : g_previousActive) toggle_minimize(w);
	g_previousActive.clear();
}

static bool unminimize_enabled() {
	for (auto &w : g_windows)
		if (!w->minimized) return false;
	return !g_previousActive.empty();
}

// ===================================================================== saving (desktop.dat)

static void save_desktop() {
	cJSON *o = cJSON_CreateObject();
	cJSON *pos = cJSON_AddObjectToObject(o, "iconPositions");
	for (int x = 0; x < g_cols; x++)
		for (int y = 0; y < g_rows; y++)
			if (slot(x, y) >= 0) {
				cJSON *v = cJSON_AddObjectToObject(pos, g_files[slot(x, y)].name.c_str());
				cJSON_AddNumberToObject(v, "X", x);
				cJSON_AddNumberToObject(v, "Y", y);
			}
	cJSON_AddStringToObject(o, "currentWallpaper", "default");
	cJSON_AddStringToObject(o, "currentTheme", g_theme.id.c_str());
	cJSON_AddBoolToObject(o, "tutorialCompleted", 1);
	char *s = cJSON_PrintUnformatted(o);
	if (s) {
		if (!sys::save_write("desktop.dat", s)) sys::log("desktop: couldn't write desktop.dat\n");
		free(s);
	}
	cJSON_Delete(o);
}

static void load_desktop() {
	std::string text;
	if (!sys::save_read("desktop.dat", text)) return;
	cJSON *o = cJSON_Parse(text.c_str());
	if (!o) { sys::log("desktop: desktop.dat is damaged, using the default layout\n"); return; }
	const cJSON *th = cJSON_GetObjectItemCaseSensitive(o, "currentTheme");
	if (cJSON_IsString(th))
		for (auto &t : g_themes)
			if (t.id == th->valuestring) g_theme = t;
	const cJSON *pos = cJSON_GetObjectItemCaseSensitive(o, "iconPositions");
	const cJSON *it;
	cJSON_ArrayForEach(it, pos) {
		int x = (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(it, "X"));
		int y = (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(it, "Y"));
		if (x < 0 || y < 0 || x >= g_cols || y >= g_rows) continue;
		// SetLayout: swap the named icon into its saved slot.
		for (int i = 0; i < g_cols * g_rows; i++)
			if (g_grid[i] >= 0 && g_files[g_grid[i]].name == it->string) {
				std::swap(g_grid[i], slot(x, y));
				break;
			}
	}
	cJSON_Delete(o);
}

// ===================================================================== init

static void load_themes() {
	g_themes.clear();
	{
		if (cJSON *o = parse_json_file("gamedata/twm/themes_metadata.json")) {
			const cJSON *it;
			cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(o, "themes")) {
				Theme t;
				const cJSON *id = cJSON_GetObjectItemCaseSensitive(it, "id");
				if (!cJSON_IsString(id)) continue;
				t.id = id->valuestring;
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
				g_themes.push_back(t);
			}
			cJSON_Delete(o);
		}
	}
	if (g_themes.empty()) sys::log("desktop: gamedata/twm/themes_metadata.json missing, using the purple theme\n");
	g_theme = Theme();
	for (auto &t : g_themes)
		if (t.id == "purple") g_theme = t;
}

void init() {
	if (g_ready) return;
	uint64_t t0 = sys::time_us();
	g_space = gm::boot_space();
	g_screen = Vec2(g_space.x / 2, g_space.y / 2);
	load_themes();
	// The desktop's files (TWMFilesystem after the tutorial): the OneShot
	// launcher and Shut Down. More apps arrive in later stages.
	g_files = { { "oneshot", "oneshot_appname", PROG_ONESHOT }, { "exit", "shutdown_app_filename", PROG_SHUTDOWN } };
	// SetIconArea: the screen minus the taskbar.
	g_cols = (g_screen.x - 8) / GRID_W;
	g_rows = (g_screen.y - 30 - 8) / GRID_H;
	g_grid.assign(g_cols * g_rows, -1);
	for (size_t i = 0; i < g_files.size() && (int)i < g_cols * g_rows; i++) g_grid[i] = (int)i;   // down the first column
	load_desktop();
	// Taskbar (SetTaskbarSizeFromScreenSize)
	g_taskArea = Rect(0, g_screen.y - 30, g_screen.x, 30);
	g_taskButtonW = (g_taskArea.w - 26 - text_w("00:00 PM")) / 6 - 4;
	g_minimizeAll.reset(new WinButton(MinimizeAll, [] { minimize_all(); }));
	g_minimizeAll->pos = Vec2(g_screen.x - 16 - 6, g_screen.y - 30 + 2 + 6);
	g_cursor.SetMousePos(Vec2(g_screen.x / 2, g_screen.y / 2));
	gm::texture("the_world_machine/wallpaper/default", gm::DesktopWallpaper);
	window().desktopMode = true;
	window().onClosed = [] {
		if (GameWindow *g = game_window()) g_pendingRemoval.push_back(g);
	};
	window().onOpenedForDebug = [] {
		if (!game_window()) add_window(new GameWindow());
	};
	g_ready = true;
	sys::log("desktop: ready (%dx%d desktop, %dx%d icon grid, theme %s) in %.0f ms\n", g_screen.x, g_screen.y, g_cols, g_rows,
	         g_theme.id.c_str(), (sys::time_us() - t0) / 1000.0);
}

bool ready() { return g_ready; }
bool quit_requested() { return g_quit; }
bool game_window_open() { return game_window() != nullptr; }

static void run_file(const FileNode &f) {
	switch (f.program) {
	case PROG_ONESHOT:
		if (GameWindow *g = game_window()) {   // AddWindow: already open -> bring it up
			if (g->minimized) toggle_minimize(g);
			else bring_to_front(g);
			g_createdThisFrame = true;
			return;
		}
		if (g_taskButtons.size() >= 6) { show_system_modal(modal::Type::Error, "error_too_many_windows"); return; }
		sys::log("desktop: launching OneShot\n");
		add_window(new GameWindow());
		window().Launch(true);
		break;
	case PROG_SHUTDOWN:
		if (game_window()) {
			show_system_modal(modal::Type::Error, "cant_shutdown_while_oneshot_running");
			break;
		}
		show_system_modal(modal::Type::YesNo, "do_you_really_want_to_shut_down", [](modal::Response r) {
			if (r != modal::Response::Yes) return;
			save_desktop();
			fade(128, 255, 120, [] { g_quit = true; });
		});
		break;
	}
}

void ensure_game_window(bool maximized) {
	if (!g_ready) init();
	GameWindow *g = game_window();
	if (!g) {
		add_window(new GameWindow());
		g = game_window();
	}
	if (!window().IsOpen()) window().Launch(false);
	if (maximized && !g->maximized) g->ToggleMaximize();
}

void close_all() {
	for (auto &w : g_windows) g_pendingRemoval.push_back(w.get());
	for (Window *w : g_pendingRemoval) remove_window(w);
	g_pendingRemoval.clear();
	g_taskButtons.clear();
	g_previousActive.clear();
}

// ===================================================================== update

static void icons_update(bool consumed) {
	g_cursorWasFree = !consumed;
	if (g_framesSinceClick <= 30) g_framesSinceClick++;
	Vec2 m = g_cursor.MousePos();
	if (g_clickedIcon >= 0) {
		if (g_cursor.Held()) {
			if (iabs(m.x - g_clickPos.x) > 3 || iabs(m.y - g_clickPos.y) > 3) {
				g_heldSlot = g_clickedIcon;   // start dragging it
				g_clickedIcon = -1;
			}
		} else {
			g_clickedIcon = -1;
		}
		return;
	}
	if (consumed || !g_cursor.Clicked()) return;
	if (!in_rect(Rect(0, 0, g_screen.x, g_screen.y - 30), m)) return;
	Vec2 gp(std::min(m.x / GRID_W, g_cols - 1), std::min(m.y / GRID_H, g_rows - 1));
	int f = slot(gp.x, gp.y);
	if (f >= 0 && in_rect(click_area(g_files[f], grid_position(gp.x, gp.y)), m)) {
		if (g_framesSinceClick < 30 && gp == g_clickGridPos) {   // double click
			g_framesSinceClick = 30;
			run_file(g_files[f]);
		} else {
			g_framesSinceClick = 0;
			g_clickedIcon = gp.x * g_rows + gp.y;
			g_clickGridPos = gp;
			g_clickPos = m;
		}
		return;
	}
	g_clickGridPos = Vec2(-1, -1);
}

// A dragged icon let go: on the desktop it swaps places (TWMDesktopManager.
// OnDragDropped); anywhere else (a window, the taskbar) it goes back.
static void drop_icon(bool overSomething) {
	int from = g_heldSlot;
	g_heldSlot = -1;
	if (overSomething) return;
	Vec2 to = point_to_grid(g_cursor.MousePos());
	std::swap(g_grid[from], slot(to.x, to.y));
	g_clickGridPos = Vec2(-1, -1);
	save_desktop();
}

static bool taskbar_update(bool consumed) {
	Vec2 m = g_cursor.MousePos();
	if (!consumed) {
		Rect r(2, g_taskArea.y + 2, 0, 24);
		for (auto &b : g_taskButtons) {
			r.w = b.w;
			if (in_rect(r, m)) {
				g_cursor.SetState(MouseCursor::Clickable);
				if (g_cursor.Clicked() && b.window) {
					// OnTaskbarSelect
					Window *w = b.window;
					Window *active = nullptr;
					for (auto &x : g_windows)
						if (!x->minimized) { active = x.get(); break; }
					if (w->minimized) { w->ToggleMinimize(); bring_to_front(w); }
					else if (active == w) w->ToggleMinimize();
					else bring_to_front(w);
				}
			}
			r.x += r.w + 2;
		}
		g_minimizeAll->type = unminimize_enabled() ? UnminimizeAll : MinimizeAll;
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

void update() {
	if (!g_ready) return;
	g_cursor.Update();
	// System dialog answered?
	if (g_systemModal && !modal::is_open()) {
		g_systemModal = false;
		auto cb = g_modalCallback;
		g_modalCallback = nullptr;
		if (cb) cb(modal::last_response());
	}
	GameWindow *gw = game_window();
	// SuppressOneshotwindowInputs: the game ignores the pad while its window
	// is minimized, a system dialog is up or the screen is fading.
	input::suppress_game(gw && (gw->minimized || g_systemModal || g_fading));
	bool dropping = g_heldSlot >= 0 && !g_cursor.Held();
	bool anyMax = false;
	for (auto &w : g_windows) anyMax |= w->maximized;
	if (g_fading) {
		g_fadeTimer++;
		float t = (float)g_fadeTimer / g_fadeTotal;
		g_fadeOpacity = (uint8_t)(g_fadeStart + (g_fadeEnd - g_fadeStart) * (t > 1 ? 1 : t));
		if (g_fadeTimer >= g_fadeTotal) {
			g_fading = false;
			if (g_onFadeDone) g_onFadeDone();
		}
	}
	bool consumed = g_fading || anyMax || g_systemModal;
	consumed |= taskbar_update(consumed);
	if (consumed && dropping) { drop_icon(true); dropping = false; }
	// The game runs inside its window (OneshotWindow.Update -> GameLogic),
	// even while minimized; system dialogs pause it.
	if (gw && window().IsOpen() && !g_systemModal) {
		window().Update();
		prof::mark("game update");
	}
	std::vector<Window *> order;
	for (auto &w : g_windows) order.push_back(w.get());
	for (Window *w : order) {
		consumed |= w->Update(consumed);
		if (consumed && dropping) { drop_icon(true); dropping = false; }
	}
	icons_update(consumed);
	if (dropping) drop_icon(false);
	for (Window *w : g_pendingRemoval) remove_window(w);
	g_pendingRemoval.clear();
	g_createdThisFrame = false;
	if (g_heldSlot >= 0) g_cursor.SetState(MouseCursor::Holding);
}

// ===================================================================== draw

static void draw_wallpaper() {
	// The default wallpaper (STRETCH): the whole screen, smoothly scaled.
	const gfx::Texture *t = gm::texture("the_world_machine/wallpaper/default", gm::DesktopWallpaper);
	if (!t) { box(Rect(0, 0, g_screen.x, g_screen.y), Color(0, 0, 0)); return; }
	const_cast<gfx::Texture *>(t)->linear = true;
	gm::blit_scaled(t, Vec2(0, 0), Rect(0, 0, (int)t->w, (int)t->h), (float)g_space.x / t->w, (float)g_space.y / t->h);
}

static void draw_taskbar(Window *active) {
	const Theme &th = g_theme;
	box(g_taskArea, th.Primary());
	Rect a = g_taskArea;
	a.y += 2;
	a.h -= 2;
	box(a, th.Background());
	Vec2 p(2, a.y + 2);
	for (auto &b : g_taskButtons) {
		Color frame, fill, fg;
		if (b.window) {
			bool on = b.window == active && !b.window->minimized;
			frame = b.window->minimized ? th.Variant() : th.Primary();
			fill = on ? th.Primary() : th.Background();
			fg = on ? th.Background() : th.Primary();
		} else {
			frame = th.Variant();
			fill = th.Background();
			fg = th.Primary();
		}
		Rect r(p.x, p.y, b.w, 24);
		box(r, frame);
		box(Rect(r.x + 2, r.y + 2, r.w - 4, r.h - 4), fill);
		if (b.window) {
			Vec2 q(r.x + 4, r.y + 4);
			if (!b.window->icon.empty()) tex(("the_world_machine/window_icons/" + b.window->icon).c_str(), q, Rect(), fg);
			text(Vec2(q.x + 20, q.y), truncate(loc::twm(b.window->title.c_str()), b.w - 4 - 24), fg);
		}
		p.x += b.w + 2;
	}
	// The clock (DateTime.Now.ToShortTimeString), in the console's format.
	int h, m, s, yd;
	sys::local_time(&h, &m, &s, &yd);
	char clock[16];
	if (sys::clock_24h()) snprintf(clock, sizeof(clock), "%d:%02d", h, m);
	else snprintf(clock, sizeof(clock), "%d:%02d %s", h % 12 == 0 ? 12 : h % 12, m, h < 12 ? "AM" : "PM");
	text(Vec2(a.x + a.w - text_w(clock) - 26, a.y + 4), clock, th.Primary());
	g_minimizeAll->Draw(Vec2(0, 0), th, 255);
}

void draw() {
	if (!g_ready) return;
	gm::begin_space(g_space);
	Window *maxed = nullptr;
	for (auto &w : g_windows)
		if (w->maximized) { maxed = w.get(); break; }
	if (!maxed) {
		draw_wallpaper();
		for (int x = 0; x < g_cols; x++)
			for (int y = 0; y < g_rows; y++) {
				int f = slot(x, y);
				if (f < 0) continue;
				bool focus = Vec2(x, y) == g_clickGridPos;
				draw_icon(g_files[f], grid_position(x, y), focus, g_cursorWasFree, x * g_rows + y == g_heldSlot ? 0.5f : 1.0f);
			}
		for (int i = (int)g_windows.size() - 1; i >= 0; i--) g_windows[i]->Draw(g_theme);
		Window *active = nullptr;
		for (auto &w : g_windows)
			if (!w->minimized) { active = w.get(); break; }
		draw_taskbar(active);
		if (g_fadeOpacity) box(Rect(0, 0, g_screen.x, g_screen.y), Color(0, 0, 0, g_fadeOpacity));
		if (g_heldSlot >= 0)   // DraggedItem: the icon follows the cursor
			draw_icon(g_files[g_grid[g_heldSlot]], g_cursor.MousePos() + Vec2(-42, -36), true, false, 0.5f);
	} else {
		maxed->Draw(g_theme);
	}
	if (g_systemModal) box(Rect(0, 0, g_screen.x, g_screen.y), Color(0, 0, 0, 128));
	gm::end_space();
}

void draw_cursor() {
	if (g_ready) g_cursor.Draw();
}

} // namespace desktop
