// script_parser.cpp - port of ScriptParser.cs. The game matches its Ruby
// script snippets as exact strings; this keeps the same strings.
//
// Desktop (TWM) side effects - files written into the World Machine's file
// system, wallpapers, profiles, badges, the window - go through twm/wm.h.
// Particles and lights are stubs for now (logged once).
#include "script_parser.h"
#include "event_runner.h"
#include "entity.h"
#include "player.h"
#include "tilemap.h"
#include "game_state.h"
#include "menus.h"
#include "modal.h"
#include "loc.h"
#include "oneshot_window.h"
#include "save.h"
#include "settings.h"
#include "twm/fs.h"
#include "twm/wm.h"
#include "../engine/audio.h"
#include "../engine/util.h"
#include "../platform/input.h"
#include "../platform/sys.h"

#include <functional>
#include <set>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>

namespace script {

static void note(const char *what, const std::string &s) {
	static std::set<std::string> seen;
	if (seen.insert(s).second) sys::log("script: %s: %s\n", what, s.c_str());
}

static bool starts(const std::string &s, const char *p) { return s.compare(0, strlen(p), p) == 0; }

static const uint8_t GLEN_PUZZLE_SOLUTION[30] = {
	0, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0 };
// s1: the middle column (x 5 of 11) - the gap between the journal's torn pages.
static const uint8_t TOWER_PUZZLE_SOLUTION_1[121] = {
	0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 };
static const uint8_t TOWER_PUZZLE_SOLUTION_3[121] = {
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
	0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0,
	0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0,
	0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
static uint8_t TOWER_PUZZLE_SOLUTION_4[121] = { 0 };   // [60] = 1, set below
static const uint8_t TOWER_PUZZLE_SOLUTION_5[132] = {
	0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1,
	1, 0, 1, 1, 0, 0, 0, 1, 1, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1,
	1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 1, 0, 0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1,
	0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0 };

struct Ctx {
	TileMap *map;
	GameState *st;
	EventRunner *caller;
	Player *player() const { return map->GetPlayer(); }
	Vec2 ptile() const { return map->GetPlayer()->GetCurrentTile(); }
	Vec2 etile(int id) const {
		Entity *e = map->GetEntityByID(id);
		return e ? e->GetCurrentTile() : Vec2();
	}
	void flag22(bool b) { st->set_flag(22, b); }
};

// ---------------------------------------------------------------- EdText (modal pop-ups)
static std::string decode_json_string(const std::string &s) {
	size_t a = s.find('"');
	if (a == std::string::npos) return s;
	std::string out;
	for (size_t i = a + 1; i < s.size(); i++) {
		char c = s[i];
		if (c == '"') break;
		if (c == '\\' && i + 1 < s.size()) {
			char n = s[++i];
			switch (n) {
			case 'n': out += '\n'; break;
			case 't': out += '\t'; break;
			case '"': out += '"'; break;
			case '\\': out += '\\'; break;
			case '/': out += '/'; break;
			default: out += '\\'; out += n; break;
			}
			continue;
		}
		out += c;
	}
	return out;
}

static void HandleEdText(Ctx &c, std::string s, int pageId) {
	s = s.substr(strlen("EdText."));
	modal::Type t;
	if (starts(s, "info(")) { s = s.substr(5); t = modal::Type::Info; }
	else if (starts(s, "err(")) { s = s.substr(4); t = modal::Type::Error; }
	else if (starts(s, "yesno(")) { s = s.substr(6); t = modal::Type::YesNo; }
	else return;
	size_t close = s.find(')');
	std::string text = decode_json_string(close == std::string::npos ? s : s.substr(0, close));
	for (auto &ch : text) if (ch == '\n') ch = ' ';
	size_t p;
	while ((p = text.find("  ")) != std::string::npos) text.erase(p, 1);
	while (!text.empty() && text.front() == ' ') text.erase(0, 1);
	while (!text.empty() && text.back() == ' ') text.pop_back();
	text = loc::map_text(pageId, text);
	while ((p = text.find("\\p")) != std::string::npos) text.replace(p, 2, c.st->player_name);
	modal::show(t, text, true);
}

static std::string quoted_arg(const std::string &s) {   // Regex ".+?['\"](.+?)['\"]"
	size_t a = s.find_first_of("'\"");
	if (a == std::string::npos) return "";
	size_t b = s.find_first_of("'\"", a + 1);
	return b == std::string::npos ? "" : s.substr(a + 1, b - a - 1);
}

static void write_files(const char *path, std::initializer_list<std::shared_ptr<twm::File>> files) {
	for (auto &f : files) {
		f->deleteRestricted = true;
		twm::filesystem().WriteFile(path, f);
	}
}

static void UnlockMap(Ctx &c, std::string s) {
	s = s.substr(strlen("unlock_map "));
	std::vector<std::string> parts;
	size_t p = 0;
	while (true) {
		size_t q = s.find(',', p);
		std::string part = s.substr(p, q == std::string::npos ? std::string::npos : q - p);
		while (!part.empty() && part.front() == ' ') part.erase(0, 1);
		while (!part.empty() && part.back() == ' ') part.pop_back();
		for (auto &ch : part) ch = (char)tolower((unsigned char)ch);
		parts.push_back(part);
		if (q == std::string::npos) break;
		p = q + 1;
	}
	GameState::TravelLocation loc;
	if (!parts.empty()) {
		if (parts[0] == ":blue") loc.zone = 0;
		else if (parts[0] == ":green") loc.zone = 1;
		else if (parts[0] == ":red") loc.zone = 2;
		else if (parts[0] == ":red_ground") loc.zone = 3;
	}
	loc.mapId = c.map->GetMapID();
	Entity *te = c.caller ? c.caller->TriggeringEntity() : nullptr;
	Vec2 t = te ? te->GetCurrentTile() : Vec2();
	loc.x = t.x;
	loc.y = t.y;
	if (parts.size() > 2) {
		if (parts[2] == ":down") loc.direction = 2;
		else if (parts[2] == ":up") loc.direction = 8;
		else if (parts[2] == ":left") loc.direction = 4;
		else if (parts[2] == ":right") loc.direction = 6;
	}
	c.st->travel_enabled = true;   // FastTravelManager.Unlocked
	c.st->travel_zone = loc.zone;  // CurrentZone
	for (auto &l : c.st->travel_locations)
		if (l.zone == loc.zone && l.mapId == loc.mapId) return;
	c.st->travel_locations.push_back(loc);
}

static void set_reflection(Ctx &c, int mirrorY, int clampY, bool copyFrame, bool copySheet, bool flipDir) {
	Entity *te = c.caller ? c.caller->TriggeringEntity() : nullptr;
	if (!te) return;
	Player *pl = c.player();
	Vec2 p = pl->GetPos();
	p.y = mirrorY - (p.y - mirrorY);
	if (clampY && p.y > clampY) p.y = clampY;
	te->SetPos(p);
	if (copyFrame) te->SetFrameIndex(pl->GetFrameIndex());
	if (copySheet) te->SetNPCSheet(pl->GetNPCSheet());
	if (flipDir) {
		switch (pl->GetDirection()) {
		case Direction::Left: te->SetDirection(Direction::Left); break;
		case Direction::Right: te->SetDirection(Direction::Right); break;
		case Direction::Down: te->SetDirection(Direction::Up); break;
		case Direction::Up: te->SetDirection(Direction::Down); break;
		default: break;
		}
	}
}

using Fn = std::function<bool(Ctx &)>;   // returns HandleScript's result

static const std::unordered_map<std::string, Fn> &script_table() {
	static std::unordered_map<std::string, Fn> t;
	if (!t.empty()) return t;
	TOWER_PUZZLE_SOLUTION_4[60] = 1;
	auto none = [](Ctx &) { return false; };
	auto flag_if = [](std::function<bool(Ctx &)> cond) {
		return Fn([cond](Ctx &c) { c.flag22(cond(c)); return false; });
	};

	t["demo_end_check"] = none;
	t["create_author_prof_file"] = [](Ctx &) {
		using namespace twm;
		Filesystem &fs = filesystem();
		if (!profile_unlocked("author") && !fs.FileTypeExists(PROG_AUTHOR_PROFILE)) {
			auto f = make_file("contacts", "author_prof_filename", PROG_AUTHOR_PROFILE);
			f->deleteRestricted = true;
			fs.WriteFile(Filesystem::MYGAMESONESHOT, f);
		}
		if (!profile_unlocked("niko") && !fs.FileTypeExists(PROG_NIKO_PROFILE)) {
			auto f = make_file("contacts", "niko_prof_filename", PROG_NIKO_PROFILE);
			f->deleteRestricted = true;
			fs.WriteFile(Filesystem::MYDOCUMENTS, f);
		}
		if (!wallpaper_unlocked("from_niko") && !fs.FileTypeExists(PROG_FROMNIKO_WALLPAPER)) {
			auto f = make_file("photo", "fromniko_wallpaper_filename", PROG_FROMNIKO_WALLPAPER);
			f->deleteRestricted = true;
			fs.WriteFile(Filesystem::MYDOCUMENTS, f);
		}
		return false;
	};
	t["GlitchPixelEffect()"] = [](Ctx &) { window().StartGlitch(20); return false; };
	t["GlitchPixelEffectLong()"] = [](Ctx &) { window().StartGlitch(40); return false; };
	t["GlitchPixelEffectWeak()"] = [](Ctx &) { window().StartGlitch(20, 0.1f); return false; };
	t["GlitchPixelEffectWeakest()"] = [](Ctx &) { window().StartGlitch(40, 0.02f); return false; };
	t["GlitchPixelEffectWeakLong()"] = [](Ctx &) { window().StartGlitch(40, 0.1f); return false; };
	t["watcher_tell_time"] = [](Ctx &c) {
		int h, m, s, yd;
		sys::local_time(&h, &m, &s, &yd);
		c.st->set_var(22, h >= 6 && h < 12 ? 0 : h >= 12 && h < 17 ? 1 : h >= 17 ? 2 : 3);
		return false;
	};
	t["$scene = Scene_Map.new"] = [](Ctx &c) { if (c.map->menus) c.map->menus->itemMenu.Close(); return false; };
	t["disable_travel"] = [](Ctx &c) { c.st->travel_enabled = false; return false; };
	t["enable_travel"] = [](Ctx &c) { c.st->travel_enabled = true; return false; };
	t["save"] = [](Ctx &c) { save::Make(c.map, c.st); return false; };
	t["Script.niko_reflection_enc_update"] = [](Ctx &c) { set_reflection(c, 83968, 79872, true, false, true); return false; };
	t["Script.niko_reflection_peng_update"] = [](Ctx &c) { set_reflection(c, 83968, 79872, false, false, false); return false; };
	t["Script.niko_reflection_update"] = [](Ctx &c) { set_reflection(c, 57344, 0, true, true, true); return false; };
	auto house = [](Ctx &c) { c.map->SetAmbientTone(GameTone(-8, -8, -8)); return false; };
	auto blue = [](Ctx &c) { c.map->SetAmbientTone(GameTone(-40, -40, -40)); return false; };
	auto green = [](Ctx &c) { c.map->SetAmbientTone(GameTone(-30, -20, -30)); return false; };
	t["house_ambient\nadd_light :eyes, 'niko_eyes', 1.0, \n14 * 32, 16 * 32"] = house;
	t["house_ambient"] = house;
	t["blue_ambient"] = blue;
	t["quit_game_bed"] = [](Ctx &c) {
		save::Make(c.map, c.st);
		window().ExitGame();
		return true;
	};
	t["quit_game_no_save"] = [](Ctx &) { window().ExitGame(); return true; };
	t["real_load"] = [](Ctx &c) {
		if (!save::Load(c.map, c.st)) {
			sys::log("ERROR: %s\n", loc::twm("game_load_failed").c_str());
			save::ClearCorrupt();
			window().ExitGame();
		}
		c.caller->EndEvent();
		return false;
	};
	t["erase_game\nload_perma_flags"] = [](Ctx &) { save::Erase(); return false; };
	t["kill_perma_flags"] = [](Ctx &c) { save::KillPermaFlags(c.st); return false; };
	t["Window_Settings.load_settings"] = [](Ctx &) { settings().Load(); return false; };
	t["$game_variables[22] = $game_player.x\n$game_variables[23] = $game_player.y"] = [](Ctx &c) {
		Vec2 p = c.ptile();
		c.st->set_var(22, p.x);
		c.st->set_var(23, p.y);
		return false;
	};
	t["Script.tmp_s1 = Script.px == 26 && \nScript.py >= 23"] = flag_if([](Ctx &c) { return c.ptile().x == 26 && c.ptile().y >= 23; });
	t["Script.tmp_s1 = Script.px == 33"] = flag_if([](Ctx &c) { return c.ptile().x == 33; });
	t["Script.tmp_s1 = Script.py >= 29"] = flag_if([](Ctx &c) { return c.ptile().y >= 29; });
	t["Script.tmp_v2 = Script.py + 2"] = [](Ctx &c) { c.st->set_var(23, c.ptile().y + 2); return false; };
	t["Script.tmp_s1 = Script.py >= 29\nScript.tmp_v2 = Script.py"] = [](Ctx &c) {
		c.flag22(c.ptile().y >= 29);
		c.st->set_var(23, c.ptile().y);
		return false;
	};
	t["Script.tmp_s1 = Script.px == 59 && \nScript.py >= 34"] = flag_if([](Ctx &c) { return c.ptile().y >= 34 && c.ptile().x == 59; });
	// calamus npc = event 37, cedric npc = 129, silver walk = 23, rue = 12, silver = 63
	t["Script.tmp_s1 = Script.py < \nScript.eve_y(\"calamus npc\") - 2"] = flag_if([](Ctx &c) { return c.ptile().y < c.etile(37).y - 2; });
	t["Script.tmp_s1 = Script.py > \nScript.eve_y(\"calamus npc\") + 2"] = flag_if([](Ctx &c) { return c.ptile().y > c.etile(37).y + 2; });
	t["Script.tmp_s1 = Script.px > \nScript.eve_x(\"calamus npc\") + 2"] = flag_if([](Ctx &c) { return c.ptile().x > c.etile(37).x + 2; });
	t["Script.tmp_s1 = Script.px < \nScript.eve_x(\"calamus npc\") - 2"] = flag_if([](Ctx &c) { return c.ptile().x < c.etile(37).x - 2; });
	t["Script.tmp_s1 = 46 > \nScript.eve_y(\"calamus npc\") "] = flag_if([](Ctx &c) { return 46 > c.etile(37).y; });
	t["Script.tmp_s1 = 46 < \nScript.eve_y(\"calamus npc\")"] = flag_if([](Ctx &c) { return 46 < c.etile(37).y; });
	t["Script.tmp_s1 = 60 > \nScript.eve_x(\"calamus npc\") "] = flag_if([](Ctx &c) { return 60 > c.etile(37).x; });
	t["Script.tmp_s1 = 60 < \nScript.eve_x(\"calamus npc\")"] = flag_if([](Ctx &c) { return 60 < c.etile(37).x; });
	t["Script.tmp_s1 = \nScript.eve_x(\"silver walk\") >= 47"] = flag_if([](Ctx &c) { return c.etile(23).x >= 47; });
	t["Script.tmp_s1 = Script.py > \nScript.eve_y(\"cedric npc\")"] = flag_if([](Ctx &c) { return c.ptile().y > c.etile(129).y; });
	t["Script.tmp_s1 = Script.py < \nScript.eve_y(\"cedric npc\")"] = flag_if([](Ctx &c) { return c.ptile().y < c.etile(129).y; });
	t["Script.tmp_s1 = Script.px > \nScript.eve_x(\"cedric npc\") + 2"] = flag_if([](Ctx &c) { return c.ptile().x > c.etile(129).x + 2; });
	t["Script.tmp_s1 = Script.px < \nScript.eve_x(\"cedric npc\") - 2"] = flag_if([](Ctx &c) { return c.ptile().x < c.etile(129).x - 2; });
	t["Script.tmp_s1 = 57 > \nScript.eve_y(\"cedric npc\") "] = flag_if([](Ctx &c) { return 57 > c.etile(129).y; });
	t["Script.tmp_s1 = 57 < \nScript.eve_y(\"cedric npc\")"] = flag_if([](Ctx &c) { return 57 < c.etile(129).y; });
	t["Script.tmp_s1 = 73 > \nScript.eve_x(\"cedric npc\") "] = flag_if([](Ctx &c) { return 73 > c.etile(129).x; });
	t["Script.tmp_s1 = 73 < \nScript.eve_x(\"cedric npc\")"] = flag_if([](Ctx &c) { return 73 < c.etile(129).x; });
	t["Script.tmp_s1 = Script.px == \nScript.eve_x(\"rue\")"] = flag_if([](Ctx &c) { return c.ptile().x == c.etile(12).x; });
	t["Script.tmp_s1 = Script.py == \nScript.eve_y(\"rue\")"] = flag_if([](Ctx &c) { return c.ptile().y == c.etile(12).y; });
	t["Script.tmp_s1 = Script.py >= 14"] = flag_if([](Ctx &c) { return c.ptile().y >= 14; });
	t["Script.tmp_s1 = Script.px > \nScript.eve_x(\"silver\")"] = flag_if([](Ctx &c) { return c.ptile().x > c.etile(63).x; });
	t["Script.tmp_s1 = Script.px < \nScript.eve_x(\"silver\")"] = flag_if([](Ctx &c) { return c.ptile().x < c.etile(63).x; });
	t["enter_name"] = [](Ctx &c) { c.caller->OpenNameInput(); return true; };
	t["Graphics.smooth = true"] = none;
	t["Graphics.smooth = false"] = none;
	t["blue_ambient\nbg 'blue_mine'\n$game_temp.footstep_sfx = \n['step_gravel']"] = [](Ctx &c) {
		c.map->SetAmbientTone(GameTone(-40, -40, -40));
		c.map->SetBackground("blue_mine");
		c.map->OverrideStepSounds({ { "step_gravel" } });
		return false;
	};
	t["$game_temp.footstep_sfx = \\\n['step_tile_soft',\\\n   'step_wood',\\\n   'step_grate_soft']"] = [](Ctx &c) {
		c.map->OverrideStepSounds({ { "step_tile_soft01", "step_tile_soft02", "step_tile_soft03", "step_tile_soft04" },
		                            { "step_wood" }, { "step_grate_soft" } });
		return false;
	};
	t["ambient -30, -30, -30\nbg 'green_maize'\n$game_temp.footstep_sfx = ['step_grass']"] = [](Ctx &c) {
		c.map->SetAmbientTone(GameTone(-30, -30, -30));
		c.map->SetBackground("green_maize");
		c.map->OverrideStepSounds({ { "step_grass" } });
		return false;
	};
	t["safe_puzzle_write"] = [](Ctx &c) {
		twm::filesystem().WriteFile(twm::Filesystem::MYDOCUMENTS,
		                            twm::make_file("doc", "app_safe_document", twm::PROG_DOCUMENT, { "document", std::to_string(c.st->var(20)) }));
		return false;
	};
	t["safe_puzzle_postgame_write"] = [](Ctx &c) {
		twm::filesystem().WriteFile(twm::Filesystem::MYDOCUMENTS,
		                            twm::make_file("doc", "app_safe_document", twm::PROG_DOCUMENT, { "document_postgame", std::to_string(c.st->var(20)) }));
		return false;
	};
	t["Script.copy_journal"] = [](Ctx &) {
		twm::filesystem().AddDir(twm::Filesystem::MYGAMESONESHOT);
		auto f = twm::make_file("clover", "app_journal", twm::PROG_JOURNAL);
		f->deleteRestricted = f->moveRestricted = true;
		twm::filesystem().WriteFile(twm::Filesystem::MYGAMESONESHOT, f);
		return false;
	};
	t["Script.create_boxes"] = [](Ctx &) {
		for (const char *d : { "portal_1_foldername/", "portal_2_foldername/", "portal_3_foldername/", "big_portal_foldername/" })
			twm::filesystem().AddDir(std::string(twm::Filesystem::PORTALS) + d);
		return false;
	};
	t["Script.clear_boxes"] = [](Ctx &) {
		for (const char *f : { "portal_1_foldername/prototype_npcsheet_filename", "portal_1_foldername/prototype_facepic_filename",
		                       "portal_1_foldername/key_b_filename", "portal_2_foldername/cedric_npcsheet_filename",
		                       "portal_2_foldername/cedric_facepic_filename", "portal_2_foldername/key_g_filename",
		                       "portal_3_foldername/rue_npcsheet_filename", "portal_3_foldername/rue_facepic_filename",
		                       "portal_3_foldername/key_r_filename", "big_portal_foldername/key_b_filename",
		                       "big_portal_foldername/key_g_filename", "big_portal_foldername/key_r_filename" })
			twm::filesystem().Delete(std::string(twm::Filesystem::PORTALS) + f);
		return false;
	};
	t["Script.put_key_in_box(1)"] = [](Ctx &) {
		using twm::make_file;
		write_files("/portals_foldername/portal_1_foldername/",
		            { make_file("photo", "prototype_npcsheet_filename", twm::PROG_PHOTO, { "blue_npc_prototype", "2" }),
		              make_file("photo", "prototype_facepic_filename", twm::PROG_PHOTO, { "proto1", "2" }),
		              make_file("doc", "key_b_filename", twm::PROG_DOCUMENT, { "keyB" }) });
		return false;
	};
	t["Script.put_key_in_box(2)"] = [](Ctx &) {
		using twm::make_file;
		write_files("/portals_foldername/portal_2_foldername/",
		            { make_file("photo", "cedric_npcsheet_filename", twm::PROG_PHOTO, { "green_npc_cedric", "2" }),
		              make_file("photo", "cedric_facepic_filename", twm::PROG_PHOTO, { "cedric", "2" }),
		              make_file("doc", "key_g_filename", twm::PROG_DOCUMENT, { "keyG" }) });
		return false;
	};
	t["Script.put_key_in_box(3)"] = [](Ctx &) {
		using twm::make_file;
		write_files("/portals_foldername/portal_3_foldername/",
		            { make_file("photo", "rue_npcsheet_filename", twm::PROG_PHOTO, { "red_rue", "2" }),
		              make_file("photo", "rue_facepic_filename", twm::PROG_PHOTO, { "rue", "2" }),
		              make_file("doc", "key_r_filename", twm::PROG_DOCUMENT, { "keyR" }) });
		return false;
	};
	auto passwords = [](const char *scenario) {
		return Fn([scenario](Ctx &) {
			for (int i = 1; i <= 4; i++)
				twm::filesystem().WriteFile(twm::Filesystem::MYDOCUMENTS,
				                            twm::make_file("photo", "oneshot_password_image_" + std::to_string(i) + "_filename",
				                                           twm::PROG_PHOTO, { std::string(scenario) + "/pw" + std::to_string(i) }));
			return false;
		});
	};
	t["Script.password1"] = passwords("scenario1");
	t["Script.password2"] = passwords("scenario2");
	t["Wallpaper.reset_persistent"] = [](Ctx &) { twm::restore_wallpaper(); return false; };
	t["Niko.do_your_thing"] = [](Ctx &c) { twm::deploy_niko(c.map->GetMapID() == 255, c.st->flag(160)); return false; };
	t["set_wallpaper_black"] = [](Ctx &) { twm::set_wallpaper("black"); return false; };
	t["stop_speedrun_timer"] = [](Ctx &) { twm::stop_playthrough_timer(); return false; };
	t["Script.fadein_bgm(90,5)"] = [](Ctx &) { audio::fade_in_bgm(0.9f, 5.0f); return false; };
	t["Script.fadein_bgm(90,3)"] = [](Ctx &) { audio::fade_in_bgm(0.9f, 3.0f); return false; };
	t["particles :fireflies"] = [](Ctx &c) { c.map->SpawnFireflyParticles(); return false; };
	t["particles :fireflies\ngreen_ambient"] = [green](Ctx &c) { green(c); c.map->SpawnFireflyParticles(); return false; };
	t["particles :fireflies\n#green_ambient"] = [](Ctx &c) { c.map->SpawnFireflyParticles(); return false; };
	t["ram_integrity_check"] = flag_if([](Ctx &c) { return c.map->RamPuzzleCheck(); });
	t["green_ambient"] = green;
	t["ambient -100, -100, -100"] = [](Ctx &c) { c.map->SetAmbientTone(GameTone(-100, -100, -100)); return false; };
	t["pixel_puzzle_check"] = flag_if([](Ctx &c) { return c.map->PixelPuzzleCheck(GLEN_PUZZLE_SOLUTION, Vec2(31, 34), Vec2(5, 6)); });
	t["puzzle_check(\"s1\")"] = flag_if([](Ctx &c) { return c.map->PixelPuzzleCheck(TOWER_PUZZLE_SOLUTION_1, Vec2(5, 2), Vec2(11, 11)); });
	t["puzzle_check(\"s3\")"] = flag_if([](Ctx &c) { return c.map->PixelPuzzleCheck(TOWER_PUZZLE_SOLUTION_3, Vec2(5, 2), Vec2(11, 11)); });
	t["puzzle_check(\"s4\")"] = flag_if([](Ctx &c) { return c.map->PixelPuzzleCheck(TOWER_PUZZLE_SOLUTION_4, Vec2(5, 2), Vec2(11, 11)); });
	t["puzzle_check(\"s5\")"] = flag_if([](Ctx &c) { return c.map->PixelPuzzleCheck(TOWER_PUZZLE_SOLUTION_5, Vec2(5, 2), Vec2(11, 12)); });
	t["pixel_puzzle_reset"] = [](Ctx &c) { c.map->PixelPuzzleReset(); return false; };
	t["Script.move_player(64,46)"] = [](Ctx &c) { c.player()->SetPosTile(Vec2(64, 46)); return false; };
	t["Script.move_player(35,15)"] = [](Ctx &c) { c.player()->SetPosTile(Vec2(35, 15)); return false; };
	t["Script.move_player(31,7)"] = [](Ctx &c) { c.player()->SetPosTile(Vec2(31, 7)); return false; };
	t["plight_start_timer"] = [](Ctx &c) {
		int h, m, s, yd;
		sys::local_time(&h, &m, &s, &yd);
		c.st->plight_timer_start = ((yd * 24 + h) * 60 + m);
		return false;
	};
	t["plight_update_timer"] = [](Ctx &c) {
		int h, m, s, yd;
		sys::local_time(&h, &m, &s, &yd);
		int now = (yd * 24 + h) * 60 + m;
		int d = now - c.st->plight_timer_start;
		if (d < 0) d += 366 * 24 * 60;   // year wrap
		c.st->set_var(22, d);
		return false;
	};
	t["pan_offset_y -64\nwrap_map"] = [](Ctx &c) { c.map->EnableWrapping(); return false; };
	t["film_puzzle_begin"] = [](Ctx &) { window().FilmPuzzleBegin(); return false; };
	t["Graphics.fullscreen = false\nfilm_puzzle_begin"] = [](Ctx &) {
		twm::set_game_maximized(false);
		window().FilmPuzzleBegin();
		return false;
	};
	t["film_puzzle_end"] = [](Ctx &) { window().FilmPuzzleEnd(); return false; };
	t["Graphics.fullscreen = true"] = [](Ctx &) { twm::set_game_maximized(true); return false; };
	t["Graphics.fullscreen = false"] = [](Ctx &) { twm::set_game_maximized(false); return false; };
	t["Interpreter.take_a_chill_pill"] = [](Ctx &) { return true; };
	t["Script.start_bruteforce"] = [](Ctx &c) { c.st->bruteforce_start = c.st->play_frames; return false; };
	t["Script.bruteforce_vars"] = [](Ctx &c) {
		if (c.st->bruteforce_start < 0) c.st->bruteforce_start = c.st->play_frames;
		int n = (int)((c.st->play_frames - c.st->bruteforce_start) / 120);
		int d0 = n % 10, d1 = (n % 100 - d0) / 10, d2 = (n % 1000 - d1 * 10 - d0) / 100;
		int d3 = (n % 10000 - d2 * 100 - d1 * 10 - d0) / 1000;
		int d4 = (n % 100000 - d3 * 1000 - d2 * 100 - d1 * 10 - d0) / 10000;
		c.st->set_var(26, d4);
		c.st->set_var(27, d3);
		c.st->set_var(28, d2);
		c.st->set_var(29, d1);
		c.st->set_var(30, d0);
		return false;
	};
	t["Script.lose_all_items"] = [](Ctx &c) {
		for (int i = 0; i < (int)c.st->inventory.size(); i++)
			if (i != 54 && i != 79 && i != 80) c.st->inventory[i] = false;
		return false;
	};
	t["Script.set_cam(0,0)"] = [](Ctx &c) { c.map->SetCamPos(Vec2(0, 0)); return false; };
	t["Script.set_cam(64,0)"] = [](Ctx &c) { c.map->SetCamPos(Vec2(8, 0)); return false; };
	t["Script.set_cam(48,0)"] = [](Ctx &c) { c.map->SetCamPos(Vec2(6, 0)); return false; };
	t["bg 'black'"] = [](Ctx &c) { c.map->SetBackground("black"); return false; };
	t["bg 'summit'"] = [](Ctx &c) { c.map->SetBackground("summit"); return false; };
	t["pixel_puzzle_niko_correct(\"s5\")"] = [](Ctx &c) {
		Vec2 p = c.ptile();
		p.x -= 5;
		p.y -= 2;
		if (p.x < 0) p.x = 0;
		if (p.x > 10) p.x = 10;
		if (p.y < 0) p.y = 0;
		if (p.y > 11) p.y = 11;
		int l = p.x, r = p.x, u = p.y, d = p.y;
		const uint8_t *S = TOWER_PUZZLE_SOLUTION_5;
		for (int i = 0; i < 20; i++) {
			if (S[p.y * 11 + l]) { c.st->set_var(7, l + 5); c.st->set_var(8, p.y + 2); break; }
			if (S[p.y * 11 + r]) { c.st->set_var(7, r + 5); c.st->set_var(8, p.y + 2); break; }
			if (S[u * 11 + p.x]) { c.st->set_var(7, p.x + 5); c.st->set_var(8, u + 2); break; }
			if (S[d * 11 + p.x]) { c.st->set_var(7, p.x + 5); c.st->set_var(8, d + 2); break; }
			if (--l < 0) l = 0;
			if (++r > 10) r = 10;
			if (--u < 0) u = 0;
			if (++d > 11) d = 11;
		}
		return false;
	};
	t["lightbulb_room_fix"] = [](Ctx &c) {
		Vec2 p = c.ptile();
		if (p.x < 5 || p.x > 15 || p.y < 2 || p.y > 13) c.player()->SetPosTile(Vec2(10, 13));
		return false;
	};
	t["Oneshot.shake"] = [](Ctx &) { window().Shake(60); return false; };
	t["$scene = Scene_Load.new"] = none;   // debug menus
	t["open_debug_flag_menu"] = none;
	t["open_var_flag_menu"] = none;
	t["plight_bruteforce_skip"] = [](Ctx &c) { c.st->bruteforce_start = c.st->play_frames - 7561680; return false; };
	// Explicit no-ops in the C#.
	for (const char *s : { "Screen.start", "Screen.finish", "Screen.set 'test1'", "Screen.set 'test2'", "Wallpaper.set 'test'",
	                       "Wallpaper.reset", "clamp_panorama", "loadQASave(\"Save12_redsky_done\")",
	                       "loadQASave \\\n(\"Save2_RightBeforeEnteringMines\")", "loadQASave(\"Save2_tutorial_done\")",
	                       "loadQASave(\"Save9_ReachedGlen\")", "loadQASave(\"Save4_barrens_done\")",
	                       "loadQASave(\"Save14_ReachedRefugeSky\")", "loadQASave(\"Save9_glen_done\")",
	                       "loadQASave(\"Save16_ReachedRedGround\")", "add_light :window, \n'start_window', 2.0, 680, 370",
	                       "add_light :bulb,\n'bulb_ground', 0.70, 304, 688", "del_light :eyes",
	                       "fake_save" })
		t[s] = none;
	return t;
}

bool HandleScript(TileMap *map, const std::string &script, EventRunner *caller, int pageId) {
	Ctx c{ map, map->State(), caller };
	if (starts(script, "check_exit")) {
		if (c.st->flag(11)) { c.st->set_flag(22, false); return false; }
		std::string s = script.size() > 11 ? script.substr(11) : "";
		std::vector<std::string> a;
		size_t p = 0;
		while (p <= s.size()) {
			size_t q = s.find(' ', p);
			a.push_back(s.substr(p, q == std::string::npos ? std::string::npos : q - p));
			if (q == std::string::npos) break;
			p = q + 1;
		}
		if (a.size() != 4) { sys::log("script: bad check_exit: %s\n", script.c_str()); return false; }
		a[0].pop_back();   // trailing commas
		a[1].pop_back();
		int x0, x1, y0, y1;
		if (a[2] == "x:") { x0 = x1 = atoi(a[3].c_str()); y0 = atoi(a[0].c_str()); y1 = atoi(a[1].c_str()); }
		else { x0 = atoi(a[0].c_str()); x1 = atoi(a[1].c_str()); y0 = y1 = atoi(a[3].c_str()); }
		Vec2 t = c.ptile();
		c.flag22(t.x >= x0 && t.x <= x1 && t.y >= y0 && t.y <= y1);
		return false;
	}
	if (starts(script, "EdText")) { HandleEdText(c, script, pageId); return true; }
	if (starts(script, "UnlockProfile") || starts(script, "UnlockWallpaper") || starts(script, "UnlockTheme") ||
	    starts(script, "Steam.unlock")) {
		std::string id = quoted_arg(script);
		if (id.empty()) sys::log("script: failed to match unlock: %s\n", script.c_str());
		else if (starts(script, "UnlockProfile")) twm::unlock_profile(id);
		else if (starts(script, "UnlockWallpaper")) twm::unlock_wallpaper(id);
		else if (starts(script, "UnlockTheme")) twm::unlock_theme(id);
		else twm::unlock_achievement(id);
		return false;
	}
	if (starts(script, "unlock_map ")) { UnlockMap(c, script); return false; }
	if (starts(script, "Journal.set ")) {
		std::string s = script.substr(12);
		twm::set_journal_page(s.size() >= 2 ? s.substr(1, s.size() - 2) : s);
		return false;
	}
	if (starts(script, "Script.fix_footsplashes(")) {
		int x = 0, y = 0;
		sscanf(script.c_str() + strlen("Script.fix_footsplashes("), "%d,%d", &x, &y);
		c.map->MoveFootsplashesRelative(x, y);
		return false;
	}
	if (starts(script, "Script.move_player_relative(")) {
		std::string s = script.substr(strlen("Script.move_player_relative("));
		int x = atoi(s.c_str());
		size_t comma = s.find(',');
		int y = comma == std::string::npos ? 0 : atoi(s.c_str() + comma + 1);
		map->MovePlayerRelative(x, y);
		return false;
	}
	if (starts(script, "change_map ")) {
		int id = atoi(script.c_str() + 11);
		map->ChangeMap(id, map->GetPlayerXTile(), map->GetPlayerYTile(), 0.0f, Direction::None);
		return false;
	}
	auto &t = script_table();
	auto it = t.find(script);
	if (it != t.end()) return it->second(c);
	note("unrecognized script command", script);
	return false;
}

// The namecheck strings are space-separated lists of names.
static bool name_in(const std::string &lname, const char *key) {
	const std::string &list = loc::twm(key);
	size_t a = 0;
	while (a <= list.size()) {
		size_t b = list.find(' ', a);
		std::string w = list.substr(a, b == std::string::npos ? std::string::npos : b - a);
		if (!w.empty() && util::lower(w) == lname) return true;
		if (b == std::string::npos) break;
		a = b + 1;
	}
	return false;
}

CondResult HandleScriptConditional(TileMap *map, const std::string &script, EventRunner *caller, int pageId) {
	Ctx c{ map, map->State(), caller };
	bool r = false;
	if (starts(script, "unlock_map ")) { UnlockMap(c, script); return CondResult::No; }
	if (starts(script, "Script.px ") || starts(script, "Script.py ")) {
		char var[16], op[4];
		int val = 0;
		if (sscanf(script.c_str(), "%15s %3s %d", var, op, &val) == 3) {
			int v = strcmp(var, "Script.px") == 0 ? c.ptile().x : c.ptile().y;
			std::string o = op;
			r = o == "==" ? v == val : o == ">" ? v > val : o == "<" ? v < val : o == ">=" ? v >= val
			  : o == "<=" ? v <= val : o == "!=" ? v != val : false;
		}
		return r ? CondResult::Yes : CondResult::No;
	}
	if (starts(script, "EdText")) { HandleEdText(c, script, pageId); return CondResult::EdText; }
	const std::string &s = script;
	const std::string name = c.st->player_name;
	std::string lname = name;
	for (auto &ch : lname) ch = (char)tolower((unsigned char)ch);
	if (s == "$debug") r = false;
	else if (s == "save_exists") r = save::Exists();
	else if (s == "$persistent.langcode != 'zh_CN'") r = true;
	else if (s == "$persistent.langcode == 'ja'") r = false;
	else if (s == "$game_player.y == 17") r = c.ptile().y == 17;
	else if (s == "Game_Oneshot.get_user_name == $game_oneshot.player_name") r = name == sys::user_name();
	else if (s == "Script.is_name_swear") r = name_in(lname, "namecheck_is_name_swear");
	else if (s == "Script.is_name_like_mom_dad") r = name_in(lname, "namecheck_is_name_like_mom_or_dad");
	else if (s == "Script.is_name_gross") r = name_in(lname, "namecheck_is_name_gross");
	else if (s == "Script.is_name_niko") r = lname == "niko";
	else if (s == "Script.is_name_like_niko") r = name_in(lname, "namecheck_is_name_like_niko");
	else if (s == "$console") r = twm::game_maximized();
	else if (s == "Wallpaper.set_persistent('desktop', 0x1a041f)") {
		r = true;
		twm::override_wallpaper("desktop", 0 /* CENTERED */, 26, 4, 31);
	} else if (s == "Wallpaper.set_persistent('save_w32', 0x0)") {
		r = true;
		twm::override_wallpaper("save_w32", 0 /* CENTERED */, 0, 0, 0);
	}
	else if (s == "activate_balcony? 17")
		r = c.player()->PlayerHasControl() && c.ptile().y == 17 && c.player()->GetDirection() == Direction::Up &&
		    input::pressed(input::BTN_OK);
	else if (s == "Oneshot.obscured_cleared?") r = window().FilmPuzzleCleared();
	else if (s == "button_pressed?") r = input::pressed(input::BTN_OK) || input::pressed(input::BTN_CANCEL);
	else if (s == "Script.check_bruteforce") {
		if (c.st->bruteforce_start < 0) c.st->bruteforce_start = c.st->play_frames;
		r = c.st->play_frames - c.st->bruteforce_start > 7561680;
	} else if (s == "Journal.active?") r = twm::journal_open();
	else if (s == "fake_save_exists") r = true;
	else if (s == "$game_temp.countdown_password.downcase == \"solstice\"" ||
	         s == "$game_temp.countdown_password.downcase == \"hello penguin\"") {
		std::string want = s.find("solstice") != std::string::npos ? "solstice" : "hello penguin";
		std::string pw = caller ? caller->PasswordInput : "";
		for (auto &ch : pw) ch = (char)tolower((unsigned char)ch);
		r = !pw.empty() && pw == want;
	} else if (s == "false") r = false;
	else if (s == "Script.countdown_update == true") {
		int h, m, sec, yd;
		sys::local_time(&h, &m, &sec, &yd);
		int v[9] = { sec % 10, sec / 10, m % 10, m / 10, h % 10, h / 10, yd % 10, yd / 10 % 10, yd / 100 };
		for (int i = 0; i < 9; i++)
			if (c.st->var(101 + i) != v[i]) { c.st->set_var(101 + i, v[i]); r = true; }
	} else if (s == "Script.countdown_over == true") r = true;
	else if (starts(s, "Script.is_key_in_bigbox(") || starts(s, "Script.is_key_in_box(")) {
		bool big = starts(s, "Script.is_key_in_bigbox(");
		char n = s[s.find('(') + 1];
		static const char *keys[3] = { "key_b_filename", "key_g_filename", "key_r_filename" };
		int i = n - '1';
		if (i >= 0 && i < 3) {
			// 1 blue, 2 green, 3 red, in the big box as in the small ones.
			std::string dir = big ? "/portals_foldername/big_portal_foldername/"
			                      : std::string("/portals_foldername/portal_") + n + "_foldername/";
			r = twm::filesystem().FileExists(dir + keys[i]);
		}
	} else note("unimplemented conditional", s);
	return r ? CondResult::Yes : CondResult::No;
}

} // namespace script
