// desktop_test_hooks.cpp - the desktop's switches for host test runs (tools/test/flow.sh).
// The PS3 has no environment, so none of these do anything there.
#include "wm_state.h"
#include "apps/collection_apps.h"
#include "apps/customize.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace twm {

// Once the desktop is up:
// OSPS3_CURSOR="x,y": where the cursor starts (desktop pixels).
// OSPS3_OPEN: an app to open: customize | badges | friends | gallery |
// musicbox | timer | controls | folder:<path> | doc:<path> (the first
// document in that folder).
void test_desktop_ready() {
	if (const char *c = getenv("OSPS3_CURSOR")) {
		int x, y;
		if (sscanf(c, "%d,%d", &x, &y) == 2) cursor().SetMousePos(Vec2(x, y));
	}
	const char *o = getenv("OSPS3_OPEN");
	if (!o) return;
	if (strcmp(o, "customize") == 0) add_window_(new CustomizeWindow());
	else if (strcmp(o, "badges") == 0) add_window_(new AchievementWindow());
	else if (strcmp(o, "friends") == 0) add_window_(new ContactsWindow());
	else if (strcmp(o, "gallery") == 0) add_window_(new GalleryWindow());
	else if (strcmp(o, "musicbox") == 0) add_window_(new JukeboxWindow());
	else if (strcmp(o, "timer") == 0) add_window_(new TimerWindow());
	else if (strcmp(o, "controls") == 0) add_window_(new ControlsWindow());
	else if (strncmp(o, "folder:", 7) == 0) add_window_(new BrowserWindow(&g_fs, o + 7));
	else if (strncmp(o, "doc:", 4) == 0) {
		if (Folder *d = g_fs.GetDir(o + 4))
			for (auto &n : d->contents)
				if (!n->IsFolder() && static_cast<File *>(n.get())->program == PROG_DOCUMENT) {
					run_file(n.get());
					break;
				}
	}
}

// OSPS3_WARP="frame:x,y frame:x,y ...": moves the cursor on those frames.
void test_move_cursor(int frame) {
	if (const char *w = getenv("OSPS3_WARP"))
		for (const char *p = w; *p;) {
			int f, x, y, n = 0;
			if (sscanf(p, "%d:%d,%d%n", &f, &x, &y, &n) != 3) break;
			if (f == frame) cursor().SetMousePos(Vec2(x, y));
			p += n;
			while (*p == ' ') p++;
		}
}

} // namespace twm
