// collection_apps.cpp - see collection_apps.h.
#include "collection_apps.h"
#include "../wm_internal.h"
#include "../../core/json_file.h"
#include "../../core/loc.h"
#include "../../core/settings.h"
#include "../../../engine/audio.h"
#include "../../../engine/gm.h"
#include "../../../engine/util.h"
#include "../../../platform/input.h"
#include "../../../platform/sys.h"
#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace twm {

static int jint(const cJSON *o, const char *k, int def = 0) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	if (cJSON_IsNumber(v)) return (int)v->valuedouble;
	if (cJSON_IsString(v)) return atoi(v->valuestring);
	return def;
}
static float jfloat(const cJSON *o, const char *k) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return cJSON_IsNumber(v) ? (float)v->valuedouble : 0.0f;
}
static std::string jstr(const cJSON *o, const char *k, const std::string &def = "") {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return cJSON_IsString(v) ? v->valuestring : def;
}
static bool jbool(const cJSON *o, const char *k, bool def) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	if (cJSON_IsBool(v)) return cJSON_IsTrue(v);
	// The metadata writes most of them as strings ("singleSprite": "true"),
	// which Json.NET reads as bools.
	if (cJSON_IsString(v)) {
		if (strcasecmp(v->valuestring, "true") == 0) return true;
		if (strcasecmp(v->valuestring, "false") == 0) return false;
	}
	return def;
}
static bool contains(const std::vector<std::string> &v, const std::string &s) { return std::find(v.begin(), v.end(), s) != v.end(); }
static void pad_slider(Slider *s, uint32_t less, uint32_t more) {
	if (!s || !s->active) return;
	if (pad_repeat(less)) s->ArrowLeft();
	else if (pad_repeat(more)) s->ArrowRight();
}

// ===================================================================== TextBox

TextBox::TextBox(Rect a, int rh, int m) : area(a), rowHeight(rh), margin(m) {
	visible = (area.h - 2 * margin) / rowHeight;
	slider.reset(new Slider("", 0, 1, Vec2(area.x + area.w - 16, area.y), area.h, true, true));
	slider->onValueChanged = [this](int v) { offset = v; };
	slider->active = false;
}

void TextBox::SetText(const std::string &t) {
	int w = area.w - 2 * margin - 2;
	lines = wrap(t, w);
	if ((int)lines.size() > visible) {
		lines = wrap(t, w - 16);
		slider->max = (int)lines.size() - visible;
		slider->active = true;
	} else {
		slider->active = false;
	}
	offset = 0;
	slider->SetValue(0);
}

bool TextBox::Update(Vec2 pp, bool can) { return slider->Update(pp, can); }

void TextBox::Draw(const Theme &th, Vec2 pp, int alpha) const {
	Rect r = translated(area, pp);
	box(shrink(r, 1), Color(0, 0, 0, 179 * alpha / 255));
	for (int i = offset; i < offset + visible && i < (int)lines.size(); i++)
		text(Vec2(r.x + margin, r.y + margin + (i - offset) * rowHeight - 5 + 4), lines[i], Color::White().withA(alpha));
	slider->Draw(th, pp, alpha);
}

// ===================================================================== AchievementWindow (Badges)

AchievementWindow::AchievementWindow() {
	icon = "achievements";
	SetTitle(loc::twm("achievements_app_name"));
	SetContents(Vec2(300, 224));
	scroll.reset(new Slider("", 0, 1, Vec2(299, 1), 222, true, true));
	Refresh();
	AddButton(WinButtonType::Close, [this] {
		gm::clear_cache(gm::Achievements);
		if (onClose) onClose(this);
	});
	AddButton(WinButtonType::Minimize);
}

void AchievementWindow::Refresh() {
	const auto &got = unlocks().achievements;
	if (seenUnlocks == got.size()) return;
	seenUnlocks = got.size();
	items.clear();
	if (cJSON *j = parse_json_file("gamedata/twm/achievements_metadata.json")) {
		const cJSON *it;
		cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(j, "achievements")) {
			Item a;
			a.id = jstr(it, "id");
			if (a.id.empty()) continue;
			a.title = loc::achievement(a.id + ":title", jstr(it, "title", a.id));
			a.label = loc::achievement(a.id + ":description", jstr(it, "description"));
			a.unlocked = contains(got, a.id);
			items.push_back(a);
		}
		cJSON_Delete(j);
	}
	int mx = std::max((int)items.size() - 4, 0);
	scroll->max = std::max(mx, 1);
	scroll->active = mx > 0;
	if (scroll->Value() > mx) scroll->SetValue(mx);
	if (scroll->active) SetContents(Vec2(316, 224));
}

bool AchievementWindow::Update(bool consumed) {
	Refresh();
	scroll->Update(Vec2(pos.x + 2, pos.y + 26), !consumed && !minimized);
	return Window::Update(consumed);
}

void AchievementWindow::PadInput() { pad_slider(scroll.get(), input::BTN_UP, input::BTN_DOWN); }

void AchievementWindow::DrawContents(const Theme &th, Vec2 sp, int alpha) {
	box(Rect(sp.x, sp.y, contents.x, contents.y), th.Background(alpha));
	int first = scroll->active ? scroll->Value() : 0;
	int end = std::min((int)items.size(), first + 4);
	for (int i = first; i < end; i++) {
		const Item &a = items[i];
		int y = (i - first) * 58;
		std::string ic = a.unlocked ? "the_world_machine/achievements/" + util::lower(a.id) : "the_world_machine/achievements/unknown";
		Vec2 sz = tex_size(ic.c_str(), gm::Achievements);
		tex(ic.c_str(), sp + Vec2(4, y + (50 - sz.y) / 2), Rect(), Color::White().withA(a.unlocked ? alpha : alpha / 2), 2, gm::Achievements);
		Vec2 t(8 + sz.x - 2, 8 + y);
		text(sp + t + Vec2(0, -4), a.title, a.unlocked ? th.Primary(alpha) : th.Variant(alpha), font::Game, font::Mode::Normal);
		if (a.unlocked) line(sp + Vec2(t.x, 32 + y), a.label, th.Primary(alpha), contents.x - t.x - (scroll->active ? 18 : 2));
		if (i != end - 1) tex("the_world_machine/achievements/separator", sp + Vec2(0, y + 50), Rect(), th.Primary(alpha), 2, gm::Achievements);
	}
	scroll->Draw(th, sp, alpha);
}

