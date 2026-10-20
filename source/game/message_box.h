// message_box.h - IMessageBox and a functional stand-in for TextBox and the
// other boxes (EdBox, DocBox, TutorialBox, ...).
//
// It follows TextBox's state machine exactly (Opening -> Opened -> Closing ->
// Closed, "ready for more input" while Closing, choices, number input,
// showChoicesFollows), which is what the event interpreter depends on.
// Drawing is a debug-font placeholder until the SpriteFont renderer and the
// real box art are ported.
#pragma once
#include <functional>
#include <string>
#include <vector>

class TileMap;
struct GameState;

class IMessageBox {
public:
	virtual ~IMessageBox() = default;
	virtual void Update() = 0;
	virtual void Draw() = 0;
	virtual bool IsReadyForMoreInput() const = 0;
	virtual bool IsFinished() const = 0;
	virtual void FeedText(const std::string &text, const std::string &playerName) = 0;
	virtual void Open() = 0;
	virtual void ClearText() = 0;
	virtual void InputNumberSetup(int var, int digits) = 0;
	virtual class TextBox *AsTextBox() { return nullptr; }   // built without RTTI
};

class TextBox : public IMessageBox {
public:
	enum class Style { GoldBorder, NoBorder };
	enum class Area { Up, Middle, Down };
	enum class Kind { Text, Ed, Desktop, Credits, CreditsSmall, Tutorial, Doc };

	TextBox(GameState *state, Style style, Area area, Kind kind = Kind::Text);
	void Update() override;
	void Draw() override;
	bool IsReadyForMoreInput() const override { return st == State::Closing; }
	bool IsFinished() const override { return st == State::Closed; }
	void FeedText(const std::string &text, const std::string &playerName) override;
	void Open() override;
	void ClearText() override;
	void InputNumberSetup(int var, int digits) override;

	TextBox *AsTextBox() override { return this; }
	void ShowChoicesIsNextCommand(bool b) { showChoicesFollows = b; }
	void SetChoices(const std::vector<std::string> &choices, int cancelValue, std::function<void(int)> cb,
	                const std::string &playerName);
	Style CurrentStyle() const { return style; }
	Area CurrentArea() const { return area; }
	Kind GetKind() const { return kind; }

	static std::string ReplaceVariableMarkup(const GameState *state, std::string text);

private:
	enum class State { Opening, Opened, Closing, Closed };
	void Close();
	GameState *state;
	Style style;
	Area area;
	Kind kind;
	State st = State::Opening;
	int transitionTimer = 0, totalTransitionTime = 6;
	float alpha = 0;
	std::string text, portrait;
	std::vector<std::string> choices;
	int choiceCursor = 0, cancelChoice = 0;
	std::function<void(int)> choiceCallback;
	bool showChoicesFollows = false;
	int inputNumberVar = -1;
	std::vector<int> digits;
	int digitCursor = 0;
};
