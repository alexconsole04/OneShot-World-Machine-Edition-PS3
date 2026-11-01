// game_window.cpp - the OneShot window on the desktop (OneshotWindow), and
// the calls the game makes into the desktop: the journal, Niko leaving,
// wallpaper overrides, the game's dialogs, the cursor over the game.
#include "wm_state.h"
#include "../../engine/util.h"
#include "../core/loc.h"
#include "../core/oneshot_window.h"
#include <math.h>

namespace twm {

// ===================================================================== the OneShot window

static bool g_windowedDraw;
Rect g_gameRect;   // the game's picture this frame, desktop draw-space pixels (w 0: none)

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

GameWindow::GameWindow() {
	icon = "oneshot";
	SetTitle(loc::twm("oneshot_appname"));
	SetContents(Vec2(320, 240));
	AddButton(WinButtonType::Close, [] { window().CloseGame(); });
	AddButton(WinButtonType::Maximize, [this] {
		if (!IsModalOpen() && !window().State().flag(123)) ToggleMaximize();   // flag 123: not during that scene
	});
	AddButton(WinButtonType::Minimize);
}

void GameWindow::ToggleMaximize() {
	Window::ToggleMaximize();
	WindowButton *b = FindButton(WinButtonType::Maximize);
	if (maximized) oldMaxPos = b->Position();
	else b->SetPosition(oldMaxPos);
	settings().fullscreen = maximized;
}

bool GameWindow::Update(bool consumed) {
	if (++borderTimer >= 6) {
		borderTimer = 0;
		borderFrame = (borderFrame + 1) % 2;
	}
	// canMouseInteract: the cursor isn't on a window in front, no dialog.
	g_gameMouseOk = !system_modal_open() && !IsModalOpen() && (maximized || !consumed);
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

Rect GameWindow::FullscreenRect() const {
	Vec2 d = space();
	Rect r;
	if (d.x * 3 >= d.y * 4) r = Rect(0, 0, 320 * d.y / 240, d.y);
	else r = Rect(0, 0, d.x, 240 * d.x / 320);
	r.x = (d.x - r.w) / 2;
	r.y = (d.y - r.h) / 2;
	return r;
}

void GameWindow::Draw(const Theme &th) {
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

void GameWindow::DrawContents(const Theme &, Vec2 sp, int) {
	g_windowedDraw = true;
	float k;
	Vec2 tv = gm::space_to_tv(space(), (float)sp.x * 2, (float)sp.y * 2, &k);
	draw_game_at((float)tv.x, (float)tv.y, k);
}

// FullscreenBorderManager's art beside (or above and below) the picture, in
// the colour of the current area.
void GameWindow::DrawBorder(Rect fr) {
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

GameWindow *game_window() {
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

// ===================================================================== the cursor over the game

bool game_cursor(Vec2 *out) {
	GameWindow *gw = game_window();
	if (!g_ready || !gw || gw->Minimized() || !g_gameMouseOk) return false;
	Vec2 m = cursor().MousePos();
	Vec2 g;
	if (gw->Maximized()) {
		Rect fr = gw->FullscreenRect();   // draw-space pixels (2 per desktop pixel)
		if (fr.w <= 0 || fr.h <= 0) return false;
		g = Vec2((m.x - fr.x / 2) * 640 / fr.w, (m.y - fr.y / 2) * 480 / fr.h);
	} else {
		g = m - Vec2(gw->pos.x + 2, gw->pos.y + 26);
	}
	if (g.x < 0 || g.y < 0 || g.x > 320 || g.y > 240) return false;
	*out = g;
	return true;
}

bool cursor_clicked() { return g_ready && cursor().Clicked(); }

void theme_rgb(const std::string &id, uint8_t p[3], uint8_t b[3]) {
	const Theme *t = id.empty() ? nullptr : theme_by_id(id);
	if (!t) t = &theme();
	p[0] = t->primary.r; p[1] = t->primary.g; p[2] = t->primary.b;
	b[0] = t->background.r; b[1] = t->background.g; b[2] = t->background.b;
}

} // namespace twm
