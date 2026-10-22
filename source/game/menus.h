// menus.h - MenuManager, ItemManager (inventory), MainMenu and SettingsMenu
// from OneShotMG.src.Menus. The fast travel menu comes later.
#pragma once
#include "../engine/types.h"
#include <functional>
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

class MenuManager;

class AbstractMenu {
public:
	virtual ~AbstractMenu() = default;
	virtual void Update() = 0;
	virtual void Draw() = 0;
	virtual void Open() = 0;
	virtual void Close() = 0;
	virtual bool IsOpen() const = 0;
};

class ItemMenu : public AbstractMenu {
public:
	enum class State { Closed, Opening, Open, Closing };
	ItemMenu(TileMap *map, GameState *state);
	bool LoadData();   // gamedata/oneshot_items.json
	void Open() override;
	void Close() override;
	bool IsOpen() const override { return st != State::Closed; }
	void Update() override;
	void Draw() override;
	MenuManager *manager = nullptr;
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

// The Start-button menu. The PC build has Fast Travel / Notes / Settings
// and quits by closing the window; on the console a fourth entry, Quit,
// does what closing the window does (common event 35: save and quit).
class MainMenu : public AbstractMenu {
public:
	MainMenu(TileMap *map, GameState *state, MenuManager *mgr) : map(map), state(state), manager(mgr) {}
	void Update() override;
	void Draw() override;
	void Open() override;
	void Close() override;
	bool IsOpen() const override { return st != Closed; }
	std::function<void()> onQuit;
private:
	enum { Closed, Opening, Shown, Closing } st = Closed;
	TileMap *map;
	GameState *state;
	MenuManager *manager;
	int opacity = 0, cursorAnimTimer = 0, cursorIndex = 0;
	float cursorAlpha = 0.5f;
};

class SettingsMenu : public AbstractMenu {
public:
	void Update() override;
	void Draw() override;
	void Open() override;
	void Close() override;
	bool IsOpen() const override { return st != Closed; }
private:
	enum Option { Movement, Colorblind, Vibration, ScreenTear, ChromaAberration, Scaling, Automash, COUNT };
	std::string Name(int i) const;
	std::string Value(int i) const;
	enum { Closed, Opening, Shown, Closing } st = Closed;
	int opacity = 0, selected = 0;
	int offsetX[COUNT] = {}, optOpacity[COUNT] = {};
};

class MenuManager {
public:
	enum class Menu { Item, Main, Settings };
	MenuManager(TileMap *map, GameState *state)
		: itemMenu(map, state), mainMenu(map, state, this), map(map) { itemMenu.manager = this; }
	void ShowMenu(Menu m);
	void Update();
	void Draw();
	bool IsMenuOpen() const;
	ItemMenu itemMenu;
	MainMenu mainMenu;
	SettingsMenu settingsMenu;
	bool titleOpen = false;   // TitleScreenManager.IsOpen: no Start/Triangle menus
private:
	TileMap *map;
	AbstractMenu *current = nullptr, *next = nullptr;
};
