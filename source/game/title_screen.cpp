#include "title_screen.h"
#include "game_state.h"
#include "loc.h"
#include "menus.h"
#include "../engine/audio.h"
#include "../engine/font.h"
#include "../engine/gm.h"
#include "../platform/input.h"

void TitleScreen::Open(GameState *s, MenuManager *m) {
	state = s;
	menus = m;
	audio::play_song("MyBurdenIsLight", 1.0f, 1.0f, 1.0f);
	fadeOpacity = 255;
	st = Opening;
	selected = 0;
	helpTimer = helpFadeIn = 0;
	anySelected = cursorMoved = showHelp = false;
	// "..." (replay a memory) appears after a finished World Machine run.
	optionCount = state->flag(160) && state->flag(152) ? 4 : 3;
}

void TitleScreen::Close() {
	audio::fade_out_bgm(0.5f);
	fadeOpacity = 0;
	st = Closing;
}

void TitleScreen::Update() {
	if (!anySelected && !showHelp) {
		helpTimer++;
		if ((!cursorMoved && helpTimer >= 600) || helpTimer >= 1200) showHelp = true;
	}
	if (showHelp && helpFadeIn < 60) helpFadeIn++;
	switch (st) {
	case Opening:
		fadeOpacity -= 5;
		if (fadeOpacity <= 0) { fadeOpacity = 0; st = Shown; }
		break;
	case Closing:
		fadeOpacity += 5;
		if (fadeOpacity >= 255) {
			fadeOpacity = 255;
			st = Closed;
			if (selected == Start) state->set_flag(157, false);
			if (selected == Memory) state->set_flag(157, true);
			if (onChosen) onChosen(selected);
		}
		break;
	case Shown: {
		if (menus->IsMenuOpen()) break;
		int n = selected;
		if (input::pressed(input::BTN_UP)) { if (--n < 0) n = 0; }
		else if (input::pressed(input::BTN_DOWN)) { if (++n > optionCount - 1) n = optionCount - 1; }
		if (n != selected) {
			cursorMoved = true;
			selected = n;
			audio::play_sound("title_cursor", 0.5f);
		}
		if (input::pressed(input::BTN_OK)) {
			anySelected = true;
			if (selected == SettingsOpt) {
				menus->ShowMenu(MenuManager::Menu::Settings);
			} else {
				audio::play_sound("title_decision");
				Close();
			}
		}
		break;
	}
	case Closed:
		break;
	}
}

void TitleScreen::Draw() {
	if (st == Closed) return;
	if (const gfx::Texture *bg = gm::texture("titles/normal")) gm::blit(bg, Vec2(0, 0));
	else font::draw_centered(font::Game, Vec2(160, 70), "OneShot", 0xffffff, 1.0f, 2);
	static const char *keys[4] = { "title_option_start", "title_option_settings", "title_option_exit", "title_option_dots" };
	Vec2 p(488, 380);
	for (int i = 0; i < optionCount; i++) {
		font::draw(font::Game, p, loc::twm(keys[i]), 0xffffff, 1.0f, 1);
		p.y += 25;
		if (i == selected) {
			Vec2 c(p.x / 2 - 5, p.y / 2 - 10);
			if (const gfx::Texture *cur = gm::texture("ui/title_cursor")) gm::blit(cur, c);
			else font::draw(font::Game, Vec2(c.x * 2 - 4, p.y - 25), ">", 0xffffff, 1.0f, 1);
		}
	}
	if (helpFadeIn > 0) {
		float a = helpFadeIn * 180 / 60 / 255.0f;
		const std::string &h1 = loc::twm("title_help_text_1"), &h2 = loc::twm("title_help_text_2");
		int w1 = font::width(font::Game, h1, true), w2 = font::width(font::Game, h2, true);
		font::draw(font::Game, Vec2(620 - w1 - 2, 32), h1, 0xffffff, a, 1, font::Mode::OnlyText);
		font::draw(font::Game, Vec2(620 - w1, 32), h1, 0xffffff, a, 1, font::Mode::OnlyGlyphs);
		font::draw(font::Game, Vec2(620 - w2 - 2, 84), h2, 0xffffff, a, 1, font::Mode::OnlyText);
		font::draw(font::Game, Vec2(620 - w2, 84), h2, 0xffffff, a, 1, font::Mode::OnlyGlyphs);
	}
	gm::color_box(Rect(0, 0, 320, 240), 0, 0, 0, (uint8_t)fadeOpacity);
}
