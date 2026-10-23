// message_boxes.cpp - EdBox, DesktopBox, TutorialBox, DocBox, CreditsBox
// (OneShotMG.src.MessageBox). The C# renders each box's text into a temp
// texture once and blits it; here the text is drawn directly each frame at
// the same positions, which looks the same and needs no render targets.
#include "message_box.h"
#include "loc.h"
#include "game_state.h"
#include "../engine/audio.h"
#include "../engine/font.h"
#include "../engine/gm.h"
#include "../platform/input.h"

static bool close_pressed() {
	return input::pressed(input::BTN_OK) || input::pressed(input::BTN_CANCEL) || auto_mashing();
}

static void replace_all(std::string &s, const std::string &from, const std::string &to) {
	size_t p = 0;
	while ((p = s.find(from, p)) != std::string::npos) { s.replace(p, from.size(), to); p += to.size(); }
}

// FeedText's common substitutions: "\n" escapes and the player name.
static std::string prepare(std::string t, const std::string &playerName) {
	replace_all(t, "\\n", "\n");
	replace_all(t, "\\p", playerName);
	return t;
}

void BoxFade::update() {
	switch (st) {
	case Closing:
		if (++timer < total) { alpha = 1.0f - (float)timer / total; break; }
		st = Closed;
		timer = 0;
		alpha = 0;
		break;
	case Opening:
		if (++timer < total) { alpha = (float)timer / total; break; }
		st = Opened;
		timer = 0;
		alpha = 1;
		break;
	default:
		break;
	}
}

static uint8_t a8(float a) { return (uint8_t)(255.0f * (a < 0 ? 0 : a > 1 ? 1 : a)); }

// ===================================================================== EdBox
EdBox::EdBox(GameState *s) : state(s) { Open(); }

void EdBox::FeedText(const std::string &text, const std::string &playerName) {
	std::string t = TextBox::ReplaceVariableMarkup(state, prepare(text, playerName));
	lines = font::word_wrap(font::Game, t, 608);
	widths.clear();
	for (const std::string &l : lines) {
		std::string plain = l;
		for (int i = 0; i < 8; i++) replace_all(plain, "\\c[" + std::to_string(i) + "]", "");
		widths.push_back(font::width(font::Game, plain));
	}
}

void EdBox::Update() {
	fade.update();
	if (fade.st == BoxFade::Opened && close_pressed()) fade.close(13);
}

void EdBox::Draw() {
	if (fade.st == BoxFade::Closed) return;
	gm::color_box(Rect(0, 0, 320, 240), 0, 0, 0, (uint8_t)(128.0f * fade.alpha));
	int maxw = 0;
	for (int w : widths) maxw = w > maxw ? w : maxw;
	int texW = maxw + 4, texH = 24 * (int)lines.size();
	int ox = 320 - texW / 2, oy = 240 - texH / 2;
	unsigned col = 0xffffff;
	for (size_t i = 0; i < lines.size(); i++) {
		int x = ox + (texW - widths[i]) / 2, y = oy + 24 * (int)i;
		std::string t = lines[i];
		while (!t.empty()) {
			size_t p = t.find("\\c[");
			if (p == std::string::npos) {
				font::draw(font::Game, Vec2(x, y), t, col, fade.alpha, 1);
				break;
			}
			std::string run = t.substr(0, p);
			x += font::draw(font::Game, Vec2(x, y), run, col, fade.alpha, 1);
			t = t.substr(p + 3);
			if (!t.empty()) col = TextBox::TextColor(t[0] - '0');
			t = t.size() >= 2 ? t.substr(2) : std::string();
		}
	}
}

// ===================================================================== DesktopBox
DesktopBox::DesktopBox() { Open(); }

void DesktopBox::Open() {
	fade.open(2);
	audio::play_sound("pc_messagebox", 0.9f, 1.5f);
}

void DesktopBox::FeedText(const std::string &text, const std::string &playerName) {
	lines = font::word_wrap(font::Game, prepare(text, playerName), 608);
}

void DesktopBox::Update() {
	fade.update();
	if (fade.st == BoxFade::Opened && close_pressed()) fade.close(2);
}

