// modal.h - the game's dialogs (EdText.info / err / yesno, "can't close
// during cutscenes", ...): a ModalWindow on the OneShot window (twm/), which
// pauses the game until it's answered.
#pragma once
#include <string>

namespace modal {
enum class Type { Info, Error, YesNo };
enum class Response { None, OK, Yes, No };
// text: a string id or the text itself (ModalWindow.ProcessText).
void show(Type type, const std::string &text, bool canAutomash = false);
bool is_open();
Response last_response();
}
