// tilemap_events.cpp - TileMapManager's side of events: starting map and
// common events, running and drawing them, the tile puzzles, followers and
// the Glen's fireflies.
#include "tilemap_internal.h"
#include "event_runner.h"
#include "follower.h"
#include "player.h"
#include "../../engine/gm.h"
#include "../../engine/util.h"
#include "../../platform/sys.h"
#include "../core/game_state.h"
#include <algorithm>
#include <math.h>

// ======================================================================= TileMap: events, puzzles, followers

void TileMap::PixelPuzzleReset() {
	for (auto &e : entities)
		if (e->GetName().compare(0, 6, "pixel ") == 0) state->set_self_switch(e->GetID(), "A", false);
}

bool TileMap::PixelPuzzleCheck(const uint8_t *solution, Vec2 start, Vec2 size) const {
	for (auto &e : entities) {
		if (e->GetName().compare(0, 6, "pixel ") != 0) continue;
		Vec2 t = e->GetCurrentTile();
		Vec2 v(t.x - start.x, t.y - start.y);
		if (v.x >= 0 && v.x < size.x && v.y >= 0 && v.y < size.y)
			if ((solution[v.x + v.y * size.x] != 0) != state->self_switch(e->GetID(), "A")) return false;
	}
	return true;
}

bool TileMap::RamPuzzleCheck() const {
	static const Vec2 ok[5] = { Vec2(80, 59), Vec2(79, 63), Vec2(80, 65), Vec2(83, 59), Vec2(85, 61) };
	for (auto &e : entities) {
		if (e->GetName().compare(0, 8, "sokoram ") != 0) continue;
		Vec2 t = e->GetCurrentTile();
		bool found = false;
		for (auto &p : ok) if (p == t) { found = true; break; }
		if (!found) return false;
	}
	return true;
}

bool TileMap::AreAnyEntitiesInForcedRoutes() const {
	if (player && player->IsInForcedRoute()) return true;
	for (auto &e : entities)
		if (e->IsInForcedRoute()) return true;
	return false;
}

// TileMapManager.AddFollower: follow the entity with that id (the previous
// follower), or Niko.
int TileMap::SpawnFollower(int type, int followId) {
	Entity *t = GetEntityByID(followId);
	if (!t) t = player;
	if (!t) return -1;
	Follower *f = new Follower(this, state, type, t);
	AddEntity(f);
	return f->GetID();
}

void TileMap::SpawnFollowers() {
	followerIds.clear();
	lastFollowerId = -1;
	for (int type : state->followers) {
		lastFollowerId = SpawnFollower(type, lastFollowerId);
		followerIds[type] = lastFollowerId;
	}
}

void TileMap::AddFollower(int type) {
	if (state->has_follower(type)) return;
	state->followers.push_back(type);
	lastFollowerId = SpawnFollower(type, lastFollowerId);
	followerIds[type] = lastFollowerId;
}

void TileMap::RemoveFollower(int type) {
	auto &fl = state->followers;
	auto pos = std::find(fl.begin(), fl.end(), type);
	if (pos == fl.end()) return;
	auto id = followerIds.find(type);
	if (id != followerIds.end()) {
		Entity *gone = GetEntityByID(id->second);
		if (pos + 1 != fl.end()) {
			auto next = followerIds.find(*(pos + 1));
			Entity *n = next != followerIds.end() ? GetEntityByID(next->second) : nullptr;
			if (gone && gone->IsFollower() && n && n->IsFollower())
				static_cast<Follower *>(n)->InheritPosition(static_cast<Follower *>(gone));
		}
		if (gone) RemoveEntity(gone);
		followerIds.erase(id);
	}
	fl.erase(pos);
}

// ---------------------------------------------------------------- fireflies
static float frand(float lo, float hi) { return lo + (hi - lo) * (float)util::random(0, 1000000) / 1000000.0f; }

void TileMap::SpawnFireflyParticles() {
	fireflies.assign(30, Firefly());
	hasFireflies = true;
	Vec2 tex = gm::texture_size("pictures/firefly");
	for (Firefly &f : fireflies) {
		f.size = frand(0.25f, 1.0f);
		f.src = Rect(0, 0, tex.x, tex.y);
		f.scaledSize = Vec2((int)(tex.x * f.size), (int)(tex.y * f.size));
		f.vel = Vec2(util::random(-512, 512), util::random(-512, 512));
		f.pos = Vec2(util::random(0, 163840), util::random(0, 122880));
		f.wavelength = util::random(120, 240);
		f.phase = util::random(0, f.wavelength);
	}
	UpdateFireflies(Vec2());
}

