// customize.h - CustomizationWindow (the Customize app) and ChooserControl.
//
// The PS3 version's layout: the PC's Vsync and Fullscreen choosers are gone
// (their places are left free), Resolution is shown but can't be changed,
// and its "Change Resolution" button is Erase All Data instead.
#pragma once
#include "widgets.h"
#include "window.h"
#include <memory>
#include <utility>
#include <vector>

namespace twm {

// ChooserControl: < value > with arrow buttons that cycle through items.
class Chooser {
public:
	// items: (key, loc key of the label)
	Chooser(Vec2 pos, int length, std::vector<std::pair<std::string, std::string>> items, const std::string &selected);
	Chooser(const Chooser &) = delete;   // its buttons point back at it
	Chooser &operator=(const Chooser &) = delete;
	std::function<void(const std::string &)> onItemChange;
	const std::string &Value() const { return items[index].first; }
	void SetValue(const std::string &key);
	bool Update(Vec2 parentPos, bool canInteract);
	void Draw(Vec2 parentPos, const Theme &th, int alpha) const;
private:
	void Step(int d);
	Rect area;
	std::vector<std::pair<std::string, std::string>> items;
	int index = 0;
	IconButton left, right;
};

class CustomizeWindow : public Window {
public:
	CustomizeWindow();
	bool Update(bool consumed) override;
	void DrawContents(const Theme &th, Vec2 screenPos, int alpha) override;
	bool IsSameContent(const Window *other) const override { return other->Kind() == Kind(); }
	int Kind() const override { return KIND_CUSTOMIZE; }
private:
	void OnEraseAllData();
	void OnChangeLanguage();
	std::unique_ptr<Chooser> badgeToasts, systemScaling;
	std::unique_ptr<Slider> gamma, bgm, sfx;
	std::unique_ptr<TextButton> eraseButton, viewWallpaper, changeLanguage;
};

} // namespace twm
