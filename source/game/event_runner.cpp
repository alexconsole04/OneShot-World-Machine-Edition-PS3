// event_runner.cpp - see event_runner.h. Follows EventRunner.cs command by
// command; differences are noted inline.
#include "event_runner.h"
#include "entity.h"
#include "player.h"
#include "tilemap.h"
#include "game_state.h"
#include "pictures.h"
#include "script_parser.h"
#include "modal.h"
#include "../engine/audio.h"
#include "../engine/util.h"
#include "../platform/input.h"
#include "../platform/sys.h"
#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

enum Code {
	Empty = 0, ShowText = 101, ShowChoices = 102, MoreText = 401, WhenChoice = 402, WhenCancel = 403,
	InputNumber = 103, ChangeTextOptions = 104, ButtonInputProcessing = 105, Wait = 106,
	ConditionalBranch = 111, Else = 411, BranchEnd = 412, BranchEndForChoices = 404, Loop = 112,
	RepeatAbove = 413, BreakLoop = 113, ExitEventProcessing = 115, EraseEvent = 116, CallCommonEvent = 117,
	Label = 118, JumpToLabel = 119, ControlSwitches = 121, ControlVariables = 122, ControlSelfSwitch = 123,
	ControlTimer = 124, ChangeGold = 125, ChangeItems = 126, ChangePartyMember = 129, ChangeWindowskin = 131,
	TransferPlayer = 201, SetEventLocation = 202, ScrollMap = 203, ChangeMapSettings = 204,
	ChangeFogColorTone = 205, ChangeFogOpacity = 206, ShowAnimation = 207, ChangeTransparentFlag = 208,
	SetMoveRoute = 209, WaitForMoveCompletion = 210, PrepareForTransition = 221, ExecuteTransition = 222,
	ChangeScreenColorTone = 223, ScreenFlash = 224, ScreenShake = 225, ShowPicture = 231, MovePicture = 232,
	RotatePicture = 233, ChangePictureColorTone = 234, ErasePicture = 235, SetWeatherEffects = 236,
	PlayBGM = 241, FadeOutBGM = 242, PlayBGS = 245, FadeOutBGS = 246, MemorizeBGMandBGS = 247,
	RestoreBGMandBGS = 248, PlayME = 249, PlaySE = 250, StopSE = 251, NameInputProcessing = 303,
	ChangeActorGraphic = 322, CallSaveScreen = 352, ReturnToTitleScreen = 354, Script = 355,
	ScriptContinuation = 655, Comment = 108, CommentContinued = 408
};

// InputManager.Button numbering used by conditional branch type 11.
static uint32_t game_button(int b) {
	switch (b) {
	case 2: return input::BTN_DOWN;
	case 4: return input::BTN_LEFT;
	case 6: return input::BTN_RIGHT;
	case 8: return input::BTN_UP;
	case 11: return input::BTN_OK;
	case 12: return input::BTN_CANCEL;
	case 13: return input::BTN_MAINMENU;
	case 14: return input::BTN_INVENTORY;
	case 15: return input::BTN_RUN;
	case 50: return input::BTN_MASHTEXT;
	default: return 0;
	}
}

static std::vector<std::string> parse_string_list(const std::string &json) {
	std::vector<std::string> out;
	cJSON *a = cJSON_Parse(json.c_str());
	const cJSON *it;
	cJSON_ArrayForEach(it, a) out.push_back(cJSON_IsString(it) ? it->valuestring : "");
	cJSON_Delete(a);
	return out;
}

