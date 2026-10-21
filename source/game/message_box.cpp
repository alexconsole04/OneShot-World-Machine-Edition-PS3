#include "message_box.h"
#include "game_state.h"
#include "player.h"
#include "tilemap.h"
#include "../engine/audio.h"
#include "../engine/font.h"
#include "../engine/gm.h"
#include "../engine/ui.h"
#include "../engine/util.h"
#include "../platform/input.h"
#include "../platform/sys.h"
#include <stdlib.h>
#include <string.h>

static bool mashing() { return auto_mashing(); }

unsigned TextBox::TextColor(int code) {
	switch (code) {
	case 1: return 0xff4040;
	case 2: return 0x00e000;
	case 3: return 0xffff00;
	case 4: return 0x4040ff;
	case 5: return 0xff40ff;
	case 6: return 0x40ffff;
	case 7: return 0x808080;
	default: return 0xffffff;
	}
}

TextBox::TextBox(TileMap *m, Style sty, Area ar) : map(m), state(m->State()), style(sty), area(ar) {
	boxY = area == Area::Down ? 168 : (area == Area::Middle ? 80 : 8);
	st = State::Opening;
	transitionTimer = 0;
	totalTransitionTime = 6;
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
		choiceBeginsOnLine = 0;
		choiceCursor = 0;
	}
}

void TextBox::ClearText() {
	lineWidth = 0;
	inputText.clear();
	inputNumberVar = -1;
	lines.clear();
	portrait.clear();
	showChoicesFollows = false;
	color = 0xffffff;
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

void TextBox::SetPortrait(const std::string &p) {
	portrait = util::lower(p);
	const std::string &sheet = map->GetPlayer()->GetNPCSheet();
	if ((sheet == "niko_gasmask" || sheet == "niko_bulb_gasmask" || sheet == "en_gasmask" || sheet == "en_bulb_gasmask") &&
	    util::starts_with(portrait, "niko"))
		portrait = "niko_gasmask";
	if (state->flag(160) && util::starts_with(portrait, "niko")) portrait.replace(0, 4, "en");
}

int TextBox::TextAreaWidth() const { return boxW - padX - padW - (portrait.empty() ? 0 : 48); }

void TextBox::FeedText(const std::string &in, const std::string &playerName) {
	std::string t = in;
	replace_all(t, "\\\\", "\\");
	replace_all(t, "\\n", "\n");
	t = ReplaceVariableMarkup(state, t);
	replace_all(t, "\\p", playerName);
	// Button glyph tokens (the PC build draws key/pad icons here).
	static const char *glyphs[][2] = { { "@OK", "(Cross)" }, { "@CANCEL", "(Circle)" }, { "@RUN", "(Square)" },
		{ "@INVENTORY", "(Triangle)" }, { "@MAINMENU", "(START)" }, { "@MOVE", "(D-pad)" } };
	for (auto &g : glyphs) replace_all(t, g[0], g[1]);
	if (inputText.empty()) {
		bool lead_nl = false;
		if (!t.empty() && t[0] == '\n') { t = t.substr(1); lead_nl = true; }
		if (!t.empty() && t[0] == '@') {
			size_t sp = t.find(' ');
			if (sp != std::string::npos) { SetPortrait(t.substr(1, sp - 1)); t = t.substr(sp + 1); }
			else { SetPortrait(t.substr(1)); t.clear(); }
		}
		if (lead_nl) t = "\n" + t;
		inputText = t;
		scrollSoundTimer = 4;
	} else {
		inputText += t;
	}
}

void TextBox::SetChoices(const std::vector<std::string> &c, int cancelValue, std::function<void(int)> cb,
                         const std::string &playerName) {
	choices = c;
	cancelChoice = cancelValue;
	choiceCallback = std::move(cb);
	choiceBeginsOnLine = (int)lines.size();
	std::string text;
	for (size_t i = 0; i < c.size(); i++) text += (i ? "\n" : "") + c[i];
	if (!lines.empty()) text = "\n" + text;
	FeedText(text, playerName);
	RollOutAllText();
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

void TextBox::RollOutAllText() {
	int guard = 100000;
	while (IsRollingText() && --guard) ProcessInputText(true);
}

void TextBox::addNewLine() {
	lines.emplace_back();
	lineWidth = 0;
}

static std::string utf8(uint32_t c) {
	std::string o;
	if (c < 0x80) o += (char)c;
	else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 63)); }
	else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
	else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 63)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
	return o;
}

// Width of one character in the Game font (TextBox measures char by char).
static int char_w(uint32_t c) { return font::width(font::Game, utf8(c)); }

void TextBox::addNewChar(uint32_t c) {
	if (lines.empty()) lines.emplace_back();
	lines.back().push_back({ c, color });
	lineWidth += char_w(c);
}