// ===================================================================== ContactsWindow (Friends)

static const std::vector<ContactsWindow::Profile> &profile_info() {
	static std::vector<ContactsWindow::Profile> v;
	static bool loaded = false;
	if (loaded) return v;
	loaded = true;
	if (cJSON *j = parse_json_file("gamedata/twm/contacts_metadata.json")) {
		const cJSON *it;
		cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(j, "profiles")) {
			ContactsWindow::Profile p;
			p.id = jstr(it, "unlockId");
			if (p.id.empty()) continue;
			p.title = jstr(it, "title", p.id);
			p.subtitle = jstr(it, "subtitle");
			p.bgImage = jstr(it, "bgImage");
			p.walksprite = jstr(it, "walkspriteId");
			p.infoText = jstr(it, "infoText");
			p.theme = jstr(it, "uxColorTheme", "white");
			p.singleSprite = jbool(it, "singleSprite", false);
			p.walkAnimation = jbool(it, "walkAnimation", true);
			p.textDropShadow = jbool(it, "textDropShadow", true);
			const cJSON *f;
			cJSON_ArrayForEach(f, cJSON_GetObjectItemCaseSensitive(it, "facepics"))
				if (cJSON_IsString(f)) p.facepics.push_back(f->valuestring);
			bool dup = false;
			for (auto &q : v) dup |= q.id == p.id;
			if (!dup) v.push_back(p);
		}
		cJSON_Delete(j);
	}
	return v;
}

ContactsWindow::ContactsWindow() {
	icon = "contacts";
	SetTitle(loc::twm("contacts_app"));
	Rect place(4, 46, 240, 140);
	textbox.reset(new TextBox(place, 12, 4));
	SetContents(Vec2(424, 20 + place.h + 20 + 14 + 48 + 4 + 30));
	AddButton(WinButtonType::Close, [this] {
		gm::clear_cache(gm::CharacterProfile);
		if (onClose) onClose(this);
	});
	AddButton(WinButtonType::Minimize);
	chooser.reset(new Chooser(Vec2(4, 6), contents.x - 8, {}, ""));
	chooser->showBorder = false;
	chooser->dropShadow = true;
	chooser->labelFont = font::Game;
	chooser->labelFn = [](const std::string &id, const std::string &name) { return loc::contact(id + ":title", name); };
	chooser->onItemChange = [this](const std::string &id) { OnProfileChange(id); };
	face = IconButton("", Vec2(6, place.y + place.h + 4 + 2 + 30), [this] {
		if (!current || current->facepics.empty()) return;
		faceIndex = (faceIndex + 1) % (int)current->facepics.size();
		face.icon = "facepics/" + current->facepics[faceIndex];
	}, Vec2(48, 48));
	face.tint = false;
	face.borderWidth = 2;
	Refresh();
}

void ContactsWindow::Refresh() {
	const auto &got = unlocks().profiles;
	if (seenUnlocks == got.size()) return;
	seenUnlocks = got.size();
	std::vector<std::pair<std::string, std::string>> items;
	for (auto &p : profile_info())
		if (contains(got, p.id)) items.push_back({ p.id, p.title });
	std::string keep = chooser->Value();
	chooser->SetItems(items, keep);
	if (!current || keep != chooser->Value()) OnProfileChange(chooser->Value());
}

void ContactsWindow::OnProfileChange(const std::string &id) {
	current = nullptr;
	for (auto &p : profile_info())
		if (p.id == id) current = &p;
	if (!current) return;
	gm::clear_cache(gm::CharacterProfile);
	faceIndex = 0;
	face.icon = current->facepics.empty() ? "" : "facepics/" + current->facepics[0];
	textbox->SetText(loc::contact(id + ":infoText", current->infoText));
	Vec2 sz = tex_size(("npc/" + current->walksprite).c_str(), gm::CharacterProfile);
	spriteSize = current->singleSprite ? sz : Vec2(sz.x / 4, sz.y / 4);
}

bool ContactsWindow::Update(bool consumed) {
	Refresh();
	Vec2 pp(pos.x + 2, pos.y + 26);
	bool can = !consumed && !minimized;
	consumed |= chooser->Update(pp, can);
	consumed |= face.Update(pp, can && current);
	consumed |= textbox->Update(pp, can);
	if (++walkTimer > 48) walkTimer = 0;
	// Dragging the window by its contents.
	MouseCursor &c = cursor();
	Vec2 m = c.MousePos() - pp;
	if (!consumed && !minimized && m.x >= 0 && m.y >= 0 && m.x < contents.x && m.y < contents.y) {
		consumed = true;
		c.SetState(MouseCursor::Grabbable);
		if (held) {
			if (c.Held()) {
				c.SetState(MouseCursor::Holding);
				pos = pos + (c.MousePos() - heldMouse);
				heldMouse = c.MousePos();
			} else {
				held = false;
			}
		} else if (c.Clicked()) {
			held = true;
			heldMouse = c.MousePos();
			if (grabFocus) grabFocus(this);
		}
	} else {
		held = false;
	}
	return Window::Update(consumed) || consumed;
}

void ContactsWindow::PadInput() {
	if (pad_repeat(input::BTN_LEFT)) chooser->Step(-1);
	else if (pad_repeat(input::BTN_RIGHT)) chooser->Step(1);
	pad_slider(&textbox->Scroll(), input::BTN_UP, input::BTN_DOWN);
	if (input::pressed_raw(input::BTN_OK) && face.action) face.action();
}