// FireflyParticle.Update: drift, parallax against the camera (bigger ones
// move more), wrap around the screen, and pulse.
void TileMap::UpdateFireflies(Vec2 camDelta) {
	for (Firefly &f : fireflies) {
		float k = f.size * (2.0f / 3.0f) + 5.0f / 6.0f;
		f.pos.x += f.vel.x - (int)((float)(camDelta.x * 2 * 256) * k);
		f.pos.y += f.vel.y - (int)((float)(camDelta.y * 2 * 256) * k);
		if (f.pos.x > 163840) f.pos.x -= (640 + f.scaledSize.x) * 256;
		else if (f.pos.x < -f.scaledSize.x * 256) f.pos.x += (640 + f.scaledSize.x) * 256;
		if (f.pos.y > 122880) f.pos.y -= (480 + f.scaledSize.y) * 256;
		else if (f.pos.y < -f.scaledSize.y * 256) f.pos.y += (480 + f.scaledSize.y) * 256;
		if (++f.phase >= f.wavelength) f.phase = 0;
		f.alpha = (float)sin((double)((float)f.phase / (float)f.wavelength) * 3.14159265358979);
	}
}

// ---------------------------------------------------------------- events
bool TileMap::CheckIfStartScript(Entity *e, bool investigate, Entity **out) {
	Entity *found = nullptr;
	Vec2 pos = e->GetPos();
	Rect cr = e->GetCollisionRect();
	Rect bp = e->GetButtonPressRect();
	Rect pointRect(pos.x - 1, pos.y - 1, 2, 2);
	Vec2 bpCenter(bp.x + bp.w / 2, bp.y + bp.h / 2);
	Rect bodyRect(pos.x + cr.x * 256, pos.y + cr.y * 256, cr.w * 256, cr.h * 256);
	// C# collects candidates into a Dictionary keyed by id (insertion order).
	std::vector<Entity *> cand;
	Rect hr = GetEntityHashRect(e, pos);
	for (int i = hr.x; i <= hr.x + hr.w; i++)
		for (int j = hr.y; j <= hr.y + hr.h; j++) {
			auto it = hashedEntities.find(j * hashMapSize.x + i);
			if (it == hashedEntities.end()) continue;
			for (Entity *o : it->second) {
				bool dup = false;
				for (Entity *c2 : cand) if (c2->GetID() == o->GetID()) { dup = true; break; }
				if (!dup) cand.push_back(o);
			}
		}
	int best = INT32_MAX;
	for (Entity *o : cand) {
		bool hit = false;
		if (o->Active() && o->HasScript()) {
			if (investigate && o->StartScriptOnPlayerAction()) {
				if (o->TouchingRect(bp)) hit = true;
				if (o->IgnoreNpcCollision() && o->TouchingRect(pointRect)) hit = true;
			}
			if (o->StartScriptOnPlayerTouch()) {
				if (!o->IgnoreNpcCollision() && o->TouchingRect(bp) && !o->isTouchingPlayerAndTriggeredEvent) hit = true;
				if (o->IgnoreNpcCollision() && o->TouchingRect(pointRect) && !o->isTouchingPlayerAndTriggeredEvent) hit = true;
			}
			if (o->StartScriptOnEventTouch() && o->TouchingRect(bodyRect) && !o->isTouchingPlayerAndTriggeredEvent) hit = true;
		}
		if (hit) {
			Vec2 p2 = o->GetPos();
			int d = iabs(p2.x - bpCenter.x) + iabs(p2.y - bpCenter.y);
			if (!found || d < best) { found = o; best = d; }
		}
	}
	if (found && (found->StartScriptOnPlayerTouch() || found->StartScriptOnEventTouch()))
		found->isTouchingPlayerAndTriggeredEvent = true;
	*out = found;
	return found != nullptr;
}

