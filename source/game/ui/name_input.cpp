#include "name_input.h"
#include "../world/event_runner.h"
#include "../core/game_state.h"
#include "../core/loc.h"
#include "../../engine/audio.h"
#include "../../engine/font.h"
#include "../../engine/gm.h"
#include "../../engine/ui.h"
#include "../../platform/input.h"
#include <stdlib.h>

// NameInputMenu's letter sets.
static const char *const UpperCaseLetters[50] = {
	"A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
	"K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
	"U", "V", "W", "X", "Y", "Z", " ", ".", ",", "-",
	"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
	"!", "?", "\"", "'", "/", "&", "[", "]", "(", ")",
};
static const char *const LowerCaseLetters[50] = {
	"a", "b", "c", "d", "e", "f", "g", "h", "i", "j",
	"k", "l", "m", "n", "o", "p", "q", "r", "s", "t",
	"u", "v", "w", "x", "y", "z", " ", ".", ",", "-",
	"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
	"!", "?", "\"", "'", "/", "&", "[", "]", "(", ")",
};
static const char *const Hiragana[90] = {
	"ー", "っ", "ぁ", "ぱ", "ば", "だ", "ざ", "が", "わ", "ら",
	"や", "ま", "は", "な", "た", "さ", "か", "あ", "～", "ゃ",
	"ぃ", "ぴ", "び", "ぢ", "じ", "ぎ", " ", "り", " ", "み",
	"ひ", "に", "ち", "し", "き", "い", "・", "ゅ", "ぅ", "ぷ",
	"ぶ", "づ", "ず", "ぐ", "を", "る", "ゆ", "む", "ふ", "ぬ",
	"つ", "す", "く", "う", "＝", "ょ", "ぇ", "ぺ", "べ", "で",
	"ぜ", "げ", " ", "れ", " ", "め", "へ", "ね", "て", "せ",
	"け", "え", "☆", "ゎ", "ぉ", "ぽ", "ぼ", "ど", "ぞ", "ご",
	"ん", "ろ", "よ", "も", "ほ", "の", "と", "そ", "こ", "お",
};
static const char *const Katakana[90] = {
	"ー", "ッ", "ァ", "パ", "バ", "ダ", "ザ", "ガ", "ワ", "ラ",
	"ヤ", "マ", "ハ", "ナ", "タ", "サ", "カ", "ア", "～", "ャ",
	"ィ", "ピ", "ビ", "ヂ", "ジ", "ギ", " ", "リ", " ", "ミ",
	"ヒ", "ニ", "チ", "シ", "キ", "イ", "・", "ュ", "ゥ", "プ",
	"ブ", "ヅ", "ズ", "グ", "ヲ", "ル", "ユ", "ム", "フ", "ヌ",
	"ツ", "ス", "ク", "ウ", "＝", "ョ", "ェ", "ペ", "ベ", "デ",
	"ゼ", "ゲ", " ", "レ", " ", "メ", "ヘ", "ネ", "テ", "セ",
	"ケ", "エ", "☆", "ヮ", "ォ", "ポ", "ボ", "ド", "ゾ", "ゴ",
	"ン", "ロ", "ヨ", "モ", "ホ", "ノ", "ト", "ソ", "コ", "オ",
};
static const char *const Romaji[90] = {
	"A", "B", "C", "D", "E", "F", "G", "H", "I", "a",
	"b", "c", "d", "e", "f", "g", "h", "i", "J", "K",
	"L", "M", "N", "O", "P", "Q", "R", "j", "k", "l",
	"m", "n", "o", "p", "q", "r", "S", "T", "U", "V",
	"W", "X", "Y", "Z", " ", "s", "t", "u", "v", "w",
	"x", "y", "z", " ", "0", "1", "2", "3", "4", "+",
	"-", "=", "&", "!", "?", "'", "\"", ".", ",", ":",
	";", "$", "5", "6", "7", "8", "9", "*", "/", "#",
	"|", "[", "]", "(", ")", "<", ">", "{", "}", "@",
};
static const char *const RuUpper[90] = {
	"A", "B", "C", "D", "E", "F", "G", "H", "I", "А",
	"Б", "В", "Г", "Д", "Е", "Ё", "Ж", "З", "J", "K",
	"L", "M", "N", "O", "P", "Q", "R", "И", "Й", "К",
	"Л", "М", "Н", "О", "П", "Р", "S", "T", "U", "V",
	"W", "X", "Y", "Z", " ", "С", "Т", "У", "Ф", "Х",
	"Ц", "Ч", "Ш", " ", "0", "1", "2", "3", "4", "+",
	"-", "=", "&", "Щ", "Ъ", "Ы", "Ь", "Э", "Ю", "Я",
	" ", " ", "5", "6", "7", "8", "9", "*", "/", "#",
	"|", "^", "%", "~", " ", " ", " ", " ", " ", " ",
};
static const char *const RuLower[90] = {
	"a", "b", "c", "d", "e", "f", "g", "h", "i", "а",
	"б", "в", "г", "д", "е", "ё", "ж", "з", "j", "k",
	"l", "m", "n", "o", "p", "q", "r", "и", "й", "к",
	"л", "м", "н", "о", "п", "р", "s", "t", "u", "v",
	"w", "x", "y", "z", " ", "с", "т", "у", "ф", "х",
	"ц", "ч", "ш", " ", "!", "?", "'", "\"", ".", ",",
	":", ";", "$", "щ", "ъ", "ы", "ь", "э", "ю", "я",
	" ", " ", "[", "]", "(", ")", "<", ">", "{", "}",
	"@", " ", " ", " ", " ", " ", " ", " ", " ", " ",
};
static const char *const EuUpper[90] = {
	"A", "B", "C", "D", "E", "F", "G", "H", "I", "À",
	"Á", "Â", "Ã", "Ä", "Å", "Æ", "Ç", "È", "J", "K",
	"L", "M", "N", "O", "P", "Q", "R", "É", "Ê", "Ë",
	"Ì", "Í", "Î", "Ï", "Ñ", "Ò", "S", "T", "U", "V",
	"W", "X", "Y", "Z", " ", "Ó", "Ô", "Õ", "Ö", "Ù",
	"Ú", "Û", "Ü", "Ý", "0", "1", "2", "3", "4", "+",
	"-", "=", "&", "!", "?", "'", "\"", ".", ",", ":",
	";", "$", "5", "6", "7", "8", "9", "*", "/", "#",
	"|", "[", "]", "(", ")", "<", ">", "{", "}", "@",
};
static const char *const EuLower[90] = {
	"a", "b", "c", "d", "e", "f", "g", "h", "i", "à",
	"á", "â", "ã", "ä", "å", "æ", "ç", "è", "j", "k",
	"l", "m", "n", "o", "p", "q", "r", "é", "ê", "ë",
	"ì", "í", "î", "ï", "ñ", "ò", "s", "t", "u", "v",
	"w", "x", "y", "z", " ", "ó", "ô", "õ", "ö", "ù",
	"ú", "û", "ü", "ý", "0", "1", "2", "3", "4", "+",
	"-", "=", "&", "¡", "¿", "'", "\"", ".", ",", ":",
	";", "$", "5", "6", "7", "8", "9", "*", "/", "#",
	"|", "[", "]", "(", ")", "<", ">", "{", "}", "@",
};

