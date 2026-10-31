// collection_apps.h - the World Machine's collection apps: AchievementWindow (Badges),
// ContactsWindow (Friends), GalleryWindow, JukeboxWindow (Music Box),
// TimerWindow and ControlsWindow (this port's own: the PS3 pad).
#pragma once
#include "customize.h"
#include "../window.h"
#include <memory>
#include <vector>

namespace twm {

// Textbox: wrapped OS-font text on a dark panel with a scroll bar.
class TextBox {
public:
	TextBox(Rect area, int rowHeight, int margin);
	void SetText(const std::string &text);
	bool Update(Vec2 parentPos, bool canInteract);
	void Draw(const Theme &th, Vec2 parentPos, int alpha) const;
	Slider &Scroll() { return *slider; }
private:
	Rect area;
	int rowHeight, margin, visible;
	std::vector<std::string> lines;
	std::unique_ptr<Slider> slider;
	int offset = 0;
};

class AchievementWindow : public Window {
public:
	AchievementWindow();
	bool Update(bool consumed) override;
	void DrawContents(const Theme &th, Vec2 screenPos, int alpha) override;
	bool IsSameContent(const Window *o) const override { return o->Kind() == Kind(); }
	int Kind() const override { return KIND_ACHIEVEMENTS; }
	void PadInput() override;
	bool WantsPad() const override { return true; }
private:
	struct Item { std::string id, title, label; bool unlocked = false; };
	void Refresh();
	std::vector<Item> items;
	size_t seenUnlocks = (size_t)-1;
	std::unique_ptr<Slider> scroll;
};

class ContactsWindow : public Window {
public:
	ContactsWindow();
	bool Update(bool consumed) override;
	void DrawContents(const Theme &th, Vec2 screenPos, int alpha) override;
	bool IsSameContent(const Window *o) const override { return o->Kind() == Kind(); }
	int Kind() const override { return KIND_CONTACTS; }
	void PadInput() override;
	bool WantsPad() const override { return true; }
	struct Profile {
		std::string id, title, subtitle, bgImage, walksprite, infoText, theme = "white";
		std::vector<std::string> facepics;
		bool singleSprite = false, walkAnimation = true, textDropShadow = true;
	};
private:
	void Refresh();
	void OnProfileChange(const std::string &id);
	const Profile *current = nullptr;
	size_t seenUnlocks = (size_t)-1;
	std::unique_ptr<Chooser> chooser;
	std::unique_ptr<TextBox> textbox;
	IconButton face;
	int faceIndex = 0, walkTimer = 0;
	Vec2 spriteSize;
	bool held = false;
	Vec2 heldMouse;
};

class GalleryWindow : public Window {
public:
	GalleryWindow();
	~GalleryWindow() override;
	bool Update(bool consumed) override;
	void Draw(const Theme &th) override;
	void DrawContents(const Theme &th, Vec2 screenPos, int alpha) override;
	bool IsSameContent(const Window *o) const override { return o->Kind() == Kind(); }
	int Kind() const override { return KIND_GALLERY; }
	void PadInput() override;
	bool WantsPad() const override { return true; }
	struct Cg {
		std::string imageId, displayName, blend;
		std::vector<std::string> layers;
		int order = 0, scale = 2;
		float scrollX = 0, scrollY = 0;
		bool oversize = false, fitHorizontal = false;
		Vec2 windowSize;   // overrideWindowSize (0 = 320x240)
	};
private:
	void Refresh();
	void ToggleMax();
	void Config();
	Vec2 ScrollbarsFor(const Cg *cg, Vec2 imageSize, Vec2 viewport);
	void PlaceScrollbars();
	const Cg *Current() const;
	Vec2 FullscreenSpace(const Cg *cg) const;
	void BestFit(const Cg *cg, Vec2 &img, Vec2 &src, Vec2 &dst, float &scale) const;
	std::vector<Cg> list;
	size_t seenUnlocks = (size_t)-1;
	std::unique_ptr<Chooser> chooser;
	std::unique_ptr<Slider> hScroll, vScroll;
	std::unique_ptr<WindowButton> exitFull;
	Vec2 rememberedPos;
	bool glitched = false;
};

class JukeboxWindow : public Window {
public:
	JukeboxWindow();
	bool Update(bool consumed) override;
	void DrawContents(const Theme &th, Vec2 screenPos, int alpha) override;
	bool IsSameContent(const Window *o) const override { return o->Kind() == Kind(); }
	int Kind() const override { return KIND_JUKEBOX; }
	void PadInput() override;
	bool WantsPad() const override { return true; }
	void Stop();
private:
	struct Track { std::string id, name; int number = 0, speed = 100; };
	struct Note { int frame; Vec2 pos, vel; float xw, yw, ws; int alpha = 255; Color color; };
	enum NikoState { SLEEP, JAMMING, BOPPING, BLINKING };
	void Refresh();
	void PlayPause();
	void Restart();
	void PlayCurrent();
	void SwitchTrack(const std::string &id);
	void PickPlayState();
	void ResetNiko() { nikoIndex = 0; nikoTimer = 0; nikoFrame = 0; }
	std::vector<Track> tracks;
	size_t seenUnlocks = (size_t)-1;
	std::unique_ptr<Chooser> chooser;
	std::unique_ptr<Slider> speed, volume;
	std::vector<std::unique_ptr<IconButton>> buttons;
	std::vector<Note> notes;
	bool playing = false, paused = false, glitched = false;
	float gramTimer = 0, noteTimer = 0, nikoTimer = 0;
	int gramIndex = 0, nikoIndex = 0, nikoFrame = 0, padSel = 0;
	NikoState niko = SLEEP;
};

class TimerWindow : public Window {
public:
	TimerWindow();
	bool Update(bool consumed) override;
	void DrawContents(const Theme &th, Vec2 screenPos, int alpha) override;
	bool IsSameContent(const Window *o) const override { return o->Kind() == Kind(); }
	int Kind() const override { return KIND_TIMER; }
private:
	void DrawTimer(Vec2 p, int frames, const Theme &th, int alpha, bool flash, bool active) const;
	IconButton help;
};

// The PS3 version's Controls app: the pad's buttons (they can't be
// rebound here), the stick dead zones and the cursor speed.
class ControlsWindow : public Window {
public:
	ControlsWindow();
	bool Update(bool consumed) override;
	void DrawContents(const Theme &th, Vec2 screenPos, int alpha) override;
	bool IsSameContent(const Window *o) const override { return o->Kind() == Kind(); }
	int Kind() const override { return KIND_CONTROLS; }
	void PadInput() override;
	bool WantsPad() const override { return true; }
private:
	std::unique_ptr<Slider> moveDead, mouseDead, mouseSpeed;
	std::unique_ptr<TextButton> defaults;
	int sel = 0;
	bool padUsed = false;
};

// The speedrun timer (WindowManager.SystemActiveTimer / CurrentPlaythroughTimer).
void tick_timers();

} // namespace twm