void TextBox::ProcessInputText(bool fastCycle) {
	if (!fastCycle || (lines.empty() && choices.empty())) {
		if (++scrollSoundTimer > 4) {
			scrollSoundTimer = 0;
			std::string snd = portrait == "af" ? "cat_2" : (!inputText.empty() && inputText[0] == '[') ? "text_robot" : "text";
			audio::play_sound(snd.c_str(), 0.5f);
		}
	}
	if (lines.empty()) lines.emplace_back();
	scrollTimer += 1.0f;
	while (scrollTimer >= 1.0f && !inputText.empty() && !forcedNod && textPauseTimer <= 0) {
		char c = inputText[0];
		size_t consume = 1;
		scrollTimer -= 1.0f;
		if (c == '\n') {
			addNewLine();
		} else if (c == '\\') {
			inputText = inputText.substr(1);
			if (inputText.empty()) break;
			c = inputText[0];
			switch (c) {
			case '@': {
				size_t sp = inputText.find(' ');
				if (sp == std::string::npos) sp = inputText.size();
				SetPortrait(inputText.substr(1, sp - 1));
				inputText = inputText.substr(sp);   // the space is consumed below
				if (inputText.empty()) inputText = " ";
				break;
			}
			case 'n': addNewLine(); break;
			case 'c':
				if (inputText.size() >= 4) {
					color = TextColor(inputText[2] - '0');
					inputText = inputText.substr(3);
				}
				break;
			case '>': forcedNod = true; break;
			case '.': if (!mashing()) { textPauseTimer = 10; scrollSoundTimer = 0; } break;
			case '|': if (!mashing()) { textPauseTimer = 40; scrollSoundTimer = 0; } break;
			default: addNewChar((unsigned char)c); break;
			}
		} else {
			bool add = true;
			int maxw = TextAreaWidth() * 2;
			if (c == ' ' && inputText.size() > 1) {
				std::string rest = inputText.substr(1);
				size_t e = rest.find_first_of(" \n");
				std::string word = " " + rest.substr(0, e);
				for (const char *esc : { "\\>", "\\.", "\\|" }) replace_all(word, esc, "");
				if (font::width(font::Game, word) + lineWidth > maxw) { addNewLine(); add = false; }
			}
			consume = (size_t)font::utf8_len((unsigned char)c);
			if (add) {
				size_t k = 0;
				uint32_t cp = font::decode_utf8(inputText, k);
				if (char_w(cp) + lineWidth > maxw) addNewLine();
				addNewChar(cp);
			}
		}
		inputText = inputText.substr(consume < inputText.size() ? consume : inputText.size());
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
		if (++cursorAnimTimer >= 32) cursorAnimTimer = 0;
		cursorAlpha = 0.5f + (float)abs(16 - cursorAnimTimer) / 32.0f;
		if (textPauseTimer > 0 && --textPauseTimer > 0) break;
		if (!inputText.empty() && !forcedNod) {
			if (input::pressed(input::BTN_OK) || input::pressed(input::BTN_CANCEL) || mashing()) RollOutAllText();
			else ProcessInputText();
			break;
		}
		if (!choices.empty()) { HandleChoiceInput(); break; }
		if (inputNumberVar >= 0) { HandleNumberInput(); break; }
		if (input::pressed(input::BTN_OK) || input::pressed(input::BTN_CANCEL) || mashing() || showChoicesFollows) {
			if (!forcedNod) Close();
			else forcedNod = false;
		}
		if (++nodTimer >= 8) { nodTimer = 0; nodIndex = (nodIndex + 1) % 4; }
		break;
	case State::Closed:
		break;
	}
}

void TextBox::HandleChoiceInput() {
	using namespace input;
	if (choices.size() > 1) {
		bool up = pressed(BTN_UP) || (auto_trigger(BTN_UP, 20, 4) && !held(BTN_DOWN));
		bool down = pressed(BTN_DOWN) || (auto_trigger(BTN_DOWN, 20, 4) && !held(BTN_UP));
		if (up) { choiceCursor = (choiceCursor + (int)choices.size() - 1) % (int)choices.size(); audio::play_sound("menu_cursor"); }
		else if (down) { choiceCursor = (choiceCursor + 1) % (int)choices.size(); audio::play_sound("menu_cursor"); }
	}
	if (cancelChoice > 0 && pressed(BTN_CANCEL)) {
		audio::play_sound("menu_cancel");
		auto cb = choiceCallback;
		int v = cancelChoice - 1;
		if (cb) cb(v);
		Close();
	} else if (pressed(BTN_OK)) {
		audio::play_sound("menu_decision");
		auto cb = choiceCallback;
		int v = choiceCursor;
		if (cb) cb(v);
		Close();
	}
}

