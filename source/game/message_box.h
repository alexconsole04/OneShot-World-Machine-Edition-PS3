// message_box.h - IMessageBox and TextBox (OneShotMG.src.MessageBox).
//
// TextBox follows the C# closely: text rolls out one character per frame,
// escape codes \n \c[n] \. \| \> and \@portrait are honoured, word wrap
// uses the measured line width, choices and number input work the same,
// and the Opening/Opened/Closing/Closed states drive the interpreter.
// Only the glyphs differ: the debug font stands in for the game's
// SpriteFont until that renderer is ported. EdBox/DocBox/TutorialBox etc.
// use the same class with a different look.
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

	TextBox(TileMap *map, Style style, Area area, Kind kind = Kind::Text);
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
	static unsigned TextColor(int code);

private:
	enum class State { Opening, Opened, Closing, Closed };
	struct Glyph { char c; unsigned rgb; };
	void Close();
	bool IsRollingText() const { return !inputText.empty() && !forcedNod && textPauseTimer <= 0; }
	void RollOutAllText();
	void ProcessInputText(bool fastCycle = false);
	void SetPortrait(const std::string &p);
	int TextAreaWidth() const;   // game px
	void addNewLine();
	void addNewChar(char c);
	void HandleChoiceInput();
	void HandleNumberInput();

	TileMap *map;
	GameState *state;
	Style style;
	Area area;
	Kind kind;
	int boxX = 8, boxY = 168, boxW = 304, boxH = 64;
	int padX = 12, padY = 8, padW = 12;
	State st = State::Opening;
	int transitionTimer = 0, totalTransitionTime = 6;
	float alpha = 0;
	std::string inputText, portrait;
	std::vector<std::vector<Glyph>> lines;
	int lineWidth = 0;           // window px
	unsigned color = 0xffffff;
	float scrollTimer = 0;
	int scrollSoundTimer = 0, textPauseTimer = 0;
	bool forcedNod = false;
	int cursorAnimTimer = 0, nodTimer = 0, nodIndex = 0;
	float cursorAlpha = 0.5f;
	std::vector<std::string> choices;
	int choiceCursor = 0, cancelChoice = -1, choiceBeginsOnLine = 0;
	std::function<void(int)> choiceCallback;
	bool showChoicesFollows = false;
	int inputNumberVar = -1;
	std::vector<int> digits;
	int digitCursor = 0;
};
