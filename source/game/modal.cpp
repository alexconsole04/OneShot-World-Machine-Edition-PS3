#include "modal.h"
#include "settings.h"
#include "../engine/audio.h"
#include "../engine/gm.h"
#include "../engine/overlay.h"
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
		if (pressed(BTN_LEFT) || pressed(BTN_RIGHT)) { cursor ^= 1; audio::play_sound("menu_cursor"); }
		if (pressed(BTN_OK)) { last_ = cursor == 0 ? Response::Yes : Response::No; open_ = false; }
		else if (pressed(BTN_CANCEL)) { last_ = Response::No; open_ = false; }
	} else if (pressed(BTN_OK) || pressed(BTN_CANCEL) || auto_mashing()) {
		last_ = Response::OK;
		open_ = false;
	}
}

static std::vector<std::string> wrap(const std::string &s, int maxw, float sc) {
	std::vector<std::string> out;
	std::string line, word;
	auto w = [&](const std::string &x) { return (int)(overlay::text_width(x.c_str()) * sc); };
	auto flush = [&]() {
		if (word.empty()) return;
		std::string cand = line.empty() ? word : line + " " + word;
		if (!line.empty() && w(cand) > maxw) { out.push_back(line); line = word; } else line = cand;
		word.clear();
	};
	for (char c : s) {
		if (c == ' ') flush();
		else if (c == '\n') { flush(); out.push_back(line); line.clear(); }
		else word += c;
	}
	flush();
	out.push_back(line);
	return out;
}

static void box(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f) {
	gfx::Shade s;
	s.r = r; s.g = g; s.b = b; s.a = a;
	gfx::draw(gm::white(), x, y, w, h, 0, 0, 1, 1, s, gfx::Blend::Normal);
}

void draw() {
	if (!open_) return;
	float sw = (float)gfx::screen_w(), sh = (float)gfx::screen_h();
	float sc = sh / 360.0f;             // ~2 at 720p
	float W = 300 * sc;
	auto lines = wrap(text_, (int)(W - 70 * sc), sc * 1.3f);
	float lh = 12 * sc * 1.3f;
	float H = 46 * sc + lines.size() * lh + 22 * sc;
	float x = (sw - W) / 2, y = (sh - H) / 2;
	box(x - 2, y - 2, W + 4, H + 4, 0.1f, 0.05f, 0.15f);
	box(x, y, W, H, 0.85f, 0.85f, 0.88f);
	box(x, y, W, 16 * sc, 0.35f, 0.15f, 0.5f);   // title bar
	overlay::text_plain(x + 6 * sc, y + 4 * sc, sc, 0xffffff, type_ == Type::Error ? "Error" : "Message");
	// icon
	box(x + 12 * sc, y + 26 * sc, 24 * sc, 24 * sc, type_ == Type::Error ? 0.8f : 0.3f, type_ == Type::Error ? 0.2f : 0.4f,
	    type_ == Type::Error ? 0.2f : 0.85f);
	overlay::text_plain(x + 21 * sc, y + 30 * sc, sc * 2, 0xffffff, type_ == Type::Error ? "x" : "i");
	float ty = y + 26 * sc;
	for (auto &l : lines) { overlay::text_plain(x + 48 * sc, ty, sc * 1.3f, 0x101010, l.c_str()); ty += lh; }
	float by = y + H - 22 * sc;
	auto button = [&](float bx, const char *label, bool sel) {
		box(bx, by, 50 * sc, 16 * sc, sel ? 0.35f : 0.6f, sel ? 0.15f : 0.6f, sel ? 0.5f : 0.65f);
		float tw = overlay::text_width(label) * sc;
		overlay::text_plain(bx + (50 * sc - tw) / 2, by + 4 * sc, sc, 0xffffff, label);
	};
	if (type_ == Type::YesNo) {
		button(x + W / 2 - 56 * sc, "Yes", cursor == 0);
		button(x + W / 2 + 6 * sc, "No", cursor == 1);
	} else {
		button(x + W / 2 - 25 * sc, "OK", true);
	}
}

} // namespace modal