void ContactsWindow::DrawContents(const Theme &th, Vec2 sp, int alpha) {
	box(Rect(sp.x, sp.y, contents.x, contents.y), th.Background(alpha));
	if (!current) {
		chooser->Draw(sp, th, alpha);
		return;
	}
	const Theme *pt = theme_by_id(current->theme);
	const Theme &t = pt ? *pt : th;
	tex(("the_world_machine/portraits/" + current->bgImage).c_str(), sp, Rect(), Color::White().withA(alpha), 2, gm::CharacterProfile);
	std::string sub = loc::contact(current->id + ":subtitle", current->subtitle);
	Vec2 v = sp + Vec2(2, 32);
	if (current->textDropShadow) line(v + Vec2(1, 1), sub, th.Background(alpha));
	line(v, sub, t.Primary(alpha));
	int y = contents.y - 4 - std::max(0, (52 - spriteSize.y) / 2) - spriteSize.y;
	int x = face.Position().x + 2 + 48 + 10;
	std::string sheet = "npc/" + current->walksprite;
	if (current->singleSprite) {
		tex(sheet.c_str(), sp + Vec2(x, y), Rect(), Color::White().withA(alpha), 2, gm::CharacterProfile);
	} else {
		for (int i = 0; i < 4; i++) {
			int fx = current->walkAnimation ? walkTimer / 12 % 4 : 0;   // 48/12 = 4: the PC wraps that to frame 0 (its sampler repeats)
			tex(sheet.c_str(), sp + Vec2(x + i * (spriteSize.x + 10), y),
			    Rect(fx * spriteSize.x, i * spriteSize.y, spriteSize.x, spriteSize.y), Color::White().withA(alpha), 2, gm::CharacterProfile);
		}
	}
	chooser->Draw(sp, t, alpha);
	face.Draw(sp, t, alpha);
	textbox->Draw(t, sp, alpha);
}

// ===================================================================== GalleryWindow

static const std::vector<GalleryWindow::Cg> &cg_info() {
	static std::vector<GalleryWindow::Cg> v;
	static bool loaded = false;
	if (loaded) return v;
	loaded = true;
	if (cJSON *j = parse_json_file("gamedata/twm/cg_unlocks.json")) {
		const cJSON *it;
		cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(j, "cgList")) {
			GalleryWindow::Cg c;
			c.imageId = jstr(it, "imageId");
			if (c.imageId.empty()) continue;
			c.displayName = jstr(it, "displayName", c.imageId);
			c.order = jint(it, "displayOrder");
			c.scale = jint(it, "scale", 2) == 1 ? 1 : 2;
			c.scrollX = jfloat(it, "scrollbarX");
			c.scrollY = jfloat(it, "scrollbarY");
			c.oversize = jbool(it, "oversize", false);
			c.fitHorizontal = jbool(it, "fitHorizontal", false);
			c.blend = jstr(it, "fullscreenBlendMode");
			if (const cJSON *w = cJSON_GetObjectItemCaseSensitive(it, "overrideWindowSize")) c.windowSize = Vec2(jint(w, "X"), jint(w, "Y"));
			const cJSON *l;
			cJSON_ArrayForEach(l, cJSON_GetObjectItemCaseSensitive(it, "additionalLayers"))
				if (cJSON_IsString(l)) c.layers.push_back(l->valuestring);
			v.push_back(c);
		}
		cJSON_Delete(j);
	}
	return v;
}

static const Vec2 CG_DISPLAY(320, 240);
static const Vec2 CHOOSER_POS(40, 244);

GalleryWindow::GalleryWindow() {
	glitched = desktop_grid().inSolstice && util::random(1, 16) == 16;
	icon = "gallery";
	SetContents(CG_DISPLAY + Vec2(0, 24));
	AddButton(WinButtonType::Close, [this] {
		gm::clear_cache(gm::GalleryWindow);
		if (onClose) onClose(this);
	});
	AddButton(WinButtonType::Maximize, [this] { ToggleMax(); });
	AddButton(WinButtonType::Minimize);
	chooser.reset(new Chooser(CHOOSER_POS, 240, {}, ""));
	chooser->labelFn = [this](const std::string &id, const std::string &name) { return glitched ? std::string("???") : loc::cg_name(id, name); };
	chooser->onItemChange = [this](const std::string &) { Config(); };
	exitFull.reset(new WindowButton(WinButtonType::Maximize, Vec2(0, 0), [this] { ToggleMax(); }));
	hScroll.reset(new Slider("", 0, 100, Vec2(0, contents.y + 24), 100, true, false));
	vScroll.reset(new Slider("", 0, 100, Vec2(contents.x, 0), 100, true, true));
	hScroll->dragIncrement = vScroll->dragIncrement = 1;
	Refresh();
}

GalleryWindow::~GalleryWindow() { gm::clear_cache(gm::GalleryWindow); }

void GalleryWindow::Refresh() {
	const auto &got = unlocks().cgs;
	if (seenUnlocks == got.size()) return;
	if (glitched && seenUnlocks != (size_t)-1) return;   // the glitched one stays as it opened
	seenUnlocks = got.size();
	list.clear();
	for (auto &c : cg_info()) {
		if (!contains(got, c.imageId)) continue;
		if (glitched && !list.empty()) break;
		if (c.layers.size() <= 1) {
			list.push_back(c);
		} else {
			// One entry per extra layer.
			for (size_t i = 0; i < c.layers.size(); i++) {
				Cg e = c;
				e.layers = { c.layers[i] };
				e.order = c.order + (int)i;
				list.push_back(e);
			}
		}
	}
	std::stable_sort(list.begin(), list.end(), [](const Cg &a, const Cg &b) { return a.order < b.order; });
	std::vector<std::pair<std::string, std::string>> items;
	for (auto &c : list) items.push_back({ c.imageId, c.displayName });
	std::string keep = chooser->Value();
	chooser->SetItems(items, keep);
	chooser->disabled = list.empty();
	Config();
}

