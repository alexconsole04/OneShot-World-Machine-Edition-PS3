// modal.h - ModalWindow: the World Machine's pop-up dialogs (EdText.info /
// err / yesno) that sit on top of the game window and block the event.
#pragma once
#include <string>

namespace modal {
enum class Type { Info, Error, YesNo };
enum class Response { None, OK, Yes, No };
void show(Type type, const std::string &text);
bool is_open();
Response last_response();
void update();   // call once per frame (before game logic)
void draw();     // screen space, after the game window
}
