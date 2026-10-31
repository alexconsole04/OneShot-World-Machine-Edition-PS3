// twm.h - shared pieces of the World Machine desktop (OneShotMG.src.TWM):
// colours and themes, drawing in desktop pixels, the cursor, and the
// window manager calls the apps make.
//
// Desktop pixels: the desktop's draw size (DrawScreenSize: 1920x1080 on a
// 1080p TV, 1280x720 otherwise) halved; everything draws at 2x.
#pragma once
#include "../../engine/types.h"
#include "../../engine/font.h"
#include "../../platform/gfx.h"
#include "../ui/mouse_cursor.h"
#include <functional>
#include <string>
#include <vector>

namespace twm {

struct Color {
	uint8_t r = 255, g = 255, b = 255, a = 255;
	Color() = default;
	Color(int r_, int g_, int b_, int a_ = 255) : r((uint8_t)r_), g((uint8_t)g_), b((uint8_t)b_), a((uint8_t)a_) {}
	Color withA(int alpha) const { Color c = *this; c.a = (uint8_t)alpha; return c; }
	bool operator==(const Color &o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
	static Color White() { return Color(255, 255, 255); }
	static Color Black() { return Color(0, 0, 0); }
};

// TWMTheme (gamedata/twm/themes_metadata.json).
struct Theme {
	std::string id = "purple", displayName = "Purple";
	Color primary = Color(150, 100, 255), variant = Color(100, 66, 165), background = Color(0, 0, 0);
	Color Primary(int a = 255) const { return primary.withA(a); }
	Color Variant(int a = 255) const { return variant.withA(a); }
	Color Background(int a = 255) const { return background.withA(a); }
};

// ------------------------------------------------------------------ geometry
static inline bool in_rect(const Rect &r, Vec2 v) { return v.x >= r.x && v.x < r.x + r.w && v.y >= r.y && v.y < r.y + r.h; }
static inline Rect translated(Rect r, Vec2 p) { return Rect(r.x + p.x, r.y + p.y, r.w, r.h); }
static inline Rect shrink(Rect r, int n) { return Rect(r.x + n, r.y + n, r.w - 2 * n, r.h - 2 * n); }

Vec2 space();    // the desktop's draw size (DrawScreenSize)
Vec2 screen();   // WindowManager.ScreenSize: desktop pixels
void set_space(Vec2 drawSize);

// ------------------------------------------------------------------ drawing (inside begin())
void begin();    // desktop draw space on
void end();
// ColorBoxBlit at scale 2.
void box(Rect r, Color c);
// MainBlit of a texture at scale 2 (pos in desktop px), tinted.
void tex(const char *name, Vec2 pos, Rect src, Color c, int scale = 2, int cache = -1);
void tex_add(const char *name, Vec2 pos, Rect src, Color c, int scale = 2, int cache = -1);   // additive
Vec2 tex_size(const char *name, int cache = -1);
// TextBlit / TextSize (OS font unless given), pos in desktop px (scale 2).
void text(Vec2 pos, const std::string &s, Color c, font::Type t = font::OS, font::Mode m = font::Mode::OnlyText);
void text_centered(Vec2 pos, const std::string &s, Color c, font::Type t = font::OS);
int text_w(const std::string &s, font::Type t = font::OS);
// TempTextureManager.GetSingleLineTexture + MainBlit(tex, pixelPos*2, colour, scale 1):
// what the C# draws as a "single line texture" at desktop position pos.
void line(Vec2 pos, const std::string &s, Color c, int maxWidth = -1, bool xCentered = false);
// MathHelper.Truncate / WordWrap / EaseOut
std::string truncate(const std::string &s, int maxLen, font::Type t = font::OS);
std::vector<std::string> wrap(const std::string &s, int width, font::Type t = font::OS);
int ease_out(int start, int target, int t, int total);

// ------------------------------------------------------------------ cursor
MouseCursor &cursor();

// ------------------------------------------------------------------ window manager services
class Window;
const Theme &theme();
const Theme *theme_by_id(const std::string &id);
const std::vector<Theme> &themes();
// TutorialStep: where the desktop tutorials are (the first launch's
// introduction, and the Help folder's three).
enum TutorialStep {
	TUT_BEGIN, TUT_WAIT_FOR_HINTBOX_FADEOUT, TUT_PREAMBLE, TUT_MOVE_CURSOR, TUT_DRAG_FILE, TUT_LAUNCH_APPLICATION,
	TUT_MOVE_WINDOW, TUT_MINIMIZE_WINDOW, TUT_UNMINIMIZE_WINDOW, TUT_CLOSE_WINDOW, TUT_WRAP_UP, TUT_ONESHOT_APPEARS,
	TUT_COMPLETE, TUT_DELETE_CLICK_FILE, TUT_DELETE_CLICK_DELETE, TUT_DELETE_CONFIRM_DELETION, TUT_DELETE_CONGRATULATIONS,
	TUT_MOVEFILE_DRAG_FILE, TUT_MOVEFILE_DROP_FILE, TUT_MOVEFILE_CONGRATULATIONS, TUT_WAIT
};
TutorialStep tutorial_step();
static inline bool tutorial_complete() { return tutorial_step() == TUT_COMPLETE; }
static inline bool tutorial_is(TutorialStep s) { return tutorial_step() == s; }
// The pulse the tutorial puts on what to click (objectHighlightTimer):
// |t - 40| / 40 * 0.6, plus 0.4 with base.
float tutorial_pulse(bool base = true);
bool allow_launch_applications();   // AllowLaunchApplications
bool is_window_focused(const Window *w);
void play_sound(const char *name, float vol = 1.0f, float pitch = 1.0f);
// Windows (dialogs) that close while their own update is running are
// deleted at the end of the frame.
void defer_delete(Window *w);
void flush_deletes();

} // namespace twm
