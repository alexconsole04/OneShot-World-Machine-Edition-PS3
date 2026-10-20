#include "message_box.h"
#include "game_state.h"
#include "../engine/gm.h"
#include "../engine/overlay.h"
#include "../engine/audio.h"
#include "../platform/input.h"
#include <stdlib.h>
#include <string.h>

static bool ok_pressed() { return input::pressed(input::BTN_OK); }
static bool cancel_pressed() { return input::pressed(input::BTN_CANCEL); }
static bool mashing() { return input::held(input::BTN_MASHTEXT); }

TextBox::TextBox(GameState *s, Style sty, Area ar, Kind k) : state(s), style(sty), area(ar), kind(k) {
	Open();
}

void TextBox::Open() {
	st = State::Opening;
	totalTransitionTime = 6;
	transitionTimer = 6 - transitionTimer;
}

void TextBox::Close() {
	st = State::Closing;
	totalTransitionTime = 6;
	transitionTimer = 0;
	if (!choices.empty()) {
		choices.clear();
		choiceCallback = nullptr;
		choiceCursor = 0;
	}
}

void TextBox::ClearText() {
	text.clear();
	portrait.clear();
	inputNumberVar = -1;
	showChoicesFollows = false;
}

std::string TextBox::ReplaceVariableMarkup(const GameState *state, std::string t) {
	size_t p;
	while ((p = t.find("\\v[")) != std::string::npos) {
		size_t e = t.find(']', p);
		if (e == std::string::npos) break;
		int id = atoi(t.c_str() + p + 3);
		t.replace(p, e - p + 1, std::to_string(state->var(id)));
	}
	return t;
}

static void replace_all(std::string &s, const char *from, const std::string &to) {
	size_t n = strlen(from), p = 0;
	while ((p = s.find(from, p)) != std::string::npos) { s.replace(p, n, to); p += to.size(); }
}

void TextBox::FeedText(const std::string &in, const std::string &playerName) {
	std::string t = in;
	replace_all(t, "\\\\", "\\");
	replace_all(t, "\\n", "\n");
	t = ReplaceVariableMarkup(state, t);
	replace_all(t, "\\p", playerName);
	if (text.empty()) {
		bool lead_nl = false;
		if (!t.empty() && t[0] == '\n') { t = t.substr(1); lead_nl = true; }
		if (!t.empty() && t[0] == '@') {
			size_t sp = t.find(' ');
			if (sp != std::string::npos) { portrait = t.substr(1, sp - 1); t = t.substr(sp + 1); }
			else { portrait = t.substr(1); t.clear(); }
		}
		if (lead_nl) t = "\n" + t;
		text = t;
	} else {
		text += t;
	}
}

void TextBox::SetChoices(const std::vector<std::string> &c, int cancelValue, std::function<void(int)> cb,
                         const std::string &playerName) {
	choices = c;
	cancelChoice = cancelValue;
	choiceCallback = std::move(cb);
	choiceCursor = 0;
	for (auto &s : choices) {
		replace_all(s, "\\p", playerName);
		s = ReplaceVariableMarkup(state, s);
	}
}

void TextBox::InputNumberSetup(int var, int n) {
	inputNumberVar = var;
	digits.assign(n, 0);
	digitCursor = 0;
	int v = state->var(var);
	for (int i = 0; i < n; i++) {
		int p = 1;
		for (int k = 0; k < n - i - 1; k++) p *= 10;
		digits[i] = v / p % 10;
	}
}

void TextBox::Update() {
	switch (st) {
	case State::Closing:
		transitionTimer++;
		if (transitionTimer < totalTransitionTime) { alpha = 1.0f - (float)transitionTimer / totalTransitionTime; break; }
		st = State::Closed;
		inputNumberVar = -1;
		transitionTimer = 0;
		alpha = 0;
		break;
	case State::Opening:
		transitionTimer++;
		if (transitionTimer < totalTransitionTime) { alpha = (float)transitionTimer / totalTransitionTime; break; }
		st = State::Opened;
		transitionTimer = 0;
		alpha = 1;
		break;
	case State::Opened:
		// Text is shown whole (no letter-by-letter roll yet).
		if (!choices.empty()) {
			if (choices.size() > 1) {
				if (input::pressed(input::BTN_UP)) { choiceCursor = (choiceCursor + (int)choices.size() - 1) % (int)choices.size(); audio::play_sound("menu_cursor"); }
				else if (input::pressed(input::BTN_DOWN)) { choiceCursor = (choiceCursor + 1) % (int)choices.size(); audio::play_sound("menu_cursor"); }
			}
			if (cancelChoice > 0 && cancel_pressed()) {
				audio::play_sound("menu_cancel");
				auto cb = choiceCallback;
				int v = cancelChoice - 1;
				Close();
				if (cb) cb(v);
			} else if (ok_pressed()) {
				audio::play_sound("menu_decision");
				auto cb = choiceCallback;
				int v = choiceCursor;
				Close();
				if (cb) cb(v);
			}
			break;
		}
		if (inputNumberVar >= 0) {
			int n = (int)digits.size();
			if (n > 1) {
				if (input::pressed(input::BTN_LEFT)) digitCursor = (digitCursor + n - 1) % n;
				else if (input::pressed(input::BTN_RIGHT)) digitCursor = (digitCursor + 1) % n;
			}
			if (input::pressed(input::BTN_UP)) digits[digitCursor] = (digits[digitCursor] + 1) % 10;
			else if (input::pressed(input::BTN_DOWN)) digits[digitCursor] = (digits[digitCursor] + 9) % 10;
			if (ok_pressed()) {
				int v = 0;
				for (int d : digits) v = v * 10 + d;
				state->set_var(inputNumberVar, v);
				audio::play_sound("menu_decision");
				Close();
			}
			break;
		}
		if (ok_pressed() || cancel_pressed() || mashing() || showChoicesFollows) Close();
		break;
	case State::Closed:
		break;
	}
}