void DesktopBox::Draw() {
	if (fade.st == BoxFade::Closed) return;
	const gfx::Texture *bg = gm::texture("pictures/cg_desktop_messagebox");
	if (bg) {
		gm::blit(bg, Vec2(0, 0), fade.alpha);
	} else {   // stand-in: a plain grey dialog
		gm::color_box(Rect(60, 80, 200, 80), 192, 192, 192, a8(fade.alpha));
		gm::color_box(Rect(60, 80, 200, 10), 0, 0, 128, a8(fade.alpha));
	}
	int maxw = 0;
	for (const std::string &l : lines) {
		int w = font::width(font::Game, l, true);
		maxw = w > maxw ? w : maxw;
	}
	int texW = maxw + 4, texH = 24 * (int)lines.size();
	int ox = 320 - texW / 2, oy = 240 - texH / 2;
	for (size_t i = 0; i < lines.size(); i++)
		font::draw_centered(font::Game, Vec2(ox + texW / 2, oy + 24 * (int)i), lines[i], 0x000000, fade.alpha, 1);
}

// ===================================================================== TutorialBox
TutorialBox::TutorialBox(bool right) : rightAligned(right) { Open(); }

int TutorialBox::LineHeight() const {
	if (rightAligned) return 15;
	const std::string &c = loc::code();
	return c == "ko" || c == "ja" || c == "zh_cn" || c == "zh_cht" ? 17 : 13;
}

void TutorialBox::FeedText(const std::string &text, const std::string &playerName) {
	lines = font::word_wrap(font::GameSmall, prepare(text, playerName), 272);
}

// MeasureLineLengths: the title in the Game font, the rest in GameSmall.
int TutorialBox::LineLength(size_t i) const {
	return font::width(i == 0 ? font::Game : font::GameSmall, lines[i], true);
}

void TutorialBox::Update() {
	fade.update();
	if (fade.st == BoxFade::Opened && close_pressed()) fade.close(30);
}

void TutorialBox::Draw() {
	if (fade.st == BoxFade::Closed) return;
	float a = fade.alpha, a2 = a * a;
	const unsigned brown = 0x864107, orange = 0xfb8200, white = 0xffffff;
	const gfx::Texture *bg = gm::texture("pictures/instruction_bg");
	if (bg) gm::blit(bg, Vec2(0, 0), a);
	else gm::color_box(Rect(0, 0, 320, 240), 20, 10, 4, (uint8_t)(220 * a));
	static const Vec2 o1[4] = { Vec2(1, 0), Vec2(-1, 0), Vec2(0, 1), Vec2(0, -1) };
	static const Vec2 o2[4] = { Vec2(2, 0), Vec2(-2, 0), Vec2(0, 2), Vec2(0, -2) };
	Vec2 pos(160, 20);
	for (size_t i = 0; i < lines.size(); i++) {
		const std::string &t = lines[i];
		if (i == 0) {
			// Title: Game font doubled, orange outer and white inner outline.
			Vec2 p(pos.x - LineLength(0) / 2 - 2, pos.y);
			for (const Vec2 &d : o2) font::draw(font::Game, p + d, t, orange, a2, 2, font::Mode::OnlyText);
			for (const Vec2 &d : o1) font::draw(font::Game, p + d, t, white, a2, 2, font::Mode::OnlyText);
			font::draw(font::Game, p, t, brown, a2, 2, font::Mode::OnlyText);
		} else {
			Vec2 p = pos;
			if (rightAligned) p.x += 60 - LineLength(i);
			else p.x -= LineLength(i) / 2;
			// Body text with a 1px brown outline, then the button glyphs on top.
			for (const Vec2 &d : o1) font::draw(font::GameSmall, p + d, t, brown, a2, 2, font::Mode::OnlyText);
			font::draw(font::GameSmall, p, t, white, a2, 2, font::Mode::OnlyText);
			font::draw(font::GameSmall, p, t, white, a2, 2, font::Mode::OnlyGlyphs);
		}
		pos.y += LineHeight();
	}
}

// ===================================================================== DocBox
static const int DOC_LINES = 11;

DocBox::DocBox(GameState *s) : state(s) { Open(); }

void DocBox::Open() {
	fade.open(10);
	audio::play_sound("page");
}

void DocBox::FeedText(const std::string &text, const std::string &playerName) {
	lines = font::word_wrap(font::GameSmall, prepare(text, playerName), 184);
}