// "(r, g, b[, a])" as written by the event exporter.
static void parse_color(const std::string &s, float *out, int n) {
	std::string t;
	for (char c : s) if (c != '(' && c != ')') t += c;
	const char *p = t.c_str();
	for (int i = 0; i < n; i++) {
		out[i] = (float)strtod(p, (char **)&p);
		while (*p == ',' || *p == ' ') p++;
	}
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

EventRunner::EventRunner(TileMap *m, CommandListPtr list, int eventId, int eventPageNumber, Entity *trig)
	: map(m), state(m->State()), commands(list ? std::move(list) : std::make_shared<CommandList>()),
	  triggeringEventId(eventId), triggeringEventPageNumber(eventPageNumber), triggeringEntity(trig) {}

EventRunner::~EventRunner() = default;

TextBox *EventRunner::textBox() { return messageBox ? messageBox->AsTextBox() : nullptr; }

Entity *EventRunner::GetEntity(int entityId) {
	if (entityId == -1) return map->GetPlayer();
	if (entityId == 0) return triggeringEntity;
	return map->GetEntityByID(entityId);
}

int EventRunner::GetOperateValue(const std::string &operation, const std::string &operandType, const std::string &operand) {
	int v = atoi(operand.c_str());
	if (atoi(operandType.c_str()) != 0) v = state->var(v);
	if (atoi(operation.c_str()) == 1) v = -v;
	return v;
}

void EventRunner::skipToIndent(int indent) {
	while (commandIndex < count() && cmd(commandIndex).indent != indent) commandIndex++;
}

void EventRunner::Update() {
	if (waitForModalWindow) {
		if (modal::is_open()) return;
		waitForModalWindow = false;
		const EventCommand &c = cmd(commandIndex);
		commandIndex++;
		if (c.code == ConditionalBranch) {
			modal::Response r = modal::last_response();
			conditionalBranchNavigation(c, r == modal::Response::OK || r == modal::Response::Yes);
		}
	}
	if (waitForMapTransition) {
		if (map->IsChangingMap()) return;
		waitForMapTransition = false;
	}
	bool stop = false;
	if (messageBox) {
		messageBox->Update();
		if (messageBox->IsFinished()) messageBox.reset();
		else if (!messageBox->IsReadyForMoreInput()) return;
	}
	if (innerRunner) {
		innerRunner->Update();
		if (!innerRunner->IsFinished()) return;
		innerRunner.reset();
	}
	if (waitTimer > 0) {
		waitTimer--;
		return;
	}
	if (nameInput) {
		nameInput->Update();
		if (nameInput->IsOpen()) return;
		nameInput.reset();
	}
	if (buttonInputVarId > 0) {
		using namespace input;
		if (!pressed(BTN_OK) && !pressed(BTN_CANCEL) && !pressed(BTN_DOWN) && !pressed(BTN_LEFT) &&
		    !pressed(BTN_RIGHT) && !pressed(BTN_UP) && !held(BTN_MASHTEXT))
			return;
		state->set_var(22, 1);   // C# sets variable 22, not buttonInputVarId
		buttonInputVarId = 0;
	}
	while (commandIndex < count() && !stop) {
		const EventCommand &c = cmd(commandIndex);
		commandIndex++;
		switch (c.code) {
		case ShowText: stop = commandShowText(c); break;
		case ConditionalBranch: stop = commandConditionalBranch(c); break;
		case Else: skipToIndent(c.indent); break;
		case ControlVariables: stop = commandControlVariables(c); break;
		case ControlSwitches: stop = commandControlSwitches(c); break;
		case ControlSelfSwitch: stop = commandControlSelfSwitch(c); break;
		case PlaySE:
			audio::play_sound(c.audio_file.name.c_str(), c.audio_file.volume / 100.0f, c.audio_file.pitch / 100.0f);
			break;
		case Script: stop = commandScript(c); break;
		case Wait:
			waitTimer = c.iparam(0) * 2;
			stop = true;
			break;
		case ButtonInputProcessing:
			buttonInputVarId = c.iparam(0);
			waitTimer = 1;
			stop = true;
			break;
		case ShowPicture: stop = commandShowPicture(c, false); break;
		case MovePicture: stop = commandShowPicture(c, true); break;
		case ErasePicture: pictures().ErasePicture(c.iparam(0)); break;
		case ChangeItems: stop = commandChangeItems(c); break;
		case PlayME:
			audio::play_me(c.audio_file.name.c_str(), c.audio_file.volume / 100.0f, c.audio_file.pitch / 100.0f);
			break;
		case CallCommonEvent: stop = commandCallCommonEvent(c); break;
		case ExitEventProcessing:
			stop = true;
			EndEvent();
			break;
		case EraseEvent:
			if (triggeringEntity) {
				triggeringEntity->KillEntityAfterUpdate = true;
				triggeringEntity = nullptr;
			}
			break;
		case ChangeActorGraphic:
			map->GetPlayer()->SetNPCSheet(c.param(1), c.iparam(2));
			break;
		case PlayBGM: {
			float vol = c.audio_file.volume / 100.0f * 0.8f, pitch = c.audio_file.pitch / 100.0f;
			if (triggeringEntity && triggeringEntity->GetName() == "music")
				audio::queue_song(c.audio_file.name.c_str(), 0.0f, vol, pitch);
			else
				audio::play_song(c.audio_file.name.c_str(), 0.0f, vol, pitch);
			break;
		}
		case PlayBGS:
			audio::play_bgs(c.audio_file.name.c_str(), c.audio_file.volume / 100.0f, c.audio_file.pitch / 100.0f);
			break;
		case TransferPlayer: stop = commandTransferPlayer(c); break;
		case ScrollMap: stop = commandScrollMap(c); break;
		case Loop: break;
		case RepeatAbove: stop = commandRepeatAbove(c); break;
		case BreakLoop: stop = commandBreakLoop(c); break;
		case MemorizeBGMandBGS: audio::memorize_bgm_bgs(); break;
		case RestoreBGMandBGS: audio::restore_bgm_bgs(); break;
		case Label: break;
		case JumpToLabel: stop = commandJumpToLabel(c); break;
		case ChangeMapSettings: stop = commandChangeMapSettings(c); break;
		case ChangePartyMember: stop = commandChangePartyMember(c); break;
		case SetMoveRoute:
			if (Entity *e = GetEntity(c.iparam(0)))
				if (c.move_route) e->ForceMoveRoute(c.move_route);
			break;
		case WaitForMoveCompletion:
			if (map->AreAnyEntitiesInForcedRoutes()) {
				commandIndex--;
				stop = true;
			}
			break;
		case ChangeTextOptions:
			currentArea = (TextBox::Area)c.iparam(0);
			currentStyle = c.iparam(1) == 0 ? TextBox::Style::GoldBorder : TextBox::Style::NoBorder;
			break;
		case ScreenFlash: stop = commandScreenFlash(c); break;
		case FadeOutBGM: audio::fade_out_bgm(c.fparam(0)); break;
		case FadeOutBGS: audio::fade_out_bgs(c.fparam(0)); break;
		case ChangeScreenColorTone: stop = commandChangeScreenColorTone(c); break;
		case ChangePictureColorTone: stop = commandChangePictureColorTone(c); break;
		case ShowAnimation:
			// Moodbubble entity: not ported yet.
			break;
		case ShowChoices: stop = commandShowChoices(c); break;
		case WhenChoice: stop = commandWhenChoice(c); break;
		case WhenCancel: stop = commandWhenCancel(c); break;
		case ScreenShake: stop = commandScreenShake(c); break;
		case SetEventLocation: stop = commandSetEventLocation(c); break;
		case InputNumber: stop = commandInputNumber(c); break;
		case PrepareForTransition: break;
		case ExecuteTransition:
			map->ExecuteMapTransition(c.param(0));
			waitForMapTransition = true;
			stop = true;
			break;
		case CallSaveScreen: break;   // debug save menu: not in the PS3 build
		case Empty: case Comment: case ChangeWindowskin: case BranchEndForChoices: case CommentContinued:
		case BranchEnd: case ScriptContinuation:
			break;
		default: {
			static bool warned[1000];
			if (c.code >= 0 && c.code < 1000 && !warned[c.code]) {
				warned[c.code] = true;
				sys::log("event: command code %d not implemented\n", c.code);
			}
			break;
		}
		}
	}
	if (IsFinished() && triggeringEntity) triggeringEntity->Unlock();
}

void EventRunner::Draw() {
	if (messageBox) messageBox->Draw();
	if (nameInput) nameInput->Draw();
	if (innerRunner) innerRunner->Draw();
}

void EventRunner::OpenNameInput() {
	if (getenv("OSPS3_TRACE")) sys::log("event %d: name input\n", triggeringEventId);
	PasswordInput.clear();
	nameInput.reset(new NameInputMenu(this, state));
	nameInput->Open();
}

void EventRunner::ensureTextBox() {
	TextBox *tb = textBox();
	if (!messageBox || !tb || tb->CurrentStyle() != currentStyle ||
	    tb->CurrentArea() != currentArea) {
		messageBox.reset(new TextBox(map, currentStyle, currentArea));
	} else {
		messageBox->Open();
		messageBox->ClearText();
	}
}

bool EventRunner::commandInputNumber(const EventCommand &c) {
	ensureTextBox();
	messageBox->InputNumberSetup(c.iparam(0), c.iparam(1));
	return true;
}

bool EventRunner::commandShowText(const EventCommand &c) {
	std::string text = c.param(0);
	if (text.empty()) return false;
	// LanguageManager.GetMapLocString: English text is used as-is.
	std::string low = util::lower(text);
	if (util::starts_with(low, "@ed ")) {
		text = text.substr(4);
		messageBox.reset(new EdBox(state));
	} else if (util::starts_with(low, "@desktop ")) {
		text = text.substr(9);
		messageBox.reset(new DesktopBox());
	} else if (util::starts_with(low, "@credits ")) {
		text = text.substr(9);
		messageBox.reset(new CreditsBox(state, false));
	} else if (util::starts_with(low, "@credits_small ")) {
		text = text.substr(15);
		messageBox.reset(new CreditsBox(state, true));
	} else if (util::starts_with(text, "@tut ") || util::starts_with(text, "@tut2 ")) {
		bool tut2 = util::starts_with(text, "@tut2 ");
		text = text.substr(tut2 ? 6 : 5);
		while (commandIndex < count() &&
		       ((cmd(commandIndex).code == ShowText && util::starts_with(cmd(commandIndex).param(0), "@tut ")) ||
		        cmd(commandIndex).code == MoreText)) {
			std::string s = cmd(commandIndex).param(0);
			if (cmd(commandIndex).code == ShowText) s = s.substr(tut2 ? 6 : 5);
			text += s;
			commandIndex++;
		}
		messageBox.reset(new TutorialBox(tut2));
	} else if (!text.empty() && text[0] == '$') {
		text = text.substr(1);
		while (commandIndex < count() && cmd(commandIndex).code == ShowText &&
		       util::starts_with(cmd(commandIndex).param(0), "$")) {
			text += "\n\n" + cmd(commandIndex).param(0).substr(1);
			commandIndex++;
		}
		messageBox.reset(new DocBox(state));
	} else {
		bool inputNumber = false, showChoices = false;
		int inputNumberDigits = -1, inputNumberVar = -1;
		int i = commandIndex;
		while (i < count()) {
			const EventCommand &n = cmd(i);
			i++;
			if (n.code == MoreText) {
				text += " " + n.param(0);
				commandIndex = i;
				continue;
			}
			if (n.code == InputNumber) {
				inputNumber = true;
				inputNumberVar = n.iparam(0);
				inputNumberDigits = n.iparam(1);
				commandIndex = i;
				continue;
			}
			if (n.code == ShowChoices && parse_string_list(n.param(0)).size() < 4) showChoices = true;
			break;
		}
		ensureTextBox();
		textBox()->ShowChoicesIsNextCommand(showChoices);
		if (inputNumber) messageBox->InputNumberSetup(inputNumberVar, inputNumberDigits);
	}
	messageBox->FeedText(text, state->player_name);
	if (getenv("OSPS3_TRACE")) sys::log("event %d: text '%s'\n", triggeringEventId, text.c_str());
	return true;
}

bool EventRunner::commandShowChoices(const EventCommand &c) {
	std::vector<std::string> list = parse_string_list(c.param(0));
	TextBox *tb = textBox();
	if (!messageBox || !tb || tb->CurrentStyle() != currentStyle ||
	    tb->CurrentArea() != currentArea) {
		messageBox.reset(new TextBox(map, currentStyle, currentArea));
	} else {
		messageBox->Open();
		if (list.size() >= 4 || commandIndex < 2 || cmd(commandIndex - 2).code != ShowText ||
		    cmd(commandIndex - 2).param(0).empty())
			messageBox->ClearText();
	}
	int cancelValue = c.iparam(1);
	currentChoicesIndent = c.indent;
	textBox()->SetChoices(list, cancelValue, [this](int choice) { selectedChoice = choice; }, state->player_name);
	return true;
}

bool EventRunner::commandWhenChoice(const EventCommand &c) {
	if (c.iparam(0) == selectedChoice && c.indent == currentChoicesIndent) {
		selectedChoice = -1;
		return false;
	}
	skipToIndent(c.indent);
	return false;
}

bool EventRunner::commandWhenCancel(const EventCommand &c) {
	if (selectedChoice == 4 && c.indent == currentChoicesIndent) {
		selectedChoice = -1;
		return false;
	}
	skipToIndent(c.indent);
	return false;
}

bool EventRunner::commandConditionalBranch(const EventCommand &c) {
	bool result = false;
	int type = c.iparam(0);
	switch (type) {
	case 0:
		result = state->flag(c.iparam(1)) == (c.iparam(2) == 0);
		break;
	case 1: {
		int v = state->var(c.iparam(1));
		int o = c.iparam(2) != 0 ? state->var(c.iparam(3)) : c.iparam(3);
		switch (c.iparam(4)) {
		case 0: result = v == o; break;
		case 1: result = v >= o; break;
		case 2: result = v <= o; break;
		case 3: result = v > o; break;
		case 4: result = v < o; break;
		case 5: result = v != o; break;
		}
		break;
	}
	case 2:
		if (triggeringEntity) result = state->self_switch(triggeringEntity->GetID(), c.param(1)) == (c.iparam(2) == 0);
		break;
	case 4:
		if (c.iparam(2) == 0) result = state->has_follower(c.iparam(1));
		break;
	case 6:
		if (Entity *e = GetEntity(c.iparam(1))) result = e->GetDirection() == (Direction)c.iparam(2);
		break;
	case 8:
		result = state->has_item(c.iparam(1));
		break;
	case 11: {
		uint32_t b = game_button(c.iparam(1));
		result = b && input::pressed(b);
		break;
	}
	case 12:
		switch (script::HandleScriptConditional(map, c.param(1), this, triggeringEventPageNumber)) {
		case script::CondResult::Yes: result = true; break;
		case script::CondResult::No: result = false; break;
		case script::CondResult::EdText:
			waitForModalWindow = true;
			commandIndex--;
			return true;
		}
		break;
	default:
		sys::log("event: unimplemented condition type %d in conditional branch\n", type);
		break;
	}
	conditionalBranchNavigation(c, result);
	return false;
}

void EventRunner::conditionalBranchNavigation(const EventCommand &c, bool result) {
	if (result) return;
	skipToIndent(c.indent);
	if (commandIndex < count() && cmd(commandIndex).code == Else) commandIndex++;
}

bool EventRunner::commandControlVariables(const EventCommand &c) {
	int value = 0;
	int src = c.iparam(3);
	switch (src) {
	case 0: value = c.iparam(4); break;
	case 1: value = state->var(c.iparam(4)); break;
	case 2: value = util::random(c.iparam(4), c.iparam(5)); break;
	case 3: value = state->has_item(c.iparam(4)) ? 1 : 0; break;
	case 6:
		if (Entity *e = GetEntity(c.iparam(4))) {
			switch (c.iparam(5)) {
			case 0: value = e->GetCurrentTile().x; break;
			case 1: value = e->GetCurrentTile().y; break;
			case 2: value = (int)e->GetDirection(); break;
			default: sys::log("event: unrecognized entityValueSource %d\n", c.iparam(5)); break;
			}
		}
		break;
	case 7:
		if (c.iparam(4) == 0) value = map->GetMapID();
		else sys::log("event: unrecognized otherValueSource %d\n", c.iparam(4));
		break;
	default:
		sys::log("event: unimplemented variable source %d\n", src);
		break;
	}
	int first = c.iparam(0), last = c.iparam(1), op = c.iparam(2);
	for (int i = first; i <= last; i++) {
		int v = state->var(i);
		switch (op) {
		case 0: v = value; break;
		case 1: v += value; break;
		case 2: v -= value; break;
		case 3: v *= value; break;
		case 4: if (value != 0) v /= value; break;
		case 5: if (value != 0) v %= value; break;
		}
		if (v > 99999999) v = 99999999;
		else if (v < -99999999) v = -99999999;
		state->set_var(i, v);
	}
	return false;
}

bool EventRunner::commandControlSwitches(const EventCommand &c) {
	int first = c.iparam(0), last = c.iparam(1);
	bool on = c.iparam(2) == 0;
	for (int i = first; i <= last; i++) state->set_flag(i, on);
	// Desktop.inSolstice: the World Machine desktop is not ported yet.
	return false;
}

bool EventRunner::commandControlSelfSwitch(const EventCommand &c) {
	if (triggeringEntity) state->set_self_switch(triggeringEntity->GetID(), c.param(0), c.iparam(1) == 0);
	return false;
}

bool EventRunner::commandScript(const EventCommand &c) {
	std::string text = c.param(0);
	while (commandIndex < count() && cmd(commandIndex).code == ScriptContinuation) {
		text += "\n" + cmd(commandIndex).param(0);
		commandIndex++;
	}
	bool r = script::HandleScript(map, text, this, triggeringEventPageNumber);
	if (modal::is_open()) {
		commandIndex--;
		waitForModalWindow = true;
		return true;
	}
	return r;
}

bool EventRunner::commandShowPicture(const EventCommand &c, bool move) {
	int slot = c.iparam(0);
	Vec2 pos;
	if (c.iparam(3) != 0) {
		pos.x = state->var(c.iparam(4));
		pos.y = state->var(c.iparam(5));
	} else {
		pos.x = c.iparam(4);
		pos.y = c.iparam(5);
	}
	bool center = c.iparam(2) != 0;
	float xs = c.fparam(6) / 100.0f, ys = c.fparam(7) / 100.0f;
	int opacity = c.iparam(8);
	gfx::Blend blend = c.iparam(9) != 1 ? gfx::Blend::Normal : gfx::Blend::Additive;
	if (move) pictures().MovePicture(slot, c.iparam(1) * 2, center, pos, xs, ys, opacity, blend);
	else pictures().ShowPicture(slot, c.param(1), center, pos, xs, ys, opacity, blend);
	return false;
}

bool EventRunner::commandChangeItems(const EventCommand &c) {
	int v = GetOperateValue(c.param(1), c.param(2), c.param(3));
	if (v > 0) state->add_item(c.iparam(0));
	else if (v < 0) state->remove_item(c.iparam(0));
	return false;
}

bool EventRunner::commandCallCommonEvent(const EventCommand &c) {
	const CommonEvent *ce = map->GetCommonEvent(c.iparam(0));
	if (ce) {
		innerRunner.reset(new EventRunner(map, ce->list, ce->id, -1));
		innerRunner->Update();
		if (!innerRunner->IsFinished()) return true;
		innerRunner.reset();
	}
	return false;
}

bool EventRunner::commandTransferPlayer(const EventCommand &c) {
	int mapId, x, y, dir;
	if (c.iparam(0) != 0) {
		mapId = state->var(c.iparam(1));
		x = state->var(c.iparam(2));
		y = state->var(c.iparam(3));
	} else {
		mapId = c.iparam(1);
		x = c.iparam(2);
		y = c.iparam(3);
	}
	dir = c.iparam(4);
	bool fade = c.iparam(5) == 0;
	map->ChangeMap(mapId, (float)x, (float)y, fade ? 0.5f : 0.0f, (Direction)dir);
	waitForMapTransition = fade;
	return fade;
}

bool EventRunner::commandScrollMap(const EventCommand &c) {
	if (map->IsMapScrolling()) {
		commandIndex--;
		return true;
	}
	map->ScrollMap(c.iparam(0), c.iparam(1), c.iparam(2));
	return false;
}

bool EventRunner::commandRepeatAbove(const EventCommand &c) {
	commandIndex--;
	do {
		commandIndex--;
	} while (commandIndex >= 0 && (cmd(commandIndex).indent != c.indent || cmd(commandIndex).code != Loop));
	if (commandIndex < 0) commandIndex = 0;
	return false;
}

bool EventRunner::commandBreakLoop(const EventCommand &c) {
	int i = commandIndex;
	while (i < count() && (cmd(i).indent >= c.indent || cmd(i).code != RepeatAbove)) i++;
	if (i + 1 < count()) commandIndex = i + 1;
	return false;
}

bool EventRunner::commandJumpToLabel(const EventCommand &c) {
	const std::string &label = c.param(0);
	int i = 0;
	while (i < count() && (cmd(i).code != Label || cmd(i).param(0) != label)) i++;
	if (i < count()) commandIndex = i;
	return false;
}

bool EventRunner::commandChangeMapSettings(const EventCommand &c) {
	switch (c.iparam(0)) {
	case 0: map->SetPanorama(c.param(1)); break;
	case 1:
		map->SetFog(c.param(1), c.iparam(2), c.fparam(3) / 255.0f,
		            c.iparam(4) == 0 ? gfx::Blend::Normal : gfx::Blend::Additive, c.iparam(6), c.iparam(7));
		break;
	case 2: sys::log("event: battleback change not supported\n"); break;
	}
	return false;
}

bool EventRunner::commandChangePartyMember(const EventCommand &c) {
	int actor = c.iparam(0);
	bool add = c.iparam(1) == 0;
	switch (actor) {
	case 1:   // Niko
		if (add) map->GetPlayer()->SetNPCSheet("niko", 0);
		return false;
	case 2: case 3: case 4: case 5: case 6:   // Alula, Rue, Plight, Cedric, Proto
		if (add) {
			if (!state->has_follower(actor)) state->followers.push_back(actor);
			map->SpawnFollowers();
		} else {
			for (size_t i = 0; i < state->followers.size(); i++)
				if (state->followers[i] == actor) { state->followers.erase(state->followers.begin() + i); break; }
			map->SpawnFollowers();
		}
		break;
	default:
		sys::log("event: unrecognized actorId %d\n", actor);
		break;
	}
	return false;
}

bool EventRunner::commandScreenFlash(const EventCommand &c) {
	float v[4] = { 0, 0, 0, 0 };
	parse_color(c.param(0), v, 4);
	for (float &f : v) f = clampf(f, 0.0f, 255.0f);
	flash().StartFlash((uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2], (uint8_t)v[3], c.iparam(1) * 2);
	return false;
}

bool EventRunner::commandChangeScreenColorTone(const EventCommand &c) {
	float v[3] = { 0, 0, 0 };
	parse_color(c.param(0), v, 3);
	for (float &f : v) f = clampf(f, -255.0f, 255.0f);
	map->SetScreenTone(GameTone((short)v[0], (short)v[1], (short)v[2]), c.iparam(1) * 2);
	return false;
}

bool EventRunner::commandChangePictureColorTone(const EventCommand &c) {
	float v[3] = { 0, 0, 0 };
	parse_color(c.param(1), v, 3);
	for (float &f : v) f = clampf(f, -255.0f, 255.0f);
	pictures().ChangePictureTone(c.iparam(0), GameTone((short)v[0], (short)v[1], (short)v[2]), c.iparam(2) * 2);
	return false;
}

bool EventRunner::commandScreenShake(const EventCommand &c) {
	bool elevator = (triggeringEventId == 25 && triggeringEventPageNumber < 0) ||
	                (triggeringEntity && triggeringEntity->GetName() == "elevator shake");
	map->ScreenShake(c.iparam(0), c.iparam(1), c.iparam(2) * 2, !elevator);
	return false;
}

bool EventRunner::commandSetEventLocation(const EventCommand &c) {
	Entity *e = GetEntity(c.iparam(0));
	if (!e) return false;
	switch (c.iparam(1)) {
	case 0: e->SetPosTile(Vec2(c.iparam(2), c.iparam(3))); break;
	case 1: e->SetPosTile(Vec2(state->var(c.iparam(2)), state->var(c.iparam(3)))); break;
	default:
		if (Entity *o = GetEntity(c.iparam(2))) {
			Vec2 t = e->GetCurrentTile();
			e->SetPosTile(o->GetCurrentTile());
			o->SetPosTile(t);
		}
		break;
	}
	e->SetDirection((Direction)c.iparam(4));
	return false;
}
