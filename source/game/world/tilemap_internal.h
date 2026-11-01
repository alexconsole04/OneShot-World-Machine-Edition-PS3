// tilemap_internal.h - pieces shared by the map's own files (tilemap*.cpp):
// MapLine (a polygon collision edge, MapLine.cs) and a bounds-checked
// lookup into the tileset data tables.
#pragma once
#include "tilemap.h"
#include <math.h>
#include <unordered_map>
#include <vector>

struct TileMap::MapLine {
	enum Slope { Floor, Ceiling, EastWall, WestWall } slope = Floor;
	Vec2 startPoint, endPoint;
	int id;

	MapLine(Vec2 s, Vec2 e, int lineID) : startPoint(s), endPoint(e), id(lineID) {
		int dx = endPoint.x - startPoint.x, dy = endPoint.y - startPoint.y;
		if (dx == 0) { slope = dy > 0 ? WestWall : EastWall; return; }
		float m = (float)dy / (float)dx;
		if (m <= 1.0 && m >= -1.0) slope = dx > 0 ? Floor : Ceiling;
		else slope = dy > 0 ? WestWall : EastWall;
	}

	void hashIntoTile(int index, std::unordered_map<int, std::vector<MapLine *>> &h) { h[index].push_back(this); }

	void hashIntoTiles(std::unordered_map<int, std::vector<MapLine *>> &h, Vec2 ts, Vec2 ms) {
		int dx = endPoint.x - startPoint.x, dy = endPoint.y - startPoint.y;
		ts.x *= 256; ts.y *= 256;
		float m;
		int x, y, y2, n5, n6;
		if (iabs(dx) >= iabs(dy)) {
			m = (float)dy / (float)dx;
			int n4;
			if (dx > 0) { n4 = endPoint.x + ts.x / 2; x = startPoint.x; y = endPoint.y; y2 = startPoint.y; }
			else { n4 = startPoint.x + ts.x / 2; x = endPoint.x; y = startPoint.y; y2 = endPoint.y; }
			(void)y;
			n5 = x / ts.x; n6 = y2 / ts.y;
			hashIntoTile(n6 * ms.x + n5, h);
			int i = x + 1;
			if (i % ts.x != 0) i = (i / ts.x + 1) * ts.x;
			for (; i <= n4; i += ts.x) {
				int n7 = i / ts.x;
				int n8 = (int)((float)(i - x) * m + (float)y2) / ts.y;
				hashIntoTile(n8 * ms.x + n7, h);
				if (n8 != n6) hashIntoTile(n8 * ms.x + (n7 - 1), h);
				n6 = n8; n5 = n7;
			}
			return;
		}
		m = (float)dx / (float)dy;
		if (dy > 0) { x = startPoint.x; y = endPoint.y + ts.y / 2; y2 = startPoint.y; }
		else { x = endPoint.x; y = startPoint.y + ts.y / 2; y2 = endPoint.y; }
		n5 = x / ts.x; n6 = y2 / ts.y;
		hashIntoTile(n6 * ms.x + n5, h);
		int j = y2 + 1;
		if (j % ts.y != 0) j = (j / ts.y + 1) * ts.y;
		for (; j <= y; j += ts.y) {
			int n8 = j / ts.y;
			int n7 = (int)((float)(j - y2) * m + (float)x) / ts.x;
			hashIntoTile(n8 * ms.x + n7, h);
			if (n7 != n5) hashIntoTile((n8 - 1) * ms.x + n7, h);
			n6 = n8; n5 = n7;
		}
	}

	void Collision(Entity *e) {
		Vec2 pos = e->GetPos(), oldPos = e->GetOldPos();
		Rect cr = e->GetCollisionRect();
		Vec2 z, z2, z3, z4;
		int n1, n2, n3, n4, n5;
		switch (slope) {
		case Floor:
			n1 = pos.x + cr.x * 256; n2 = pos.x + (cr.x + cr.w) * 256;
			n3 = pos.y + cr.y * 256; n4 = pos.y + (cr.y + cr.h) * 256; n5 = oldPos.y + (cr.y + cr.h) * 256;
			z3 = pos; z4 = oldPos; z = startPoint; z2 = endPoint;
			break;
		case Ceiling:
			n1 = pos.x + cr.x * 256; n2 = pos.x + (cr.x + cr.w) * 256;
			n3 = -(pos.y + (cr.y + cr.h) * 256); n4 = -(pos.y + cr.y * 256); n5 = -(oldPos.y + cr.y * 256);
			z3 = Vec2(pos.x, -pos.y); z4 = Vec2(oldPos.x, -oldPos.y);
			z = Vec2(endPoint.x, -endPoint.y); z2 = Vec2(startPoint.x, -startPoint.y);
			break;
		case EastWall:
			n1 = -(pos.y + (cr.y + cr.h) * 256); n2 = -(pos.y + cr.y * 256);
			n3 = pos.x + cr.x * 256; n4 = pos.x + (cr.x + cr.w) * 256; n5 = oldPos.x + (cr.x + cr.w) * 256;
			z3 = Vec2(-pos.y, pos.x); z4 = Vec2(-oldPos.y, oldPos.x);
			z = Vec2(-startPoint.y, startPoint.x); z2 = Vec2(-endPoint.y, endPoint.x);
			break;
		default: // WestWall
			n1 = pos.y + cr.y * 256; n2 = pos.y + (cr.y + cr.h) * 256;
			n3 = -(pos.x + (cr.x + cr.w) * 256); n4 = -(pos.x + cr.x * 256); n5 = -(oldPos.x + cr.x * 256);
			z3 = Vec2(pos.y, -pos.x); z4 = Vec2(oldPos.y, -oldPos.x);
			z = Vec2(startPoint.y, -startPoint.x); z2 = Vec2(endPoint.y, -endPoint.x);
			break;
		}
		if (n2 <= z.x || n1 >= z2.x) return;
		float m = (float)(z2.y - z.y) / (float)(z2.x - z.x);
		int n7 = (int)((float)(z3.x - z.x) * m + (float)z.y);
		int n8 = (int)((float)(z4.x - z.x) * m + (float)z.y);
		if (n4 > n7 && n3 < n7 && n5 <= n8 + 512) {
			int n9 = n7 - n4;
			double ang = atan2((double)(z2.y - z.y), (double)(z2.x - z.x));
			int n11 = (int)(sin(ang) * n9), n12 = (int)(cos(ang) * n9);
			switch (slope) {
			case Floor:    e->SetCollisionDirection(Direction::Down);  e->SetVel(Vec2()); e->SetPos(Vec2(pos.x - n11, pos.y + n12)); break;
			case Ceiling:  e->SetCollisionDirection(Direction::Up);    e->SetVel(Vec2()); e->SetPos(Vec2(pos.x - n11, pos.y - n12)); break;
			case EastWall: e->SetCollisionDirection(Direction::Right); e->SetVel(Vec2()); e->SetPos(Vec2(pos.x + n12, pos.y + n11)); break;
			case WestWall: e->SetCollisionDirection(Direction::Right); e->SetVel(Vec2()); e->SetPos(Vec2(pos.x - n12, pos.y - n11)); break;
			}
		}
	}
};

// tilesets' passages/priorities/terrain_tags: 0 past the end of the table.
static inline uint8_t at(const std::vector<uint8_t> &v, int i) { return (i >= 0 && i < (int)v.size()) ? v[i] : 0; }