// Strip message escape codes (\. \| \> \< \! \^ \c[n] \i[...] ...) for the
// placeholder renderer.
static std::string plain(const std::string &s) {
	std::string o;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == '\\' && i + 1 < s.size()) {
			i++;
			if (i + 1 < s.size() && s[i + 1] == '[') {
				size_t e = s.find(']', i);
				if (e != std::string::npos) i = e;
			}
			continue;
		}
		o += s[i];
	}
	return o;
}

// Greedy word wrap to `cols` characters.
static std::string wrap(const std::string &s, int cols) {
	std::string out, line, word;
	auto flush_word = [&]() {
		if (word.empty()) return;
		if (!line.empty() && (int)(line.size() + 1 + word.size()) > cols) { out += line + "\n"; line.clear(); }
		line += (line.empty() ? "" : " ") + word;
		word.clear();
	};
	for (char c : s) {
		if (c == ' ') flush_word();
		else if (c == '\n') { flush_word(); out += line + "\n"; line.clear(); }
		else word += c;
	}
	flush_word();
	out += line;
	return out;
}

void TextBox::Draw() {
	if (st == State::Closed) return;
	// Game-pixel box (320x240 space).
	Rect box(8, 168, 304, 64);
	if (area == Area::Up) box.y = 8;
	else if (area == Area::Middle) box.y = 88;
	if (kind == Kind::Doc || kind == Kind::Tutorial) box = Rect(24, 24, 272, 192);
	uint8_t a = (uint8_t)(alpha * 235);
	if (style == Style::GoldBorder || kind != Kind::Text) {
		uint8_t ba = (uint8_t)(alpha * 255);
		gm::color_box(Rect(box.x - 1, box.y - 1, box.w + 2, 1), 200, 160, 60, ba);
		gm::color_box(Rect(box.x - 1, box.y + box.h, box.w + 2, 1), 200, 160, 60, ba);
		gm::color_box(Rect(box.x - 1, box.y, 1, box.h), 200, 160, 60, ba);
		gm::color_box(Rect(box.x + box.w, box.y, 1, box.h), 200, 160, 60, ba);
		gm::color_box(box, 20, 14, 30, a);
	}
	if (alpha < 1.0f) return;
	float S = gm::window_scale();
	Vec2 o = gm::window_origin();
	float x = o.x + (box.x * 2 + 12) * S, y = o.y + (box.y * 2 + 10) * S;
	float ts = 2.0f * S;   // stb_easy_font is ~6px/char
	if (!portrait.empty()) {
		overlay::text_plain(x, y, S * 1.5f, 0xffd070, ("[" + portrait + "]").c_str());
		y += 12 * S * 1.5f;
	}
	int cols = (int)((box.w * 2 - 24) / (6.0f * 2.0f));
	std::string body = wrap(plain(text), cols);
	unsigned col = kind == Kind::Ed ? 0xc0e0ff : 0xffffff;
	overlay::text_plain(x, y, ts, col, body.c_str());
	int lines = 1;
	for (char c : body) lines += c == '\n';
	y += lines * 10 * ts;
	for (size_t i = 0; i < choices.size(); i++) {
		std::string c = ((int)i == choiceCursor ? "> " : "  ") + plain(choices[i]);
		overlay::text_plain(x, y, ts, (int)i == choiceCursor ? 0xffff80 : 0xc0c0c0, c.c_str());
		y += 10 * ts;
	}
	if (inputNumberVar >= 0) {
		std::string d;
		for (size_t i = 0; i < digits.size(); i++) {
			if ((int)i == digitCursor) d += "[";
			d += (char)('0' + digits[i]);
			if ((int)i == digitCursor) d += "]";
		}
		overlay::text_plain(x, y, ts, 0xffff80, d.c_str());
	}
}
