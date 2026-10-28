// special_entities.h - map events with their own classes in the PC build:
// Collectible ("!collectible": an artifact the cursor picks up from the
// game window) and CreditsTextEntity ("@text", "@text_small": credits lines).
#pragma once
#include "entity.h"

class Collectible : public Entity {
public:
	Collectible(TileMap *map, GameState *state, const EventData *data);
	void Update() override;
	void Draw(Vec2 camPos, GameTone tone) override;
private:
	bool hovering = false;
	std::string themeId;   // the theme it unlocks, drawn in that theme's colours
	Vec2 aberr[3];
	int aberrTimer[3] = {};
};

class CreditsText : public Entity {
public:
	CreditsText(TileMap *map, GameState *state, const EventData *data, bool small);
	void Update() override;
	void Draw(Vec2 camPos, GameTone tone) override;
private:
	std::string text;
	bool small;
};