void DocBox::Update() {
	fade.update();
	if (fade.st != BoxFade::Opened) return;
	if (startLine > 0 && input::pressed(input::BTN_UP)) startLine--;
	else if (startLine + DOC_LINES < (int)lines.size() && input::pressed(input::BTN_DOWN)) startLine++;
	if (close_pressed()) fade.close(10);
}

void DocBox::Draw() {
	if (fade.st == BoxFade::Closed) return;
	float a = fade.alpha;
	bool red = state->flag(124);
	unsigned tint = red ? 0xff5c64 : 0x645cff;
	const char *paper = red ? "pictures/lined_paper_red" : "pictures/lined_paper_blue";
	const gfx::Texture *bg = gm::texture(paper);
	if (bg) gm::blit(bg, Vec2(60, 12), a * 0.86f);
	else gm::color_box(Rect(60, 12, 200, 216), 240, 240, 232, (uint8_t)(219 * a));
	if (startLine > 0)
		if (const gfx::Texture *t = gm::texture(red ? "pictures/scroll_up_red" : "pictures/scroll_up_blue"))
			gm::blit(t, Vec2(262, 12), a);
	if (startLine + DOC_LINES < (int)lines.size())
		if (const gfx::Texture *t = gm::texture(red ? "pictures/scroll_down_red" : "pictures/scroll_down_blue"))
			gm::blit(t, Vec2(262, 212), a);
	// White text tinted by the ink colour at 180/255 alpha.
	float ta = 180.0f / 255.0f * a;
	Vec2 p(68, 20);
	for (int i = startLine; i < startLine + DOC_LINES && i < (int)lines.size(); i++) {
		font::draw(font::GameSmall, p, lines[i], tint, ta, 2);
		p.y += 18;
	}
}

// ===================================================================== CreditsBox
CreditsBox::CreditsBox(GameState *s, bool sm) : state(s), small(sm) { Open(); }

void CreditsBox::FeedText(const std::string &text, const std::string &) {
	lines.clear();
	std::string t = text;
	replace_all(t, "\\n", "\n");
	size_t p = 0;
	while (true) {
		size_t e = t.find('\n', p);
		std::string s = t.substr(p, e == std::string::npos ? std::string::npos : e - p);
		Line l{ "", "", "", false };
		if (s.compare(0, 2, "\\h") == 0) { s = s.substr(2); l.header = true; }
		if (s.compare(0, 2, "\\l") == 0) {
			size_t r = s.find("\\r");
			if (r != std::string::npos) { l.left = s.substr(2, r - 2); l.right = s.substr(r + 2); }
			else l.left = s.substr(2);
		} else {
			l.center = s;
		}
		if (l.left.empty() && l.right.empty() && l.center.empty()) l.header = true;
		lines.push_back(l);
		if (e == std::string::npos) break;
		p = e + 1;
	}
}

void CreditsBox::Update() {
	switch (fade.st) {
	case BoxFade::Opening:
		fade.update();
		if (fade.st == BoxFade::Opened) fade.total = small ? 250 : 150;   // stay-open time
		break;
	case BoxFade::Opened:
		if (++fade.timer > fade.total) fade.close(51);
		break;
	default:
		fade.update();
		break;
	}
}

void CreditsBox::Draw() {
	if (fade.st == BoxFade::Closed) return;
	bool inverted = state->flag(104);
	uint8_t bg = inverted ? 0 : 255;
	unsigned fg = inverted ? 0xffffff : 0x000000;
	gm::color_box(Rect(0, 0, 320, 240), bg, bg, bg, a8(fade.alpha));
	int scale = small ? 1 : 2, num = small ? 2 : 1;
	int total = 0;
	for (const Line &l : lines) total += l.header ? 18 : 24;
	int y = 240 * num / 2 - total / 2;
	for (const Line &l : lines) {
		font::Type f = l.header ? font::GameSmall : font::Game;
		if (!l.left.empty()) font::draw(f, Vec2(20 * num, y), l.left, fg, fade.alpha, scale);
		if (!l.center.empty()) font::draw_centered(f, Vec2(320 * num / 2, y), l.center, fg, fade.alpha, scale);
		if (!l.right.empty())
			font::draw(f, Vec2(300 * num - font::width(f, l.right), y), l.right, fg, fade.alpha, scale);
		y += l.header ? 18 : 24;
	}
}
