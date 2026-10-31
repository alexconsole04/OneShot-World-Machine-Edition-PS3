// menus.h - MenuManager, ItemManager (inventory), MainMenu and SettingsMenu
// and FastTravelMenu from OneShotMG.src.Menus.
#pragma once
#include "../../engine/types.h"
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

// FastTravelMenu: the zone's minimap; pick an unlocked map with the d-pad
// and Niko warps there (fast_travel_menu.cpp).
class FastTravelMenu : public AbstractMenu {
public:
	FastTravelMenu(TileMap *map, GameState *state) : map(map), state(state) {}
	void Update() override;
	void Draw() override;
	void Open() override;
	void Close() override;
	bool IsOpen() const override { return st != Closed; }
private:
	struct Piece { int mapId = 0; Vec2 offset, warp; std::string image; bool HasWarp() const { return warp.x != 0 && warp.y != 0; } };
	struct Edge { int id = 0; int dir = 0; };   // EdgeDirection: 0 up, 1 down, 2 left, 3 right
	bool IsOption(int mapId) const;
	void SetTarget(int mapId);
	void Load();
	void DrawMinimap(float a);
	enum { Closed, Opening, Shown, Closing, FadeOut, FadeIn } st = Closed;
	TileMap *map;
	GameState *state;
	bool loaded = false;
	std::map<int, std::vector<Piece>> zones;                    // zone -> pieces
	std::map<int, std::map<int, std::vector<Edge>>> zoneEdges;  // zone -> mapId -> edges
	std::map<int, std::string> mapNames;
	std::vector<std::string> zoneNames;
	std::vector<int> options;          // unlocked map ids in the current zone
	const std::vector<Piece> *pieces = nullptr;
	std::vector<Edge> edges;           // from the selected map to other options
	int zone = 0, current = 0, selected = -1;
	int opacity = 0, fadeTimer = 0, arrowAnimTimer = 0;
	Vec2 cursor, cursorStart, cursorTarget, shake;
	int cursorMoveTimer = 0, shakeTimer = 0;
	Vec2 aberr[3];
	int aberrTimer[3] = {};
};

class SettingsMenu : public AbstractMenu {
public:
	void Update() override;
	void Draw() override;
	void Open() override;
	void Close() override;
	bool IsOpen() const override { return st != Closed; }
private:
	// The PC's order (Fullscreen first; scaling and border only while
	// maximized), plus this port's Language row.
	enum Option { Fullscreen, Movement, Colorblind, Vibration, ScreenTear, ChromaAberration, Scaling, Border, Language, Automash,
	              COUNT };
	std::string Name(int i) const;
	std::string Value(int i) const;
	bool Shown_(int i) const;
	int langIndex = 0;              // language picked in the Language row
	bool confirmingLanguage = false;   // the "restart?" question is up
public:
	// The Language row only appears on the title screen (changing language
	// restarts the boot sequence, which would end a game in progress).
	bool allowLanguage = false;
private:
	enum { Closed, Opening, Shown, Closing } st = Closed;
	int opacity = 0, selected = 0;
	int offsetX[COUNT] = {}, optOpacity[COUNT] = {};
};

class MenuManager {
public:
	enum class Menu { Item, Main, Settings, FastTravel };
	MenuManager(TileMap *map, GameState *state)
		: itemMenu(map, state), mainMenu(map, state, this), fastTravelMenu(map, state), map(map) { itemMenu.manager = this; }
	void ShowMenu(Menu m);
	void Update();
	void Draw();
	bool IsMenuOpen() const;
	ItemMenu itemMenu;
	MainMenu mainMenu;
	SettingsMenu settingsMenu;
	FastTravelMenu fastTravelMenu;
	bool titleOpen = false;   // TitleScreenManager.IsOpen: no Start/Triangle menus
private:
	TileMap *map;
	AbstractMenu *current = nullptr, *next = nullptr;
};
