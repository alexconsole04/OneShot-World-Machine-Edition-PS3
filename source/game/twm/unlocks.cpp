#include "unlocks.h"
#include "wm_internal.h"
#include "../loc.h"
#include "../json_file.h"
#include "../../engine/gm.h"
#include "../../platform/sys.h"
#include <algorithm>

namespace twm {

std::string format1(const std::string &fmt, const std::string &arg) {
	std::string s = fmt;
	size_t p = s.find("{0}");
	if (p != std::string::npos) s.replace(p, 3, arg);
	return s;
}

static bool has(const std::vector<std::string> &v, const std::string &s) { return std::find(v.begin(), v.end(), s) != v.end(); }

// ===================================================================== ToastManager

void ToastManager::Update() {
	for (auto &t : shown) t.timer--;
	while (!shown.empty() && shown.front().timer <= 0) {
		shown.pop_front();
		cardOffset -= 44;
		if (shown.empty()) gm::clear_cache(gm::Toasts);
	}
	if (releaseTimer > 0) releaseTimer--;
	else if (!pending.empty()) {
		shown.push_back(pending.front());
		pending.pop_front();
		releaseTimer = 10;
	}
	int target = (int)shown.size() * 44;
	if (cardOffset < target) cardOffset += std::max((target - cardOffset) / 10, 1);
	else cardOffset = target;
}

void ToastManager::Draw(const Theme &th, Vec2 screenSize) {
	Rect r(screenSize.x - 180 - 4, 0, 180, 44);
	int y = cardOffset;
	for (auto &t : shown) {
		y -= 44;
		r.y = y;
		int a = 255;
		if (t.timer < 20) a = (int)(255.0f * (t.timer / 20.0f));
		box(r, th.Primary(a));
		box(shrink(r, 2), th.Background(a));
		Vec2 v(r.x + 4, (44 - (int)t.text.size() * 12) / 2 + r.y - 4);
		if (!t.icon.empty()) {
			Vec2 sz = tex_size(t.icon.c_str(), gm::Toasts);
			tex(t.icon.c_str(), Vec2(v.x, (44 - sz.y) / 2 + r.y), Rect(), Color::White().withA(a), 2, gm::Toasts);
			v.x += sz.x + 4;
		} else {
			v.x += 4;
		}
		for (auto &l : t.text) {
			line(v + Vec2(-2, 4), l, th.Primary(a));
			v.y += 12;
		}
	}
}

void ToastManager::AddToast(const std::string &icon, const std::string &text) {
	Toast t;
	if (!icon.empty()) t.icon = "the_world_machine/" + icon;
	size_t p = 0;
	while (true) {
		size_t q = text.find('\n', p);
		t.text.push_back(text.substr(p, q == std::string::npos ? std::string::npos : q - p));
		if (q == std::string::npos) break;
		p = q + 1;
	}
	pending.push_back(t);
}

// ===================================================================== metadata

const std::vector<UnlockManager::Profile> &UnlockManager::ProfileInfo() {
	if (!profilesLoaded) {
		profilesLoaded = true;
		if (cJSON *j = parse_json_file("gamedata/twm/contacts_metadata.json")) {
			const cJSON *it;
			cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(j, "profiles")) {
				const cJSON *id = cJSON_GetObjectItemCaseSensitive(it, "unlockId"), *t = cJSON_GetObjectItemCaseSensitive(it, "title");
				if (!cJSON_IsString(id)) continue;
				bool dup = false;
				for (auto &p : profileInfo) dup |= p.id == id->valuestring;
				if (!dup) profileInfo.push_back({ id->valuestring, cJSON_IsString(t) ? t->valuestring : id->valuestring });
			}
			cJSON_Delete(j);
		} else {
			sys::log("unlocks: gamedata/twm/contacts_metadata.json missing\n");
		}
	}
	return profileInfo;
}

const std::vector<UnlockManager::Achievement> &UnlockManager::AchievementInfo() {
	if (!achievementsLoaded) {
		achievementsLoaded = true;
		if (cJSON *j = parse_json_file("gamedata/twm/achievements_metadata.json")) {
			const cJSON *it;
			cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(j, "achievements")) {
				const cJSON *id = cJSON_GetObjectItemCaseSensitive(it, "id"), *t = cJSON_GetObjectItemCaseSensitive(it, "title");
				if (!cJSON_IsString(id)) continue;
				bool dup = false;
				for (auto &a : achievementInfo) dup |= a.id == id->valuestring;
				if (!dup) achievementInfo.push_back({ id->valuestring, cJSON_IsString(t) ? t->valuestring : id->valuestring });
			}
			cJSON_Delete(j);
		} else {
			sys::log("unlocks: gamedata/twm/achievements_metadata.json missing\n");
		}
	}
	return achievementInfo;
}

