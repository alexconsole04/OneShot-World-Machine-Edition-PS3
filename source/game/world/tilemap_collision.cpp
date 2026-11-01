// tilemap_collision.cpp - TileMapManager's collision: the entity spatial
// hash, entity against entity, and entity against tiles, polygons and the
// map's edges.
#include "tilemap_internal.h"
#include <algorithm>
#include <functional>

// ======================================================================= TileMap: collision

void TileMap::KeepEntityOnMap(Entity *e) {
	Vec2 pos = e->GetPos(), vel = e->GetVel();
	if (pos.x < 0) { pos.x = 0; vel.x = 0; }
	else if (pos.x > mapSize.x * tileSize.x * 256) { pos.x = mapSize.x * tileSize.x * 256; vel.x = 0; }
	if (pos.y < 0) { pos.y = 0; vel.y = 0; }
	else if (pos.y > mapSize.y * tileSize.y * 256) { pos.y = mapSize.y * tileSize.y * 256; vel.y = 0; }
	e->SetPos(pos);
	e->SetVel(vel);
}

// ---------------------------------------------------------------- hashing
Rect TileMap::GetEntityHashRect(Entity *e, Vec2 p) const {
	Rect c = e->GetCollisionRect();
	int a = ((p.x + c.x * 256) / 256 - hashTileSize.x / 2) / hashTileSize.x;
	int b = ((p.y + c.y * 256) / 256 - hashTileSize.y / 2) / hashTileSize.y;
	int cc = ((p.x + (c.x + c.w) * 256) / 256 + hashTileSize.x / 2) / hashTileSize.x;
	int d = ((p.y + (c.y + c.h) * 256) / 256 + hashTileSize.y / 2) / hashTileSize.y;
	a = std::max(a, 0); b = std::max(b, 0); cc = std::max(cc, 0); d = std::max(d, 0);
	if (a >= hashMapSize.x) a = hashMapSize.x;
	if (b >= hashMapSize.y) b = hashMapSize.y;
	if (cc >= hashMapSize.x) cc = hashMapSize.x;
	if (d >= hashMapSize.y) d = hashMapSize.y;
	return Rect(a, b, cc - a, d - b);
}

void TileMap::RemoveEntityFromHash(Entity *e) {
	Rect r = GetEntityHashRect(e, e->GetOldPos());
	for (int i = r.x; i <= r.x + r.w; i++)
		for (int j = r.y; j <= r.y + r.h; j++) {
			auto it = hashedEntities.find(j * hashMapSize.x + i);
			if (it == hashedEntities.end()) continue;
			auto &v = it->second;
			auto f = std::find(v.begin(), v.end(), e);
			if (f != v.end()) v.erase(f);
		}
}

void TileMap::HashEntities() {
	for (auto &up : entities) {
		Entity *e = up.get();
		if (e->NeverHash || (!e->HasMoved() && e->HasBeenHashed)) continue;
		Rect o = GetEntityHashRect(e, e->GetOldPos()), r = GetEntityHashRect(e, e->GetPos());
		if (o == r && e->HasBeenHashed) continue;
		RemoveEntityFromHash(e);
		for (int i = r.x; i <= r.x + r.w; i++)
			for (int j = r.y; j <= r.y + r.h; j++) {
				auto &v = hashedEntities[j * hashMapSize.x + i];
				if (std::find(v.begin(), v.end(), e) == v.end()) v.push_back(e);
			}
		e->HasBeenHashed = true;
	}
}

void TileMap::HashedEntityCollision(Entity *e, Entity *ignored, bool isTest) {
	Vec2 pos = e->GetPos();
	int key = (pos.y / 256 / hashTileSize.y) * hashMapSize.x + (pos.x / 256 / hashTileSize.x);
	struct EWD { Entity *e; int d; };
	EWD buf[64];
	int n = 0;
	auto it = hashedEntities.find(key);
	if (it != hashedEntities.end())
		for (Entity *o : it->second)
			if (o != e && o != ignored && !o->IgnoreNpcCollision() && o->Active() && n < 64) {
				Vec2 p2 = o->GetPos();
				buf[n++] = { o, iabs(pos.x - p2.x) + iabs(pos.y - p2.y) };
			}
	std::stable_sort(buf, buf + n, [](const EWD &a, const EWD &b) { return a.d < b.d; });
	for (int i = 0; i < n; i++) {
		if (buf[i].e->IsPlayer() && e->GetVel().x == 0 && e->GetVel().y == 0 && !isTest)
			buf[i].e->EntityCollision(e);
		else
			e->EntityCollision(buf[i].e);
	}
}

