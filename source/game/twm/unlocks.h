// unlocks.h - UnlockManager (profiles, wallpapers, themes, music, CGs,
// badges) and ToastManager (the cards that slide in at the top right).
#pragma once
#include "twm.h"
#include <deque>
#include <string>
#include <vector>

struct cJSON;

namespace twm {

class ToastManager {
public:
	void Update();
	void Draw(const Theme &th, Vec2 screenSize);
	void AddToast(const std::string &icon, const std::string &text);
	int ToastCount() const { return (int)(shown.size() + pending.size()); }
private:
	struct Toast { int timer = 200; std::string icon; std::vector<std::string> text; };
	std::deque<Toast> shown, pending;
	int cardOffset = 0, releaseTimer = 0;
};

class UnlockManager {
public:
	UnlockManager();
	bool unlockedFirstWallpaper = false, unlockedFirstProfile = false, unlockedFirstTheme = false;
	bool badgeToastsEnabled = true;
	std::vector<std::string> wallpapers, themes, tracks, cgs, achievements, profiles;

	void LoadSaveData(const cJSON *desktopDat);
	void PopulateSaveData(cJSON *desktopDat) const;

	void UnlockProfile(const std::string &id);
	void UnlockWallpaper(const std::string &id);
	void UnlockTheme(const std::string &id);
	void UnlockTrack(const std::string &trackId);
	void UnlockCg(const std::string &imageName);
	void UnlockAchievement(const std::string &id);
	bool IsProfileUnlocked(const std::string &id) const;
	bool IsWallpaperUnlocked(const std::string &id) const;
	bool IsThemeUnlocked(const std::string &id) const;

	// Metadata (gamedata/twm, gamedata/music)
	struct Profile { std::string id, title; };
	struct Achievement { std::string id, title; };
	const std::vector<Profile> &ProfileInfo();
	const std::vector<Achievement> &AchievementInfo();
	bool IsTrack(const std::string &id);
private:
	void CheckIfAllProfilesUnlocked();
	void CheckIfAllWallpapersUnlocked();
	void CheckIfAllThemesUnlocked();
	void CheckIfAllAchievementsUnlocked();
	std::vector<Profile> profileInfo;
	std::vector<Achievement> achievementInfo;
	std::vector<std::string> trackIds;
	bool profilesLoaded = false, achievementsLoaded = false, tracksLoaded = false;
};

// C# string.Format with one argument: {0} -> arg.
std::string format1(const std::string &fmt, const std::string &arg);

} // namespace twm
