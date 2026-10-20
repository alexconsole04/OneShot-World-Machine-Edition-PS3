// event_runner.h - port of OneShotMG.src.Entities.EventRunner: the RPG Maker
// XP event-command interpreter.
#pragma once
#include "event_data.h"
#include "message_box.h"
#include <memory>
#include <string>

class TileMap;
class Entity;
struct GameState;

class EventRunner {
public:
	EventRunner(TileMap *map, CommandListPtr list, int eventId, int eventPageNumber, Entity *triggeringEntity = nullptr);
	~EventRunner();
	void Update();
	void Draw();
	void EndEvent() { commandIndex = (int)commands->size(); }
	bool IsFinished() const { return commandIndex >= (int)commands->size(); }
	Entity *TriggeringEntity() const { return triggeringEntity; }
	int EventId() const { return triggeringEventId; }

	bool KeepAlive = true;
	std::string PasswordInput;

private:
	const EventCommand &cmd(int i) const { return (*commands)[i]; }
	int count() const { return (int)commands->size(); }
	Entity *GetEntity(int entityId);
	int GetOperateValue(const std::string &operation, const std::string &operandType, const std::string &operand);
	void conditionalBranchNavigation(const EventCommand &c, bool result);
	void skipToIndent(int indent);
	TextBox *textBox();   // messageBox if it is a TextBox, else null
	void ensureTextBox();

	bool commandShowText(const EventCommand &c);
	bool commandShowChoices(const EventCommand &c);
	bool commandWhenChoice(const EventCommand &c);
	bool commandWhenCancel(const EventCommand &c);
	bool commandInputNumber(const EventCommand &c);
	bool commandConditionalBranch(const EventCommand &c);
	bool commandControlVariables(const EventCommand &c);
	bool commandControlSwitches(const EventCommand &c);
	bool commandControlSelfSwitch(const EventCommand &c);
	bool commandScript(const EventCommand &c);
	bool commandShowPicture(const EventCommand &c, bool move);
	bool commandChangeItems(const EventCommand &c);
	bool commandCallCommonEvent(const EventCommand &c);
	bool commandTransferPlayer(const EventCommand &c);
	bool commandScrollMap(const EventCommand &c);
	bool commandRepeatAbove(const EventCommand &c);
	bool commandBreakLoop(const EventCommand &c);
	bool commandJumpToLabel(const EventCommand &c);
	bool commandChangeMapSettings(const EventCommand &c);
	bool commandChangePartyMember(const EventCommand &c);
	bool commandScreenFlash(const EventCommand &c);
	bool commandChangeScreenColorTone(const EventCommand &c);
	bool commandChangePictureColorTone(const EventCommand &c);
	bool commandScreenShake(const EventCommand &c);
	bool commandSetEventLocation(const EventCommand &c);

	TileMap *map;
	GameState *state;
	CommandListPtr commands;
	int commandIndex = 0;
	int waitTimer = 0;
	int buttonInputVarId = 0;
	int selectedChoice = -1;
	int currentChoicesIndent = -1;
	int triggeringEventId, triggeringEventPageNumber;
	Entity *triggeringEntity;
	std::unique_ptr<IMessageBox> messageBox;
	std::unique_ptr<EventRunner> innerRunner;
	bool waitForMapTransition = false;
	bool waitForModalWindow = false;
	TextBox::Style currentStyle = TextBox::Style::GoldBorder;
	TextBox::Area currentArea = TextBox::Area::Down;
};
