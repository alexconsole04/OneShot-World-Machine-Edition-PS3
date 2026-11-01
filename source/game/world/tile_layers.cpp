// tile_layers.cpp - the pieces a map is drawn with: TileSet (a .tsx
// tileset, animated autotiles) and TileLayer (a layer of tile ids). Port of
// TileSet.cs and TileLayer.cs.
#include "tilemap.h"
#include "../../engine/gm.h"
#include "../../engine/xml.h"
#include "../../platform/sys.h"
#include <stdlib.h>

// ======================================================================= TileSet
bool TileSet::Read(int firstId, const std::string &fileName) {
	// Maps share a few dozen .tsx files: parse each once.
	static std::unordered_map<std::string, TileSet> parsed;
	auto pc = parsed.find(fileName);
	if (pc != parsed.end()) {
		*this = pc->second;
		firstTileId = firstId;
		ResolveTexture();
		return true;
	}
	if (!ReadTsx(fileName)) return false;
	TileSet proto = *this;
	proto.tex = nullptr;
	parsed.emplace(fileName, proto);
	firstTileId = firstId;
	ResolveTexture();
	return true;
}

bool TileSet::ReadTsx(const std::string &fileName) {
	size_t num = fileName.find("tilesets/");
	autotile = false;
	if (num == std::string::npos) {
		num = fileName.find("autotiles/");
		autotile = true;
		if (num == std::string::npos) {
			sys::log("tileset '%s' is not in the tilesets folder!\n", fileName.c_str());
			return false;
		}
	}
	std::string rel = "gamedata/" + fileName.substr(num);
	uint8_t *data;
	size_t size;
	if (!sys::read_file(rel.c_str(), &data, &size)) {
		sys::log("cannot read %s\n", rel.c_str());
		return false;
	}
	std::string err;
	auto root = xml::parse((const char *)data, &err);
	free(data);
	if (!root) { sys::log("%s: %s\n", rel.c_str(), err.c_str()); return false; }
	tileSize = Vec2(root->attr_int("tilewidth", 16), root->attr_int("tileheight", 16));
	totalTiles = root->attr_int("tilecount", 0);
	tileColumns = root->attr_int("columns", 1);
	if (tileColumns <= 0) tileColumns = 1;
	const xml::Node *img = root->child("image");
	imageFileName = img ? img->attr("source", "") : "";
	size_t c = imageFileName.find("Content/");
	if (c == std::string::npos) {
		sys::log("image '%s' in tileset '%s' is not in the Content folder!\n", imageFileName.c_str(), rel.c_str());
	} else {
		imageFileName = imageFileName.substr(c + 8);
		size_t png = imageFileName.find(".png");
		if (png != std::string::npos) imageFileName = imageFileName.substr(0, png);
	}
	ResolveTexture();
	return true;
}

void TileSet::ResolveTexture() {
	tex = gm::texture(imageFileName.c_str());
	animated = false;
	if (autotile && tex && tex->h > 96) {
		animated = true;
		animAreaRows = 6;
		animFrames = (int)tex->h / 96;
		animFrameTime = 16;
		animTimer = 0;
		frameIndex = 0;
	}
}

void TileSet::DrawTile(Vec2 drawPos, int tileID, GameTone tone, float alpha) const {
	int num = tileID - firstTileId + frameIndex * animAreaRows * tileColumns;
	if (num < 0) return;
	int x = num % tileColumns * tileSize.x;
	int y = num / tileColumns * tileSize.y;
	gm::blit(tex, drawPos, Rect(x, y, tileSize.x, tileSize.y), alpha, 0, gfx::Blend::Normal, 2, tone);
}

void TileSet::Update() {
	if (!animated) return;
	if (++animTimer > animFrameTime) {
		animTimer = 0;
		if (++frameIndex >= animFrames) frameIndex = 0;
	}
}

// ======================================================================= TileLayer
static const TileSet &tileset_for(const std::vector<TileSet> &tilesets, int id) {
	const TileSet *ts = &tilesets[0];
	for (const TileSet &t : tilesets) {
		if (t.GetFirstTileID() > id) break;
		ts = &t;
	}
	return *ts;
}

TileLayer::TileLayer(Vec2 mapSize) : tiles(mapSize.x * mapSize.y, 0), layerSize(mapSize) {}

TileLayer::TileLayer(Vec2 mapSize, const std::string &csv, Vec2 ls)
	: tiles(mapSize.x * mapSize.y, 0), layerSize(ls) {
	const char *p = csv.c_str();
	size_t n = tiles.size();
	for (size_t i = 0; i < n && *p; i++) {
		while (*p && (*p < '0' || *p > '9') && *p != '-') p++;
		tiles[i] = (int16_t)strtol(p, (char **)&p, 10);
	}
}

int16_t TileLayer::GetRPGMakerTileId(int x, int y, const std::vector<TileSet> &tilesets) const {
	int16_t tileId = GetTileId(x, y);
	if (tileId == 0) return 0;
	int num2 = 0;
	bool first = true;
	const TileSet *ts = &tilesets[0];
	for (const TileSet &t : tilesets) {
		if (first) { first = false; continue; }
		if (t.GetFirstTileID() > tileId) break;
		num2++;
		ts = &t;
	}
	return (int16_t)(tileId - ts->GetFirstTileID() + num2 * 48);
}

void TileLayer::DrawTile(Vec2 drawPos, int xIndex, int yIndex, const std::vector<TileSet> &tilesets,
                         int height, GameTone tone) const {
	yIndex -= height;
	if (yIndex < 0) yIndex += layerSize.y;
	int num = tiles[xIndex + yIndex * layerSize.x];
	if (num == 0) return;
	tileset_for(tilesets, num).DrawTile(drawPos, num, tone);
}

void TileLayer::Draw(const std::vector<TileSet> &tilesets, Vec2 camPos, Vec2 tileSize, bool wrapping,
                     GameTone tone, float alpha) const {
	int num = camPos.x / tileSize.x, num2 = camPos.y / tileSize.y;
	int num3 = (camPos.x + 320) / tileSize.x + 1, num4 = (camPos.y + 240) / tileSize.y + 1;
	if (!wrapping) {
		if (num < 0) num = 0;
		if (num2 < 0) num2 = 0;
		if (num3 >= layerSize.x) num3 = layerSize.x - 1;
		if (num4 >= layerSize.y) num4 = layerSize.y - 1;
	} else {
		if (num <= 0) num--;
		if (num2 <= 0) num2--;
	}
	int x = (camPos.x - num * tileSize.x) * -1;
	int y = (camPos.y - num2 * tileSize.y) * -1;
	Vec2 zero(x, y);
	for (int i = num2; i <= num4; i++) {
		int r = i;
		if (r < 0) r += layerSize.y; else if (r >= layerSize.y) r -= layerSize.y;
		for (int j = num; j <= num3; j++) {
			int c = j;
			if (c < 0) c += layerSize.x; else if (c >= layerSize.x) c -= layerSize.x;
			int id = tiles[c + r * layerSize.x];
			if (id != 0) tileset_for(tilesets, id).DrawTile(zero, id, tone, alpha);
			zero.x += tileSize.x;
		}
		zero.y += tileSize.y;
		zero.x = x;
	}
}

