#include "menus.h"
#include "tilemap.h"
#include "game_state.h"
#include "../engine/audio.h"
#include "../engine/gm.h"
#include "../engine/ui.h"
#include "../engine/font.h"
#include "loc.h"
#include "modal.h"
#include "boot_sequence.h"
#include "settings.h"
#include "twm/wm.h"
#include <algorithm>
#include "../platform/input.h"
#include "../platform/sys.h"
#include "cJSON.h"
#include <stdlib.h>

ItemMenu::ItemMenu(TileMap *m, GameState *s) : map(m), state(s) {
	static const int c[][3] = {
		{ 3, 4, 5 }, { 8, 9, 10 }, { 10, 12, 13 }, { 1, 13, 14 }, { 1, 12, 102 }, { 16, 20, 17 }, { 11, 22, 101 },
		{ 15, 22, 16 }, { 11, 18, 103 }, { 11, 19, 103 }, { 1, 18, 104 }, { 1, 19, 104 }, { 25, 26, 30 },
		{ 27, 56, 57 }, { 32, 36, 33 }, { 37, 38, 32 }, { 36, 37, 100 }, { 44, 46, 52 }, { 46, 53, 51 },
		{ 44, 45, 53 }, { 44, 61, 71 }, { 44, 62, 72 }, { 44, 63, 73 }, { 44, 64, 74 }, { 44, 65, 75 },
		{ 44, 66, 54 }, { 44, 67, 77 }, { 44, 68, 78 }, { 44, 69, 79 }, { 44, 70, 80 }, { 45, 52, 51 },
		{ 52, 61, 61 }, { 52, 62, 62 }, { 52, 63, 63 }, { 52, 64, 64 }, { 52, 65, 65 }, { 52, 66, 54 },
		{ 52, 67, 67 }, { 52, 68, 68 }, { 52, 69, 69 }, { 52, 70, 70 },
	};
	for (auto &e : c) combos[{ e[0], e[1] }] = e[2];
}

bool ItemMenu::LoadData() {
	uint8_t *data;
	size_t size;
	if (!sys::read_file("gamedata/oneshot_items.json", &data, &size)) {
		sys::log("WARNING: gamedata/oneshot_items.json missing - inventory shows item ids only\n");
		return false;
	}
	cJSON *root = cJSON_ParseWithLength((const char *)data, size);
	free(data);
	if (!root) return false;
	const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "items"), *it;
	cJSON_ArrayForEach(it, arr) {
		ItemData d;
		auto str = [&](const char *k) {
			const cJSON *v = cJSON_GetObjectItemCaseSensitive(it, k);
			return cJSON_IsString(v) ? std::string(v->valuestring) : std::string();
		};
		const cJSON *id = cJSON_GetObjectItemCaseSensitive(it, "id");
		const cJSON *ce = cJSON_GetObjectItemCaseSensitive(it, "common_event_id");
		d.id = cJSON_IsNumber(id) ? id->valueint : 0;
		d.common_event_id = cJSON_IsNumber(ce) ? ce->valueint : 0;
		d.name = str("name");
		d.icon_name = str("icon_name");
		d.description = str("description");
		items[d.id] = d;
	}
	cJSON_Delete(root);
	sys::log("loaded %d items\n", (int)items.size());
	return true;
}

const ItemData *ItemMenu::Item(int id) const {
	auto it = items.find(id);
	return it == items.end() ? nullptr : &it->second;
}

void ItemMenu::RefreshDisplayed() {
	displayed.clear();
	for (int i = 0; i < (int)state->inventory.size(); i++)
		if (state->inventory[i]) displayed.push_back(i);
}

void ItemMenu::Open() {
	audio::play_sound("menu_decision");
	st = State::Opening;
	opacity = 0;
	cursorAlpha = 0.5f;
	RefreshDisplayed();
}

void ItemMenu::Close() {
	st = State::Closing;
	opacity = 255;
}

