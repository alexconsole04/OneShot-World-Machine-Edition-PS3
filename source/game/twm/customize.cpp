// customize.cpp - see customize.h. Port of CustomizationWindow and
// ChooserControl, with this port's Erase All Data.
#include "customize.h"
#include "wm_internal.h"
#include "../../platform/input.h"
#include "../loc.h"
#include "../settings.h"
#include "../../engine/gm.h"
#include <stdio.h>

namespace twm {

// ===================================================================== Chooser

Chooser::Chooser(Vec2 pos, int length, std::vector<std::pair<std::string, std::string>> it, const std::string &selected)
	: area(pos.x, pos.y, length, 16), items(std::move(it)) {
	left = IconButton("the_world_machine/window_icons/arrow_left", pos, [this] { Step(-1); });
	right = IconButton("the_world_machine/window_icons/arrow_right", pos + Vec2(length - 16, 0), [this] { Step(1); });
	SetValue(selected);
}

void Chooser::SetValue(const std::string &key) {
	for (size_t i = 0; i < items.size(); i++)
		if (items[i].first == key) index = (int)i;
}

void Chooser::Step(int d) {
	int n = (int)items.size();
	if (n == 0) return;
	index = ((index + d) % n + n) % n;
	if (onItemChange) onItemChange(items[index].first);
}

bool Chooser::Update(Vec2 parentPos, bool canInteract) {
	bool used = !canInteract;
	used |= left.Update(parentPos, canInteract);
	used |= right.Update(parentPos, canInteract);
	return used;
}

void Chooser::Draw(Vec2 parentPos, const Theme &th, int alpha) const {
	Rect r = translated(area, parentPos);
	box(r, th.Primary(alpha));
	box(shrink(r, 1), th.Background(alpha));
	left.Draw(parentPos, th, alpha);
	right.Draw(parentPos, th, alpha);
	if (!items.empty())
		line(Vec2(r.x + area.w / 2, r.y + 2), loc::twm(items[index].second.c_str()), th.Primary(alpha), area.w - 36, true);
}

// ===================================================================== CustomizeWindow

static const int COL = 240, W = 232;

CustomizeWindow::CustomizeWindow() {
	icon = "customize";
	SetTitle(loc::twm("customize_app_name"));
	SetContents(Vec2(480, 204));
	typedef std::vector<std::pair<std::string, std::string>> Items;
	// Left column (the PC's Vsync row, at y 24, is left free).
	badgeToasts.reset(new Chooser(Vec2(4, 64), W, Items{ { "ON", "settings_on" }, { "OFF", "settings_off" } },
	                              unlocks().badgeToastsEnabled ? "ON" : "OFF"));
	badgeToasts->onItemChange = [](const std::string &v) {
		unlocks().badgeToastsEnabled = v == "ON";
		if (toasts().ToastCount() < 5) {
			if (unlocks().badgeToastsEnabled) toasts().AddToast("achievements/unknown", loc::twm("badge_toasts_enabled_text"));
			else toasts().AddToast("achievements/unknown_gray", loc::twm("badge_toasts_disabled_text"));
		}
		save_desktop();
	};
	eraseButton.reset(new TextButton("Erase All Data", Vec2(4, 104), [this] { OnEraseAllData(); }, W));
	// System Scaling is the maximized game window's scaling here (the
	// desktop itself is drawn pixel for pixel on the PS3).
	systemScaling.reset(new Chooser(Vec2(4, 144), W,
	                                Items{ { "SMOOTH", "customize_system_scaling_smooth" }, { "SHARP", "customize_system_scaling_sharp" } },
	                                settings().scaling == gm::SCALE_SMOOTH ? "SMOOTH" : "SHARP"));
	systemScaling->onItemChange = [](const std::string &v) {
		settings().scaling = v == "SMOOTH" ? gm::SCALE_SMOOTH : gm::SCALE_SHARP;
		settings().Save();
		save_desktop();
	};
	viewWallpaper.reset(new TextButton(loc::twm("customize_view_wallpaper_button"), Vec2(4, 168), [] { hide_all_ui(); }, W));
	// Right column (the PC's Fullscreen row, at y 144, is left free).
	gamma.reset(new Slider(loc::twm("customize_slider_label_brightness"), 50, 150, Vec2(4 + COL, 8), W, true, false, "%"));
	gamma->SetValue(brightness());
	gamma->onValueChanged = [](int v) { set_brightness(v); };
	bgm.reset(new Slider(loc::twm("customize_slider_label_musicvolume"), 0, 100, Vec2(4 + COL, 48), W, true, false, "%"));
	bgm->SetValue(bgm_volume());
	bgm->onValueChanged = [](int v) { set_volumes(v, sfx_volume()); };
	sfx.reset(new Slider(loc::twm("customize_slider_label_soundvolume"), 0, 100, Vec2(4 + COL, 88), W, true, false, "%"));
	sfx->SetValue(sfx_volume());
	sfx->onValueChanged = [](int v) { set_volumes(bgm_volume(), v); };
	changeLanguage.reset(new TextButton(loc::twm("change_language_button"), Vec2(4 + COL, 168), [this] { OnChangeLanguage(); }, W));
	AddButton(WinButtonType::Close);
	AddButton(WinButtonType::Minimize);
	onClose = [](Window *) { save_desktop(); };
}

void CustomizeWindow::OnChangeLanguage() {
	if (game_window_open()) {
		ShowModal(MODAL_ERROR, "cant_change_language_when_oneshot_running");
		return;
	}
	show_system_modal(MODAL_YESNO, "change_language_yesno", [](int r) {
		if (r == RESP_YES) request_restart(RESTART_LANGUAGE);
	});
}

void CustomizeWindow::OnEraseAllData() {
	if (game_window_open()) {
		ShowModal(MODAL_ERROR, "Close OneShot before erasing your data.");
		return;
	}
	show_system_modal(MODAL_YESNO,
	                  "Erase All Data?\nThis deletes your game save, the desktop, every unlock, and what carries over "
	                  "between playthroughs. Language and settings are kept.",
	                  [](int r) {
		if (r != RESP_YES) return;
		show_system_modal(MODAL_YESNO, "Are you sure?\nThis can't be undone. The World Machine will restart as on its first launch.",
		                  [](int r2) {
			if (r2 == RESP_YES) request_restart(RESTART_ERASE);
		});
	});
}

Rect CustomizeWindow::CtlRect(int c) const {
	switch (c) {
	case BADGE: return badgeToasts->Area();
	case ERASE: return Rect(4, 104, W, 16);
	case SCALING: return systemScaling->Area();
	case WALLPAPER: return Rect(4, 168, W, 16);
	case GAMMA: return Rect(4 + COL, 8, W, 32);
	case BGM: return Rect(4 + COL, 48, W, 32);
	case SFX: return Rect(4 + COL, 88, W, 32);
	default: return Rect(4 + COL, 168, W, 16);
	}
}

void CustomizeWindow::PadInput() {
	using namespace input;
	static const int left[4] = { BADGE, ERASE, SCALING, WALLPAPER }, right[4] = { GAMMA, BGM, SFX, LANGUAGE };
	bool isRight = sel >= GAMMA;
	const int *col = isRight ? right : left;
	int row = 0;
	for (int i = 0; i < 4; i++)
		if (col[i] == sel) row = i;
	bool any = false;
	if (pad_repeat(BTN_UP)) { row = (row + 3) % 4; any = true; }
	else if (pad_repeat(BTN_DOWN)) { row = (row + 1) % 4; any = true; }
	int dx = pad_repeat(BTN_LEFT) ? -1 : pad_repeat(BTN_RIGHT) ? 1 : 0;
	if (any) sel = col[row];
	if (dx) {
		any = true;
		switch (sel) {
		case BADGE: badgeToasts->Step(dx); break;
		case SCALING: systemScaling->Step(dx); break;
		case GAMMA: if (dx < 0) gamma->ArrowLeft(); else gamma->ArrowRight(); break;
		case BGM: if (dx < 0) bgm->ArrowLeft(); else bgm->ArrowRight(); break;
		case SFX: if (dx < 0) sfx->ArrowLeft(); else sfx->ArrowRight(); break;
		default:   // a button: over to the other column
			if (pressed_raw(BTN_LEFT) || pressed_raw(BTN_RIGHT)) sel = (isRight ? left : right)[row];
			break;
		}
	}
	if (pressed_raw(BTN_OK)) {
		any = true;
		switch (sel) {
		case BADGE: badgeToasts->Step(1); break;
		case SCALING: systemScaling->Step(1); break;
		case ERASE: OnEraseAllData(); break;
		case WALLPAPER: hide_all_ui(); break;
		case LANGUAGE: OnChangeLanguage(); break;
		default: break;
		}
	}
	if (any) padUsed = true;
}

bool CustomizeWindow::Update(bool consumed) {
	if (!IsModalOpen()) {
		bool can = !consumed && !minimized;
		Vec2 pp(pos.x + 2, pos.y + 26);
		bool used = false;
		used |= badgeToasts->Update(pp, can);
		used |= eraseButton->Update(pp, can);
		used |= gamma->Update(pp, can);
		used |= bgm->Update(pp, can);
		used |= sfx->Update(pp, can);
		used |= systemScaling->Update(pp, can);
		used |= changeLanguage->Update(pp, can);
		used |= viewWallpaper->Update(pp, can);
		(void)used;
	}
	return Window::Update(consumed);
}

void CustomizeWindow::DrawContents(const Theme &th, Vec2 sp, int alpha) {
	box(Rect(sp.x, sp.y, contents.x, contents.y), th.Background(alpha));
	badgeToasts->Draw(sp, th, alpha);
	eraseButton->Draw(sp, th, alpha);
	viewWallpaper->Draw(sp, th, alpha);
	gamma->Draw(th, sp, alpha);
	bgm->Draw(th, sp, alpha);
	sfx->Draw(th, sp, alpha);
	systemScaling->Draw(sp, th, alpha);
	changeLanguage->Draw(sp, th, alpha);
	Color c = th.Primary(alpha);
	if (padUsed) {
		// The pad's pick: a frame around the control.
		Rect r = translated(CtlRect(sel), sp);
		r = Rect(r.x - 3, r.y - 3, r.w + 6, r.h + 6);
		Color v = th.Primary(alpha);
		box(Rect(r.x, r.y, r.w, 1), v);
		box(Rect(r.x, r.y + r.h - 1, r.w, 1), v);
		box(Rect(r.x, r.y, 1, r.h), v);
		box(Rect(r.x + r.w - 1, r.y, 1, r.h), v);
	}
	Vec2 lbl = sp + Vec2(120, 46);
	line(lbl, loc::twm("customize_chooser_label_badgetoasts"), c, COL, true);
	line(lbl + Vec2(0, 40), "Save Management", c, COL, true);   // the PC's Resolution row
	line(lbl + Vec2(0, 80), loc::twm("customize_chooser_label_system_scaling"), c, COL, true);
}

} // namespace twm
