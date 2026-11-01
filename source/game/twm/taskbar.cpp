// taskbar.cpp - TWMTaskbar: a button per window, the clock, and the
// minimize-all button.
#include "wm_state.h"
#include "../../platform/sys.h"
#include <stdio.h>

namespace twm {

// ===================================================================== taskbar

bool taskbar_update(bool consumed) {
	Vec2 m = cursor().MousePos();
	if (!consumed && (g_tutStep == TUT_UNMINIMIZE_WINDOW || g_tutStep == TUT_COMPLETE)) {
		Rect r(2, g_taskArea.y + 2, 0, 24);
		for (auto &b : g_taskButtons) {
			r.w = b.w;
			if (in_rect(r, m)) {
				cursor().SetState(MouseCursor::Clickable);
				if (cursor().Clicked() && b.window) {   // OnTaskbarSelect
					Window *w = b.window, *active = nullptr;
					for (auto &x : g_windows)
						if (!x->Minimized()) { active = x.get(); break; }
					if (w->Minimized()) { w->ToggleMinimize(); bring_to_front(w); }
					else if (active == w) w->ToggleMinimize();
					else bring_to_front(w);
				}
			}
			r.x += r.w + 2;
		}
	}
	if (!consumed) {
		g_minimizeAll->type = unminimize_enabled() ? WinButtonType::UnminimizeAll : WinButtonType::MinimizeAll;
	}
	g_minimizeAll->Update(Vec2(0, 0), !consumed);
	for (auto it = g_taskButtons.begin(); it != g_taskButtons.end();) {
		if (!it->window) {
			it->w -= 6;
			if (it->w < 6) { it = g_taskButtons.erase(it); continue; }
		}
		++it;
	}
	return m.y >= g_taskArea.y;
}

void draw_taskbar(Window *active) {
	const Theme &th = theme();
	box(g_taskArea, th.Primary());
	Rect a = g_taskArea;
	a.y += 2;
	a.h -= 2;
	box(a, th.Background());
	Vec2 p(2, a.y + 2);
	for (auto &b : g_taskButtons) {
		Color frame, fill, fg;
		if (b.window) {
			bool on = b.window == active && !b.window->Minimized();
			frame = b.window->Minimized() ? th.Variant() : th.Primary();
			fill = on ? th.Primary() : th.Background();
			fg = on ? th.Background() : th.Primary();
		} else {
			frame = th.Variant();
			fill = th.Background();
			fg = th.Primary();
		}
		Rect r(p.x, p.y, b.w, 24);
		box(r, frame);
		box(shrink(r, 2), fill);
		if (b.window) {
			Vec2 q(r.x + 4, r.y + 4);
			std::string t = b.window->Title();
			if (b.window->Kind() == KIND_BROWSER) t = t.substr(t.find_last_of('/') + 1);
			if (!b.window->icon.empty()) tex(("the_world_machine/window_icons/" + b.window->icon).c_str(), q, Rect(), fg);
			text(Vec2(q.x + 20, q.y), truncate(t, b.w - 4 - 24), fg);
		}
		p.x += b.w + 2;
	}
	// The clock (DateTime.Now.ToShortTimeString), in the console's format,
	// read once a second.
	static char clock[16];
	static uint64_t nextRead, nextFormat;
	static bool h24;
	uint64_t now = sys::time_us();
	if (now >= nextRead) {
		int h, m, s, yd;
		sys::local_time(&h, &m, &s, &yd);
		if (now >= nextFormat) { h24 = sys::clock_24h(); nextFormat = now + 30000000; }
		if (h24) snprintf(clock, sizeof(clock), "%d:%02d", h, m);
		else snprintf(clock, sizeof(clock), "%d:%02d %s", h % 12 == 0 ? 12 : h % 12, m, h < 12 ? "AM" : "PM");
		nextRead = now + 1000000;
	}
	text(Vec2(a.x + a.w - text_w(clock) - 26, a.y + 4), clock, th.Primary());
	g_minimizeAll->Draw(Vec2(0, 0), th, 255);
}

} // namespace twm