void ItemMenu::Update() {
	if (IsOpen()) {
		if (st != State::Closing) RefreshDisplayed();   // AddItem/RemoveItem while open
		if (displayed.empty()) cursorIndex = -1;
		else if (cursorIndex < 0) cursorIndex = 0;
		else if (cursorIndex >= (int)displayed.size()) cursorIndex = (int)displayed.size() - 1;
	}
	switch (st) {
	case State::Opening:
		opacity += 48;
		if (opacity >= 255) { opacity = 255; st = State::Open; }
		break;
	case State::Closing:
		opacity -= 48;
		if (opacity <= 0) { opacity = 0; st = State::Closed; displayed.clear(); }
		break;
	case State::Open: {
		if (++arrowTimer >= 20) { arrowTimer = 0; displaceArrows = !displaceArrows; }
		if (++cursorAnimTimer >= 32) cursorAnimTimer = 0;
		cursorAlpha = 0.5f + (float)abs(16 - cursorAnimTimer) / 32.0f;
		if (map->IsInScript()) break;
		comboSelection = { -1, -1 };
		HandleCursorInput();
		if (input::pressed(input::BTN_OK)) {
			if (cursorIndex < 0 || cursorIndex >= (int)displayed.size()) { audio::play_sound("menu_buzzer"); break; }
			int itemId = displayed[cursorIndex];
			const ItemData *d = Item(itemId);
			if (d && d->common_event_id > 0) {
				audio::play_sound("menu_decision");
				map->StartCommonEvent(d->common_event_id);
				break;
			}
			int active = state->var(1);
			if (itemId == active) {
				audio::play_sound("menu_cancel");
				state->set_var(1, 0);
				break;
			}
			audio::play_sound("menu_decision");
			if (active == 0) {
				state->set_var(1, itemId);
				break;
			}
			std::pair<int, int> key = active > itemId ? std::make_pair(itemId, active) : std::make_pair(active, itemId);
			auto c = combos.find(key);
			comboSelection = key;
			state->set_var(2, c == combos.end() ? 0 : c->second);
			state->set_var(1, 0);
			map->StartCommonEvent(1);
		} else if (input::pressed(input::BTN_INVENTORY) || input::pressed(input::BTN_CANCEL)) {
			audio::play_sound("menu_cancel");
			Close();
		} else if (input::pressed(input::BTN_MAINMENU)) {
			Close();
			if (manager) manager->ShowMenu(MenuManager::Menu::Main);
		}
		break;
	}
	case State::Closed:
		break;
	}
}

void ItemMenu::HandleCursorInput() {
	if (displayed.empty()) return;
	using namespace input;
	auto dir = [](uint32_t b, uint32_t opp) { return pressed(b) || (auto_trigger(b, 20, 4) && !held(opp)); };
	int n = cursorIndex;
	if (dir(BTN_RIGHT, BTN_LEFT)) n++;
	else if (dir(BTN_LEFT, BTN_RIGHT)) n--;
	if (dir(BTN_UP, BTN_DOWN)) n -= 2;
	else if (dir(BTN_DOWN, BTN_UP)) n += 2;
	if (n < 0) n = 0;
	else if (n >= (int)displayed.size()) n = (int)displayed.size() - 1;
	if (n != cursorIndex) {
		cursorIndex = n;
		audio::play_sound("menu_cursor");
	}
	while (cursorIndex < firstDisplayed) firstDisplayed -= 2;
	while (cursorIndex >= firstDisplayed + 12) firstDisplayed += 2;
}

static unsigned rgb(int r, int g, int b) { return (unsigned)(r << 16 | g << 8 | b); }

