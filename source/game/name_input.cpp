#include "name_input.h"
#include "event_runner.h"
#include "game_state.h"
#include "../engine/audio.h"
#include "../engine/gm.h"
#include "../engine/ui.h"
#include "../platform/input.h"
#include <stdlib.h>

static const char *UPPER[50] = { "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q",
	"R", "S", "T", "U", "V", "W", "X", "Y", "Z", " ", ".", ",", "-", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
	"!", "?", "\"", "'", "/", "&", "[", "]", "(", ")" };
static const char *LOWER[50] = { "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q",
	"r", "s", "t", "u", "v", "w", "x", "y", "z", " ", ".", ",", "-", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
	"!", "?", "\"", "'", "/", "&", "[", "]", "(", ")" };
static const int GW = 10, GH = 5, MAXLEN = 16;

void NameInputMenu::Open() {
	name = state->flag(91) ? std::string() : state->player_name;
	st = 1;
	opacity = 0;
	cursorAlpha = 0.5f;
}

void NameInputMenu::Update() {
	switch (st) {
	case 1:
		opacity += 30;
		if (opacity >= 255) { opacity = 255; st = 2; }
		break;
	case 3:
		opacity -= 30;
		if (opacity <= 0) { opacity = 0; st = 0; }
		break;
	case 2:
		if (++cursorAnimTimer >= 32) cursorAnimTimer = 0;
		cursorAlpha = 0.5f + (float)abs(16 - cursorAnimTimer) / 32.0f;
		HandleCursorInput();
		break;
	}
}

void NameInputMenu::HandleCursorInput() {
	using namespace input;
	Vec2 old = cursor;
	auto dir = [](uint32_t b, uint32_t o) { return pressed(b) || (auto_trigger(b, 20, 4) && !held(o)); };
	if (dir(BTN_RIGHT, BTN_LEFT)) {
		cursor.x++;
		if (cursor.y < GH) { if (cursor.x >= GW) cursor.x = 0; }
		else if (cursor.x > 1) cursor.x = 0;
	} else if (dir(BTN_LEFT, BTN_RIGHT)) {
		cursor.x--;
		if (cursor.y < GH) { if (cursor.x < 0) cursor.x = GW - 1; }
		else if (cursor.x < 0) cursor.x = 1;
	}
	if (dir(BTN_UP, BTN_DOWN)) {
		if (cursor.y == GH && cursor.x > 0) cursor.x = GW - 1;
		if (--cursor.y < 0) cursor.y = 0;
	} else if (dir(BTN_DOWN, BTN_UP)) {
		cursor.y++;
		if (cursor.y == GH) cursor.x = cursor.x >= GW / 2 ? 1 : 0;
		if (cursor.y > GH) cursor.y = GH;
	}
	if (old != cursor) audio::play_sound("menu_cursor");
	if (pressed(BTN_OK)) {
		if (cursor.y == GH) {
			if (cursor.x == 0) {
				lower = !lower;
				audio::play_sound("menu_decision");
			} else if (!name.empty()) {
				audio::play_sound("menu_decision");
				if (state->flag(91)) owner->PasswordInput = name;
				else state->player_name = name;
				Close();
			}
		} else if ((int)name.size() >= MAXLEN) {
			audio::play_sound("menu_buzzer");
		} else {
			audio::play_sound("menu_decision");
			name += (lower ? LOWER : UPPER)[cursor.x + cursor.y * GW];
		}
	} else if (pressed(BTN_CANCEL) && !name.empty()) {
		audio::play_sound("menu_cancel");
		name.pop_back();
	}
}

static void cursor_box(const char *tex, int wx, int wy, int w, int h, float a) {
	const gfx::Texture *t = gm::texture(tex);
	if (t) gm::blit(t, Vec2(wx, wy), a, 0, gfx::Blend::Normal, 1);
	else gm::color_box(Rect(wx / 2, wy / 2, w / 2, h / 2), 255, 255, 255, (uint8_t)(a * 80));
}

void NameInputMenu::Draw() {
	if (st == 0) return;
	float a = opacity / 255.0f;
	gm::color_box(Rect(2, 2, 316, 236), 24, 12, 30, (uint8_t)(opacity * 200 / 255));
	ui::window_border(Rect(0, 0, 320, 64), gfx::Blend::Normal, a);
	ui::window_border(Rect(0, 64, 320, 176), gfx::Blend::Normal, a);
	Vec2 p(96, 48);
	for (int i = 0; i < MAXLEN; i++) {
		std::string ch = "_";
		if ((int)name.size() > i) ch = std::string(1, name[i]);
		else if ((int)name.size() == i) cursor_box("ui/name_select_letter_cursor", p.x, p.y, 28, 32, cursorAlpha * a);
		ui::text(p.x + 15, p.y + 5, ch, 0xffffff, a, ui::Align::Center);
		p.x += 28;
	}
	int gx = 320 - GW * 28 / 2;
	if (cursor.y < GH) cursor_box("ui/name_select_letter_cursor", gx + cursor.x * 28, 180 + cursor.y * 32, 28, 32, cursorAlpha * a);
	const char **letters = lower ? LOWER : UPPER;
	for (int i = 0; i < GW; i++)
		for (int j = 0; j < GH; j++)
			ui::text(gx + i * 28 + 15, 180 + j * 32 + 5, letters[i + j * GW], 0xffffff, a, ui::Align::Center);
	int by = 180 + GH * 32 + 8;
	if (cursor.y == GH) {
		int bx = cursor.x == 0 ? 320 - GW * 28 / 2 : 320 + GW * 28 / 2 - 80;
		cursor_box("ui/name_select_bottom_cursor", bx, by, 80, 32, cursorAlpha * a);
	}
	ui::text(gx + 40, by + 5, lower ? "Upper" : "Lower", 0xffffff, a, ui::Align::Center);
	ui::text(320 + GW * 28 / 2 - 40, by + 5, "OK", 0xffffff, a, ui::Align::Center);
	ui::text(320, 420, "Press (Circle) to delete characters.", 0xffffff, a, ui::Align::Center);
}
