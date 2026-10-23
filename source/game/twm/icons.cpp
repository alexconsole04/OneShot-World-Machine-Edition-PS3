#include "icons.h"
#include "wm_internal.h"
#include "../loc.h"
#include "../json_file.h"
#include "../../engine/gm.h"
#include "../../platform/sys.h"
#include <algorithm>
#include <set>

namespace twm {


// ===================================================================== FileIcon

FileIcon::FileIcon(std::shared_ptr<Node> f) : file(f) {}

bool FileIcon::IsWallpaper() const { return !file->IsFolder() && static_cast<File *>(file.get())->program == PROG_WALLPAPER; }
bool FileIcon::IsTheme() const { return !file->IsFolder() && static_cast<File *>(file.get())->program == PROG_THEME; }

void FileIcon::Layout() {
	// processFileName: the name on one or two lines, each at most 76 wide.
	if (layoutGen == loc::generation()) return;
	layoutGen = loc::generation();
	std::string name;
	if (IsWallpaper()) name = loc::wallpaper_name(file->name, file->name);
	else if (IsTheme()) {
		const Theme *t = theme_by_id(file->name);
		name = loc::theme_name(file->name, t ? t->displayName : file->name);
	} else name = loc::twm(file->name.c_str());
	text2.clear();
	std::vector<std::string> lines = wrap(name, 76);
	if (lines.size() <= 1) {
		text1 = truncate(name, 76);
		int w = text_w(text1);
		r1 = Rect((76 - w) / 2 + 4, 36, w + 2, 12);
		textHeight = 12;
	} else {
		text1 = truncate(lines[0], 76);
		while (!text1.empty() && text1.back() == ' ') text1.pop_back();
		int w = text_w(text1);
		r1 = Rect((76 - w) / 2 + 4, 36, w + 2, 12);
		text2 = truncate(lines[1], 76);
		w = text_w(text2);
		r2 = Rect((76 - w) / 2 + 4, 48, w + 2, 12);
		textHeight = 24;
	}
}

void FileIcon::Update() { Layout(); }

Rect FileIcon::ClickArea(Vec2 p) {
	Layout();
	return Rect(p.x + 2, p.y + 2, 80, 36 + textHeight + 2 + 4);
}

void FileIcon::Draw(const Theme &th0, Vec2 p, bool focus, bool canHover, float alpha) {
	Layout();
	const Theme *thp = &th0;
	if (IsTheme()) {
		const File *f = static_cast<const File *>(file.get());
		if (!f->args.empty())
			if (const Theme *t = theme_by_id(f->args[0])) thp = t;   // a theme's icon wears that theme
	}
	const Theme &th = *thp;
	Color bg = th.Background(), frame = focus ? th.Primary() : th.Variant(), fg = th.Primary(), shadow = th.Background();
	if (alpha != 1.0f) fg.a = frame.a = bg.a = (uint8_t)(255 * alpha);
	Rect area = ClickArea(p);
	if (canHover && in_rect(area, cursor().MousePos())) {
		focus = true;
		cursor().SetState(MouseCursor::Clickable);
	}
	if (focus) {
		frame.a /= 2;
		bg.a /= 2;
		box(area, frame);
		box(shrink(area, 2), bg);
	}
	std::string path = "the_world_machine/desktop_icons/" + file->icon;
	if (IsWallpaper()) {
		// Thumbnails are drawn at window scale.
		Vec2 v((p.x + 21) * 2, (p.y + 10) * 2);
		Color w = Color::White().withA(fg.a);
		tex(path.c_str(), v + Vec2(2, 2), Rect(), shadow, 1, gm::Game);
		tex(path.c_str(), v, Rect(), w, 1, gm::Game);
	} else {
		tex(path.c_str(), Vec2(p.x + 27, p.y + 7), Rect(), shadow);
		tex(path.c_str(), Vec2(p.x + 26, p.y + 6), Rect(), fg);
	}
	Rect b1(r1.x + p.x - 1, r1.y + p.y + 4, r1.w, r1.h);
	box(b1, Color(0, 0, 0, fg.a));
	line(Vec2(b1.x - 1, b1.y), text1, fg);
	if (!text2.empty()) {
		Rect b2(r2.x + p.x - 1, r2.y + p.y + 4, r2.w, r2.h);
		box(b2, Color(0, 0, 0, fg.a));
		line(Vec2(b2.x - 1, b2.y), text2, fg);
	}
}

void DraggedItem::Draw(const Theme &th, Vec2 mouse) {
	if (icon) icon->Draw(th, mouse + Vec2(-42, -36), true, false, 0.5f);
}

// ===================================================================== IconGrid

IconGrid::IconGrid(const std::string &path, Filesystem *f, Vec2 size, bool br, std::function<void()> dropped)
	: fs(f), fsPath(path), browser(br), onItemDropped(dropped) {
	SetIconArea(size);
	auto cb = [this](const std::string &p) { OnFsChange(p); };
	listenId = fs->Listen(cb, cb);
	OnFsChange(path);
}

IconGrid::~IconGrid() { fs->Unlisten(listenId); }

void IconGrid::SetIconArea(Vec2 size) {
	bounds = size;
	int c = (size.x - 8) / 84;
	int r = displayHeight = (size.y - 8) / 72;
	int n = fs->ItemCount(fsPath);
	if (browser && c * r < n) r = n / c + (n % c > 0 ? 1 : 0);
	if (c == cols && r == rows && !grid.empty()) return;
	if (!grid.empty() && !browser && fsPath == "/" && r * c < n) fs->MoveFilesOffDesktop(r * c);
	std::vector<FileIcon *> old = grid;
	int oc = cols, orr = rows;
	cols = c;
	rows = r;
	grid.assign(cols * rows, nullptr);
	for (int x = 0; x < oc; x++)
		for (int y = 0; y < orr; y++) {
			FileIcon *ic = old[x * orr + y];
			if (!ic) continue;
			if (browser && (x >= cols || y >= rows)) continue;
			InsertIcon(ic, Vec2(std::min(x, cols - 1), std::min(y, rows - 1)));
		}
	if (clickGridPos.x >= cols || clickGridPos.y >= rows) Unselect();
	if (displayRowStart + displayHeight > rows) displayRowStart = std::max(0, rows - displayHeight);
}

void IconGrid::AddIcon(std::shared_ptr<Node> n) {
	owned.emplace_back(new FileIcon(n));
	FileIcon *ic = owned.back().get();
	// The desktop fills down each column; folder windows fill along each row.
	int outer = browser ? rows : cols, inner = browser ? cols : rows;
	for (int i = 0; i < outer; i++)
		for (int j = 0; j < inner; j++) {
			int x = browser ? j : i, y = browser ? i : j;
			if (!Slot(x, y)) { Slot(x, y) = ic; return; }
		}
}

void IconGrid::InsertIcon(FileIcon *icon, Vec2 at) {
	// Put it at `at`, pushing the icons after it along (down the columns).
	if (Slot(at.x, at.y) == icon) return;
	FileIcon *carry = icon;
	bool first = true;
	for (int x = at.x; x < cols; x = (x + 1) % cols) {
		for (int y = first ? at.y : 0; y < rows; y++) {
			if (!first && x == at.x && y == at.y) {
				if (browser) return;
				sys::log("twm: no free place on the desktop for an icon\n");
				return;
			}
			FileIcon *was = Slot(x, y);
			Slot(x, y) = carry;
			if (!was) return;
			carry = was;
		}
		first = false;
	}
}

void IconGrid::OnFsChange(const std::string &path) {
	if (path != fsPath) return;
	Folder *dir = fs->GetDir(path);
	if (!dir) { sys::log("twm: no folder for icon grid at %s\n", path.c_str()); return; }
	if (browser) SetIconArea(bounds);
	std::set<Node *> inDir;
	for (auto &c : dir->contents) inDir.insert(c.get());
	// Icons whose files went away.
	for (auto &g : grid)
		if (g && !inDir.count(g->file.get())) {
			if (g == clickedIcon) clickedIcon = nullptr;
			for (size_t i = 0; i < owned.size(); i++)
				if (owned[i].get() == g) {
					graveyard.push_back(std::move(owned[i]));
					owned.erase(owned.begin() + i);
					break;
				}
			g = nullptr;
		}
	std::set<Node *> shown;
	for (auto &g : grid)
		if (g) shown.insert(g->file.get());
	for (auto &c : dir->contents)
		if (!shown.count(c.get())) AddIcon(c);
	if (!browser) return;
	// Folder windows: no gaps.
	int n = cols * rows;
	for (int m = 0; m < n; m++) {
		int x = m % cols, y = m / cols;
		if (Slot(x, y)) continue;
		bool moved = false;
		for (int k = m + 1; k < n; k++) {
			int x2 = k % cols, y2 = k / cols;
			if (Slot(x2, y2)) {
				Slot(x, y) = Slot(x2, y2);
				Slot(x2, y2) = nullptr;
				moved = true;
				break;
			}
		}
		if (!moved) break;
	}
}

std::unique_ptr<FileIcon> IconGrid::Release(FileIcon *icon) {
	for (auto &g : grid)
		if (g == icon) g = nullptr;
	if (clickedIcon == icon) clickedIcon = nullptr;
	if (heldIcon == icon) heldIcon = nullptr;
	for (size_t i = 0; i < owned.size(); i++)
		if (owned[i].get() == icon) {
			std::unique_ptr<FileIcon> r = std::move(owned[i]);
			owned.erase(owned.begin() + i);
			return r;
		}
	return nullptr;
}

FileIcon *IconGrid::Selected() {
	if (clickGridPos.x >= 0 && clickGridPos.y >= 0 && clickGridPos.x < cols && clickGridPos.y < rows) return Slot(clickGridPos.x, clickGridPos.y);
	return nullptr;
}

Vec2 IconGrid::PointToGrid(Vec2 p) const {
	return Vec2(std::min(std::max(p.x, 0) / 84, cols - 1), std::min(std::max(p.y, 0) / 72, rows - 1));
}

void IconGrid::Update(Vec2 offset, bool consumed) {
	if (!heldIcon) graveyard.clear();   // a dragged icon whose file went away lives until it's dropped
	for (auto &g : grid)
		if (g) g->Update();
	cursorWasFree = !consumed;
	if (framesSinceClick <= 30) framesSinceClick++;
	MouseCursor &c = cursor();
	Vec2 v = c.MousePos() - offset;
	if (clickedIcon) {
		if (c.Held()) {
			if (onBeginDrag && (iabs(v.x - clickPos.x) > 3 || iabs(v.y - clickPos.y) > 3)) {
				heldIcon = clickedIcon;
				clickedIcon = nullptr;
				DraggedItem *d = new DraggedItem;
				d->icon = heldIcon;
				d->source = this;
				d->onDropComplete = [this](bool) { heldIcon = nullptr; };
				onBeginDrag(d);
			}
		} else {
			clickedIcon = nullptr;
		}
		return;
	}
	if (consumed || !c.Clicked()) return;
	if (!in_rect(Rect(0, 0, bounds.x, bounds.y), v)) return;
	int gx = std::min(v.x / 84, cols - 1), gy = std::min(v.y / 72, displayHeight - 1) + displayRowStart;
	if (gx < 0 || gy < 0 || gy >= rows) { Unselect(); return; }
	FileIcon *ic = Slot(gx, gy);
	if (ic && in_rect(ic->ClickArea(GridPosition(gx, gy)), v)) {
		Vec2 gp(gx, gy);
		if (framesSinceClick < 30 && gp == clickGridPos) {   // double click
			framesSinceClick = 30;
			if (folderOverride && ic->file->IsFolder()) folderOverride(static_cast<Folder *>(ic->file.get())->Path());
			else run_file(ic->file.get());
		} else {
			framesSinceClick = 0;
			clickedIcon = ic;
			clickGridPos = gp;
			clickPos = v;
		}
		return;
	}
	Unselect();
}

void IconGrid::Draw(const Theme &th, Vec2 offset, int alpha) {
	for (int x = 0; x < cols; x++)
		for (int y = displayRowStart; y < displayRowStart + displayHeight && y < rows; y++) {
			FileIcon *ic = Slot(x, y);
			if (!ic) continue;
			bool focus = x == clickGridPos.x && y == clickGridPos.y;
			float a = alpha / 255.0f;
			if (ic == heldIcon) a /= 2;
			ic->Draw(th, GridPosition(x, y) + offset, focus, cursorWasFree, a);
		}
}

// ------------------------------------------------------------------ dropping

bool IconGrid::CheckPreventMoveFolder(DraggedItem *item) {
	if (item->icon->file->IsFolder() && item->source != this) {
		Error("error_move_folder");
		if (item->onDropComplete) item->onDropComplete(false);
		return true;
	}
	return false;
}

bool IconGrid::CheckPreventMoveRestricted(DraggedItem *item) {
	if (item->icon->file->moveRestricted && item->source != this) {
		Error("error_move_restricted");
		if (item->onDropComplete) item->onDropComplete(false);
		return true;
	}
	return false;
}

bool IconGrid::CheckPathMoveRestricted(DraggedItem *item) {
	if (fs->IsPathMoveRestricted(fsPath) && item->source != this) {
		Error("error_move_restricted");
		if (item->onDropComplete) item->onDropComplete(false);
		return true;
	}
	return false;
}

bool IconGrid::CheckForFolderDrop(DraggedItem *item, Vec2 cursorPos) {
	Vec2 g = PointToGrid(cursorPos);
	FileIcon *target = Slot(g.x, g.y);
	if (!target) return false;
	if (!in_rect(target->ClickArea(GridPosition(g.x, g.y)), cursorPos) || !target->file->IsFolder()) return false;
	Folder *folder = static_cast<Folder *>(target->file.get());
	if (item->icon->file->IsFolder()) {
		if (target->file != item->icon->file && target->file->parentPath != "/") Error("error_move_folder");
		if (item->onDropComplete) item->onDropComplete(false);
		return false;
	}
	File *f = static_cast<File *>(item->icon->file.get());
	if (f->moveRestricted || fs->IsPathMoveRestricted(folder->Path())) {
		Error("error_move_restricted");
		if (item->onDropComplete) item->onDropComplete(false);
		return false;
	}
	fs->MoveFile(f, folder->Path());
	if (item->onDropComplete) item->onDropComplete(true);
	return true;
}

static bool still_there(Filesystem *fs, DraggedItem *item) {
	Node *n = item->icon ? item->icon->file.get() : nullptr;
	if (n && (n->IsFolder() || fs->FileExists(n->parentPath + n->name))) return true;
	if (item->onDropComplete) item->onDropComplete(false);   // deleted while it was being dragged
	return false;
}

void IconGrid::OnDragDropped(DraggedItem *item, Vec2 pos) {
	if (!still_there(fs, item)) return;
	if (CheckPreventMoveFolder(item) || CheckPreventMoveRestricted(item) || CheckForFolderDrop(item, pos) ||
	    CheckPathMoveRestricted(item))
		return;
	if (item->source != this) {
		if (!item->icon->file->IsFolder()) fs->MoveFile(static_cast<File *>(item->icon->file.get()), fsPath);
		if (item->onDropComplete) item->onDropComplete(true);
		if (onItemDropped) onItemDropped();
	} else if (item->onDropComplete) {
		item->onDropComplete(false);
	}
}

// ===================================================================== wallpapers

void WallpaperInfo::Draw(Rect area, Rect hole) const {
	// area in desktop pixels; textures in draw-space pixels.
	std::string name = "the_world_machine/wallpaper/" + imageFile;
	const gfx::Texture *t = gm::texture(name.c_str(), gm::DesktopWallpaper);
	Vec2 sp = space();
	if (!t) { box(area, bg); return; }
	switch (mode) {
	case CENTERED: {
		box(area, bg);
		gm::blit(t, Vec2(area.x * 2 + area.w - (int)t->w / 2, area.y * 2 + area.h - (int)t->h / 2), 1.0f, 0, gfx::Blend::Normal, 1);
		break;
	}
	case STRETCH: {
		// Exactly one texel per TV pixel (1080p): no filtering needed.
		const_cast<gfx::Texture *>(t)->linear = !((int)t->w == gfx::screen_w() && (int)t->h == gfx::screen_h() &&
		                                          sp.x == gfx::screen_w());
		float sx = (float)t->w / sp.x, sy = (float)t->h / sp.y;
		auto part = [&](int x, int y, int w, int h) {
			if (w <= 0 || h <= 0) return;
			Rect src((int)(x * sx), (int)(y * sy), (int)(w * sx), (int)(h * sy));
			if (src.w <= 0 || src.h <= 0) return;
			gm::blit_scaled(t, Vec2(x, y), src, (float)w / src.w, (float)h / src.h);
		};
		Rect g = hole;
		if (g.w <= 0 || g.x < 0 || g.y < 0 || g.x + g.w > sp.x || g.y + g.h > sp.y) { part(0, 0, sp.x, sp.y); break; }
		part(0, 0, sp.x, g.y);
		part(0, g.y + g.h, sp.x, sp.y - g.y - g.h);
		part(0, g.y, g.x, g.h);
		part(g.x + g.w, g.y, sp.x - g.x - g.w, g.h);
		break;
	}
	case TILE_CENTER:
	case TILE_CORNER: {
		Vec2 z;
		int tw = (int)t->w / 2, th = (int)t->h / 2;   // desktop pixels (scale 2)
		if (mode == TILE_CENTER && tw > 0 && th > 0) z = Vec2(-(area.w % tw) / 2, -(area.h % th) / 2);
		for (int x = z.x; x < area.w; x += tw)
			for (int y = z.y; y < area.h; y += th) gm::blit(t, Vec2(x * 2, y * 2), 1.0f, 0, gfx::Blend::Normal, 1);
		break;
	}
	}
}

// ===================================================================== DesktopGrid

DesktopGrid::DesktopGrid(Filesystem *f, Vec2 size) : IconGrid("/", f, size, false) {
	if (cJSON *j = parse_json_file("gamedata/twm/wallpapers_metadata.json")) {
		const cJSON *it;
		cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(j, "wallpapers")) {
			WallpaperInfo w;
			const cJSON *v = cJSON_GetObjectItemCaseSensitive(it, "imageFile");
			if (!cJSON_IsString(v)) continue;
			w.imageFile = v->valuestring;
			if (cJSON_IsString(v = cJSON_GetObjectItemCaseSensitive(it, "displayName"))) w.displayName = v->valuestring;
			if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(it, "mode"))) w.mode = v->valueint;
			if ((v = cJSON_GetObjectItemCaseSensitive(it, "bgColor")))
				w.bg = Color((int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(v, "r")),
				             (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(v, "g")),
				             (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(v, "b")));
			bool dup = false;
			for (auto &m : metadata) dup |= m.imageFile == w.imageFile;
			if (!dup) metadata.push_back(w);
		}
		cJSON_Delete(j);
	}
	if (metadata.empty()) sys::log("twm: gamedata/twm/wallpapers_metadata.json missing\n");
	SetWallpaper("default");
}