void ItemMenu::Draw() {
	if (st == State::Closed) return;
	float alpha = opacity / 255.0f;
	uint8_t boxA = (uint8_t)(opacity * 200 / 255);
	gm::color_box(Rect(10, 10, 300, 28), 24, 12, 30, boxA);
	ui::window_border(Rect(8, 8, 304, 32), gfx::Blend::Normal, alpha);
	std::string desc = loc::twm("inventory_no_items_item_description");
	int cur = cursorIndex >= 0 && cursorIndex < (int)displayed.size() ? displayed[cursorIndex] : -1;
	if (cur >= 0) desc = Item(cur) ? loc::item_description(cur, Item(cur)->description) : "item " + std::to_string(cur);
	ui::text(320, 36, desc, 0xffffff, alpha, ui::Align::Center);
	gm::color_box(Rect(10, 50, 300, 108), 24, 12, 30, boxA);
	ui::window_border(Rect(8, 48, 304, 112), gfx::Blend::Normal, alpha);
	const gfx::Texture *arrows = gm::texture("ui/item_box_arrows");
	if (firstDisplayed > 0) gm::blit(arrows, Vec2(152, displaceArrows ? 50 : 49), Rect(0, 0, 16, 8), alpha);
	if (firstDisplayed + 12 < (int)displayed.size())
		gm::blit(arrows, Vec2(152, displaceArrows ? 151 : 152), Rect(0, 8, 16, 8), alpha);
	for (int i = 0; i < 12; i++) {
		int idx = i + firstDisplayed;
		if (idx >= (int)displayed.size()) break;
		Vec2 p(16 + i % 2 * 152, 56 + i / 2 * 16);
		int itemId = displayed[idx];
		if (idx == cursorIndex) gm::blit(gm::texture("ui/item_cursor"), Vec2(p.x * 2, p.y * 2), alpha * cursorAlpha, 0, gfx::Blend::Normal, 1);
		const ItemData *d = Item(itemId);
		if (d && !d->icon_name.empty())
			gm::blit(gm::texture(("item_icons/" + d->icon_name).c_str()), Vec2(p.x * 2, p.y * 2), alpha, 0, gfx::Blend::Normal, 1);
		int r = 255, g = 255, b = 255;
		if (d && d->common_event_id > 0) { r = 128; b = 128; }
		if (itemId == state->var(1) || itemId == comboSelection.first || itemId == comboSelection.second) { r = 222; g = 134; b = 0; }
		std::string name = d ? loc::item_name(itemId, d->name) : "item " + std::to_string(itemId);
		ui::text((p.x + 76) * 2, p.y * 2 + 4, name, rgb(r, g, b), alpha, ui::Align::Center);
	}
}

void ItemMenu::UpdateSelectedItemIcon() {
	if (state->var(1) != 58) return;   // clover flashes
	if (flashWait > 0) { flashWait--; return; }
	if (flashFadeIn) {
		flashOpacity += 6;
		if (flashOpacity >= 255) { flashOpacity = 255; flashFadeIn = false; flashWait = 40; }
	} else {
		flashOpacity -= 6;
		if (flashOpacity <= 0) { flashOpacity = 0; flashFadeIn = true; flashWait = 160; }
	}
}

void ItemMenu::DrawSelectedItemIcon() {
	int v = state->var(1);
	if (v <= 0) return;
	const ItemData *d = Item(v);
	if (!d || d->icon_name.empty()) return;
	std::string name = "item_icons/" + d->icon_name;
	const gfx::Texture *t = gm::texture(name.c_str());
	if (!t) return;
	Vec2 pos(320 - (int)t->w, 240 - (int)t->h);
	gm::blit(t, pos);
	if (v == 58) gm::blit(gm::texture((name + "2").c_str()), pos, flashOpacity / 255.0f);
}

void MenuManager::ShowMenu(Menu m) {
	AbstractMenu *n = m == Menu::Item ? (AbstractMenu *)&itemMenu
	                : m == Menu::Main ? (AbstractMenu *)&mainMenu
	                : m == Menu::FastTravel ? (AbstractMenu *)&fastTravelMenu : (AbstractMenu *)&settingsMenu;
	next = n;
	if (current == next) { next = nullptr; return; }
	settingsMenu.allowLanguage = titleOpen;
	if (current) current->Close();
	next->Open();
}

void MenuManager::Update() {
	if (current) current->Update();
	if (next) next->Update();
	if (current && current->IsOpen()) return;
	if (next) { current = next; next = nullptr; return; }
	current = nullptr;
	if (!map->IsInScript() && !titleOpen) {
		if (input::pressed(input::BTN_INVENTORY)) ShowMenu(Menu::Item);
		else if (input::pressed(input::BTN_MAINMENU)) ShowMenu(Menu::Main);
	}
}

