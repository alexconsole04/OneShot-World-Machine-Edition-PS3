// wm.cpp - WindowManager: the World Machine desktop's core. Windows (front to
// back), the system dialog, dragging files, fades and flashes, start-up
// (init), and each frame's update and draw. Its neighbours: appearance.cpp,
// game_window.cpp, desktop_saves.cpp, run_file.cpp, taskbar.cpp, tutorial.cpp
// and desktop_test_hooks.cpp (shared state in wm_state.h). The desktop::
// calls (desktop.h) at the end are what main.cpp uses.
#include "wm_state.h"
#include "desktop.h"
#include "apps/collection_apps.h"
#include "apps/custom_wallpapers.h"
#include "../../engine/audio.h"
#include "../../engine/util.h"
#include "../../platform/input.h"
#include "../../platform/sys.h"
#include "../core/oneshot_window.h"
#include <algorithm>
#include <stdio.h>

namespace twm {

// ===================================================================== state

Filesystem g_fs;
static UnlockManager *g_unlocks;
static ToastManager g_toasts;
std::unique_ptr<DesktopGrid> g_desktop;
std::vector<std::unique_ptr<Window>> g_windows;   // [0] = front
std::vector<Window *> g_pendingRemoval, g_previousActive;
bool g_hasPreviousActive, g_createdThisFrame;
std::unique_ptr<ModalWindow> g_systemModal;
std::unique_ptr<DraggedItem> g_drag;
bool g_ready, g_quit, g_fsDirty, g_desktopDirty;
bool g_gameMouseOk;   // OneshotWindow.canMouseInteract, this frame
int g_gamma = 100, g_bgmVol = 100, g_sfxVol = 100;   // Customize (desktop.dat)
bool g_hideUI;                                        // HideAllUI (View Wallpaper)
int g_restartRequest;
// Taskbar
std::vector<TaskButton> g_taskButtons;
static int g_taskButtonW = 100;
Rect g_taskArea;
std::unique_ptr<WindowButton> g_minimizeAll;
// Fades and flashes
bool g_fading;
static int g_fadeTimer, g_fadeTotal;
static int g_fadeStart, g_fadeEnd, g_fadeOpacity;
static std::function<void()> g_onFadeDone;
static Color g_flashColor = Color::White();
static float g_flashStrength = 1;
static int g_flashTimer, g_flashDuration = 1;
std::unique_ptr<Niko> g_niko;
std::string g_pendingWallpaper;   // a custom wallpaper still decoding

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

// ===================================================================== windows

Window *find_window(bool (*pred)(Window *)) {
	for (auto &w : g_windows)
		if (pred(w.get())) return w.get();
	return nullptr;
}

bool is_window_focused(const Window *w) { return !g_windows.empty() && g_windows[0].get() == w; }

void bring_to_front(Window *w) {
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

bool add_window_(Window *raw) {
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
	// Delegate.Combine: the window's own close handler (stop the music,
	// save the controls...) runs first.
	std::function<void(Window *)> own = raw->onClose;
	raw->onClose = [own](Window *x) {
		if (std::find(g_pendingRemoval.begin(), g_pendingRemoval.end(), x) != g_pendingRemoval.end()) return;
		if (own) own(x);
		g_pendingRemoval.push_back(x);
	};
	raw->grabFocus = [](Window *x) { if (!g_createdThisFrame) bring_to_front(x); };
	g_taskButtons.push_back({ g_taskButtonW, raw });
	g_windows.insert(g_windows.begin(), std::move(w));
	g_createdThisFrame = true;
	return true;
}

void add_window(Window *w) { add_window_(w); }

void remove_window(Window *w) {
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

bool unminimize_enabled() {
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

void fade(int from, int to, int frames, std::function<void()> done) {
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

void cancel_drag() {
	if (g_drag && g_drag->onDropComplete) g_drag->onDropComplete(false);
	g_drag.reset();
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
	scan_custom_wallpapers();
	apply_custom_wallpapers(false);   // before desktop.dat names one as the wallpaper
	// A playthrough saved before the file system existed (builds before m14)
	// skips the introduction: its desktop starts as the tutorial leaves it,
	// with the save's file where MakeSave puts it.
	bool oldSave = freshFs && sys::save_exists("save.dat");
	if (oldSave) {
		g_fs.CreateDocumentsFolder();
		g_desktop->MoveDocumentsToTopLeft();
		g_fs.CreateOneshotFile();
		g_fs.CreateTutorialCompleteFiles(unlocks().wallpapers, unlocks().themes);
		g_desktop->MoveDocumentsToTopLeft();
		auto f = make_file("save", "fakesave_filename", PROG_DOCUMENT, { "save_progress_text" });
		f->moveRestricted = true;
		g_fs.WriteFile(Filesystem::MYGAMESONESHOT, f);
	}
	bool tutorialDone = true;
	bool haveDesktop = load_desktop(&tutorialDone);   // the icons' places, unlocks, theme
	// The first launch (no file system yet) begins with the introduction,
	// as does a desktop.dat that says it wasn't finished.
	if (freshFs) tutorialDone = oldSave;
	g_tutStep = tutorialDone ? TUT_COMPLETE : TUT_BEGIN;
	g_redoIntro = false;
	g_hint.reset();
	if (freshFs) sys::log("twm: new file system%s%s\n", haveDesktop ? " (desktop.dat kept)" : "", tutorialDone ? "" : ", introduction");
	if (!haveDesktop) g_desktop->SetWallpaper("default");
	// Start decoding the player's own picture now, while the boot logo shows.
	if (is_custom_wallpaper(g_desktop->Showing().imageFile)) custom_wallpaper_ready(g_desktop->Showing().imageFile);
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
	test_desktop_ready();
	sys::log("desktop: ready (%dx%d desktop drawn at %dx%d, theme %s) in %.0f ms\n", screen().x, screen().y, space().x,
	         space().y, g_themeId.c_str(), (sys::time_us() - t0) / 1000.0);
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
	tick_timers();
	cursor().Update();
	custom_wallpapers_poll();
	if (!g_pendingWallpaper.empty() && custom_wallpaper_ready(g_pendingWallpaper)) {
		if (!g_desktop->IsOverridden()) {
			g_desktop->SetWallpaper(g_pendingWallpaper);
			save_desktop();
		}
		g_pendingWallpaper.clear();
	}
	static int frame;
	test_move_cursor(++frame);
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
	// The dialog the pad answers: a system one, else the game's (the game
	// has the pad even when its window isn't in front), else the front
	// window's.
	ModalWindow::padOwner = nullptr;
	if (g_systemModal) ModalWindow::padOwner = g_systemModal.get();
	else if (gw && gw->IsModalOpen()) ModalWindow::padOwner = static_cast<ModalWindow *>(gw->Modal());
	else if (!g_windows.empty() && g_windows[0]->IsModalOpen()) ModalWindow::padOwner = static_cast<ModalWindow *>(g_windows[0]->Modal());
	// A dialog of another window (or the system's) owns the pad: the game
	// doesn't hear those presses (it used to play its menu sounds).
	bool otherDialog = ModalWindow::padOwner && !(gw && gw->IsModalOpen() && ModalWindow::padOwner == gw->Modal());
	// The front window owns the pad: another window in front of the game
	// takes it (d-pad scrolling); clicking the game or picking it on the
	// taskbar brings it back. The game's own dialogs still hear it.
	Window *front = nullptr;
	for (auto &w : g_windows)
		if (!w->Minimized()) { front = w.get(); break; }
	bool otherFront = front && front != gw && front->WantsPad() && !(gw && gw->IsModalOpen());
	input::suppress_game(gw && (gw->Minimized() || g_systemModal || g_fading || otherDialog || otherFront));
	if (otherFront && !front->IsModalOpen() && !g_systemModal && !g_fading) front->PadInput();
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
			std::function<void()> done = std::move(g_onFadeDone);   // it may start another fade
			g_onFadeDone = nullptr;
			if (done) done();
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
	update_tutorial();
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
	if (g_hint) g_hint->Draw(th);
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
bool wallpaper_ready() {
	using namespace twm;
	if (!g_ready || !g_desktop) return true;
	const std::string &id = g_desktop->Showing().imageFile;
	return !is_custom_wallpaper(id) || custom_wallpaper_ready(id);
}
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