const GalleryWindow::Cg *GalleryWindow::Current() const {
	int i = chooser->Index();
	return i >= 0 && i < (int)list.size() ? &list[i] : nullptr;
}

void GalleryWindow::ToggleMax() {
	Window::ToggleMaximize();
	if (maximized) {
		rememberedPos = pos;
		pos = Vec2(0, 0);
	} else {
		pos = rememberedPos;
	}
	Config();
}

Vec2 GalleryWindow::FullscreenSpace(const Cg *cg) const {
	Vec2 v(contents.x, contents.y - 40);
	if (cg && cg->fitHorizontal) v.x -= 16;
	return Vec2(v.x * 2, v.y * 2);
}

void GalleryWindow::BestFit(const Cg *cg, Vec2 &img, Vec2 &src, Vec2 &dst, float &scale) const {
	Vec2 fs = FullscreenSpace(cg);
	float fa = (float)fs.x / fs.y;
	img = cg ? tex_size(("pictures/" + cg->imageId).c_str(), gm::GalleryWindow) : CG_DISPLAY;
	if (img.x <= 0 || img.y <= 0) img = CG_DISPLAY;
	float ia = (float)img.x / img.y;
	if (!cg || cg->fitHorizontal || (fa < 1.33f && ia < 1.4f)) {
		scale = (float)fs.x / img.x;
		src = Vec2(img.x, (int)std::min((float)img.y, fs.y / scale));
	} else {
		scale = (float)fs.y / img.y;
		src = Vec2((int)std::min((float)img.x, fs.x / scale), img.y);
	}
	dst = Vec2((int)(img.x * scale), (int)(img.y * scale));
}

Vec2 GalleryWindow::ScrollbarsFor(const Cg *cg, Vec2 imageSize, Vec2 viewport) {
	Vec2 extra;
	if (cg && imageSize.x > viewport.x) {
		hScroll->active = true;
		hScroll->max = imageSize.x - viewport.x;
		hScroll->SetValue((int)(cg->scrollX * hScroll->max));
		hScroll->increment = std::max(1, hScroll->max / 10);
		extra.y += 16;
	} else {
		hScroll->active = false;
		hScroll->SetValue(0);
	}
	if (cg && imageSize.y > viewport.y) {
		vScroll->active = true;
		vScroll->max = imageSize.y - viewport.y;
		vScroll->SetValue((int)(cg->scrollY * vScroll->max));
		vScroll->increment = std::max(1, vScroll->max / 10);
		extra.x += 16;
	} else {
		vScroll->active = false;
		vScroll->SetValue(0);
	}
	return extra;
}

void GalleryWindow::PlaceScrollbars() {
	if (maximized) {
		chooser->SetPosition(Vec2((contents.x - 240) / 2, contents.y - 40 + 4));
		vScroll->SetPlacement(Vec2(contents.x - 16, (contents.y - 320) / 2), 320);
		hScroll->SetPlacement(Vec2((contents.x - 320) / 2, contents.y - 16), 320);
		exitFull->SetPosition(Vec2(250, 0) + Vec2((contents.x - 240) / 2, contents.y - 40 + 4));
	} else {
		int hy = contents.y - 16;
		int vx = contents.x - 16;
		vScroll->SetPlacement(Vec2(vx, 0), hScroll->active ? hy : contents.y);
		hScroll->SetPlacement(Vec2(0, hy), vScroll->active ? vx : contents.x);
		chooser->SetPosition(CHOOSER_POS);
	}
	// SetPlacement keeps the values but rebuilds the arrows.
}

void GalleryWindow::Config() {
	const Cg *cg = Current();
	Vec2 img = CG_DISPLAY;
	if (cg) {
		img = tex_size(("pictures/" + cg->imageId).c_str(), gm::GalleryWindow);
		std::string name = glitched ? std::string("???") : loc::cg_name(cg->imageId, cg->displayName);
		SetTitle(loc::twm("gallery_app") + " - " + name);
	} else {
		SetTitle(loc::twm("gallery_app"));
		hScroll->active = vScroll->active = false;
	}
	if (!maximized) {
		Vec2 view = CG_DISPLAY;
		if (cg) {
			if (cg->windowSize.x > 0) view = cg->windowSize;
			int div = 2 / cg->scale;
			img = Vec2(img.x / div, img.y / div);
			if (cg->oversize) img = CG_DISPLAY;
		}
		Vec2 extra = ScrollbarsFor(cg, img, view);
		SetContents(view + Vec2(0, 24) + extra);
	} else {
		SetContents(screen());
		Vec2 im, src, dst;
		float sc;
		BestFit(cg, im, src, dst, sc);
		ScrollbarsFor(cg, im, src);
	}
	PlaceScrollbars();
}

bool GalleryWindow::Update(bool consumed) {
	Refresh();
	Vec2 pp;
	if (maximized) {
		consumed = false;
		pp = Vec2(0, 0);
		exitFull->Update(Vec2(0, 0), true);
	} else {
		pp = Vec2(pos.x + 2, pos.y + 26);
	}
	bool can = !consumed && !minimized;
	chooser->Update(pp, can);
	hScroll->Update(pp, can);
	vScroll->Update(pp, can);
	return Window::Update(consumed);
}

void GalleryWindow::PadInput() {
	if (pad_repeat(input::BTN_LEFT)) chooser->Step(-1);
	else if (pad_repeat(input::BTN_RIGHT)) chooser->Step(1);
	pad_slider(vScroll.get(), input::BTN_UP, input::BTN_DOWN);
	if (input::pressed_raw(input::BTN_OK)) ToggleMax();
}