void MenuManager::Draw() {
	if (current) current->Draw();
	if (next) next->Draw();
}

bool MenuManager::IsMenuOpen() const {
	if (current && current->IsOpen()) return true;
	return next && next->IsOpen();
}

// ===================================================================== MainMenu
static const int MAIN_ITEMS = 4, MAIN_SPACING = 150;   // PC: 3 items, 202 apart

void MainMenu::Open() {
	audio::play_sound("menu_decision");
	st = Opening;
	opacity = 0;
	cursorAlpha = 0.5f;
}

void MainMenu::Close() {
	st = Closing;
	opacity = 255;
}

void MainMenu::Update() {
	switch (st) {
	case Opening:
		opacity += 48;
		if (opacity >= 255) { opacity = 255; st = Shown; }
		break;
	case Closing:
		opacity -= 48;
		if (opacity <= 0) { opacity = 0; st = Closed; }
		break;
	case Shown:
		if (++cursorAnimTimer >= 32) cursorAnimTimer = 0;
		cursorAlpha = 0.5f + (float)abs(16 - cursorAnimTimer) / 32.0f;
		if (map->IsInScript()) break;
		if (input::pressed(input::BTN_LEFT)) {
			if (cursorIndex > 0) { cursorIndex--; audio::play_sound("menu_cursor"); }
		} else if (input::pressed(input::BTN_RIGHT)) {
			if (cursorIndex < MAIN_ITEMS - 1) { cursorIndex++; audio::play_sound("menu_cursor"); }
		}
		if (input::pressed(input::BTN_OK)) {
			switch (cursorIndex) {
			case 0:
				if (state->travel_enabled && !state->travel_locations.empty()) {
					manager->ShowMenu(MenuManager::Menu::FastTravel);
					break;
				}
				map->StartCommonEvent(4);
				audio::play_sound("menu_buzzer");
				break;
			case 1:
				map->StartCommonEvent(15);   // the instruction pages
				Close();
				break;
			case 2:
				manager->ShowMenu(MenuManager::Menu::Settings);
				break;
			case 3:
				audio::play_sound("menu_decision");
				Close();
				if (onQuit) onQuit();
				break;
			}
		} else if (input::pressed(input::BTN_MAINMENU) || input::pressed(input::BTN_CANCEL)) {
			audio::play_sound("menu_cancel");
			Close();
		} else if (input::pressed(input::BTN_INVENTORY)) {
			Close();
			manager->ShowMenu(MenuManager::Menu::Item);
		}
		break;
	case Closed:
		break;
	}
}

void MainMenu::Draw() {
	if (st == Closed) return;
	float a = opacity / 255.0f;
	gm::color_box(Rect(10, 10, 300, 28), 24, 12, 30, (uint8_t)(opacity * 200 / 255));
	ui::window_border(Rect(8, 8, 304, 32), gfx::Blend::Normal, a);
	// The cursor art spans one PC slot; squeeze it to the narrower slots.
	if (const gfx::Texture *cur = gm::texture("ui/main_menu_cursor")) {
		float sx = (float)MAIN_SPACING / 202.0f;
		int cx = 95 + cursorIndex * MAIN_SPACING - (int)(84 * sx);
		gm::blit_scaled(cur, Vec2(cx, 32), Rect(0, 0, (int)cur->w, (int)cur->h), sx, 1.0f, a * cursorAlpha);
	}
	static const char *keys[MAIN_ITEMS] = { "mainmenu_fasttravel", "mainmenu_notes", "mainmenu_settings", "mainmenu_quit" };
	for (int i = 0; i < MAIN_ITEMS; i++) {
		float ia = a;
		if (i == 0 && !state->travel_enabled) ia *= 0.5f;
		font::draw_centered(font::Game, Vec2(95 + i * MAIN_SPACING, 37), loc::twm(keys[i]), 0xffffff, ia, 1);
	}
}

// ===================================================================== SettingsMenu
bool SettingsMenu::Shown_(int i) const {
	if (i == Language) return allowLanguage;
	if (i == Scaling || i == Border) return twm::game_maximized();   // skipFullscreenRelatedSettings
	return true;
}

