// wm_state.h - the window manager's shared state and internal calls, for the
// files that make up the desktop (wm.cpp and its neighbours). Not for apps:
// they use wm_internal.h.
#pragma once
#include "wm_internal.h"
#include "hintbox.h"
#include "apps/story_apps.h"
#include "game_window.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace twm {

struct TaskButton { int w; Window *window; };
// TWMNiko: Niko walking off the bottom of the screen.
struct Niko { Vec2 pos; int frame = 0, timer = 0; bool moved = false, solstice = false, entity = false; };

// ------------------------------------------------------------------ state
extern std::vector<Theme> g_themes;
extern std::string g_themeId;
extern Rect g_gameRect;
extern TutorialStep g_tutStep;
extern bool g_redoIntro;
extern std::unique_ptr<HintBox> g_hint;
extern std::vector<std::string> g_tutLines;
extern Vec2 g_tutMouseStart, g_tutWindowStart;
extern int g_tutHighlight;
extern Filesystem g_fs;
extern std::unique_ptr<DesktopGrid> g_desktop;
extern std::vector<std::unique_ptr<Window>> g_windows;
extern std::vector<Window *> g_pendingRemoval, g_previousActive;
extern bool g_hasPreviousActive, g_createdThisFrame;
extern std::unique_ptr<ModalWindow> g_systemModal;
extern std::unique_ptr<DraggedItem> g_drag;
extern bool g_ready, g_quit, g_fsDirty, g_desktopDirty;
extern bool g_gameMouseOk;
extern int g_gamma, g_bgmVol, g_sfxVol;
extern bool g_hideUI;
extern int g_restartRequest;
extern std::vector<TaskButton> g_taskButtons;
extern Rect g_taskArea;
extern std::unique_ptr<WindowButton> g_minimizeAll;
extern bool g_fading;
extern std::unique_ptr<Niko> g_niko;
extern std::string g_pendingWallpaper;

// ------------------------------------------------------------------ calls between the desktop's files
void load_themes();
void update_themes();
void apply_custom_wallpapers(bool rebuildFolder);
void draw_brightness();
GameWindow *game_window();
void write_saves();
bool load_desktop(bool *tutorialCompleted = nullptr);
bool taskbar_update(bool consumed);
void draw_taskbar(Window *active);
void start_tutorial(Program which);
void update_tutorial();
void bring_to_front(Window *w);
bool add_window_(Window *raw);
void remove_window(Window *w);
bool unminimize_enabled();
void fade(int from, int to, int frames, std::function<void()> done);
void cancel_drag();
// desktop_test_hooks.cpp (host test switches; nothing happens on the PS3)
void test_desktop_ready();
void test_move_cursor(int frame);

} // namespace twm