void GalleryWindow::Draw(const Theme &th) {
	if (maximized) DrawContents(th, Vec2(0, 0), 255);
	else Window::Draw(th);
}

void GalleryWindow::DrawContents(const Theme &th, Vec2 sp, int alpha) {
	box(Rect(sp.x, sp.y, contents.x, contents.y), th.Background(alpha));
	const Cg *cg = Current();
	if (cg) {
		std::string name = glitched ? "pictures/ohno" : "pictures/" + cg->imageId;
		const gfx::Texture *t = gm::texture(name.c_str(), gm::GalleryWindow);
		float a = alpha / 255.0f;
		if (t && cg->oversize && !maximized) {
			float s = (float)CG_DISPLAY.y / t->h * 2.0f;
			gm::blit_scaled(t, Vec2(sp.x * 2, sp.y * 2), Rect(0, 0, (int)t->w, (int)t->h), s, s, 1.0f);
		} else if (t && maximized) {
			Vec2 im, src, dst;
			float sc;
			BestFit(cg, im, src, dst, sc);
			Vec2 fs = FullscreenSpace(cg);
			Vec2 at(std::max(0, (fs.x - dst.x) / 2), std::max(0, (fs.y - dst.y) / 2));
			Rect sr(hScroll->Value(), vScroll->Value(), src.x, src.y);
			gm::blit_scaled(t, at, sr, sc, sc, a);
			for (auto &l : cg->layers)
				if (const gfx::Texture *lt = gm::texture(("pictures/" + l).c_str(), gm::GalleryWindow)) gm::blit_scaled(lt, at, sr, sc, sc, a);
		} else if (t) {
			Vec2 view = cg->windowSize.x > 0 ? cg->windowSize : CG_DISPLAY;
			int div = 2 / cg->scale;
			Vec2 at(sp.x * div, sp.y * div);
			Rect sr(hScroll->Value(), vScroll->Value(), view.x * div, view.y * div);
			gm::blit(t, at, sr, a, 0, gfx::Blend::Normal, cg->scale);
			for (auto &l : cg->layers)
				if (const gfx::Texture *lt = gm::texture(("pictures/" + l).c_str(), gm::GalleryWindow)) gm::blit(lt, at, sr, a, 0, gfx::Blend::Normal, cg->scale);
		}
	} else {
		text_centered(sp + Vec2(contents.x / 2, contents.y / 2 - 8), loc::twm("gallery_empty_message"), th.Primary(alpha));
	}
	hScroll->Draw(th, sp, alpha);
	vScroll->Draw(th, sp, alpha);
	chooser->Draw(sp, th, alpha);
	if (maximized) exitFull->Draw(sp, th, alpha);
}

// ===================================================================== JukeboxWindow (Music Box)

static std::string g_lastTrack;   // JukeboxWindow.lastTrack (static: kept between openings)

JukeboxWindow::JukeboxWindow() {
	glitched = desktop_grid().inSolstice && util::random(1, 16) == 16;
	if (glitched) g_lastTrack.clear();
	icon = "jukebox";
	SetTitle(loc::twm("musicbox_appname"));
	SetContents(Vec2(320, 220));
	AddButton(WinButtonType::Close);
	AddButton(WinButtonType::Minimize);
	onClose = [this](Window *) { Stop(); };
	int w = (contents.x - 18) / 2;
	speed.reset(new Slider(loc::twm("musicbox_playback_speed_slider_label"), 50, 150, Vec2(6, 144), w, true, false, "%"));
	speed->increment = 5;
	speed->SetValue(100);
	speed->onValueChanged = [this](int) { if (!glitched && playing) PlayCurrent(); };
	volume.reset(new Slider(loc::twm("musicbox_volume_slider_label"), 0, 100, Vec2(12 + w, 144), w, true, false, "%"));
	volume->increment = 5;
	volume->SetValue(100);
	volume->onValueChanged = [this](int) { if (!glitched && playing) PlayCurrent(); };
	chooser.reset(new Chooser(Vec2(6, 122), contents.x - 12, {}, ""));
	chooser->labelFn = [](const std::string &id, const std::string &name) { return loc::music(id, name); };
	chooser->onItemChange = [this](const std::string &t) {
		g_lastTrack = t;
		SwitchTrack(t);
	};
	Vec2 b((contents.x - 12 - 96) / 2, 182);
	buttons.emplace_back(new IconButton("the_world_machine/window_icons/stop", b, [this] { Stop(); }));
	buttons.emplace_back(new IconButton("the_world_machine/window_icons/play", b + Vec2(38, 0), [this] { PlayPause(); }));
	buttons.emplace_back(new IconButton("the_world_machine/window_icons/restart_track", b + Vec2(76, 0), [this] { Restart(); }));
	Refresh();
	if (g_lastTrack.empty()) g_lastTrack = chooser->Value();
	SwitchTrack(g_lastTrack);
}

void JukeboxWindow::Refresh() {
	const auto &got = unlocks().tracks;
	if (seenUnlocks == got.size()) return;
	if (glitched && seenUnlocks != (size_t)-1) return;
	seenUnlocks = got.size();
	tracks.clear();
	if (cJSON *j = parse_json_file("gamedata/music/tracklist.json")) {
		const cJSON *it;
		cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(j, "tracklist")) {
			Track t;
			t.id = jstr(it, "trackId");
			if (t.id.empty()) continue;
			if (glitched ? t.id != "MyBurdenIsLight" : !contains(got, t.id)) continue;
			t.name = jstr(it, "displayName", t.id);
			t.number = jint(it, "trackNumber");
			t.speed = jint(it, "defaultSpeed", 100);
			tracks.push_back(t);
		}
		cJSON_Delete(j);
	}
	std::stable_sort(tracks.begin(), tracks.end(), [](const Track &a, const Track &b) { return a.number < b.number; });
	std::vector<std::pair<std::string, std::string>> items;
	for (auto &t : tracks) items.push_back({ t.id, t.name });
	chooser->SetItems(items, g_lastTrack);
	if (g_lastTrack.empty() && !tracks.empty()) {
		g_lastTrack = chooser->Value();
		SwitchTrack(g_lastTrack);
	}
}

