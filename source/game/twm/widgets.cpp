#include "widgets.h"
#include <math.h>
#include <algorithm>

namespace twm {

// ===================================================================== IconButton

IconButton::IconButton(const std::string &ic, Vec2 relPos, std::function<void()> a, Vec2 size) : icon(ic), action(a) {
	if (size.x <= 0 && !ic.empty()) size = tex_size(ic.c_str());
	rect = Rect(relPos.x, relPos.y, size.x, size.y);
}

bool IconButton::Update(Vec2 parentPos, bool canInteract) {
	MouseCursor &c = cursor();
	if (!canInteract) {
		hovering = false;
		autoRepeating = false;
		autoRepeatTimer = 0;
		return false;
	}
	hovering = in_rect(rect, c.MousePos() - parentPos);
	if (hovering && clickable) c.SetState(disabled ? MouseCursor::NotAllowed : MouseCursor::Clickable);
	if (hovering && clickable && !disabled && c.Clicked()) {
		pressed = true;
		autoRepeating = false;
	} else if (pressed) {
		if (!c.Held()) {
			if (hovering && action && !autoRepeating) action();
			pressed = false;
			autoRepeatTimer = 0;
		} else if (autoRepeatDelay > 0 && hovering) {
			autoRepeatTimer++;
			int wait = autoRepeating ? autoRepeatDelay : autoRepeatTriggerDelay;
			if (autoRepeatTimer > wait) {
				if (action) action();
				autoRepeatTimer = 0;
				autoRepeating = true;
			}
		}
	}
	return hovering;
}

void IconButton::Draw(Vec2 parentPos, const Theme &th, int alpha, bool dropShadow) const {
	Color c = (hovering || disabled) && clickable ? th.Variant(alpha) : th.Primary(alpha);
	Color bg = th.Background(alpha);
	if (borderWidth > 0) {
		Rect r = translated(rect, parentPos);
		box(shrink(r, -borderWidth), c);
		box(r, th.Background(alpha));
	}
	Vec2 p = parentPos + Vec2(rect.x, rect.y);
	if (!tint) c = Color::White().withA(alpha);
	if (dropShadow) tex(icon.c_str(), p + Vec2(1, 1), Rect(), bg);
	tex(icon.c_str(), p, Rect(), c);
}

// ===================================================================== WindowButton

WindowButton::WindowButton(WinButtonType t, Vec2 relPos, std::function<void()> a) : IconButton("", relPos, a, Vec2(16, 16)), type(t) {}

void WindowButton::Draw(Vec2 parentPos, const Theme &th, int alpha, bool) const {
	Color c = hovering || disabled ? th.Variant(alpha) : th.Primary(alpha);
	Vec2 p(parentPos.x + rect.x, parentPos.y + rect.y);
	box(Rect(p.x, p.y, 16, 16), th.Background(alpha));
	tex("the_world_machine/window_buttons", p, Rect(16 * (int)type, 0, 16, 16), c);
}

// ===================================================================== TextButton

TextButton::TextButton(const std::string &l, Vec2 relPos, std::function<void()> a, int w, int h)
	: action(a), label(l), rect(relPos.x, relPos.y, w, h) {}

bool TextButton::Update(Vec2 parentPos, bool canInteract) {
	if (!canInteract) return false;
	MouseCursor &c = cursor();
	hovering = in_rect(rect, c.MousePos() - parentPos);
	if (hovering) c.SetState(MouseCursor::Clickable);
	if (hovering && c.Clicked()) pressed = true;
	else if (pressed && !c.Held()) {
		if (hovering && action) action();
		pressed = false;
	}
	return hovering;
}

void TextButton::Draw(Vec2 parentPos, const Theme &th, int alpha) const {
	Color fg = hovering ? th.Variant(alpha) : th.Primary(alpha);
	Vec2 p(parentPos.x + rect.x, parentPos.y + rect.y);
	box(Rect(p.x, p.y, rect.w, rect.h), fg);
	if (highlight && !hovering) fg = th.Background(alpha);   // filled: picked with the pad
	else box(Rect(p.x + 1, p.y + 1, rect.w - 2, rect.h - 2), th.Background(alpha));
	// MainBlit(labelTexture, (x*2 + w, (y+2)*2), xCentered): centred on the button.
	line(Vec2(p.x + rect.w / 2, p.y + 2), label, fg, rect.w - 4, true);
}

void TextButton::DrawHighlight(Vec2 parentPos, const Theme &th, int alpha) const {
	box(Rect(parentPos.x + rect.x, parentPos.y + rect.y, rect.w, rect.h), hovering ? th.Variant(alpha) : th.Primary(alpha));
}

// ===================================================================== Slider

Slider::Slider(const std::string &l, int mn, int mx, Vec2 p, int len, bool buttons, bool vert, const std::string &suf)
	: min(mn), max(mx), label(l), suffix(suf), pos(p), length(len), vertical(vert), useButtons(buttons) {
	Layout();
	value = mn;
}

void Slider::Layout() {
	int labelH = label.empty() ? 0 : 16;
	if (vertical) area = Rect(pos.x + 6, pos.y, 4, length);
	else area = Rect(pos.x, pos.y + labelH + 6, length, 4);
	if (!useButtons) return;
	if (vertical) {
		area.y += 16;
		area.h -= 32;
		left = IconButton("the_world_machine/window_icons/arrow_up", pos, [this] { ArrowLeft(); });
		right = IconButton("the_world_machine/window_icons/arrow_down", Vec2(pos.x, pos.y + length - 16), [this] { ArrowRight(); });
		scrollArea = Rect(pos.x, pos.y, 16, length);
	} else {
		area.x += 16;
		area.w -= 32;
		left = IconButton("the_world_machine/window_icons/arrow_left", pos + Vec2(0, labelH), [this] { ArrowLeft(); });
		right = IconButton("the_world_machine/window_icons/arrow_right", Vec2(pos.x + length - 16, pos.y + labelH), [this] { ArrowRight(); });
	}
	left.autoRepeatDelay = 6;
	right.autoRepeatDelay = 6;
}

Vec2 Slider::KnobPos() const {
	float f = max > min ? (float)(value - min) / (max - min) : 0.0f;
	if (vertical) return Vec2(area.x - 6, area.y + (int)((area.h - 16) * f));
	return Vec2(area.x + (int)((area.w - 16) * f), area.y - 6);
}

void Slider::ArrowLeft() {
	if (value > min) {
		value = std::max(min, value - increment);
		if (onValueChanged) onValueChanged(value);
	}
}

void Slider::ArrowRight() {
	if (value < max) {
		value = std::min(max, value + increment);
		if (onValueChanged) onValueChanged(value);
	}
}

bool Slider::Update(Vec2 parentPos, bool canInteract) {
	bool used = !canInteract;
	if (!active) return false;
	used |= left.Update(parentPos, canInteract);
	used |= right.Update(parentPos, canInteract);
	MouseCursor &c = cursor();
	Vec2 m = c.MousePos();
	if (!c.Held()) grabbed = false;
	if (used) return used;
	if (grabbed) {
		c.SetState(MouseCursor::Holding);
		int old = value;
		float f = vertical ? (float)(m.y - grabOffset.y - (parentPos.y + area.y)) / (area.h - 16)
		                   : (float)(m.x - grabOffset.x - (parentPos.x + area.x)) / (area.w - 16);
		value = min + (int)ceilf(f * (max - min));
		int step = dragIncrement > 0 ? dragIncrement : increment;
		if (step > 1) value -= value % step;
		value = std::max(std::min(value, max), min);
		if (value != old && onValueChanged) onValueChanged(value);
		return true;
	}
	Vec2 k = KnobPos() + parentPos;
	if (in_rect(Rect(k.x, k.y, 16, 16), m)) {
		used = true;
		c.SetState(MouseCursor::Grabbable);
		if (c.Clicked()) {
			grabbed = true;
			grabOffset = m - k;
		}
	}
	return used;
}

void Slider::Draw(const Theme &th, Vec2 parentPos, int alpha) const {
	if (!active) return;
	Rect r = translated(area, parentPos);
	box(r, th.Primary(alpha));
	Rect in = shrink(r, 1);
	float f = max > min ? (float)(value - min) / (max - min) : 0.0f;
	if (vertical) { int n = (int)(f * in.h); in.y += n; in.h -= n; }
	else { int n = (int)(f * in.w); in.x += n; in.w -= n; }
	box(in, th.Background(alpha));
	left.Draw(parentPos, th, alpha);
	right.Draw(parentPos, th, alpha);
	if (!label.empty() && !vertical) {
		std::string t = label + ": " + std::to_string(value) + suffix;
		line(Vec2(r.x - 16 + (area.w + 32) / 2, parentPos.y + pos.y + 2), t, th.Primary(alpha), area.w + 32, true);
	}
	tex("the_world_machine/window_icons/knob", KnobPos() + parentPos, Rect(), grabbed ? th.Variant(alpha) : th.Primary(alpha));
}

} // namespace twm
