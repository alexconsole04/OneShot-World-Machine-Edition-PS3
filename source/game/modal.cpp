#include "modal.h"
#include "twm/window.h"

// The World Machine's dialogs live in twm/ (ModalWindow); these are the
// game's way in (wm.cpp).
namespace twm {
void game_modal_show(int type, const std::string &text, bool canAutomash);
bool game_modal_open();
int game_modal_response();
}

namespace modal {

void show(Type t, const std::string &text, bool canAutomash) {
	int type = t == Type::Error ? twm::MODAL_ERROR : t == Type::YesNo ? twm::MODAL_YESNO : twm::MODAL_INFO;
	twm::game_modal_show(type, text, canAutomash);
}

bool is_open() { return twm::game_modal_open(); }

Response last_response() {
	switch (twm::game_modal_response()) {
	case twm::RESP_OK: return Response::OK;
	case twm::RESP_YES: return Response::Yes;
	case twm::RESP_NO: return Response::No;
	default: return Response::None;
	}
}

} // namespace modal