static const int MAXLEN = 16;

// The name's characters (UTF-8).
static std::vector<std::string> chars_of(const std::string &s) {
	std::vector<std::string> v;
	for (size_t i = 0; i < s.size();) {
		int n = font::utf8_len((unsigned char)s[i]);
		v.push_back(s.substr(i, n));
		i += n;
	}
	return v;
}

NameInputMenu::NameInputMenu(EventRunner *owner, GameState *state) : owner(owner), state(state) {
	const std::string &c = loc::code();
	if (c == "ja") { letters = Romaji; gw = 18; gh = 5; }
	else if (c == "ru") { letters = RuUpper; gw = 18; gh = 5; }
	else if (c == "es" || c == "fr" || c == "it" || c == "pt_br") { letters = EuUpper; gw = 18; gh = 5; }
	else { letters = UpperCaseLetters; gw = 10; gh = 5; }
}

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

void NameInputMenu::SwitchLetters() {
	const std::string &c = loc::code();
	if (c == "ja") letters = letters == Romaji ? Hiragana : letters == Hiragana ? Katakana : Romaji;
	else if (c == "ru") letters = letters == RuUpper ? RuLower : RuUpper;
	else if (c == "es" || c == "fr" || c == "it" || c == "pt_br") letters = letters == EuUpper ? EuLower : EuUpper;
	else letters = letters == UpperCaseLetters ? LowerCaseLetters : UpperCaseLetters;
}

