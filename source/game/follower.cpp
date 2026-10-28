// follower.cpp - see follower.h. Positions are in 1/256 pixels.
#include "follower.h"
#include "tilemap.h"
#include <math.h>
#include <stdlib.h>

static const int FRAMES_TO_LAG_BEHIND = 120;
static const int DISTANCE_TO_LAG_BEHIND = 6144;   // 24 px

const char *Follower::Sheet(int t) {
	switch (t) {
	case Alula: return "green_npc_alula";
	case Rue: return "red_rue";
	case Plight: return "red_lamplighter";
	case Cedric: return "green_npc_cedric";
	case Proto: return "blue_npc_prototype";
	default: return nullptr;
	}
}

bool Follower::State::Same(const State &o) const {
	int d = abs(pos.x - o.pos.x) + abs(pos.y - o.pos.y);
	return frameIndex == o.frameIndex && d < 128 && direction == o.direction;
}

Follower::Follower(TileMap *m, GameState *s, int t, Entity *tgt) : Entity(m, s), type(t), target(tgt) {
	id = m->GetNextEntityID();
	ignoreMapCollision = true;
	ignoreNpcCollision = true;
	const char *sheet = Sheet(t);
	SetNPCSheet(sheet ? sheet : "");
	opacity = 255;
	collisionRect = Rect(-8, -8, 16, 16);
	pos = oldPos = tgt->GetPos();
	direction = tgt->GetDirection();
	frameIndex = 0;
	NeverHash = true;
}

void Follower::Update() {
	oldPos = pos;
	if (!target) return;
	State now{ target->GetFrameIndex(), target->GetPos(), target->GetDirection() };
	if (tracked.empty() || !haveLast || !now.Same(last)) {
		tracked.push_back(now);
		last = now;
		haveLast = true;
	}
	long long dx = abs(pos.x - target->GetPos().x), dy = abs(pos.y - target->GetPos().y);
	int dist = (int)sqrt((double)(dx * dx + dy * dy));
	if (dist > DISTANCE_TO_LAG_BEHIND || (int)tracked.size() > FRAMES_TO_LAG_BEHIND) {
		State st = tracked.front();
		tracked.pop_front();
		// Far behind (a run): catch up two steps a frame.
		if (dist > DISTANCE_TO_LAG_BEHIND * 2 && tracked.size() > 40) {
			st = tracked.front();
			tracked.pop_front();
		}
		frameIndex = st.frameIndex;
		direction = st.direction;
		pos = st.pos;
	} else {
		frameIndex = 0;
	}
}

void Follower::InheritPosition(Follower *from) {
	target = from->target;
	while (!from->tracked.empty()) {
		tracked.push_back(from->tracked.front());
		from->tracked.pop_front();
	}
}
