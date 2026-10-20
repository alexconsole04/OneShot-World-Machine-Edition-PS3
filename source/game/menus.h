// menus.h - MenuManager and ItemManager (inventory menu) from
// OneShotMG.src.Menus. Other menus (main/settings/fast travel) come later.
#pragma once
#include "../engine/types.h"
#include <map>
#include <string>
#include <vector>

class TileMap;
struct GameState;

struct ItemData {
	int id = 0;
	std::string name, icon_name, description;
	int common_event_id = 0;
};

class ItemMenu {
public:
	enum class State { Closed, Opening, Open, Closing };
	ItemMenu(TileMap *map, GameState *state);
	bool LoadData();   // gamedata/oneshot_items.json
	void Open();
	void Close();
	bool IsOpen() const { return st != State::Closed; }
	void Update();
	void Draw();
	void UpdateSelectedItemIcon();
	void DrawSelectedItemIcon();
	const ItemData *Item(int id) const;

private:
	void HandleCursorInput();
	void RefreshDisplayed();
	TileMap *map;
	GameState *state;
	std::map<int, ItemData> items;
	std::map<std::pair<int, int>, int> combos;
	std::vector<int> displayed;   // item ids, ascending (as C# builds and inserts them)
	State st = State::Closed;
	int opacity = 0;
	int firstDisplayed = 0, cursorIndex = -1;
	int cursorAnimTimer = 0;
	float cursorAlpha = 0.5f;
	int arrowTimer = 0;
	bool displaceArrows = false;
	std::pair<int, int> comboSelection = { -1, -1 };
	int flashOpacity = 0, flashWait = 160;
	bool flashFadeIn = true;
};

class MenuManager {
public:
	MenuManager(TileMap *map, GameState *state) : itemMenu(map, state), map(map) {}
	void Update();
	void Draw() { itemMenu.Draw(); }
	bool IsMenuOpen() const { return itemMenu.IsOpen(); }
	ItemMenu itemMenu;
private:
	TileMap *map;
};