void JukeboxWindow::PickPlayState() {
	static const NikoState states[3] = { JAMMING, BOPPING, BLINKING };
	niko = states[util::random(0, 2)];
}

void JukeboxWindow::Stop() {
	audio::play_song("", 0, 1, 1);
	playing = paused = false;
	niko = SLEEP;
	ResetNiko();
}

void JukeboxWindow::PlayPause() {
	if (playing && !paused) {
		audio::set_song_paused(true);
		paused = true;
		niko = SLEEP;
	} else {
		if (paused) {
			audio::set_song_paused(false);
			paused = false;
		} else {
			PlayCurrent();
		}
		PickPlayState();
	}
	ResetNiko();
}

void JukeboxWindow::Restart() {
	bool was = playing && !paused;
	Stop();
	if (was) {
		PlayCurrent();
		PickPlayState();
	}
	ResetNiko();
}

void JukeboxWindow::PlayCurrent() {
	for (auto &t : tracks) {
		if (t.id != g_lastTrack) continue;
		if (glitched) audio::play_song("MyBurdenIsDead", 0, 1, 1);
		else audio::play_song(t.id.c_str(), 0, volume->Value() / 100.0f, speed->Value() / 100.0f);
		playing = true;
		return;
	}
}

void JukeboxWindow::SwitchTrack(const std::string &id) {
	for (auto &t : tracks) {
		if (t.id != id) continue;
		speed->SetValue(t.speed);
		if (playing) {
			PlayCurrent();
			if (paused) audio::set_song_paused(true);
		}
		return;
	}
	Stop();
}

bool JukeboxWindow::Update(bool consumed) {
	Refresh();
	bool can = !consumed && !minimized;
	Vec2 pp(pos.x + 2, pos.y + 26);
	speed->Update(pp, can);
	volume->Update(pp, can);
	chooser->Update(pp, can);
	buttons[1]->icon = !playing || paused ? "the_world_machine/window_icons/play" : "the_world_machine/window_icons/pause";
	for (auto &b : buttons) {
		b->Update(pp, can);
		b->disabled = tracks.empty();
	}
	float sp = speed->Value() / 100.0f;
	if (playing && !paused) {
		gramTimer += sp;
		if (gramTimer >= 5) {
			gramTimer = 0;
			if (++gramIndex >= 8) gramIndex = 0;
		}
		noteTimer += sp;
		if (noteTimer >= 30) {
			noteTimer = 0;
			if (!glitched) {
				// MusicNote: drifts up-left on a wobble, a random hue, fading out.
				Note n;
				n.frame = util::random(0, 2);
				n.pos = Vec2(152 * 80, 47 * 80);
				n.vel = Vec2(util::random(-100, -10), util::random(-100, -75));
				n.xw = util::random(0, 6283) / 1000.0f;
				n.yw = util::random(0, 6283) / 1000.0f;
				n.ws = 0.075f + util::random(0, 35) / 1000.0f;
				n.pos.x -= (int)(sinf(n.xw) * 640);
				n.pos.y -= (int)(sinf(n.yw) * 640);
				float h = util::random(0, 6283) / 1000.0f, cs = cosf(h), sn = sinf(h), k = (1 - cs) / 3, q = 0.57735026f * sn;
				float r = 1, g = 0.75f, b = 0.75f;
				float R = r * (cs + k) + g * (k - q) + b * (k + q), G = r * (k + q) + g * (cs + k) + b * (k - q), B = r * (k - q) + g * (k + q) + b * (cs + k);
				auto cl = [](float v) { return (int)(std::max(0.0f, std::min(1.0f, v)) * 255); };
				n.color = Color(cl(R), cl(G), cl(B));
				notes.push_back(n);
			}
		}
	}
	// Niko's little animation.
	static const int blinkT[4] = { 110, 10, 50, 10 }, blinkI[4] = { 0, 1, 2, 1 }, bopI[4] = { 6, 5, 4, 5 };
	switch (niko) {
	case SLEEP:
		if (++nikoTimer >= 60) { nikoTimer = 0; nikoIndex = (nikoIndex + 1) % 2; nikoFrame = nikoIndex; }
		break;
	case JAMMING:
		nikoTimer += sp;
		if (nikoTimer >= 15) { nikoTimer = 0; nikoIndex = (nikoIndex + 1) % 4; nikoFrame = nikoIndex; }
		break;
	case BLINKING:
		nikoTimer += sp;
		if (nikoTimer >= blinkT[nikoIndex]) { nikoTimer = 0; nikoIndex = (nikoIndex + 1) % 4; nikoFrame = blinkI[nikoIndex]; }
		break;
	case BOPPING:
		nikoTimer += sp;
		if (nikoTimer >= 10) { nikoTimer = 0; nikoIndex = (nikoIndex + 1) % 4; nikoFrame = bopI[nikoIndex]; }
		break;
	}
	for (auto &n : notes) {
		n.pos = n.pos + n.vel;
		n.xw += n.ws;
		if (n.xw >= 6.2831853f) n.xw -= 6.2831853f;
		n.yw += n.ws;
		if (n.yw >= 6.2831853f) n.yw -= 6.2831853f;
		n.alpha = std::max(0, n.alpha - 2);
	}
	notes.erase(std::remove_if(notes.begin(), notes.end(), [](const Note &n) { return n.alpha <= 0; }), notes.end());
	return Window::Update(consumed);
}

