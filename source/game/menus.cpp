#include "menus.h"
#include "tilemap.h"
#include "game_state.h"
#include "../engine/audio.h"
#include "../engine/gm.h"
#include "../engine/ui.h"
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
			Close();   // main menu: not ported yet
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
	std::string desc;   // "inventory_no_items_item_description" (localized): blank here
	int cur = cursorIndex >= 0 && cursorIndex < (int)displayed.size() ? displayed[cursorIndex] : -1;
	if (cur >= 0) desc = Item(cur) ? Item(cur)->description : "item " + std::to_string(cur);
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
		std::string name = d ? d->name : "item " + std::to_string(itemId);
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

void MenuManager::Update() {
	// Single menu for now; MenuManager's current/next hand-off collapses to this.
	itemMenu.Update();
	if (itemMenu.IsOpen()) return;
	if (!map->IsInScript()) {
		if (input::pressed(input::BTN_INVENTORY)) itemMenu.Open();
	}
}
