// player.h - port of OneShotMG.src.Player (free movement, walk/run, animation).
#pragma once
#include "entity.h"

class Player : public Entity {
public:
	Player(TileMap *map, GameState *state, int id, float xPixel, float yPixel);
	void MapChanged(int id, float xPixel, float yPixel);
	void Update() override;
	bool IsPlayer() const override { return true; }
	bool PlayerHasControl() const;
	bool defaultMovementRun = false;   // SettingsMenu.IsDefaultMovementRun
private:
	void NormalizeWalkRunSpeed(bool run);
	int footprintEmitTimer = 0;
};
