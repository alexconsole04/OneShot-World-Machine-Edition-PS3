#include "../twm/wm.h"
#include "pictures.h"
#include "../../engine/gm.h"
#include "../../engine/util.h"

PictureManager &pictures() { static PictureManager m; return m; }
FlashManager &flash() { static FlashManager m; return m; }

void PictureManager::ShowPicture(int slot, const std::string &name, bool isPosCenter, Vec2 pos, float xs, float ys,
                                 int opacity, gfx::Blend blend) {
	twm::unlock_cg(name);   // UnlockCg
	pictures.erase(slot);
	Picture p;
	p.name = name;
	p.pos = pos;
	p.isPosCenter = isPosCenter;
	p.xScale = xs;
	p.yScale = ys;
	p.opacity = opacity;
	p.blend = blend;
	p.picSize = gm::texture_size(("pictures/" + name).c_str());
	pictures.emplace(slot, p);
}

void PictureManager::MovePicture(int slot, int duration, bool isPosCenter, Vec2 pos, float xs, float ys, int opacity,
                                 gfx::Blend blend) {
	auto it = pictures.find(slot);
	if (it == pictures.end()) return;
	Picture &p = it->second;
	p.startPos = p.pos;
	p.startXScale = p.xScale;
	p.startYScale = p.yScale;
	p.startOpacity = p.opacity;
	p.targetPos = pos;
	p.isPosCenter = isPosCenter;
	p.targetXScale = xs;
	p.targetYScale = ys;
	p.targetOpacity = opacity;
	p.blend = blend;
	p.moveTimer = 0;
	p.totalMoveTime = duration;
}

void PictureManager::ChangePictureTone(int slot, GameTone tone, int duration) {
	auto it = pictures.find(slot);
	if (it == pictures.end()) return;
	Picture &p = it->second;
	p.toneChangeTimer = 0;
	if (duration <= 0) {
		p.totalToneChangeTime = 0;
		p.tone = tone;
	} else {
		p.totalToneChangeTime = duration;
		p.startTone = p.tone;
		p.targetTone = tone;
	}
}

void PictureManager::Update() {
	for (auto &kv : pictures) kv.second.Update();
}

void PictureManager::Draw() {
	for (auto &kv : pictures) kv.second.Draw();
}

void PictureManager::Picture::Update() {
	if (totalMoveTime > 0) {
		moveTimer++;
		if (moveTimer >= totalMoveTime) {
			pos = targetPos;
			xScale = targetXScale;
			yScale = targetYScale;
			opacity = targetOpacity;
			totalMoveTime = 0;
			moveTimer = 0;
		} else {
			float t = (float)moveTimer / (float)totalMoveTime;
			pos.x = (int)util::lerp((float)startPos.x, (float)targetPos.x, t);
			pos.y = (int)util::lerp((float)startPos.y, (float)targetPos.y, t);
			xScale = util::lerp(startXScale, targetXScale, t);
			yScale = util::lerp(startYScale, targetYScale, t);
			opacity = (int)util::lerp((float)startOpacity, (float)targetOpacity, t);
		}
	}
	if (totalToneChangeTime > 0) {
		toneChangeTimer++;
		if (toneChangeTimer >= totalToneChangeTime) {
			tone = targetTone;
			totalToneChangeTime = 0;
			toneChangeTimer = 0;
		} else {
			float t = (float)toneChangeTimer / (float)totalToneChangeTime;
			tone = startTone * (1.0f - t) + targetTone * t;
		}
	}
}

void PictureManager::Picture::Draw() const {
	if (opacity <= 0) return;
	Vec2 p = pos;
	if (isPosCenter) {
		p.x -= (int)((float)picSize.x * xScale / 2.0f);
		p.y -= (int)((float)picSize.y * xScale / 2.0f);   // C# uses xScale here too
	}
	const gfx::Texture *t = gm::texture(("pictures/" + name).c_str());
	gm::blit_scaled(t, p, Rect(0, 0, picSize.x, picSize.y), xScale, yScale, opacity / 255.0f, 0, blend, tone);
}

void FlashManager::Draw() {
	if (timeRemaining <= 0) return;
	uint8_t a = (uint8_t)(ca * timeRemaining / duration);
	gm::color_box(Rect(0, 0, 320, 240), cr, cg, cb, a);
}

// ---------------------------------------------------------------- save data
#include "cJSON.h"

cJSON *PictureManager::GetSaveData() const {
	cJSON *a = cJSON_CreateArray();
	for (auto &kv : pictures) {
		const Picture &p = kv.second;
		cJSON *o = cJSON_CreateObject();
		cJSON_AddNumberToObject(o, "id", kv.first);
		cJSON_AddStringToObject(o, "name", p.name.c_str());
		cJSON *pos = cJSON_AddObjectToObject(o, "pos");
		cJSON_AddNumberToObject(pos, "X", p.pos.x);
		cJSON_AddNumberToObject(pos, "Y", p.pos.y);
		cJSON_AddBoolToObject(o, "isPosCenter", p.isPosCenter);
		cJSON_AddNumberToObject(o, "xScale", p.xScale);
		cJSON_AddNumberToObject(o, "yScale", p.yScale);
		cJSON_AddNumberToObject(o, "opacity", p.opacity);
		cJSON_AddNumberToObject(o, "blendMode", (int)p.blend);
		cJSON_AddItemToArray(a, o);
	}
	return a;
}

static double jd(const cJSON *o, const char *k, double def) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return cJSON_IsNumber(v) ? v->valuedouble : def;
}

void PictureManager::LoadSaveData(const cJSON *list) {
	pictures.clear();
	const cJSON *o;
	cJSON_ArrayForEach(o, list) {
		const cJSON *n = cJSON_GetObjectItemCaseSensitive(o, "name");
		const cJSON *pos = cJSON_GetObjectItemCaseSensitive(o, "pos");
		Vec2 p(pos ? (int)jd(pos, "X", 0) : 0, pos ? (int)jd(pos, "Y", 0) : 0);
		ShowPicture((int)jd(o, "id", 0), cJSON_IsString(n) ? n->valuestring : "",
		            cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "isPosCenter")), p, (float)jd(o, "xScale", 1),
		            (float)jd(o, "yScale", 1), (int)jd(o, "opacity", 255), (gfx::Blend)(int)jd(o, "blendMode", 0));
	}
}
