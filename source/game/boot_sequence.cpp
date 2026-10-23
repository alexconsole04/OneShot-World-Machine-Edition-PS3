#include "boot_sequence.h"
#include "loc.h"
#include "../engine/audio.h"
#include "../engine/font.h"
#include "../engine/gm.h"
#include "../platform/input.h"
#include "../platform/sys.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <deque>
#include <vector>

namespace boot {

enum Step {
	BEGIN, BREAK_BEFORE_FELIX, FELIX_FADEIN, FELIX_DISPLAY, FELIX_FADEOUT,
	FUTURECAT_LOGO_FADEIN, FUTURECAT_LOGO_DISPLAY, FUTURECAT_LOGO_FADEOUT,
	KOMODO_LOGO_FADEIN, KOMODO_LOGO_DISPLAY, KOMODO_LOGO_FADEOUT,
	LANGUAGE_SELECT_FADEIN, LANGUAGE_SELECT, LANGUAGE_SELECT_FADEOUT,
	AUTOSAVE_EXPLAIN_FADEIN, AUTOSAVE_EXPLAIN, AUTOSAVE_EXPLAIN_FADEOUT,
	BREAK_BEFORE_CONSOLE, CONSOLE_SHOW_BIOS_LOGO, CONSOLE_TEXT, CONSOLE_LOAD_SOUNDS, CONSOLE_TEXT2,
	CONSOLE_LOAD_TEXTURES, CONSOLE_TEXT3, BREAK_BEFORE_BIG_LOGO, BIG_LOGO_BEFORE_SOUND, BIG_LOGO_AFTER_SOUND,
	TWM_LOGO_FADEOUT, COMPLETE
};

static Step step = COMPLETE;
static bool finishedFlag;
static float opacity;
static int stateTimer, textDelay = 12;
static std::vector<std::string> postText;
static std::string srcText;
static std::deque<std::string> unloadedSystemTextures;
static std::vector<std::string> autosaveLines;
static int selectedLanguage;
static std::string version;
static std::string pendingLanguage;
static uint64_t stepStart;

// SpinningBulbIcon
static int bulbFrameTimer, bulbFrame, bulbLightCycle;
static uint8_t bulbLight = 128;

static const unsigned BIOS_COLOR = 0xa15bff;   // (161, 91, 255)

static void set_step(Step s) {
	step = s;
	if (getenv("OSPS3_TRACE_BOOT")) sys::log("boot: step %d\n", (int)s);
}

static void setup_system_texture_queue() {
	unloadedSystemTextures.clear();
	uint8_t *d;
	size_t n;
	if (!sys::read_file("gamedata/twm/system_tex_list.txt", &d, &n)) {
		sys::log("boot: gamedata/twm/system_tex_list.txt missing\n");
		return;
	}
	std::string all((const char *)d, n);
	free(d);
	size_t p = 0;
	while (p <= all.size()) {
		size_t e = all.find('\n', p);
		std::string line = all.substr(p, e == std::string::npos ? std::string::npos : e - p);
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
		if (!line.empty()) unloadedSystemTextures.push_back(line);
		if (e == std::string::npos) break;
		p = e + 1;
	}
}

static void populate_autosave_lines() {
	Vec2 sz = gm::boot_space();
	autosaveLines = font::word_wrap(font::GameSmall,
	                                loc::twm("boot_autosave_explain1") + "\n" + loc::twm("boot_autosave_explain2"), sz.x / 2 - 20);
}

void set_version(const char *v) { version = v ? v : ""; }

void start() {
	setup_system_texture_queue();
	step = BEGIN;
	stateTimer = 0;
	opacity = 0;
	finishedFlag = false;
	stepStart = sys::time_us();
	sys::log("boot: start\n");
}

void restart_console() {
	// RestartBootSequence (after a language change).
	setup_system_texture_queue();
	step = BREAK_BEFORE_CONSOLE;
	stateTimer = 0;
	opacity = 0;
	postText.clear();
	srcText.clear();
	textDelay = 12;
	finishedFlag = false;
	stepStart = sys::time_us();
	sys::log("boot: restarting at the console\n");
}

bool active() { return step != COMPLETE; }

bool just_finished() {
	bool f = finishedFlag;
	finishedFlag = false;
	return f;
}

void request_language(const std::string &code) { pendingLanguage = code; }
bool take_language_request(std::string &code) {
	if (pendingLanguage.empty()) return false;
	code = pendingLanguage;
	pendingLanguage.clear();
	return true;
}

static bool skip_pressed() {
	return input::pressed(input::BTN_OK) || input::pressed(input::BTN_CANCEL) || input::pressed(input::BTN_R2);
}

static void set_src_text(const char *name) {
	// SetSrcText: the language's BIOS text, or the English one.
	std::string rel = std::string("twm/") + name + ".txt";
	std::string path = loc::localized_path(rel.c_str());
	uint8_t *d;
	size_t n;
	srcText.clear();
	if (sys::read_file(path.c_str(), &d, &n)) {
		srcText.assign((const char *)d, n);
		free(d);
	}
	// Windows line ends: the '\r' would draw as the font's '?'.
	std::string t;
	t.reserve(srcText.size());
	for (char c : srcText)
		if (c != '\r') t += c;
	srcText = t;
}

static void add_to_console(const std::string &s) {
	if (postText.empty()) postText.push_back(std::string());
	postText.back() += s;
}

static void process_text() {
	while (!srcText.empty() && textDelay <= 0) {
		char c = srcText[0];
		if (c == '@') {
			size_t sp = srcText.find(' ');
			if (sp != std::string::npos && sp > 0) {
				std::string cmd = srcText.substr(1, sp - 1);
				srcText = srcText.substr(sp + 1);
				if (cmd == "WAIT") {
					size_t sp2 = srcText.find(' ');
					float secs = (float)atof(srcText.substr(0, sp2).c_str());
					textDelay = (int)(secs * 60.0f);
					srcText = sp2 == std::string::npos ? std::string() : srcText.substr(sp2 + 1);
				} else if (cmd == "SOUNDS") {
					add_to_console(std::to_string(audio::sound_count()));
				} else if (cmd == "SCANSOUNDS") {
					audio::scan_sounds();
				} else {
					add_to_console("@" + cmd);
				}
			} else {
				add_to_console("@");
				srcText = srcText.substr(1);
			}
		} else if (c == '\n') {
			postText.push_back(std::string());
			srcText = srcText.substr(1);
			textDelay = 12;
		} else {
			// One whole UTF-8 character.
			int len = font::utf8_len((unsigned char)c);
			add_to_console(srcText.substr(0, len));
			srcText = srcText.substr(len);
		}
	}
}

static void skip_console() {
	stateTimer = 0;
	opacity = 0;
	audio::fade_out_bgm(1.0f);
	set_step(BREAK_BEFORE_BIG_LOGO);
	audio::load_all_sounds();
	while (!unloadedSystemTextures.empty()) {
		gm::texture(unloadedSystemTextures.front().c_str(), gm::TheWorldMachine);
		unloadedSystemTextures.pop_front();
	}
}

// Fade helpers for the many fade-in / display / fade-out triples.
static bool fade_in(Step next) {
	opacity = (float)stateTimer / 30.0f;
	if (stateTimer >= 30) { opacity = 1; stateTimer = 0; set_step(next); return true; }
	return false;
}
static bool fade_out(Step next) {
	opacity = 1.0f - (float)stateTimer / 30.0f;
	if (stateTimer >= 30) { opacity = 0; stateTimer = 0; set_step(next); return true; }
	return false;
}
static void display(int frames, Step next) {
	if (stateTimer >= frames || skip_pressed()) { stateTimer = 0; set_step(next); }
}

static void bulb_update() {
	if (++bulbFrameTimer >= 3) {
		bulbFrameTimer = 0;
		if (++bulbFrame >= 24) bulbFrame = 0;
	}
	if (++bulbLightCycle >= 60) bulbLightCycle = 0;
	double k = (sin(bulbLightCycle / 60.0 * 3.14159265358979 * 2.0) + 1.0) / 2.0;
	bulbLight = (uint8_t)(128.0 + 127.0 * k);
}

void update() {
	if (step == COMPLETE) return;
	stateTimer++;
	bulb_update();
	switch (step) {
	case BEGIN:
		set_step(BREAK_BEFORE_FELIX);
		postText.clear();
		stateTimer = 0;
		populate_autosave_lines();
		break;
	case BREAK_BEFORE_FELIX:
		if (stateTimer >= 30 || skip_pressed()) { stateTimer = 0; set_step(FELIX_FADEIN); }
		break;
	case FELIX_FADEIN: fade_in(FELIX_DISPLAY); break;
	case FELIX_DISPLAY: display(120, FELIX_FADEOUT); break;
	case FELIX_FADEOUT: fade_out(FUTURECAT_LOGO_FADEIN); break;
	case FUTURECAT_LOGO_FADEIN: fade_in(FUTURECAT_LOGO_DISPLAY); break;
	case FUTURECAT_LOGO_DISPLAY: display(120, FUTURECAT_LOGO_FADEOUT); break;
	case FUTURECAT_LOGO_FADEOUT: fade_out(KOMODO_LOGO_FADEIN); break;
	case KOMODO_LOGO_FADEIN: fade_in(KOMODO_LOGO_DISPLAY); break;
	case KOMODO_LOGO_DISPLAY: display(120, KOMODO_LOGO_FADEOUT); break;
	case KOMODO_LOGO_FADEOUT:
		// (The PC build shows the FMOD logo next; this port doesn't use FMOD.)
		if (fade_out(AUTOSAVE_EXPLAIN_FADEIN) && !loc::was_set_on_startup()) {
			const auto &langs = loc::languages();
			selectedLanguage = 0;
			for (size_t i = 0; i < langs.size(); i++)
				if (langs[i].code == loc::code()) selectedLanguage = (int)i;
			set_step(LANGUAGE_SELECT_FADEIN);
		}
		break;
	case LANGUAGE_SELECT_FADEIN: fade_in(LANGUAGE_SELECT); break;
	case LANGUAGE_SELECT: {
		int n = (int)loc::languages().size();
		if (input::pressed(input::BTN_LEFT)) {
			audio::play_sound("menu_cursor", 0.8f);
			if (--selectedLanguage < 0) selectedLanguage = n - 1;
		} else if (input::pressed(input::BTN_RIGHT)) {
			audio::play_sound("menu_cursor", 0.8f);
			if (++selectedLanguage >= n) selectedLanguage = 0;
		} else if (input::pressed(input::BTN_OK)) {
			stateTimer = 0;
			audio::play_sound("menu_decision", 0.8f);
			set_step(LANGUAGE_SELECT_FADEOUT);
		}
		break;
	}
	case LANGUAGE_SELECT_FADEOUT:
		if (fade_out(AUTOSAVE_EXPLAIN_FADEIN)) {
			loc::set(loc::languages()[selectedLanguage].code);   // saves language.dat
			populate_autosave_lines();
		}
		break;
	case AUTOSAVE_EXPLAIN_FADEIN: fade_in(AUTOSAVE_EXPLAIN); break;
	case AUTOSAVE_EXPLAIN: display(300, AUTOSAVE_EXPLAIN_FADEOUT); break;
	case AUTOSAVE_EXPLAIN_FADEOUT: fade_out(BREAK_BEFORE_CONSOLE); break;
	case BREAK_BEFORE_CONSOLE:
		if (stateTimer >= 60 || skip_pressed()) {
			stateTimer = 0;
			set_step(CONSOLE_SHOW_BIOS_LOGO);
			audio::play_song("computer_startup", 0.0f, 1.0f, 1.0f);
		}
		break;
	case CONSOLE_SHOW_BIOS_LOGO:
		opacity = 1;
		if (skip_pressed()) skip_console();
		else if (stateTimer >= 60) {
			stateTimer = 0;
			set_step(CONSOLE_TEXT);
			set_src_text("bios");
		}
		break;
	case CONSOLE_TEXT:
	case CONSOLE_TEXT2:
	case CONSOLE_TEXT3:
		if (skip_pressed()) { skip_console(); break; }
		if (stateTimer < textDelay) break;
		stateTimer = 0;
		textDelay = 0;
		if (!srcText.empty()) { process_text(); break; }
		if (step == CONSOLE_TEXT) set_step(CONSOLE_LOAD_SOUNDS);
		else if (step == CONSOLE_TEXT2) set_step(CONSOLE_LOAD_TEXTURES);
		else {
			opacity = 0;
			audio::fade_out_bgm(1.0f);
			set_step(BREAK_BEFORE_BIG_LOGO);
		}
		break;
	case CONSOLE_LOAD_SOUNDS:
		if (skip_pressed()) { skip_console(); break; }
		if (postText.empty()) postText.push_back(std::string());
		if (audio::has_more_sounds()) {
			postText.back() = audio::next_sound_name();
			audio::load_next_sound();   // waits a frame if the file isn't read yet
			break;
		}
		postText.back().clear();
		set_src_text("bios2");
		stateTimer = 0;
		set_step(CONSOLE_TEXT2);
		break;
	case CONSOLE_LOAD_TEXTURES:
		if (skip_pressed()) { skip_console(); break; }
		if (stateTimer <= 1) break;
		stateTimer = 0;
		if (postText.empty()) postText.push_back(std::string());
		if (!unloadedSystemTextures.empty()) {
			std::string t = unloadedSystemTextures.front();
			unloadedSystemTextures.pop_front();
			gm::texture(t.c_str(), gm::TheWorldMachine);
			if (t.compare(0, 18, "the_world_machine/") == 0) t = t.substr(18) + ".xnb";
			postText.back() = t;
		} else {
			postText.back().clear();
			set_src_text("bios3");
			set_step(CONSOLE_TEXT3);
		}
		break;
	case BREAK_BEFORE_BIG_LOGO:
		if (stateTimer >= 60 || skip_pressed()) {
			stateTimer = 0;
			opacity = 0;
			set_step(BIG_LOGO_BEFORE_SOUND);
			audio::play_song("", 0.0f, 1.0f, 1.0f);
		}
		break;
	case BIG_LOGO_BEFORE_SOUND:
		opacity = (float)(stateTimer / 3) / 20.0f;
		if (stateTimer == 30) audio::play_sound("twm_startup");
		if (stateTimer >= 60) { stateTimer = 0; opacity = 1; set_step(BIG_LOGO_AFTER_SOUND); }
		break;
	case BIG_LOGO_AFTER_SOUND:
		display(120, TWM_LOGO_FADEOUT);
		break;
	case TWM_LOGO_FADEOUT:
		opacity = 1.0f - (float)(stateTimer / 3) / 40.0f;
		if (stateTimer >= 120) {
			stateTimer = 0;
			opacity = 0;
			set_step(COMPLETE);
			gm::clear_cache(gm::BootScreen);
			finishedFlag = true;
			sys::log("boot: complete (%.1f s)\n", (sys::time_us() - stepStart) / 1e6);
		}
		break;
	case COMPLETE:
		break;
	}
}

// ------------------------------------------------------------------ drawing

static const gfx::Texture *boot_tex(const char *name, bool smooth = false) {
	const gfx::Texture *t = gm::texture(name, gm::BootScreen);
	// BlendMode.LinearStretch: these pictures are scaled smoothly.
	if (t && smooth) const_cast<gfx::Texture *>(t)->linear = true;
	return t;
}

static void black(Rect box, float a = 1.0f, gfx::Blend blend = gfx::Blend::Normal) {
	gm::color_box(box, 0, 0, 0, (uint8_t)(a * 255.0f), blend, 1);
}

// The Felix / KOMODO / big logo pictures: as large as fits, centred.
static void draw_fitted(const gfx::Texture *t, Rect box, float alpha, gfx::Blend blend = gfx::Blend::Normal) {
	if (!t) return;
	double screenAspect = (double)box.w / box.h;
	float scale;
	Vec2 pos;
	if ((double)t->w / t->h <= screenAspect) {
		int w = (int)t->w * box.h / (int)t->h;
		scale = (float)w / t->w;
		pos = Vec2((box.w - w) / 2, 0);
	} else {
		int h = (int)t->h * box.w / (int)t->w;
		scale = (float)h / t->h;
		pos = Vec2(0, (box.h - h) / 2);
	}
	gm::blit_scaled(t, pos, Rect(0, 0, (int)t->w, (int)t->h), scale, scale, alpha, 0, blend);
}

static void draw_bulb(Vec2 pos, float alpha, int scale) {
	const gfx::Texture *t = gm::texture("the_world_machine/bulb_anim_24", gm::TheWorldMachine);
	const gfx::Texture *lm = gm::texture("the_world_machine/bulb_anim_24_lightmap", gm::TheWorldMachine);
	Rect r(48 * (bulbFrame % 5), 48 * (bulbFrame / 5), 48, 48);
	gm::blit(t, pos + Vec2(1, 1), r, alpha, 0, gfx::Blend::Normal, scale, GameTone(), 0, 0, 0);
	gm::blit(t, pos, r, alpha, 0, gfx::Blend::Normal, scale);
	gm::blit(lm, pos, r, bulbLight / 255.0f * alpha, 0, gfx::Blend::Additive, scale);
}

void draw() {
	if (step == COMPLETE) return;
	Vec2 sz = gm::boot_space();
	gm::begin_space(sz);
	Rect box(0, 0, sz.x, sz.y);
	switch (step) {
	case AUTOSAVE_EXPLAIN_FADEIN:
	case AUTOSAVE_EXPLAIN:
	case AUTOSAVE_EXPLAIN_FADEOUT: {
		black(box);
		draw_bulb(Vec2(box.w / 8 - 24, box.h / 8 - 24), opacity, 4);
		Vec2 p(box.w / 4, box.h / 4 + 48);
		for (auto &l : autosaveLines) {
			font::draw_centered(font::GameSmall, p, l, 0xffffff, opacity, 2);
			p.y += 18;
		}
		break;
	}
	case LANGUAGE_SELECT_FADEIN:
	case LANGUAGE_SELECT:
	case LANGUAGE_SELECT_FADEOUT: {
		black(box);
		Vec2 p(box.w / 4, box.h / 4 - 16);
		font::draw_centered(font::Game, p, loc::twm("boot_lang_select"), 0xffffff, opacity, 2);
		p.y += 20;
		const auto &langs = loc::languages();
		std::string tn = "the_world_machine/languages/" + langs[selectedLanguage].code;
		const gfx::Texture *lt = gm::texture(tn.c_str(), gm::BootScreen);
		Vec2 ts = lt ? Vec2((int)lt->w, (int)lt->h) : Vec2(0, 0);
		Vec2 lp(p.x - ts.x / 2, p.y);
		if (lt) gm::blit(lt, lp, opacity);
		else font::draw_centered(font::Game, Vec2(p.x, p.y + 2), langs[selectedLanguage].displayName, 0xffffff, opacity, 2);
		gm::blit(gm::texture("the_world_machine/window_icons/arrow_left", gm::BootScreen), lp + Vec2(-16, 8), opacity);
		gm::blit(gm::texture("the_world_machine/window_icons/arrow_right", gm::BootScreen), lp + Vec2(ts.x, 8), opacity);
		p.y += 32;
		font::draw_centered(font::GameSmall, p, loc::twm("boot_lang_select_hint"), 0xffffff, opacity, 2);
		break;
	}
	case FUTURECAT_LOGO_FADEIN:
	case FUTURECAT_LOGO_DISPLAY:
	case FUTURECAT_LOGO_FADEOUT: {
		black(box);
		const gfx::Texture *t = boot_tex("partner_logos/futurecat_white");
		if (t) gm::blit(t, Vec2((box.w - (int)t->w) / 2, (box.h - (int)t->h) / 2), opacity, 0, gfx::Blend::Normal, 1);
		break;
	}
	case FELIX_FADEIN:
	case FELIX_DISPLAY:
	case FELIX_FADEOUT:
		black(box);
		draw_fitted(boot_tex("the_world_machine/felix", true), box, opacity);
		break;
	case KOMODO_LOGO_FADEIN:
	case KOMODO_LOGO_DISPLAY:
	case KOMODO_LOGO_FADEOUT:
		black(box);
		draw_fitted(boot_tex("partner_logos/komodo_white", true), box, opacity);
		break;
	case CONSOLE_SHOW_BIOS_LOGO:
	case CONSOLE_TEXT:
	case CONSOLE_LOAD_SOUNDS:
	case CONSOLE_TEXT2:
	case CONSOLE_LOAD_TEXTURES:
	case CONSOLE_TEXT3: {
		black(box);
		gm::blit(boot_tex("the_world_machine/logo"), Vec2(0, 0), opacity, 0, gfx::Blend::Normal, 2);
		int vw = font::width(font::Game, version);
		font::draw(font::Game, Vec2(box.w / 2 - vw - 10, 4), version, BIOS_COLOR, opacity, 2);
		int y = 100;
		int lines = (box.h / 2 - y - 4) / 22;
		for (size_t i = postText.size() > (size_t)lines ? postText.size() - lines : 0; i < postText.size(); i++) {
			font::draw(font::Game, Vec2(10, y), postText[i], BIOS_COLOR, opacity, 2);
			y += 22;
		}
		break;
	}
	case BIG_LOGO_BEFORE_SOUND:
	case BIG_LOGO_AFTER_SOUND:
	case TWM_LOGO_FADEOUT:
		// DitherShader: the logo (and the black behind it) wipes in from the
		// top through a dithered band, and back out again.
		gm::set_dither(opacity);
		black(box, opacity, gfx::Blend::Dither);
		draw_fitted(boot_tex("the_world_machine/logo_full", true), box, opacity, gfx::Blend::Dither);
		break;
	default:
		break;
	}
	gm::end_space();
}

} // namespace boot