const WallpaperInfo *DesktopGrid::WallpaperMeta(const std::string &id) const {
	for (auto &m : metadata)
		if (m.imageFile == id) return &m;
	return nullptr;
}

void DesktopGrid::SetWallpaper(const std::string &id) {
	const WallpaperInfo *m = WallpaperMeta(id);
	if (!m) {
		if (metadata.empty()) { wallpaper.imageFile = "default"; return; }
		return;
	}
	gm::clear_cache(gm::DesktopWallpaper);
	wallpaper = *m;
}

void DesktopGrid::OverrideWallpaper(const WallpaperInfo &w) {
	gm::clear_cache(gm::DesktopWallpaper);
	if (!hasRemembered) remembered = wallpaper;
	hasRemembered = true;
	wallpaper = w;
}

void DesktopGrid::RestoreWallpaper() {
	if (!hasRemembered) return;
	gm::clear_cache(gm::DesktopWallpaper);
	wallpaper = remembered;
	hasRemembered = false;
}

void DesktopGrid::DrawDesktop(const Theme &th, Rect hole, bool drawIcons) {
	wallpaper.Draw(Rect(0, 0, screen().x, screen().y), hole);
	if (drawIcons) IconGrid::Draw(th, Vec2(0, 0), 255);
}

std::vector<std::pair<std::string, Vec2>> DesktopGrid::Layout() const {
	std::vector<std::pair<std::string, Vec2>> r;
	for (int x = 0; x < cols; x++)
		for (int y = 0; y < rows; y++)
			if (FileIcon *ic = grid[x * rows + y]) r.push_back({ ic->file->name, Vec2(x, y) });
	return r;
}