// ---------------------------------------------------------------- collision
void TileMap::MapCollision(Entity *entity) {
	Vec2 ep = entity->GetPos();
	Rect cr = entity->GetCollisionRect();
	Vec2 a((ep.x / 256 + cr.x) / tileSize.x, (ep.y / 256 + cr.y) / tileSize.y);
	Vec2 b((ep.x / 256 + cr.x + cr.w) / tileSize.x, (ep.y / 256 + cr.y + cr.h) / tileSize.y);
	auto clampv = [&](Vec2 &v) {
		if (v.x < 0) v.x = 0; else if (v.x >= mapSize.x) v.x = mapSize.x - 1;
		if (v.y < 0) v.y = 0; else if (v.y >= mapSize.y) v.y = mapSize.y - 1;
	};
	clampv(a);
	clampv(b);
	Vec2 tiles[32];
	int nt = 0;
	MapLine *lines[64];
	int nl = 0;
	for (int i = a.x; i <= b.x; i++)
		for (int j = a.y; j <= b.y; j++) {
			auto hl = hashedMapLines.find(j * mapSize.x + i);
			if (hl != hashedMapLines.end())
				for (MapLine *ml : hl->second) {
					bool dup = false;
					for (int k = 0; k < nl; k++) if (lines[k]->id == ml->id) { dup = true; break; }
					if (!dup && nl < 64) lines[nl++] = ml;
				}
			if (IsTileSolid(i, j) && nt < 32)
				tiles[nt++] = Vec2(i * tileSize.x * 256 + tileSize.x * 256 / 2, j * tileSize.y * 256 + tileSize.y * 256 / 2);
		}
	std::stable_sort(tiles, tiles + nt, [&](const Vec2 &t1, const Vec2 &t2) {
		return iabs(ep.x - t1.x) + iabs(ep.y - t1.y) < iabs(ep.x - t2.x) + iabs(ep.y - t2.y);
	});
	for (int k = 0; k < nt; k++) {
		Rect tc(tiles[k].x - tileSize.x * 256 / 2, tiles[k].y - tileSize.y * 256 / 2, tileSize.x * 256, tileSize.y * 256);
		TileCollision(tc, entity, tc.x / (tileSize.x * 256), tc.y / (tileSize.y * 256), true);
	}
	// C# iterates a Dictionary<int, MapLine> by insertion order.
	for (int k = 0; k < nl; k++) lines[k]->Collision(entity);
}

void TileMap::TileCollision(Rect tc, Entity *entity, int tileX, int tileY, bool isMapTile) {
	Vec2 pos = entity->GetPos();
	Rect cr = entity->GetCollisionRect();
	Rect rect(pos.x + cr.x * 256, pos.y + cr.y * 256, cr.w * 256, cr.h * 256);
	if (rect.x >= tc.x + tc.w || rect.x + rect.w <= tc.x || rect.y >= tc.y + tc.h || rect.y + rect.h <= tc.y) return;
	Vec2 v(tc.x + tc.w / 2, tc.y + tc.h / 2);
	Vec2 v2(rect.x + rect.w / 2, rect.y + rect.h / 2);
	Vec2 vel = entity->GetVel();
	Direction dir = Direction::None;
	int n = iabs(v2.x - v.x), n2 = iabs(v2.y - v.y);
	if (tc.w > tc.h) n2 += (tc.w - tc.h) / 2; else n += (tc.h - tc.w) / 2;
	const int n3 = 0;
	if (n - n2 > n3) {
		dir = (v2.x >= v.x) ? Direction::Left : Direction::Right;
		if (vel.y == 0) {
			if (v2.y < tc.y) pos.y -= iabs(vel.x / 2);
			else if (v2.y > tc.y + tc.h) pos.y += iabs(vel.x / 2);
		}
	} else if (n - n2 < -n3) {
		dir = (v2.y >= v.y) ? Direction::Up : Direction::Down;
		if (vel.x == 0) {
			if (v2.x < tc.x) pos.x -= iabs(vel.y / 2);
			else if (v2.x > tc.x + tc.w) pos.x += iabs(vel.y / 2);
		}
	} else if (v2.x < v.x) {
		if (v2.y < v.y) {
			bool f = false;
			if (vel.x > 0 && vel.x > vel.y) {
				if (isMapTile && tileY > 0) f = IsTileSolid(tileX, tileY - 1);
				dir = f ? Direction::Right : Direction::Down;
			} else {
				if (isMapTile && tileX > 0) f = IsTileSolid(tileX - 1, tileY);
				dir = f ? Direction::Down : Direction::Right;
			}
		} else {
			bool f = false;
			if (vel.x > 0 && vel.x > -vel.y) {
				if (isMapTile && tileY < mapSize.y - 1) f = IsTileSolid(tileX, tileY + 1);
				dir = f ? Direction::Right : Direction::Up;
			} else {
				if (isMapTile && tileX > 0) f = IsTileSolid(tileX - 1, tileY);
				dir = f ? Direction::Up : Direction::Right;
			}
		}
	} else if (v2.y < v.y) {
		bool f = false;
		if (vel.x < 0 && vel.x < -vel.y) {
			if (isMapTile && tileY > 0) f = IsTileSolid(tileX, tileY - 1);
			dir = f ? Direction::Left : Direction::Down;
		} else {
			if (isMapTile && tileX < mapSize.x - 1) f = IsTileSolid(tileX + 1, tileY);
			dir = f ? Direction::Down : Direction::Left;
		}
	} else {
		bool f = false;
		if (vel.x < 0 && vel.x < vel.y) {
			if (isMapTile && tileY < mapSize.y - 1) f = IsTileSolid(tileX, tileY + 1);
			dir = f ? Direction::Left : Direction::Up;
		} else {
			if (isMapTile && tileX < mapSize.x - 1) f = IsTileSolid(tileX + 1, tileY);
			dir = f ? Direction::Up : Direction::Left;
		}
	}
	switch (dir) {
	case Direction::Right: pos.x = tc.x - rect.w - cr.x * 256; break;
	case Direction::Left:  pos.x = tc.x + tc.w - cr.x * 256; break;
	case Direction::Down:  pos.y = tc.y - rect.h - cr.y * 256; break;
	case Direction::Up:    pos.y = tc.y + tc.h - cr.y * 256; break;
	default: break;
	}
	entity->SetCollisionDirection(dir);
	entity->SetPos(pos);
}

// ---------------------------------------------------------------- update / camera
