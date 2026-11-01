// run_file.cpp - WindowManager.RunFile: what opening each kind of file does,
// and adding the unlockable apps to the desktop.
#include "wm_state.h"
#include "apps/collection_apps.h"
#include "apps/custom_wallpapers.h"
#include "../../platform/sys.h"
#include "../core/loc.h"
#include "../core/oneshot_window.h"
#include <stdlib.h>

namespace twm {

// ===================================================================== RunFile

static void granted(File *f, const char *what, const std::string &id) {
	std::string type = what;
	g_fs.Delete(f);
	play_sound("pc_granted", 0.8f, 1.5f);
	flash(Color(0, 255, 255, 255), 20, 0.25f);
	if (type == "profile") unlocks().UnlockProfile(id);
	else unlocks().UnlockWallpaper(id);
	save_desktop();
}

static void not_yet(const std::string &name) {
	// Apps of the next stage.
	show_system_modal(MODAL_INFO, loc::twm(name.c_str()) + ":\n" + "This app isn't in the PS3 version yet.");
}


bool run_file(Node *node) {
	if (!node) return false;
	if (node->IsFolder()) {
		add_window_(new BrowserWindow(&g_fs, static_cast<Folder *>(node)->Path()));
		return true;
	}
	File *f = static_cast<File *>(node);
	auto arg = [f](size_t i) { return i < f->args.size() ? f->args[i] : std::string(); };
	sys::log("twm: run %s (%s)\n", f->name.c_str(), program_name(f->program));
	switch (f->program) {
	case PROG_ONESHOT:
		if (find_window([](Window *w) { return w->Kind() == KIND_JUKEBOX; })) {
			show_system_modal(MODAL_ERROR, "error_launch_oneshot_musicbox_open");
		} else if (!game_window()) {
			if (add_window_(new GameWindow())) {
				sys::log("desktop: launching OneShot\n");
				window().Launch(true);
				if (settings().fullscreen) set_game_maximized(true);   // the saved Fullscreen setting
			}
		} else {
			add_window_(new GameWindow());   // brings the open one up
		}
		break;
	case PROG_PHOTO:
		add_window_(new PhotoWindow(arg(0), f->name, f->args.size() > 1 ? atoi(arg(1).c_str()) : 1));
		break;
	case PROG_JOURNAL: add_window_(new JournalWindow()); break;
	case PROG_DOCUMENT: add_window_(new DocumentWindow(f->name, arg(0), arg(1))); break;
	case PROG_JUKEBOX:
		if (game_window()) show_system_modal(MODAL_ERROR, "error_launch_musicbox_oneshot_open");
		else add_window_(new JukeboxWindow());
		break;
	case PROG_CONTACTS:
		if (unlocks().profiles.empty()) show_system_modal(MODAL_ERROR, "error_no_friends_unlocked");
		else add_window_(new ContactsWindow());
		break;
	case PROG_GALLERY: add_window_(new GalleryWindow()); break;
	case PROG_ACHIEVEMENT: add_window_(new AchievementWindow()); break;
	case PROG_TIMER: add_window_(new TimerWindow()); break;
	case PROG_CONTROLS: add_window_(new ControlsWindow()); break;
	case PROG_AUTHOR_PROFILE: granted(f, "profile", "author"); break;
	case PROG_NIKO_PROFILE: granted(f, "profile", "niko"); break;
	case PROG_FROMNIKO_WALLPAPER: granted(f, "wallpaper", "from_niko"); break;
	case PROG_WALLPAPER:
		if (!g_desktop->IsOverridden()) {
			// The player's own pictures are decoded in the background: the
			// old wallpaper stays up until the new one is ready.
			g_pendingWallpaper.clear();
			if (is_custom_wallpaper(arg(0)) && !custom_wallpaper_ready(arg(0))) {
				g_pendingWallpaper = arg(0);
				break;
			}
			g_desktop->SetWallpaper(arg(0));
			save_desktop();
		}
		break;
	case PROG_THEME:
		if (theme_by_id(arg(0))) g_themeId = arg(0);
		save_desktop();
		break;
	case PROG_SHUTDOWN:
		if (game_window()) {
			show_system_modal(MODAL_ERROR, "cant_shutdown_while_oneshot_running");
			break;
		}
		show_system_modal(MODAL_YESNO, "do_you_really_want_to_shut_down", [](int r) {
			if (r != RESP_YES) return;
			save_desktop_and_fs();
			write_saves();
			fade(128, 255, 120, [] {
				for (auto &w : g_windows)
					if (w->onClose) w->onClose(w.get());
				g_quit = true;
			});
		});
		break;
	case PROG_CUSTOMIZE: add_window_(new CustomizeWindow()); break;
	case PROG_DUMMY_FILE_FOR_TUTORIALS: break;
	case PROG_TUTORIAL_INTRO_FILE: start_tutorial(PROG_TUTORIAL_INTRO_FILE); break;
	case PROG_TUTORIAL_DELETE_FILE: start_tutorial(PROG_TUTORIAL_DELETE_FILE); break;
	case PROG_TUTORIAL_MOVE_FILE: start_tutorial(PROG_TUTORIAL_MOVE_FILE); break;
	default: not_yet(f->name); break;
	}
	return true;
}

void unlock_app(Program p) {
	if (g_fs.FileTypeExists(p)) return;
	std::shared_ptr<File> f;
	switch (p) {
	case PROG_CONTACTS: f = make_file("contacts", "contacts_app", p); break;
	case PROG_JUKEBOX: f = make_file("jukebox", "musicbox_appname", p); break;
	case PROG_GALLERY: f = make_file("gallery", "gallery_app", p); break;
	case PROG_ACHIEVEMENT: f = make_file("achievements", "achievements_app_name", p); break;
	default: return;
	}
	f->deleteRestricted = true;
	g_fs.WriteFile(Filesystem::DESKTOP, f);
}

} // namespace twm