std::string NameInputMenu::SwitchLabel() const {
	// DrawCharacterSwitchTexture: what the button switches to.
	if (letters == Romaji) return loc::twm("name_input_hiragana");
	if (letters == Hiragana) return loc::twm("name_input_katakana");
	if (letters == Katakana) return loc::twm("name_input_romaji");
	if (letters == UpperCaseLetters || letters == RuUpper || letters == EuUpper) return loc::twm("name_input_lower");
	return loc::twm("name_input_upper");
}

void NameInputMenu::HandleCursorInput() {
	using namespace input;
	Vec2 old = cursor;
	auto dir = [](uint32_t b, uint32_t o) { return pressed(b) || (auto_trigger(b, 20, 4) && !held(o)); };
	if (dir(BTN_RIGHT, BTN_LEFT)) {
		cursor.x++;
		if (cursor.y < gh) { if (cursor.x >= gw) cursor.x = 0; }
		else if (cursor.x > 1) cursor.x = 0;
	} else if (dir(BTN_LEFT, BTN_RIGHT)) {
		cursor.x--;
		if (cursor.y < gh) { if (cursor.x < 0) cursor.x = gw - 1; }
		else if (cursor.x < 0) cursor.x = 1;
	}
	if (dir(BTN_UP, BTN_DOWN)) {
		if (cursor.y == gh && cursor.x > 0) cursor.x = gw - 1;
		if (--cursor.y < 0) cursor.y = 0;
	} else if (dir(BTN_DOWN, BTN_UP)) {
		cursor.y++;
		if (cursor.y == gh) cursor.x = cursor.x >= gw / 2 ? 1 : 0;
		if (cursor.y > gh) cursor.y = gh;
	}
	if (old != cursor) audio::play_sound("menu_cursor");
	if (pressed(BTN_OK)) {
		if (cursor.y == gh) {
			if (cursor.x == 0) {
				SwitchLetters();
				audio::play_sound("menu_decision");
			} else if (!name.empty()) {
				audio::play_sound("menu_decision");
				if (state->flag(91)) owner->PasswordInput = name;
				else state->player_name = name;
				Close();
			}
		} else if ((int)chars_of(name).size() >= MAXLEN) {
			audio::play_sound("menu_buzzer");
		} else {
			audio::play_sound("menu_decision");
			name += letters[cursor.x + cursor.y * gw];
		}
	} else if (pressed(BTN_CANCEL) && !name.empty()) {
		audio::play_sound("menu_cancel");
		std::vector<std::string> c = chars_of(name);
		c.pop_back();
		name.clear();
		for (auto &s : c) name += s;
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
	std::vector<std::string> nc = chars_of(name);
	Vec2 p(96, 48);
	for (int i = 0; i < MAXLEN; i++) {
		std::string ch = "_";
		if ((int)nc.size() > i) ch = nc[i];
		else if ((int)nc.size() == i) cursor_box("ui/name_select_letter_cursor", p.x, p.y, 28, 32, cursorAlpha * a);
		font::draw_centered(font::Game, Vec2(p.x + 14 + 1, p.y + 5), ch, 0xffffff, a, 1);
		p.x += 28;
	}
	int gx = 320 - gw * 28 / 2;
	if (cursor.y < gh) cursor_box("ui/name_select_letter_cursor", gx + cursor.x * 28, 180 + cursor.y * 32, 28, 32, cursorAlpha * a);
	for (int i = 0; i < gw; i++)
		for (int j = 0; j < gh; j++)
			font::draw_centered(font::Game, Vec2(gx + i * 28 + 15, 180 + j * 32 + 5), letters[i + j * gw], 0xffffff, a, 1);
	int by = 180 + gh * 32 + 8;
	if (cursor.y == gh) {
		int bx = cursor.x == 0 ? 320 - gw * 28 / 2 : 320 + gw * 28 / 2 - 80;
		cursor_box("ui/name_select_bottom_cursor", bx, by, 80, 32, cursorAlpha * a);
	}
	font::draw_fit(font::Game, Vec2(gx + 40, by + 5), SwitchLabel(), 0xffffff, a, 76, true);
	font::draw_fit(font::Game, Vec2(320 + gw * 28 / 2 - 40, by + 5), loc::twm("name_input_ok"), 0xffffff, a, 0, true);
	const std::string &del = loc::twm("name_input_delete_instruction");
	font::draw_fit(font::Game, Vec2(320, 420), del, 0xffffff, a, 0, true);
	font::draw_centered(font::Game, Vec2(320, 420), del, 0xffffff, a, 1, font::Mode::OnlyGlyphs);
}