void JukeboxWindow::PadInput() {
	// Left/Right: track; Up/Down: speed; Cross: play/pause; Square: stop; Triangle: restart.
	if (pad_repeat(input::BTN_LEFT)) chooser->Step(-1);
	else if (pad_repeat(input::BTN_RIGHT)) chooser->Step(1);
	pad_slider(speed.get(), input::BTN_DOWN, input::BTN_UP);
	if (tracks.empty()) return;
	if (input::pressed_raw(input::BTN_OK)) PlayPause();
	else if (input::pressed_raw(input::BTN_RUN)) Stop();
	else if (input::pressed_raw(input::BTN_INVENTORY)) Restart();
}

void JukeboxWindow::DrawContents(const Theme &th, Vec2 sp, int alpha) {
	Color c = th.Primary(alpha), bg = th.Background(alpha);
	box(Rect(sp.x, sp.y, contents.x, contents.y), bg);
	static const int gramFrames[8] = { 0, 1, 2, 3, 4, 3, 2, 1 };
	Vec2 v((sp.x + 110) * 2, sp.y * 2);
	char name[64];
	snprintf(name, sizeof(name), "the_world_machine/jukebox/grammaphone%d", gramFrames[gramIndex] + 1);
	float a = alpha / 255.0f;
	if (const gfx::Texture *t = gm::texture(name, gm::TheWorldMachine)) gm::blit_scaled(t, v, Rect(0, 0, 50, 50), 4, 4, a);
	if (!glitched) {
		const char *nt = niko == SLEEP ? "the_world_machine/jukebox/niko_sleep"
		               : niko == JAMMING ? "the_world_machine/jukebox/niko_jam" : "the_world_machine/jukebox/niko_stand";
		if (const gfx::Texture *t = gm::texture(nt, gm::TheWorldMachine)) gm::blit_scaled(t, v + Vec2(160, 0), Rect(50 * nikoFrame, 0, 50, 50), 4, 4, a);
		if (const gfx::Texture *t = gm::texture("the_world_machine/jukebox/small_sun", gm::TheWorldMachine))
			gm::blit_scaled(t, v + Vec2(-80, 128), Rect(0, 0, 18, 18), 4, 4, a);
	}
	speed->Draw(th, sp, alpha);
	volume->Draw(th, sp, alpha);
	chooser->Draw(sp, th, alpha);
	for (auto &b : buttons) b->Draw(sp, th, alpha);
	line(sp + Vec2(contents.x / 2, 108), loc::twm("musicbox_current_track_chooser_label"), c, -1, true);
	for (auto &n : notes) {
		Vec2 p(n.pos.x + (int)(sinf(n.xw) * 640), n.pos.y + (int)(sinf(n.yw) * 640));
		p = Vec2(p.x / 80 + sp.x - 8, p.y / 80 + sp.y - 8);
		Rect src(n.frame * 16, 0, 16, 16);
		tex("the_world_machine/jukebox/notes", p + Vec2(1, 1), src, Color(0, 0, 0, n.alpha * alpha / 255));
		tex("the_world_machine/jukebox/notes", p, src, n.color.withA(n.alpha * alpha / 255));
	}
}

// ===================================================================== timers / TimerWindow

static int g_systemTimer, g_runTimer;
static bool g_runActive;

void tick_timers() {
	if (g_systemTimer <= 999999999) g_systemTimer++;
	if (g_runActive && g_runTimer <= 999999999) g_runTimer++;
}
void start_playthrough_timer() { g_runActive = true; }
void stop_playthrough_timer() { g_runActive = false; }
void reset_playthrough_timer() { g_runActive = false; g_runTimer = 0; }

TimerWindow::TimerWindow() {
	icon = "timer";
	SetTitle(loc::twm("timer_app_name"));
	SetContents(Vec2(120, 86));
	AddButton(WinButtonType::Close);
	AddButton(WinButtonType::Minimize);
	help = IconButton("the_world_machine/window_icons/question", Vec2(4, 68), [this] {
		ShowModal(MODAL_INFO, "timer_explanation_1", [this](int) {
			ShowModal(MODAL_INFO, "timer_explanation_2", [this](int) { ShowModal(MODAL_INFO, "timer_explanation_3"); });
		});
	});
}

bool TimerWindow::Update(bool consumed) {
	help.Update(Vec2(pos.x + 2, pos.y + 26), !consumed && !IsModalOpen());
	return Window::Update(consumed);
}

void TimerWindow::DrawTimer(Vec2 p, int frames, const Theme &th, int alpha, bool flash, bool active) const {
	Color bg = th.Background(alpha), tc = th.Primary(alpha), dim = th.Variant(alpha);
	box(Rect(p.x, p.y, 116, 16), tc);
	box(Rect(p.x + 1, p.y + 1, 114, 14), bg);
	int s = frames / 60, h = s / 3600, m = s / 60 % 60, sec = s % 60, ms = (int)(frames * 1000.0f / 60.0f) % 1000;
	if (h > 999) { h = 999; m = 59; sec = 59; ms = 999; }
	char buf[32];
	snprintf(buf, sizeof(buf), "%03d:%02d:%02d.%03d", h, m, sec, ms);
	// Right-aligned, the separators flashing.
	int x = p.x + 116 - 2;
	Color num = active ? tc : dim, sep = flash || !active ? dim : tc;
	for (int i = (int)strlen(buf) - 1; i >= 0; i--) {
		char ch[2] = { buf[i], 0 };
		int w = text_w(ch);
		x -= w;
		text(Vec2(x, p.y - 1), ch, (buf[i] == ':' || buf[i] == '.') ? sep : num);
	}
}