std::string SettingsMenu::Name(int i) const {
	static const char *k[COUNT] = { "settings_fullscreen", "settings_movement", "settings_colorblind", "settings_vibration",
	                                "settings_screentear", "settings_chromaaberration", "settings_fullscreen_scaling",
	                                "settings_fullscreen_border", "change_language_button", "settings_automash" };
	return loc::twm(k[i]);
}

std::string SettingsMenu::Value(int i) const {
	const Settings &s = settings();
	bool v = false;
	switch (i) {
	case Movement: return loc::twm(s.defaultMovementRun ? "settings_run" : "settings_walk");
	case Scaling: return loc::twm(s.scaling == 1 ? "settings_fullscreen_scaling_smooth" : "settings_fullscreen_scaling_sharp");
	case Fullscreen: v = twm::game_maximized(); break;
	case Border: v = s.fullscreenBorder; break;
	case Language: {
		const auto &l = loc::languages();
		return langIndex >= 0 && langIndex < (int)l.size() ? l[langIndex].displayName : std::string();
	}
	case Colorblind: v = s.colorblindMode; break;
	case Vibration: v = s.vibration; break;
	case ScreenTear: v = s.screenTear; break;
	case ChromaAberration: v = s.chromaAberration; break;
	case Automash: v = s.automash; break;
	}
	return loc::twm(v ? "settings_on" : "settings_off");
}

void SettingsMenu::Open() {
	audio::play_sound("menu_decision");
	st = Opening;
	opacity = 0;
	selected = 0;
	for (int i = 0; i < COUNT; i++) { offsetX[i] = 0; optOpacity[i] = 128; }
	confirmingLanguage = false;
	langIndex = 0;
	for (size_t i = 0; i < loc::languages().size(); i++)
		if (loc::languages()[i].code == loc::code()) langIndex = (int)i;
}

void SettingsMenu::Close() {
	settings().Save();
	st = Closing;
	opacity = 255;
}

void SettingsMenu::Update() {
	using namespace input;
	switch (st) {
	case Opening: {
		opacity = opacity + 20 > 255 ? 255 : opacity + 20;
		bool sliding = false;
		for (int i = 0; i < COUNT; i++)
			if (offsetX[i] < 30) {
				sliding = true;
				offsetX[i] = offsetX[i] + 6 > 30 ? 30 : offsetX[i] + 6;
				break;
			}
		if (!sliding && opacity >= 255) st = Shown;
		break;
	}
	case Closing:
		opacity -= 20;
		if (opacity <= 0) { opacity = 0; st = Closed; }
		break;
	case Shown: {
		if (confirmingLanguage) {
			// "Change Language? (This will restart The World Machine)"
			if (modal::is_open()) break;
			confirmingLanguage = false;
			if (modal::last_response() == modal::Response::Yes) {
				boot::request_language(loc::languages()[langIndex].code);
				Close();
			}
			break;   // the button that closed the question isn't ours
		}
		if (modal::is_open()) break;
		while (!Shown_(selected)) selected = (selected + 1) % COUNT;   // the window was un-maximized
		if (pressed(BTN_UP)) {
			audio::play_sound("menu_cursor");
			do { if (--selected < 0) selected = COUNT - 1; } while (!Shown_(selected));
		} else if (pressed(BTN_DOWN)) {
			audio::play_sound("menu_cursor");
			do { if (++selected >= COUNT) selected = 0; } while (!Shown_(selected));
		}
		for (int i = 0; i < COUNT; i++) {
			if (i == selected) {
				offsetX[i] = offsetX[i] + 6 > 50 ? 50 : offsetX[i] + 6;
				optOpacity[i] = optOpacity[i] + 10 > 255 ? 255 : optOpacity[i] + 10;
			} else {
				offsetX[i] = offsetX[i] - 6 < 30 ? 30 : offsetX[i] - 6;
				optOpacity[i] = optOpacity[i] - 10 < 128 ? 128 : optOpacity[i] - 10;
			}
		}
		if (selected == Language) {
			int n = (int)loc::languages().size();
			if (pressed(BTN_LEFT)) { audio::play_sound("menu_cursor"); langIndex = (langIndex + n - 1) % n; }
			else if (pressed(BTN_RIGHT)) { audio::play_sound("menu_cursor"); langIndex = (langIndex + 1) % n; }
			else if (pressed(BTN_OK)) {
				if (loc::languages()[langIndex].code != loc::code()) {
					audio::play_sound("menu_decision");
					modal::show(modal::Type::YesNo, loc::twm("change_language_yesno"));
					confirmingLanguage = true;
				} else {
					audio::play_sound("menu_buzzer");
				}
			}
		} else if (pressed(BTN_LEFT) || pressed(BTN_RIGHT) || pressed(BTN_OK)) {
			audio::play_sound("menu_decision");
			Settings &s = settings();
			switch (selected) {
			case Fullscreen: twm::set_game_maximized(!twm::game_maximized()); break;
			case Scaling: s.scaling = s.scaling == 1 ? 0 : 1; break;
			case Border: s.fullscreenBorder = !s.fullscreenBorder; break;
			case Movement: s.defaultMovementRun = !s.defaultMovementRun; break;
			case Colorblind: s.colorblindMode = !s.colorblindMode; break;
			case Vibration: s.vibration = !s.vibration; break;
			case ScreenTear: s.screenTear = !s.screenTear; break;
			case ChromaAberration: s.chromaAberration = !s.chromaAberration; break;
			case Automash: s.automash = !s.automash; break;
			}
		}
		if (pressed(BTN_CANCEL)) {
			audio::play_sound("menu_cancel");
			Close();
		}
		break;
	}
	case Closed:
		break;
	}
}

