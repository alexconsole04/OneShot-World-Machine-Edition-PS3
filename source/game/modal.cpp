#include "modal.h"
#include "settings.h"
#include "loc.h"
#include "../engine/font.h"
#include "../engine/audio.h"
#include "../engine/gm.h"
#include "../platform/gfx.h"
#include "../platform/input.h"
#include "../platform/sys.h"
#include <vector>
#include <stdio.h>

namespace modal {

static bool open_;
static Type type_;
static std::string text_;
static Response last_ = Response::None;
static int cursor;        // YesNo: 0 = Yes, 1 = No
static int age;           // frames since shown (ignore the press that opened it)

void show(Type t, const std::string &text) {
	open_ = true;
	type_ = t;
	text_ = text;
	cursor = 0;
	age = 0;
	sys::log("modal: %s\n", text.c_str());
	static const int order[4] = { 2, 3, 4, 2 };
	static int idx;
	char snd[16];
	snprintf(snd, sizeof(snd), "twm_notif%d", order[idx]);
	idx = (idx + 1) % 4;
	audio::play_sound(snd, 0.5f);
}

bool is_open() { return open_; }
Response last_response() { return last_; }

void update() {
	if (!open_) return;
	if (++age < 8) return;
	using namespace input;
	if (type_ == Type::YesNo) {
		if (pressed_raw(BTN_LEFT) || pressed_raw(BTN_RIGHT)) { cursor ^= 1; audio::play_sound("menu_cursor"); }
		if (pressed_raw(BTN_OK)) { last_ = cursor == 0 ? Response::Yes : Response::No; open_ = false; }
		else if (pressed_raw(BTN_CANCEL)) { last_ = Response::No; open_ = false; }
	} else if (pressed_raw(BTN_OK) || pressed_raw(BTN_CANCEL) || auto_mashing()) {
		last_ = Response::OK;
		open_ = false;
	}
}

void draw() {
	// A stand-in for the World Machine's ModalWindow until the desktop's
	// windows exist: drawn in desktop space with the language's OS font, so
	// every translation shows.
	if (!open_) return;
	Vec2 sp = gm::boot_space();
	gm::begin_space(sp);
	const int W = 300;   // desktop pixels (the boot space is 2x these)
	std::vector<std::string> lines = font::word_wrap(font::OS, text_, W - 70);
	int lh = 14;
	int H = 46 + (int)lines.size() * lh + 22;
	int x = (sp.x / 2 - W) / 2, y = (sp.y / 2 - H) / 2;
	gm::color_box(Rect(x - 1, y - 1, W + 2, H + 2), 26, 13, 38, 255);
	gm::color_box(Rect(x, y, W, H), 217, 217, 224, 255);
	gm::color_box(Rect(x, y, W, 16), 89, 38, 128, 255);   // title bar
	bool err = type_ == Type::Error;
	gm::color_box(Rect(x + 12, y + 26, 24, 24), err ? 204 : 77, err ? 51 : 102, err ? 51 : 217, 255);
	font::draw_centered(font::OS, Vec2(x + 24, y + 30), err ? "x" : "i", 0xffffff, 1.0f, 2);
	int ty = y + 26;
	for (auto &l : lines) { font::draw(font::OS, Vec2(x + 48, ty), l, 0x101010, 1.0f, 2); ty += lh; }
	int by = y + H - 22;
	auto button = [&](int bx, const std::string &label, bool sel) {
		gm::color_box(Rect(bx, by, 50, 16), sel ? 89 : 153, sel ? 38 : 153, sel ? 128 : 166, 255);
		font::draw_centered(font::OS, Vec2(bx + 25, by + 3), label, 0xffffff, 1.0f, 2);
	};
	if (type_ == Type::YesNo) {
		button(x + W / 2 - 56, loc::twm("dialog_yes"), cursor == 0);
		button(x + W / 2 + 6, loc::twm("dialog_no"), cursor == 1);
	} else {
		button(x + W / 2 - 25, loc::twm("dialog_ok"), true);
	}
	gm::end_space();
}

} // namespace modal
