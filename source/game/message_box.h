// message_box.h - IMessageBox and TextBox (OneShotMG.src.MessageBox).
//
// TextBox follows the C# closely: text rolls out one character per frame,
// escape codes \n \c[n] \. \| \> and \@portrait are honoured, word wrap
// uses the measured line width, choices and number input work the same,
// and the Opening/Opened/Closing/Closed states drive the interpreter.
// The other IMessageBox kinds (EdBox, DesktopBox, TutorialBox, DocBox,
// CreditsBox) are separate classes below, as in the C#.
#pragma once
#include <stdint.h>
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
	TextBox(TileMap *map, Style style, Area area);
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

	static std::string ReplaceVariableMarkup(const GameState *state, std::string text);
	static unsigned TextColor(int code);

private:
	enum class State { Opening, Opened, Closing, Closed };
	struct Glyph { uint32_t c; unsigned rgb; };
	void Close();
	bool IsRollingText() const { return !inputText.empty() && !forcedNod && textPauseTimer <= 0; }
	void RollOutAllText();
	void ProcessInputText(bool fastCycle = false);
	void SetPortrait(const std::string &p);
	int TextAreaWidth() const;   // game px
	void addNewLine();
	void addNewChar(uint32_t c);
	void HandleChoiceInput();
	void HandleNumberInput();

	TileMap *map;
	GameState *state;
	Style style;
	Area area;
	int boxX = 8, boxY = 168, boxW = 304, boxH = 64;
	int padX = 12, padY = 8, padW = 12;
	State st = State::Opening;
	int transitionTimer = 0, totalTransitionTime = 6;
	float alpha = 0;
	std::string inputText, portrait, scrollSoundName = "text";
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

// Shared Opening/Opened/Closing/Closed fade used by the simple boxes.
struct BoxFade {
	enum State { Opening, Opened, Closing, Closed } st = Opening;
	int timer = 0, total = 0;
	float alpha = 0;
	void open(int frames) { st = Opening; total = frames; timer = 0; }
	void close(int frames) { st = Closing; total = frames; timer = 0; }
	void update();   // advances Opening/Closing
};

// "@ed": centred white text over a dimmed screen (EdBox).
class EdBox : public IMessageBox {
public:
	explicit EdBox(GameState *state);
	void Update() override;
	void Draw() override;
	bool IsReadyForMoreInput() const override { return false; }
	bool IsFinished() const override { return fade.st == BoxFade::Closed; }
	void FeedText(const std::string &text, const std::string &playerName) override;
	void Open() override { fade.open(13); }
	void ClearText() override { lines.clear(); widths.clear(); }
	void InputNumberSetup(int, int) override {}
private:
	GameState *state;
	BoxFade fade;
	std::vector<std::string> lines;
	std::vector<int> widths;
};

// "@desktop": the PC's message box picture with centred black text.
class DesktopBox : public IMessageBox {
public:
	DesktopBox();
	void Update() override;
	void Draw() override;
	bool IsReadyForMoreInput() const override { return false; }
	bool IsFinished() const override { return fade.st == BoxFade::Closed; }
	void FeedText(const std::string &text, const std::string &playerName) override;
	void Open() override;
	void ClearText() override { lines.clear(); }
	void InputNumberSetup(int, int) override {}
private:
	BoxFade fade;
	std::vector<std::string> lines;
};

// "@tut" / "@tut2": the instruction pages (TutorialBox).
class TutorialBox : public IMessageBox {
public:
	explicit TutorialBox(bool rightAligned);
	void Update() override;
	void Draw() override;
	bool IsReadyForMoreInput() const override { return false; }
	bool IsFinished() const override { return fade.st == BoxFade::Closed; }
	void FeedText(const std::string &text, const std::string &playerName) override;
	void Open() override { fade.open(30); }
	void ClearText() override { lines.clear(); }
	void InputNumberSetup(int, int) override {}
private:
	int LineHeight() const;   // 13, 17 for Japanese / Korean / Chinese, 15 right-aligned
	int LineLength(size_t i) const;
	bool rightAligned;
	BoxFade fade;
	std::vector<std::string> lines;
};

// "$...": a document page on lined paper, scrollable (DocBox).
class DocBox : public IMessageBox {
public:
	explicit DocBox(GameState *state);
	void Update() override;
	void Draw() override;
	bool IsReadyForMoreInput() const override { return false; }
	bool IsFinished() const override { return fade.st == BoxFade::Closed; }
	void FeedText(const std::string &text, const std::string &playerName) override;
	void Open() override;
	void ClearText() override { lines.clear(); }
	void InputNumberSetup(int, int) override {}
private:
	GameState *state;
	BoxFade fade;
	std::vector<std::string> lines;
	int startLine = 0;
};

// "@credits" / "@credits_small" (CreditsBox).
class CreditsBox : public IMessageBox {
public:
	CreditsBox(GameState *state, bool small);
	void Update() override;
	void Draw() override;
	bool IsReadyForMoreInput() const override { return false; }
	bool IsFinished() const override { return fade.st == BoxFade::Closed; }
	void FeedText(const std::string &text, const std::string &playerName) override;
	void Open() override { fade.open(51); }
	void ClearText() override { lines.clear(); }
	void InputNumberSetup(int, int) override {}
private:
	struct Line { std::string center, left, right; bool header; };
	GameState *state;
	bool small;
	BoxFade fade;
	std::vector<Line> lines;
};