void TimerWindow::DrawContents(const Theme &th, Vec2 sp, int alpha) {
	box(Rect(sp.x, sp.y, contents.x, contents.y), th.Background(alpha));
	bool flash = g_systemTimer / 20 % 2 == 1;
	line(sp + Vec2(2, 4), loc::twm("timer_system_active_time"), th.Primary(alpha), 116);
	DrawTimer(sp + Vec2(2, 18), g_systemTimer, th, alpha, flash, true);
	line(sp + Vec2(2, 36), loc::twm("timer_current_playthrough"), th.Primary(alpha), 116);
	DrawTimer(sp + Vec2(2, 50), g_runTimer, th, alpha, flash, g_runActive);
	help.Draw(sp, th, alpha);
}

// ===================================================================== ControlsWindow

ControlsWindow::ControlsWindow() {
	icon = "controls";
	SetTitle(loc::twm("controls_app_name"));
	SetContents(Vec2(450, 202));
	AddButton(WinButtonType::Close);
	AddButton(WinButtonType::Minimize);
	moveDead.reset(new Slider(loc::twm("controls_app_move_deadzone_slider_label"), 0, 50, Vec2(246, 8), 200));
	moveDead->increment = 2;
	moveDead->SetValue(settings().moveDeadZone);
	moveDead->onValueChanged = [](int v) { settings().moveDeadZone = v; settings().ApplyInput(); };
	mouseDead.reset(new Slider(loc::twm("controls_app_mouse_deadzone_slider_label"), 0, 50, Vec2(246, 48), 200));
	mouseDead->increment = 2;
	mouseDead->SetValue(settings().mouseDeadZone);
	mouseDead->onValueChanged = [](int v) { settings().mouseDeadZone = v; settings().ApplyInput(); };
	mouseSpeed.reset(new Slider(loc::twm("controls_app_mouse_speed_slider_label"), 2, 8, Vec2(246, 88), 200));
	mouseSpeed->SetValue(settings().cursorSpeed);
	mouseSpeed->onValueChanged = [](int v) { settings().cursorSpeed = v; settings().ApplyInput(); };
	defaults.reset(new TextButton(loc::twm("controls_app_default_controls_button_label"), Vec2(246, 178), [this] {
		ShowModal(MODAL_YESNO, "controls_app_default_controls_confirm_popup", [this](int r) {
			if (r != RESP_YES) return;
			Settings d;
			settings().moveDeadZone = d.moveDeadZone;
			settings().mouseDeadZone = d.mouseDeadZone;
			settings().cursorSpeed = d.cursorSpeed;
			settings().ApplyInput();
			settings().Save();
			moveDead->SetValue(d.moveDeadZone);
			mouseDead->SetValue(d.mouseDeadZone);
			mouseSpeed->SetValue(d.cursorSpeed);
		});
	}, 200));
	onClose = [](Window *) { settings().Save(); };
}

bool ControlsWindow::Update(bool consumed) {
	if (!IsModalOpen()) {
		Vec2 pp(pos.x + 2, pos.y + 26);
		bool can = !consumed && !minimized;
		moveDead->Update(pp, can);
		mouseDead->Update(pp, can);
		mouseSpeed->Update(pp, can);
		defaults->Update(pp, can);
	}
	return Window::Update(consumed);
}

void ControlsWindow::PadInput() {
	// Up/Down: pick a slider or the button; Left/Right: change; Cross: press.
	if (pad_repeat(input::BTN_UP)) { sel = (sel + 3) % 4; padUsed = true; }
	else if (pad_repeat(input::BTN_DOWN)) { sel = (sel + 1) % 4; padUsed = true; }
	Slider *s = sel == 0 ? moveDead.get() : sel == 1 ? mouseDead.get() : sel == 2 ? mouseSpeed.get() : nullptr;
	if (s) {
		if (pad_repeat(input::BTN_LEFT)) { s->ArrowLeft(); padUsed = true; }
		else if (pad_repeat(input::BTN_RIGHT)) { s->ArrowRight(); padUsed = true; }
	} else if (input::pressed_raw(input::BTN_OK) && defaults->action) {
		padUsed = true;
		defaults->action();
	}
}

void ControlsWindow::DrawContents(const Theme &th, Vec2 sp, int alpha) {
	Color c = th.Primary(alpha), dim = th.Variant(alpha);
	box(Rect(sp.x, sp.y, contents.x, contents.y), th.Background(alpha));
	// The pad: buttons can't be changed in this version.
	static const struct { const char *glyph, *key; } rows[] = {
		{ "@MOVE", "controls_button_move" },       { "@OK", "controls_button_ok" },
		{ "@CANCEL", "controls_button_cancel" },   { "@RUN", "controls_button_run" },
		{ "@INVENTORY", "controls_button_inventory" }, { "@MAINMENU", "controls_button_mainmenu" },
		{ "@MASHTEXT", "controls_button_mashtext" }, { "@MOVEMOUSE", "controls_button_movemouse" },
		{ "@CLICK", "controls_button_mousebutton" },
	};
	int y = 4;
	for (auto &r : rows) {
		text(sp + Vec2(8, y - 4), r.glyph, c, font::OS, font::Mode::Normal);
		line(sp + Vec2(44, y), loc::twm(r.key), c, 180);
		y += 22;
	}
	box(Rect(sp.x + 236, sp.y + 4, 1, contents.y - 8), dim);
	moveDead->Draw(th, sp, alpha);
	mouseDead->Draw(th, sp, alpha);
	mouseSpeed->Draw(th, sp, alpha);
	defaults->Draw(sp, th, alpha);
	if (padUsed) {
		Rect r = sel == 3 ? Rect(246, 178, 200, 16) : Rect(246, 8 + sel * 40, 200, 32);
		r = translated(Rect(r.x - 3, r.y - 3, r.w + 6, r.h + 6), sp);
		box(Rect(r.x, r.y, r.w, 1), c);
		box(Rect(r.x, r.y + r.h - 1, r.w, 1), c);
		box(Rect(r.x, r.y, 1, r.h), c);
		box(Rect(r.x + r.w - 1, r.y, 1, r.h), c);
	}
}

} // namespace twm
