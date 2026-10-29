// widgets.h - IconButton, TWMWindowButton, TextButton, SliderControl.
#pragma once
#include "twm.h"

namespace twm {

// IconButton: an icon that acts when the click is released over it.
class IconButton {
public:
	IconButton() = default;
	IconButton(const std::string &icon, Vec2 relPos, std::function<void()> action, Vec2 size = Vec2(0, 0));
	std::string icon;
	bool disabled = false, clickable = true, tint = true;
	int autoRepeatTriggerDelay = 20, autoRepeatDelay = 0, borderWidth = 0;
	std::function<void()> action;
	Vec2 Position() const { return Vec2(rect.x, rect.y); }
	void SetPosition(Vec2 p) { rect.x = p.x; rect.y = p.y; }
	virtual ~IconButton() {}
	virtual bool Update(Vec2 parentPos, bool canInteract);
	virtual void Draw(Vec2 parentPos, const Theme &th, int alpha, bool dropShadow = false) const;
protected:
	Rect rect;
	bool hovering = false, pressed = false, autoRepeating = false;
	int autoRepeatTimer = 0;
};

enum class WinButtonType { Minimize, Maximize, Close, MinimizeAll, UnminimizeAll };

// TWMWindowButton: 16x16 from the_world_machine/window_buttons.
class WindowButton : public IconButton {
public:
	WindowButton(WinButtonType t, Vec2 relPos, std::function<void()> action);
	WinButtonType type;
	void Draw(Vec2 parentPos, const Theme &th, int alpha, bool dropShadow = false) const override;
};

// TextButton: a framed label (56x16 by default).
class TextButton {
public:
	TextButton(const std::string &label, Vec2 relPos, std::function<void()> action, int w = 56, int h = 16);
	std::function<void()> action;
	bool Update(Vec2 parentPos, bool canInteract);
	void Draw(Vec2 parentPos, const Theme &th, int alpha) const;
	void DrawHighlight(Vec2 parentPos, const Theme &th, int alpha) const;
	void Shift(Vec2 d) { rect.x += d.x; rect.y += d.y; }
	Vec2 Pos() const { return Vec2(rect.x + 28, rect.y + 8); }
	bool Hovering() const { return hovering; }
	std::string label;
	bool highlight = false;   // chosen with the pad (drawn filled)
private:
	Rect rect;
	bool hovering = false, pressed = false;
};

// SliderControl (vertical scroll bars here; horizontal sliders later).
class Slider {
public:
	Slider(const std::string &label, int min, int max, Vec2 pos, int length, bool useButtons = true, bool vertical = false,
	       const std::string &suffix = "");
	Slider(const Slider &) = delete;   // its arrow buttons point back at it
	Slider &operator=(const Slider &) = delete;
	bool active = true;
	int min, max, increment = 1, dragIncrement = 0;
	std::function<void(int)> onValueChanged;
	bool hasScrollZone = false;
	Rect scrollZone;
	int Value() const { return value; }
	void SetValue(int v) { value = v; }
	void SetPlacement(Vec2 p, int len) { pos = p; length = len; Layout(); }
	Vec2 Position() const { return pos; }
	int Length() const { return length; }
	bool Update(Vec2 parentPos, bool canInteract);
	void Draw(const Theme &th, Vec2 parentPos, int alpha) const;
	void ArrowLeft();
	void ArrowRight();
private:
	Vec2 KnobPos() const;
	void Layout();
	std::string label, suffix;
	Vec2 pos;
	int length;
	bool vertical, useButtons, grabbed = false;
	Vec2 grabOffset;
	int value = 0;
	Rect area, scrollArea;
	IconButton left, right;
};

} // namespace twm
