// tutorial.cpp - the desktop tutorials (WindowManager.CheckTutorialStep):
// the first launch's introduction and the Help folder's three, with the
// hint box that points at what to do next.
#include "wm_state.h"
#include "../../platform/input.h"
#include "../../platform/sys.h"
#include "../core/loc.h"

namespace twm {

// ===================================================================== the desktop tutorials

TutorialStep g_tutStep = TUT_COMPLETE;
bool g_redoIntro;
std::unique_ptr<HintBox> g_hint;
std::vector<std::string> g_tutLines;
Vec2 g_tutMouseStart, g_tutWindowStart;
int g_tutHighlight;

TutorialStep tutorial_step() { return g_tutStep; }
float tutorial_pulse(bool base) {
	float n = iabs(g_tutHighlight - 40) / 40.0f * 0.6f;
	return base ? n + 0.4f : n;
}
bool allow_launch_applications() { return g_tutStep == TUT_LAUNCH_APPLICATION || g_tutStep == TUT_COMPLETE; }
// WindowManager.CheckTutorialStep and its helpers. The tutorial runs on a
// stand-in desktop; the real one is saved first and loaded again after
// (nothing is saved while a tutorial runs).

static std::vector<std::string> tut_lines(const char *key) {
	std::string t = loc::twm(key);
	for (size_t p; (p = t.find("\\n")) != std::string::npos;) t.replace(p, 2, "\n");
	std::vector<std::string> r;
	size_t a = 0;
	for (;;) {
		size_t b = t.find('\n', a);
		r.push_back(t.substr(a, b == std::string::npos ? std::string::npos : b - a));
		if (b == std::string::npos) break;
		a = b + 1;
	}
	return r;
}

static void hint(const std::string &text, std::function<Vec2()> target) {
	g_hint.reset(new HintBox(Vec2(screen().x / 2, screen().y / 2), text));
	g_hint->GetTargetPos = target;
}
static Vec2 tut_mouse() { return cursor().MousePos(); }
// The next line of g_tutLines, at the cursor.
static void hint_next_line() {
	hint(g_tutLines.front(), tut_mouse);
	g_tutLines.erase(g_tutLines.begin());
}

// GetFolderPosition: the folder's desktop icon (the cursor while it's dragged).
static Vec2 tut_folder_pos(const std::string &path) {
	auto isIt = [&path](FileIcon *ic) {
		return ic && ic->file->IsFolder() && static_cast<Folder *>(ic->file.get())->Path() == path;
	};
	if (g_drag && isIt(g_drag->icon)) return cursor().MousePos();
	return g_desktop->IconPosition(isIt, Vec2(0, 0));
}
// GetWindowPosition: the first window with that icon.
static Vec2 tut_window_pos(const char *icon, Vec2 offset) {
	for (auto &w : g_windows)
		if (w->icon == icon) return w->pos + offset;
	return Vec2(0, 0);
}
static BrowserWindow *tut_browser(const std::string &path) {
	for (auto &w : g_windows)
		if (w->Kind() == KIND_BROWSER && static_cast<BrowserWindow *>(w.get())->Path() == path) return static_cast<BrowserWindow *>(w.get());
	return nullptr;
}
static BrowserWindow *tut_front_browser() {
	return !g_windows.empty() && g_windows[0]->Kind() == KIND_BROWSER ? static_cast<BrowserWindow *>(g_windows[0].get()) : nullptr;
}
static const char *const MOVE_FOLDER1 = "/movefile_tutorial_folder1_name/", *const MOVE_FOLDER2 = "/movefile_tutorial_folder2_name/";
static Vec2 tut_dummy_in(const char *path) {
	BrowserWindow *b = tut_browser(path);
	return b ? b->IconPos(PROG_DUMMY_FILE_FOR_TUTORIALS) : Vec2(0, 0);
}

// InputManager.IsTutorialButtonPressed
static bool tut_button() {
	using namespace input;
	return pressed_raw(BTN_OK) || pressed_raw(BTN_CANCEL) || cursor().Clicked() || (held_raw(BTN_MASHTEXT) && cursor().Held());
}

static void close_all_windows() {   // CloseAllWindows
	cancel_drag();
	std::vector<Window *> open;
	for (auto &w : g_windows) open.push_back(w.get());
	for (Window *w : open)
		if (w->onClose) w->onClose(w);
	for (Window *w : g_pendingRemoval) remove_window(w);
	g_pendingRemoval.clear();
	g_taskButtons.clear();
	g_previousActive.clear();
	g_hasPreviousActive = false;
}

static void load_fs_and_desktop() {   // LoadFilesystemAndDesktop
	std::string text;
	if (sys::save_read("fs.dat", text) && g_fs.Load(text)) g_fs.CreateExtraSystemFiles();
	load_desktop();
}

static void fade_out_and_end_tutorial() {
	g_tutStep = TUT_WAIT;
	g_hint.reset();
	fade(0, 255, 90, [] {
		close_all_windows();
		load_fs_and_desktop();
		fade(255, 0, 90, [] {
			g_tutStep = TUT_COMPLETE;
			sys::log("tutorial: done\n");
		});
	});
}

static void prepare_for_tutorial() {
	close_all_windows();
	save_desktop_and_fs();
	write_saves();   // now: the tutorial's stand-in desktop is never saved
	g_tutStep = TUT_BEGIN;
	g_fs.LoadEmpty();
	g_desktop->SetLayout({});   // the wallpaper stays
}

void start_tutorial(Program which) {
	const char *name = which == PROG_TUTORIAL_INTRO_FILE ? "tutorial_introduction_filename"
	                 : which == PROG_TUTORIAL_DELETE_FILE ? "tutorial_delete_file_filename" : "tutorial_move_file_filename";
	if (game_window()) {
		show_system_modal(MODAL_ERROR, format1(loc::twm("error_launch_when_oneshot_open"), loc::twm(name)));
		return;
	}
	if (g_tutStep != TUT_COMPLETE || g_fading) return;
	sys::log("tutorial: %s\n", program_name(which));
	fade(0, 255, 90, [which] {
		prepare_for_tutorial();
		g_tutStep = TUT_WAIT;
		if (which == PROG_TUTORIAL_INTRO_FILE) {
			fade(255, 0, 90, [] {
				g_tutStep = TUT_BEGIN;
				g_redoIntro = true;
			});
		} else if (which == PROG_TUTORIAL_DELETE_FILE) {
			g_fs.CreateDeleteTutorialFiles();
			run_file(g_fs.GetDir(Filesystem::MYDOCUMENTS));
			fade(255, 0, 90, [] {
				g_tutStep = TUT_DELETE_CLICK_FILE;
				hint(loc::twm("tutorial_delete_click_file"), [] { return tut_dummy_in(Filesystem::MYDOCUMENTS); });
			});
		} else {
			g_fs.CreateMoveTutorialFiles();
			run_file(g_fs.GetDir(MOVE_FOLDER2));
			if (!g_windows.empty()) g_windows[0]->pos = Vec2(screen().x - 360 - 10, (screen().y - 200) / 2 - 50);
			run_file(g_fs.GetDir(MOVE_FOLDER1));
			if (!g_windows.empty()) g_windows[0]->pos = Vec2(10, (screen().y - 200) / 2);
			fade(255, 0, 90, [] {
				g_tutStep = TUT_MOVEFILE_DRAG_FILE;
				hint(loc::twm("tutorial_movefile_drag_file"), [] { return tut_dummy_in(MOVE_FOLDER1); });
			});
		}
	});
}

// Fades the hint box out, then moves on to `next` with a new box.
static void tut_then(TutorialStep wait, std::function<void()> next) {
	if (wait != TUT_COMPLETE) g_tutStep = wait;
	g_hint->OnFadeOutComplete = next;
	g_hint->FadeOut = true;
}

static void check_tutorial_step() {
	const TutorialStep W = TUT_WAIT_FOR_HINTBOX_FADEOUT, SAME = TUT_COMPLETE;   // SAME: keep the step while fading
	switch (g_tutStep) {
	case TUT_BEGIN:
		g_tutLines = tut_lines(g_redoIntro ? "tutorial_redo_preamble" : "tutorial_preamble");
		hint_next_line();
		g_tutStep = TUT_PREAMBLE;
		break;
	case TUT_PREAMBLE:
	case TUT_WRAP_UP:
	case TUT_ONESHOT_APPEARS:
		if (!g_hint || !g_hint->IsFadeInComplete() || g_hint->FadeOut || !tut_button()) break;
		tut_then(SAME, [] {
			if (!g_tutLines.empty()) {
				hint_next_line();
			} else if (g_tutStep == TUT_PREAMBLE) {
				g_tutMouseStart = cursor().MousePos();
				hint(loc::twm("tutorial_move_cursor"), tut_mouse);
				g_tutStep = TUT_MOVE_CURSOR;
			} else if (g_tutStep == TUT_WRAP_UP) {
				if (!g_redoIntro) {
					flash(Color(0, 255, 255, 255), 20, 0.25f);
					play_sound("pc_granted", 0.8f, 1.5f);
					g_desktop->MoveDocumentsToTopLeft();
					g_fs.CreateOneshotFile();
					g_tutLines = tut_lines("tutorial_oneshot_appears");
					hint_next_line();
					g_tutStep = TUT_ONESHOT_APPEARS;
				} else {
					fade_out_and_end_tutorial();
				}
			} else if (g_tutStep == TUT_ONESHOT_APPEARS) {
				g_hint.reset();
				g_tutStep = TUT_COMPLETE;
				g_fs.CreateTutorialCompleteFiles(unlocks().wallpapers, unlocks().themes);
				save_desktop_and_fs();
				sys::log("tutorial: introduction done\n");
			}
		});
		break;
	case TUT_MOVE_CURSOR: {
		Vec2 d = cursor().MousePos() - g_tutMouseStart;
		if (iabs(d.x) <= 80 && iabs(d.y) <= 80) break;
		tut_then(W, [] {
			g_fs.CreateDocumentsFolder();
			g_tutStep = TUT_DRAG_FILE;
			hint(loc::twm("tutorial_drag_file"), [] { return tut_folder_pos(Filesystem::MYDOCUMENTS); });
		});
		break;
	}
	case TUT_DRAG_FILE:
		if (!g_drag) break;
		tut_then(W, [] {
			g_tutStep = TUT_LAUNCH_APPLICATION;
			hint(loc::twm("tutorial_launch_application"), [] { return tut_folder_pos(Filesystem::MYDOCUMENTS); });
		});
		break;
	case TUT_LAUNCH_APPLICATION:
		if (g_windows.empty()) break;
		g_tutWindowStart = tut_window_pos("folder", Vec2(0, 0));
		tut_then(W, [] {
			g_tutStep = TUT_MOVE_WINDOW;
			hint(loc::twm("tutorial_move_window"), [] { return tut_window_pos("folder", Vec2(180, 0)); });
		});
		break;
	case TUT_MOVE_WINDOW: {
		Vec2 d = tut_window_pos("folder", Vec2(0, 0)) - g_tutWindowStart;
		if (iabs(d.x) <= 80 && iabs(d.y) <= 80) break;
		tut_then(W, [] {
			g_tutStep = TUT_MINIMIZE_WINDOW;
			hint(loc::twm("tutorial_minimize_window"), [] { return tut_window_pos("folder", Vec2(360, 0)); });
		});
		break;
	}
	case TUT_MINIMIZE_WINDOW:
	case TUT_UNMINIMIZE_WINDOW: {
		bool anyMin = false;
		for (auto &w : g_windows) anyMin |= w->Minimized();
		if (anyMin != (g_tutStep == TUT_MINIMIZE_WINDOW)) break;
		if (g_tutStep == TUT_MINIMIZE_WINDOW)
			tut_then(W, [] {
				g_tutStep = TUT_UNMINIMIZE_WINDOW;
				hint(loc::twm("tutorial_unminimize_window"), [] { return Vec2(0, screen().y); });
			});
		else
			tut_then(W, [] {
				g_tutStep = TUT_CLOSE_WINDOW;
				hint(loc::twm("tutorial_close_window"), [] { return tut_window_pos("folder", Vec2(360, 0)); });
			});
		break;
	}
	case TUT_CLOSE_WINDOW:
		if (!g_windows.empty()) break;
		tut_then(W, [] {
			g_tutStep = TUT_WRAP_UP;
			g_tutLines = tut_lines(g_redoIntro ? "tutorial_redo_wrap_up" : "tutorial_wrap_up");
			hint_next_line();
		});
		break;
	case TUT_DELETE_CLICK_FILE: {
		BrowserWindow *b = tut_front_browser();
		if (!b || !b->IsIconSelected(PROG_DUMMY_FILE_FOR_TUTORIALS)) break;
		tut_then(SAME, [] {
			g_tutStep = TUT_DELETE_CLICK_DELETE;
			hint(loc::twm("tutorial_delete_click_delete_button"), [] {
				BrowserWindow *x = tut_browser(Filesystem::MYDOCUMENTS);
				return x ? x->DeleteButtonPos() : Vec2(0, 0);
			});
		});
		break;
	}
	case TUT_DELETE_CLICK_DELETE: {
		BrowserWindow *b = tut_front_browser();
		if (!b) break;
		if (!b->IsIconSelected(PROG_DUMMY_FILE_FOR_TUTORIALS)) {
			tut_then(SAME, [] {
				g_tutStep = TUT_DELETE_CLICK_FILE;
				hint(loc::twm("tutorial_delete_click_file"), [] { return tut_dummy_in(Filesystem::MYDOCUMENTS); });
			});
		} else if (b->IsShowingDeleteConfirmModal()) {
			tut_then(SAME, [] {
				g_tutStep = TUT_DELETE_CONFIRM_DELETION;
				hint(loc::twm("tutorial_delete_confirm_delete"), [] {
					BrowserWindow *x = tut_browser(Filesystem::MYDOCUMENTS);
					return x ? x->ModalButtonsPos() : Vec2(0, 0);
				});
			});
		}
		break;
	}
	case TUT_DELETE_CONFIRM_DELETION: {
		BrowserWindow *b = tut_front_browser();
		if (!b || b->IsModalOpen()) break;
		tut_then(SAME, [] {
			g_tutStep = TUT_DELETE_CONGRATULATIONS;
			hint(loc::twm("tutorial_delete_congratulations"), [] { return Vec2(screen().x / 2, screen().y / 2); });
		});
		break;
	}
	case TUT_DELETE_CONGRATULATIONS:
	case TUT_MOVEFILE_CONGRATULATIONS:
		if (g_hint && g_hint->IsFadeInComplete() && !g_hint->FadeOut && tut_button()) tut_then(SAME, [] { fade_out_and_end_tutorial(); });
		break;
	case TUT_MOVEFILE_DRAG_FILE:
		if (!g_drag) break;
		tut_then(W, [] {
			g_tutStep = TUT_MOVEFILE_DROP_FILE;
			hint(loc::twm("tutorial_movefile_drop_file"), [] {
				BrowserWindow *x = tut_browser(MOVE_FOLDER2);
				return x ? x->pos + Vec2(x->Contents().x, x->Contents().y / 2) : Vec2(0, 0);
			});
		});
		break;
	case TUT_MOVEFILE_DROP_FILE:
		if (g_fs.FileExists(std::string(MOVE_FOLDER2) + "tutorial_dummyfile_filename")) {
			tut_then(W, [] {
				g_tutStep = TUT_MOVEFILE_CONGRATULATIONS;
				hint(loc::twm("tutorial_movefile_congratulations"), [] {
					BrowserWindow *x = tut_browser(MOVE_FOLDER2);
					return x ? x->pos + Vec2(x->Contents().x / 2, x->Contents().y / 2) : Vec2(0, 0);
				});
			});
		} else if (!g_drag) {
			tut_then(W, [] {
				g_tutStep = TUT_MOVEFILE_DRAG_FILE;
				hint(loc::twm("tutorial_movefile_drag_file"), [] { return tut_dummy_in(MOVE_FOLDER1); });
			});
		}
		break;
	default:
		break;
	}
}

void update_tutorial() {
	if (g_tutStep != TUT_COMPLETE) g_tutHighlight = (g_tutHighlight + 1) % 80;
	if (g_hint && g_hint->Update()) {
		std::function<void()> done = g_hint->OnFadeOutComplete;   // it may replace the box
		if (done) done();
	}
	if ((g_tutStep != TUT_COMPLETE && g_hint) || g_tutStep == TUT_BEGIN) check_tutorial_step();
	static TutorialStep last = TUT_COMPLETE;
	if (g_tutStep != last) {
		sys::log("tutorial: step %d\n", (int)g_tutStep);
		last = g_tutStep;
	}
}

} // namespace twm
