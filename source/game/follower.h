// follower.h - port of OneShotMG.src.Entities.Follower: a party member that
// walks Niko's path a little behind (Alula, Rue, Plight, Cedric, Proto).
#pragma once
#include "entity.h"
#include <deque>

class Follower : public Entity {
public:
	// FollowerManager.FollowerType (the actor ids the events use).
	enum Type { Niko = 1, Alula = 2, Rue = 3, Plight = 6, Cedric = 7, Proto = 8 };
	static const char *Sheet(int type);   // FollowerManager.GetFollowerSheet (null for others)

	Follower(TileMap *map, GameState *state, int type, Entity *target);
	void Update() override;
	bool IsFollower() const override { return true; }
	int Type() const { return type; }
	Entity *Target() const { return target; }
	// RemoveFollower: the one behind takes over the removed one's place in line.
	void InheritPosition(Follower *from);

private:
	struct State {
		int frameIndex;
		Vec2 pos;
		Direction direction;
		bool Same(const State &o) const;
	};
	int type;
	Entity *target;
	std::deque<State> tracked;
	State last{};
	bool haveLast = false;
};
