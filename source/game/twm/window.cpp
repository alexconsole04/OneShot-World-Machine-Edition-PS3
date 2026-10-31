#include "window.h"
#include "icons.h"
#include "../core/loc.h"
#include "../core/settings.h"
#include "../../engine/util.h"
#include "../../platform/sys.h"
#include "../../platform/input.h"
#include "../../engine/audio.h"
#include <algorithm>

namespace twm {

bool pad_repeat(uint32_t b) { return input::pressed_raw(b) || input::auto_trigger_raw(b, 20, 4); }

// ===================================================================== Window

Window::~Window() {}

void Window::SetTitle(const std::string &t) {
	title = t;
	if (onTitleChange) onTitleChange(this);
}

void Window::RecalcButtons() {
	int x = contents.x + 2 - 16 - 2;
	for (size_t i = 0; i < buttons.size(); i++) buttons[i]->SetPosition(Vec2(x - 18 * (int)i, 4));
}

void Window::AddButton(WinButtonType t, std::function<void()> action) {
	if (!action) {
		if (t == WinButtonType::Close) action = [this] { if (onClose) onClose(this); };
		else if (t == WinButtonType::Minimize) action = [this] { if (onMinimize) onMinimize(this); };
	}
	buttons.emplace_back(new WindowButton(t, Vec2(0, 0), action));
	RecalcButtons();
}

WindowButton *Window::FindButton(WinButtonType t) {
	for (auto &b : buttons)
		if (b->type == t) return b.get();
	return nullptr;
}

void Window::ToggleMinimize() {
	minimized = !minimized;
	if (!minimized && minimizedFrames >= 30) {
		int maxX = screen().x - contents.x, maxY = screen().y - 26 - 30;
		pos.y = std::max(0, std::min(pos.y, maxY));
		pos.x = std::max(0, std::min(pos.x, maxX));
	}
}

bool Window::Update(bool consumed) {
	if (minimized) {
		if (minimizedFrames < 30) minimizedFrames++;
		held = false;
		return false;
	}
	if (minimizedFrames > 0) minimizedFrames--;
	bool modalUsed = false;
	if (modal) {
		Window *m = modal.get();
		modalUsed = m->Update(consumed);
	}
	bool canInteract = !(consumed || modalUsed);
	bool onButton = false;
	for (auto &b : buttons) onButton |= b->Update(pos, canInteract);
	if (!canInteract) {
		held = false;
		return modalUsed;
	}
	if (onButton) return true;
	MouseCursor &c = cursor();
	Rect bar(pos.x + 2, pos.y + 2, contents.x, 20);
	// The move-file tutorial keeps both folder windows where they are.
	bool canGrab = !tutorial_is(TUT_MOVEFILE_DRAG_FILE) && !tutorial_is(TUT_MOVEFILE_DROP_FILE);
	if (held) {
		if (c.Held()) {
			c.SetState(MouseCursor::Holding);
			Vec2 m = c.MousePos();
			pos = pos + (m - heldMouse);
			heldMouse = m;
			return true;
		}
		held = false;
	} else if (!maximized && in_rect(bar, c.MousePos()) && canGrab) {
		c.SetState(MouseCursor::Grabbable);
		if (c.Clicked()) {
			held = true;
			heldMouse = c.MousePos();
			if (grabFocus) grabFocus(this);
		}
		return true;
	}
	if (in_rect(Rect(pos.x, pos.y, contents.x + 4, contents.y + 28), c.MousePos()) && showBackground) {
		if (c.Clicked() && canGrab && grabFocus) grabFocus(this);
		return true;
	}
	return modalUsed;
}

void Window::Draw(const Theme &th) {
	if (minimizedFrames >= 30) return;
	Vec2 p = pos;
	if (minimizedFrames > 0) p.y = ease_out(pos.y, screen().y, minimizedFrames, 30);
	if (showBackground) box(Rect(p.x, p.y, contents.x + 4, contents.y + 28), th.Primary());
	else box(Rect(p.x, p.y, contents.x + 4, 24), th.Primary());
	box(Rect(p.x + 2, p.y + 2, contents.x, 20), th.Background());
	if (tutorial_is(TUT_MOVE_WINDOW) && Kind() == KIND_BROWSER)   // the tutorial's pulse on the title bar
		box(Rect(p.x + 2, p.y + 2, contents.x, 20), th.Primary((int)(255 * tutorial_pulse(false))));
	// GenerateTitleTexture: the icon, then the title, in the theme's colour.
	int maxLen = contents.x - 24 - (int)buttons.size() * 18;
	if (!icon.empty()) tex(("the_world_machine/window_icons/" + icon).c_str(), Vec2(p.x + 4, p.y + 4), Rect(), th.Primary());
	text(Vec2(p.x + 24, p.y + 3), truncate(title, maxLen), th.Primary());
	for (auto &b : buttons) {
		int a = 255;
		if ((tutorial_is(TUT_MINIMIZE_WINDOW) && b->type == WinButtonType::Minimize) ||
		    (tutorial_is(TUT_CLOSE_WINDOW) && b->type == WinButtonType::Close))
			a = (int)(a * tutorial_pulse());
		b->Draw(p, th, a);
	}
	DrawContents(th, Vec2(p.x + 2, p.y + 26), 255);
	if (modal) modal->Draw(th);
}

void Window::ShowModal(int type, const std::string &message, std::function<void(int)> callback, bool sound, bool canAutomash) {
	if (maximized) ToggleMaximize();
	ModalWindow *m = new ModalWindow(type, message, sound, canAutomash);
	if (modal) defer_delete(modal.release());   // it may be the one whose answer brought this one up
	modal.reset(m);
	m->callback = callback;
	m->onClose = [this](Window *w) {
		ModalWindow *mw = static_cast<ModalWindow *>(w);
		lastModalResponse = mw->response;
		// The dialog closes after its update returns (it's still running).
		if (modal.get() == w) defer_delete(modal.release());
	};
	m->grabFocus = [this](Window *) { if (grabFocus) grabFocus(this); };
	Vec2 p = pos;
	p.x += contents.x / 2 - m->Contents().x / 2 - 2;
	p.y += contents.y / 2 - m->Contents().y / 2;
	if (!maximized) {
		Rect r(p.x, p.y, m->Contents().x, m->Contents().y + 20 + 4);
		Vec2 lim(screen().x, screen().y - 30);
		if (r.x < 0) { p.x = 0; r.x = 0; }
		if (r.x + r.w > lim.x) { p.x = lim.x - r.w; r.x = p.x; }
		if (r.y < 0) { p.y = 0; r.y = 0; }
		if (r.y + r.h > lim.y) { p.y = lim.y - r.h; r.y = p.y; }
	}
	m->pos = p;
	if (grabFocus) grabFocus(this);
}

void Window::OnDragDropped(DraggedItem *item, Vec2 cursorPos) {
	if (item) HandleDragDropped(item, cursorPos - Vec2(pos.x + 2, pos.y + 26));
}

void Window::HandleDragDropped(DraggedItem *item, Vec2) {
	if (item->onDropComplete) item->onDropComplete(false);
}

// ===================================================================== ModalWindow

ModalWindow::ModalWindow(int t, const std::string &message, bool sound, bool canAutomash) : type(t), automashable(canAutomash) {
	// ProcessText: the message may be a string id; "\n" written out means a line break.
	std::string text = loc::twm(message.c_str());
	for (size_t p; (p = text.find("\\n")) != std::string::npos;) text.replace(p, 2, "\n");
	lines = wrap(text, 152);
	int by = (int)lines.size() * 12 + 8;
	if (type == MODAL_YESNO) {
		modalButtons.emplace_back(new TextButton(loc::twm("dialog_yes"), Vec2(20, by), [this] { Click(RESP_YES); }));
		modalButtons.emplace_back(new TextButton(loc::twm("dialog_no"), Vec2(84, by), [this] { Click(RESP_NO); }));
	} else {
		modalButtons.emplace_back(new TextButton(loc::twm("dialog_ok"), Vec2(52, by), [this] { Click(RESP_OK); }));
	}
	icon = type == MODAL_ERROR ? "error" : type == MODAL_YESNO ? "question" : "info";
	if (sound) {
		// The notification sounds go 2, 3, 4, 2, ... (back to the start after 30 s of quiet).
		static const int order[4] = { 2, 3, 4, 2 };
		static int idx;
		static uint64_t last;
		uint64_t now = sys::time_us();
		if (now - last > 30000000ull) idx = 0;
		last = now;
		char snd[16];
		snprintf(snd, sizeof(snd), "twm_notif%d", order[idx]);
		play_sound(snd, 0.5f);
		idx = (idx + 1) % 4;
	}
	SetContents(Vec2(160, (int)lines.size() * 12 + 8 + 20));
	sys::log("dialog: %s\n", text.c_str());
}

void ModalWindow::Click(int r) {
	if (closed) return;
	if (tutorial_is(TUT_DELETE_CONFIRM_DELETION) && r == RESP_NO) return;   // the delete tutorial wants a Yes
	closed = true;
	response = r;
	if (callback) callback(r);
	if (onClose) onClose(this);
}

ModalWindow *ModalWindow::padOwner;

bool ModalWindow::Update(bool consumed) {
	Vec2 parent(pos.x + 2, pos.y + 26);
	for (auto &b : modalButtons) {
		b->Update(parent, !consumed);
		if (closed) return true;
	}
	// The pad as well as the cursor (the console versions' way): not in the
	// first frames, so the press that brought the dialog up doesn't answer it.
	if (age < 8) age++;
	else if (padOwner == this) {
		using namespace input;
		int n = (int)modalButtons.size();
		if (n > 1 && (pressed_raw(BTN_LEFT) || pressed_raw(BTN_RIGHT))) {
			padChoice = (padChoice + 1) % n;   // silent: the World Machine's dialogs have no cursor sound
		}
		if (pressed_raw(BTN_OK)) {
			modalButtons[padChoice]->action();
			return true;
		}
		if (pressed_raw(BTN_CANCEL) && type == MODAL_YESNO) {
			Click(RESP_NO);
			return true;
		}
	}
	for (int i = 0; i < (int)modalButtons.size(); i++) modalButtons[i]->highlight = padOwner == this && i == padChoice;
	if (updatedOnce && automashable && modalButtons.size() == 1 && auto_mashing()) {
		modalButtons[0]->action();
		return true;
	}
	updatedOnce = true;
	return Window::Update(consumed);
}

void ModalWindow::DrawContents(const Theme &th, Vec2 sp, int alpha) {
	box(Rect(sp.x, sp.y, contents.x, contents.y), th.Background(alpha));
	Vec2 p(sp.x + 80, sp.y + 1);
	for (auto &l : lines) {
		text_centered(p, l, th.Primary(alpha));
		p.y += 12;
	}
	for (size_t i = 0; i < modalButtons.size(); i++) {
		modalButtons[i]->Draw(sp, th, alpha);
		if (i == 0 && tutorial_is(TUT_DELETE_CONFIRM_DELETION)) modalButtons[i]->DrawHighlight(sp, th, (int)(alpha * tutorial_pulse(false)));
	}
}

Vec2 ModalWindow::ButtonsPos() const {
	return pos + Vec2(80, modalButtons.empty() ? 0 : modalButtons[0]->Pos().y + 26);
}

} // namespace twm
