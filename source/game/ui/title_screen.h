// title_screen.h - TitleScreenManager: Start / Settings / Exit (and "..."
// once a playthrough as the World Machine has finished).
#pragma once
#include <functional>

struct GameState;
class MenuManager;

class TitleScreen {
public:
	enum Choice { Start = 0, SettingsOpt = 1, Exit = 2, Memory = 3 };
	void Open(GameState *state, MenuManager *menus);
	bool IsOpen() const { return st != Closed; }
	void Update();
	void Draw();
	void ForceClose() { st = Closed; }   // debug map switcher
	std::function<void(int)> onChosen;   // called once the fade-out ends
private:
	void Close();
	enum { Closed, Opening, Shown, Closing } st = Closed;
	GameState *state = nullptr;
	MenuManager *menus = nullptr;
	int fadeOpacity = 255, selected = 0, optionCount = 3;
	int helpTimer = 0, helpFadeIn = 0;
	bool anySelected = false, cursorMoved = false, showHelp = false;
};
