// pictures.h - PictureManager (event "Show/Move/Erase Picture") and
// FlashManager (screen flash), ported from OneShotMG.src.
#pragma once
#include "../engine/types.h"
#include "../platform/gfx.h"
#include <map>
#include <string>

class PictureManager {
public:
	void Update();
	void Draw();
	void ShowPicture(int slot, const std::string &name, bool isPosCenter, Vec2 pos, float xScale, float yScale,
	                 int opacity, gfx::Blend blend);
	void ErasePicture(int slot) { pictures.erase(slot); }
	void MovePicture(int slot, int duration, bool isPosCenter, Vec2 pos, float xScale, float yScale, int opacity,
	                 gfx::Blend blend);
	void ChangePictureTone(int slot, GameTone tone, int duration);
	void Clear() { pictures.clear(); }
	struct cJSON *GetSaveData() const;          // PictureSaveData list
	void LoadSaveData(const struct cJSON *list);
	int Count() const { return (int)pictures.size(); }

private:
	struct Picture {
		std::string name;
		Vec2 pos;
		bool isPosCenter = false;
		float xScale = 1, yScale = 1;
		int opacity = 255;
		gfx::Blend blend = gfx::Blend::Normal;
		Vec2 picSize;
		int moveTimer = 0, totalMoveTime = 0;
		Vec2 startPos, targetPos;
		float startXScale = 1, targetXScale = 1, startYScale = 1, targetYScale = 1;
		int startOpacity = 0, targetOpacity = 0;
		GameTone tone, startTone, targetTone;
		int toneChangeTimer = 0, totalToneChangeTime = 0;
		void Update();
		void Draw() const;
	};
	std::map<int, Picture> pictures;   // SortedDictionary: drawn in slot order
};

class FlashManager {
public:
	void Update() { if (timeRemaining > 0) timeRemaining--; }
	void Draw();
	void StartFlash(uint8_t r, uint8_t g, uint8_t b, uint8_t a, int time) {
		cr = r; cg = g; cb = b; ca = a;
		duration = time; timeRemaining = time;
	}
private:
	uint8_t cr = 255, cg = 255, cb = 255, ca = 255;
	int duration = 1, timeRemaining = 0;
};

PictureManager &pictures();
FlashManager &flash();