void TileMap::StartEvent(Entity *e, bool parallel) {
	if (parallel) {
		for (auto &pr : parallelEventRunners)
			if (pr.first == e->GetID()) { pr.second->KeepAlive = true; return; }
		parallelEventRunners.emplace_back(e->GetID(),
			std::unique_ptr<EventRunner>(new EventRunner(this, e->GetList(), e->GetID(), e->GetCurrentPageNumber(), e)));
		parallelEventRunners.back().second->KeepAlive = true;
		return;
	}
	if (mainEventRunner || IsMapTransitioning()) return;
	mainEventRunner.reset(new EventRunner(this, e->GetList(), e->GetID(), e->GetCurrentPageNumber(), e));
	mainEventRunner->Update();
	if (!mainEventRunner || !mainEventRunner->IsFinished()) return;
	mainEventRunner.reset();
	if (state->var(4) == 0) return;
	for (size_t i = 0; i < entities.size(); i++)
		if (entities[i]->HandlesMapEventVar) { entities[i]->Update(); break; }
}

void TileMap::StartCommonEvent(int id, bool parallel) {
	auto it = commonEvents.find(id);
	if (it == commonEvents.end()) {
		sys::log("No common event exists with id %d\n", id);
		return;
	}
	if (parallel) {
		int key = 10000 + id;
		for (auto &pr : parallelEventRunners)
			if (pr.first == key) { pr.second->KeepAlive = true; return; }
		parallelEventRunners.emplace_back(key, std::unique_ptr<EventRunner>(new EventRunner(this, it->second.list, id, -1)));
		parallelEventRunners.back().second->KeepAlive = true;
	} else if (!mainEventRunner) {
		mainEventRunner.reset(new EventRunner(this, it->second.list, id, -1));
		queuedCommonEventId = -1;
	} else {
		queuedCommonEventId = id;
	}
}

// Host testing: run a list of Show Text commands as a script.
void TileMap::DebugRunTexts(const std::vector<std::string> &texts) {
	auto list = std::make_shared<CommandList>();
	for (const std::string &t : texts) {
		EventCommand c;
		c.code = 101;
		if (t.compare(0, 8, "!script ") == 0) { c.code = 355; c.parameters.push_back(t.substr(8)); }   // host testing
		else c.parameters.push_back(t);
		list->push_back(c);
	}
	list->push_back(EventCommand());
	mainEventRunner.reset(new EventRunner(this, list, 0, -1));
}

const CommonEvent *TileMap::GetCommonEvent(int id) const {
	auto it = commonEvents.find(id);
	if (it == commonEvents.end()) {
		sys::log("No common event exists with id %d\n", id);
		return nullptr;
	}
	return &it->second;
}

Entity *TileMap::GetNPCTriggeringEvent() const {
	return mainEventRunner ? mainEventRunner->TriggeringEntity() : nullptr;
}

void TileMap::UpdateCommonEvents() {
	for (auto &kv : commonEvents) {
		const CommonEvent &ce = kv.second;
		if (ce.trigger == 1) {
			if (!IsInScript() && state->flag(ce.switch_id)) StartCommonEvent(ce.id);
		} else if (ce.trigger == 2) {
			if (state->flag(ce.switch_id)) StartCommonEvent(ce.id, true);
		}
	}
}

void TileMap::EventRunnerUpdate() {
	retiredRunners.clear();
	if (mainEventRunner) {
		mainEventRunner->Update();
		if (mainEventRunner->IsFinished()) mainEventRunner.reset();
	} else if (queuedCommonEventId > 0) {
		StartCommonEvent(queuedCommonEventId);
	}
	std::vector<int> kill;
	// Index loop: a runner may start new parallel events (e.g. after a transfer).
	for (size_t i = 0; i < parallelEventRunners.size(); i++) {
		EventRunner *r = parallelEventRunners[i].second.get();
		if (r->KeepAlive) {
			r->Update();
			r->KeepAlive = false;
			if (r->IsFinished()) kill.push_back(parallelEventRunners[i].first);
		} else {
			kill.push_back(parallelEventRunners[i].first);
		}
	}
	for (int k : kill)
		for (size_t i = 0; i < parallelEventRunners.size(); i++)
			if (parallelEventRunners[i].first == k) { parallelEventRunners.erase(parallelEventRunners.begin() + i); break; }
	ReleaseRetired();
}