bool UnlockManager::IsTrack(const std::string &id) {
	if (!tracksLoaded) {
		tracksLoaded = true;
		if (cJSON *j = parse_json_file("gamedata/music/tracklist.json")) {
			const cJSON *it;
			cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(j, "tracklist")) {
				const cJSON *id2 = cJSON_GetObjectItemCaseSensitive(it, "trackId");
				if (cJSON_IsString(id2)) trackIds.push_back(id2->valuestring);
			}
			cJSON_Delete(j);
		}
	}
	return has(trackIds, id);
}

// ===================================================================== UnlockManager

UnlockManager::UnlockManager() {
	wallpapers = { "default", "barrens", "glen", "refuge", "niko", "title", "planet", "black" };   // DEFAULT_WALLPAPERS
	themes = { "purple" };
}

static void read_list(const cJSON *o, const char *k, std::vector<std::string> &v) {
	const cJSON *a = cJSON_GetObjectItemCaseSensitive(o, k);
	if (!cJSON_IsArray(a)) return;   // null: keep the defaults
	v.clear();
	const cJSON *it;
	cJSON_ArrayForEach(it, a)
		if (cJSON_IsString(it) && !has(v, it->valuestring)) v.push_back(it->valuestring);
}

static void write_list(cJSON *o, const char *k, const std::vector<std::string> &v) {
	cJSON *a = cJSON_AddArrayToObject(o, k);
	for (auto &s : v) cJSON_AddItemToArray(a, cJSON_CreateString(s.c_str()));
}

void UnlockManager::LoadSaveData(const cJSON *o) {
	read_list(o, "unlockedProfiles", profiles);
	read_list(o, "unlockedWallpapers", wallpapers);
	read_list(o, "unlockedThemes", themes);
	read_list(o, "unlockedMusicTracks", tracks);
	read_list(o, "unlockedCgs", cgs);
	read_list(o, "unlockedAchievements", achievements);
	unlockedFirstWallpaper = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "unlockedFirstWallpaper"));
	unlockedFirstProfile = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "unlockedFirstProfile"));
	unlockedFirstTheme = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "unlockedFirstTheme"));
}

void UnlockManager::PopulateSaveData(cJSON *o) const {
	write_list(o, "unlockedProfiles", profiles);
	write_list(o, "unlockedWallpapers", wallpapers);
	write_list(o, "unlockedThemes", themes);
	write_list(o, "unlockedMusicTracks", tracks);
	write_list(o, "unlockedCgs", cgs);
	write_list(o, "unlockedAchievements", achievements);
	cJSON_AddBoolToObject(o, "unlockedFirstWallpaper", unlockedFirstWallpaper);
	cJSON_AddBoolToObject(o, "unlockedFirstProfile", unlockedFirstProfile);
	cJSON_AddBoolToObject(o, "unlockedFirstTheme", unlockedFirstTheme);
}

bool UnlockManager::IsProfileUnlocked(const std::string &id) const { return has(profiles, id); }
bool UnlockManager::IsWallpaperUnlocked(const std::string &id) const { return has(wallpapers, id); }
bool UnlockManager::IsThemeUnlocked(const std::string &id) const { return has(themes, id); }

void UnlockManager::UnlockProfile(const std::string &id) {
	const Profile *info = nullptr;
	for (auto &p : ProfileInfo())
		if (p.id == id) info = &p;
	if (has(profiles, id) || !info) return;
	std::string name = loc::contact(info->id + ":title", info->title);
	profiles.push_back(id);
	sys::log("unlocks: profile %s\n", id.c_str());
	if (!unlockedFirstProfile) {
		unlockedFirstProfile = true;
		show_system_modal(MODAL_INFO, format1(loc::twm("unlock_first_profile_modal"), name), nullptr, false);
	}
	toasts().AddToast("profile_toasts/" + id, format1(loc::twm("unlock_profile_toast"), name));
	unlock_app(PROG_CONTACTS);
	CheckIfAllProfilesUnlocked();
	save_desktop();
}

void UnlockManager::UnlockWallpaper(const std::string &id) {
	const WallpaperInfo *w = desktop_grid().WallpaperMeta(id);
	if (has(wallpapers, id) || !w) return;
	std::string name = loc::wallpaper_name(w->imageFile, w->displayName);
	wallpapers.push_back(id);
	sys::log("unlocks: wallpaper %s\n", id.c_str());
	if (!unlockedFirstWallpaper) {
		unlockedFirstWallpaper = true;
		show_system_modal(MODAL_INFO, format1(loc::twm("unlock_first_wallpaper_modal"), name), nullptr, false);
	}
	toasts().AddToast("", format1(loc::twm("unlock_wallpaper_toast"), name));
	CheckIfAllWallpapersUnlocked();
	filesystem().CreateWallpaperFile(id);
	save_desktop();
}