void TextBox::HandleNumberInput() {
	using namespace input;
	int n = (int)digits.size();
	bool right = pressed(BTN_RIGHT) || (auto_trigger(BTN_RIGHT, 20, 4) && !held(BTN_LEFT));
	bool left = pressed(BTN_LEFT) || (auto_trigger(BTN_LEFT, 20, 4) && !held(BTN_RIGHT));
	bool up = pressed(BTN_UP) || (auto_trigger(BTN_UP, 20, 4) && !held(BTN_DOWN));
	bool down = pressed(BTN_DOWN) || (auto_trigger(BTN_DOWN, 20, 4) && !held(BTN_UP));
	if (n > 1) {
		if (left) { digitCursor = (digitCursor + n - 1) % n; audio::play_sound("menu_cursor"); }
		else if (right) { digitCursor = (digitCursor + 1) % n; audio::play_sound("menu_cursor"); }
	}
	if (up) { digits[digitCursor] = (digits[digitCursor] + 1) % 10; audio::play_sound("menu_cursor"); }
	else if (down) { digits[digitCursor] = (digits[digitCursor] + 9) % 10; audio::play_sound("menu_cursor"); }
	if (pressed(BTN_OK)) {
		audio::play_sound("menu_decision");
		int v = 0;
		for (int d : digits) v = v * 10 + d;
		state->set_var(inputNumberVar, v);
		Close();
	}
}

void TextBox::Draw() {
	if (st == State::Closed) return;
	Rect box(boxX, boxY, boxW, boxH);
	if (style == Style::GoldBorder) {
		gm::color_box(Rect(box.x + 4, box.y + 4, box.w - 8, box.h - 8), 24, 12, 30, (uint8_t)(210 * alpha));
		ui::window_border(box, gfx::Blend::Normal, alpha);
	}
	if (!portrait.empty()) {
		const gfx::Texture *fp = gm::texture(("facepics/" + portrait).c_str());
		Vec2 p(box.x + box.w - 48 - 8, box.y + 8);
		if (portrait == "af") { p.x *= 2; p.y *= 2; }   // the cat's facepic is drawn at window scale
		if (fp) gm::blit(fp, p, alpha, 0, gfx::Blend::Normal, portrait == "af" ? 1 : 2);
	}
	int tx = (box.x + padX) * 2, ty = (box.y + padY) * 2;
	// Number input: cursor + digits on the line after the text.
	if (inputNumberVar >= 0 && !IsRollingText()) {
		int cw = ui::text_width("0") + 10;
		int y = ty + (int)lines.size() * 24 + 4;
		for (size_t i = 0; i < digits.size(); i++) {
			unsigned col = 0xffffff;
			if (state->flag(179)) {
				static const int map4[4] = { 4, 2, 1, 3 };
				col = TextColor(map4[i % 4]);
			}
			int x = tx + (int)i * cw;
			if ((int)i == digitCursor) gm::color_box(Rect(x / 2 - 1, y / 2 - 1, cw / 2, 11), 255, 255, 255, (uint8_t)(cursorAlpha * alpha * 90));
			ui::text(x + 2, y, std::string(1, (char)('0' + digits[i])), col, alpha);
		}
	}
	if (!choices.empty() && !IsRollingText()) {
		int y = ty + (choiceBeginsOnLine + choiceCursor) * 24 - 2;
		const gfx::Texture *cc = gm::texture(portrait.empty() ? "ui/choice_cursor" : "ui/choice_cursor_w_facepic");
		if (cc) gm::blit(cc, Vec2(tx - 4, y), cursorAlpha * alpha, 0, gfx::Blend::Normal, 1);
		else gm::color_box(Rect(tx / 2 - 2, y / 2, TextAreaWidth() + 4, 12), 255, 255, 255, (uint8_t)(cursorAlpha * alpha * 60));
	}
	// Text, in runs of one colour.
	for (size_t l = 0; l < lines.size(); l++) {
		int x = tx;
		const auto &ln = lines[l];
		size_t i = 0;
		while (i < ln.size()) {
			std::string run;
			unsigned col = ln[i].rgb;
			while (i < ln.size() && ln[i].rgb == col) run += utf8(ln[i++].c);
			x += ui::text(x, ty + (int)l * 24, run, col, alpha);
		}
	}
	if (!IsRollingText() && choices.empty() && inputNumberVar < 0 && (inputText.empty() || forcedNod) && st != State::Closing) {
		static const int nod[4] = { 0, 1, 2, 1 };
		const gfx::Texture *ar = gm::texture("ui/item_box_arrows");
		if (ar)
			gm::blit(ar, Vec2((box.x + box.w / 2) * 2 - 8, (box.y + box.h) * 2 - 13 + nod[nodIndex]), Rect(0, 8, 16, 8),
			         alpha, 0, gfx::Blend::Normal, 1);
	}
}