void DesktopGrid::SetLayout(const std::vector<std::pair<std::string, Vec2>> &positions) {
	for (auto &p : positions) {
		Vec2 v = p.second;
		if (v.x < 0 || v.y < 0 || v.x >= cols || v.y >= rows) continue;
		FileIcon *at = Slot(v.x, v.y);
		if (at && at->file->name == p.first) continue;
		for (int i = 0; i < cols * rows; i++)
			if (grid[i] && grid[i]->file->name == p.first) {
				std::swap(grid[i], Slot(v.x, v.y));
				break;
			}
	}
}

void DesktopGrid::MoveDocumentsToTopLeft() {
	for (int i = 0; i < cols * rows; i++)
		if (grid[i] && grid[i]->file->IsFolder() && static_cast<Folder *>(grid[i]->file.get())->Path() == "/docs_foldername/") {
			std::swap(grid[i], grid[0]);
			return;
		}
}

void DesktopGrid::OnFsChange(const std::string &path) {
	IconGrid::OnFsChange(path);
	if (path == fsPath && onDesktopChange) onDesktopChange();
}

void DesktopGrid::OnDragDropped(DraggedItem *item, Vec2 pos) {
	if (!item->icon || !still_there(fs, item)) return;
	if (CheckPreventMoveFolder(item) || CheckPreventMoveRestricted(item) || CheckForFolderDrop(item, pos)) return;
	if (IsFull() && item->icon->file->parentPath != "/") {
		Error("error_too_many_items");
		if (item->onDropComplete) item->onDropComplete(false);
		return;
	}
	if (CheckPathMoveRestricted(item)) return;
	if (system_modal_open()) {
		if (item->onDropComplete) item->onDropComplete(false);
		return;
	}
	Vec2 to = PointToGrid(pos);
	if (item->source == this) {
		std::swap(Slot(clickGridPos.x, clickGridPos.y), Slot(to.x, to.y));   // clickGridPos: where the drag began
	} else if (item->source) {
		std::unique_ptr<FileIcon> ic = item->source->Release(item->icon);
		if (ic) {
			InsertIcon(ic.get(), to);
			owned.push_back(std::move(ic));
		}
	}
	heldIcon = nullptr;
	if (item->onDropComplete) item->onDropComplete(true);
	if (item->source != this && !item->icon->file->IsFolder()) fs->MoveFile(static_cast<File *>(item->icon->file.get()), fsPath);
	Unselect();
}

} // namespace twm
