// window.h - TWMWindow (title bar, buttons, dragging, minimize animation,
// a dialog of its own) and ModalWindow (the World Machine's dialogs).
#pragma once
#include "widgets.h"
#include <memory>

namespace twm {

// A d-pad direction for menus: on the press, then repeating while held.
bool pad_repeat(uint32_t button);

struct DraggedItem;

// What a window is (the build has no RTTI).
enum WindowKind { KIND_OTHER, KIND_MODAL, KIND_GAME, KIND_BROWSER, KIND_DOCUMENT, KIND_PHOTO, KIND_JOURNAL, KIND_JUKEBOX, KIND_CUSTOMIZE,
                  KIND_ACHIEVEMENTS, KIND_CONTACTS, KIND_GALLERY, KIND_TIMER, KIND_CONTROLS };

class Window {
public:
	virtual ~Window();
	virtual int Kind() const { return KIND_OTHER; }
	Vec2 pos;
	std::string icon;      // the_world_machine/window_icons/<icon>
	bool showBackground = true;

	std::function<void(Window *)> onMinimize, onClose, grabFocus;
	std::function<void(Window *)> onTitleChange;

	Vec2 Contents() const { return contents; }
	const std::string &Title() const { return title; }   // already localized
	bool Minimized() const { return minimized; }
	bool Maximized() const { return maximized; }
	int MinimizedFrames() const { return minimizedFrames; }

	virtual bool Update(bool consumed);   // true: the cursor is used up
	virtual void PreDraw() {}
	virtual void Draw(const Theme &th);
	virtual void DrawContents(const Theme &th, Vec2 screenPos, int alpha) = 0;
	virtual bool IsSameContent(const Window *other) const = 0;
	// The pad, while this window is in front and isn't the game (the game
	// then doesn't hear it): scroll, pick, change values.
	virtual void PadInput() {}
	// Only windows that use the pad take it from the game (a photo or the
	// journal in front leaves Niko walking, as the clover puzzle needs).
	virtual bool WantsPad() const { return false; }
	virtual void ToggleMaximize() { maximized = !maximized; }
	void ToggleMinimize();

	void AddButton(WinButtonType t, std::function<void()> action = nullptr);
	WindowButton *FindButton(WinButtonType t);

	// A dialog over this window (ShowModalWindow).
	void ShowModal(int type, const std::string &message, std::function<void(int)> callback = nullptr, bool sound = true,
	               bool canAutomash = false);
	bool IsModalOpen() const { return modal != nullptr; }
	Window *Modal() const { return modal.get(); }
	int lastModalResponse = 0;

	// A dragged file let go over this window.
	void OnDragDropped(DraggedItem *item, Vec2 cursorPos);

protected:
	virtual void HandleDragDropped(DraggedItem *item, Vec2 contentPos);
	void SetContents(Vec2 c) { contents = c; RecalcButtons(); }
	void SetTitle(const std::string &t);
	void RecalcButtons();
	Vec2 contents;
	std::string title;
	bool minimized = false, maximized = false;
	int minimizedFrames = 0;
	std::vector<std::unique_ptr<WindowButton>> buttons;
	std::unique_ptr<Window> modal;
	bool held = false;
	Vec2 heldMouse;
};

// ModalWindow
enum ModalType { MODAL_INFO, MODAL_ERROR, MODAL_YESNO };
enum ModalResponse { RESP_NONE, RESP_OK, RESP_YES, RESP_NO, RESP_CANCEL };

class ModalWindow : public Window {
public:
	ModalWindow(int type, const std::string &message, bool sound = true, bool canAutomash = false);
	int type;
	int response = RESP_NONE;
	std::function<void(int)> callback;
	bool Update(bool consumed) override;
	void DrawContents(const Theme &th, Vec2 screenPos, int alpha) override;
	bool IsSameContent(const Window *) const override { return false; }
	int Kind() const override { return KIND_MODAL; }
	// The dialog that answers to the pad this frame (Left/Right to pick,
	// Cross to press, Circle for No): set by the window manager.
	static ModalWindow *padOwner;
private:
	void Click(int r);
	int padChoice = 0, age = 0;
	std::vector<std::string> lines;
	std::vector<std::unique_ptr<TextButton>> modalButtons;
	bool automashable, updatedOnce = false, closed = false;
};

} // namespace twm