void SettingsMenu::Draw() {
	if (st == Closed) return;
	float a = opacity / 255.0f;
	gm::color_box(Rect(0, 0, 320, 240), 0, 0, 0, (uint8_t)(opacity * 180 / 255));
	// The title texture is the Game font blitted at game scale (doubled).
	font::draw(font::Game, Vec2(13, 24), loc::twm("settings_title"), 0xffffff, a, 2);
	int rows = 1;   // the gap before Automash
	for (int i = 0; i < COUNT; i++) rows += Shown_(i);
	int y = 118 - std::max(0, rows - 10) * 32;   // all rows shown: start higher to fit
	for (int i = 0; i < COUNT; i++) {
		if (!Shown_(i)) continue;
		float oa = optOpacity[i] / 255.0f * a;
		if (i == Automash) y += 32;   // the PC build sets this one apart
		font::draw(font::Game, Vec2(30 + offsetX[i] - 2, y), Name(i), 0xffffff, oa, 1);
		if (i == Language) {
			// The language's name as the boot screen shows it (its own script),
			// between arrows while picking.
			const auto &l = loc::languages();
			std::string tn = "the_world_machine/languages/" + (langIndex < (int)l.size() ? l[langIndex].code : std::string("en"));
			const gfx::Texture *lt = gm::texture(tn.c_str(), gm::TheWorldMachine);
			int x = 400 + offsetX[i] - 2;
			if (lt) gm::blit(lt, Vec2(x, y - 2), oa, 0, gfx::Blend::Normal, 1);
			else font::draw(font::Game, Vec2(x, y), Value(i), 0xffffff, oa, 1);
			if (i == selected) {
				int w = lt ? (int)lt->w : font::width(font::Game, Value(i));
				gm::blit(gm::texture("the_world_machine/window_icons/arrow_left", gm::TheWorldMachine), Vec2(x - 18, y + 2), oa, 0, gfx::Blend::Normal, 1);
				gm::blit(gm::texture("the_world_machine/window_icons/arrow_right", gm::TheWorldMachine), Vec2(x + w + 2, y + 2), oa, 0, gfx::Blend::Normal, 1);
			}
		} else {
			font::draw(font::Game, Vec2(400 + offsetX[i] - 2, y), Value(i), 0xffffff, oa, 1);
		}
		y += 32;
	}
}