void UnlockManager::UnlockTheme(const std::string &id) {
	const Theme *t = theme_by_id(id);
	if (has(themes, id) || !t) return;
	std::string name = loc::theme_name(t->id, t->displayName);
	themes.push_back(id);
	sys::log("unlocks: theme %s\n", id.c_str());
	if (!unlockedFirstTheme) {
		unlockedFirstTheme = true;
		show_system_modal(MODAL_INFO, format1(loc::twm("unlock_first_theme_modal"), name), nullptr, false);
	}
	toasts().AddToast("", format1(loc::twm("unlock_theme_toast"), name));
	CheckIfAllThemesUnlocked();
	filesystem().CreateThemeFile(id);
	save_desktop();
}

void UnlockManager::UnlockTrack(const std::string &trackId) {
	if (has(tracks, trackId)) return;
	tracks.push_back(trackId);
	unlock_app(PROG_JUKEBOX);
}

void UnlockManager::UnlockCg(const std::string &imageName) {
	if (has(cgs, imageName)) return;
	cgs.push_back(imageName);
	unlock_app(PROG_GALLERY);
}

void UnlockManager::UnlockAchievement(const std::string &id) {
	const Achievement *info = nullptr;
	for (auto &a : AchievementInfo())
		if (a.id == id) info = &a;
	if (has(achievements, id) || !info) return;
	achievements.push_back(id);
	sys::log("unlocks: badge %s\n", id.c_str());
	unlock_app(PROG_ACHIEVEMENT);
	if (badgeToastsEnabled) {
		std::string lowered = id;
		for (auto &c : lowered) c = (char)tolower((unsigned char)c);
		toasts().AddToast("achievements/" + lowered,
		                  format1(loc::twm("unlock_achievement_toast"), loc::achievement(info->id + ":title", info->title)));
	}
	if (id != "EVERYTHING") CheckIfAllAchievementsUnlocked();
	save_desktop();
}

void UnlockManager::CheckIfAllProfilesUnlocked() {
	bool all = true, george = false;
	for (auto &p : ProfileInfo()) {
		if (p.id.compare(0, 6, "george") == 0 && p.id.size() == 7 && p.id[6] >= '1' && p.id[6] <= '6') {
			if (has(profiles, p.id)) george = true;   // any one of the Georges
		} else if (!has(profiles, p.id)) {
			all = false;
			break;
		}
	}
	if (all && george) UnlockAchievement("FRIEND");
}

void UnlockManager::CheckIfAllWallpapersUnlocked() {
	for (auto &w : desktop_grid().AllWallpapers())
		if (!has(wallpapers, w.imageFile)) return;
	UnlockAchievement("WALLPAPER");
}

void UnlockManager::CheckIfAllThemesUnlocked() {
	for (auto &t : twm::themes())
		if (!has(themes, t.id)) return;
	UnlockAchievement("COLOR");
}

void UnlockManager::CheckIfAllAchievementsUnlocked() {
	for (auto &a : AchievementInfo())
		if (a.id != "EVERYTHING" && !has(achievements, a.id)) return;
	UnlockAchievement("EVERYTHING");
}

// ===================================================================== FlagManager's computed flags

int computed_flag(int id) {
	static const char *wallpaperFlags[] = { "lamp", "factory", "navigate", "courtyard", "ruins", "catwalks", "lamplighter",
	                                        "secret", "library", "cafe", "plant", "tower", "minecart", nullptr, "siblings",
	                                        "world_machine" };   // 401..416 (414 isn't one)
	static const char *profileFlags[] = { "prophetbot", "silver", "rowbot", "shepherd", "magpie", "calamus", "alula",
	                                      "maize", "ling", "watcher", "mason", "lamplighter", "kelvin", "kip", "george1",
	                                      "george2", "george3", "george4", "george5", "george6", "prototype", "cedric",
	                                      "rue", "world_machine", "author", "niko" };   // 426..451
	static const char *themeFlags[] = { "blue", "teal", "green", "yellow", "red", "pink", "orange", "white", "rainbow" };   // 461..469
	if (id >= 401 && id <= 416) {
		const char *w = wallpaperFlags[id - 401];
		return w ? unlocks().IsWallpaperUnlocked(w) : -1;
	}
	if (id >= 426 && id <= 451) return unlocks().IsProfileUnlocked(profileFlags[id - 426]);
	if (id >= 461 && id <= 469) return unlocks().IsThemeUnlocked(themeFlags[id - 461]);
	return -1;
}

void unlock_profile(const std::string &id) { unlocks().UnlockProfile(id); }
void unlock_wallpaper(const std::string &id) { unlocks().UnlockWallpaper(id); }
void unlock_theme(const std::string &id) { unlocks().UnlockTheme(id); }
void unlock_achievement(const std::string &id) { unlocks().UnlockAchievement(id); }
void unlock_cg(const std::string &n) { if (desktop_ready()) unlocks().UnlockCg(n); }
bool profile_unlocked(const std::string &id) { return unlocks().IsProfileUnlocked(id); }
bool wallpaper_unlocked(const std::string &id) { return unlocks().IsWallpaperUnlocked(id); }

} // namespace twm
