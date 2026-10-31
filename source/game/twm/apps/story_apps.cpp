#include "story_apps.h"
#include "../../../platform/input.h"
#include "../wm_internal.h"
#include "custom_wallpapers.h"
#include "../../core/loc.h"
#include "../../core/json_file.h"
#include "../../core/save.h"
#include "../../../engine/font.h"
#include "../../../engine/gm.h"
#include "../../../engine/util.h"
#include "../../../platform/sys.h"
#include <algorithm>
#include <stdlib.h>

namespace twm {

// ===================================================================== BrowserWindow

static const int CRUMB_BAR_H = 22;

BrowserWindow::BrowserWindow(Filesystem *f, const std::string &p) : fs(f) {
	icon = "folder";
	SetContents(Vec2(360, 200));
	NavigateTo(p);
	AddButton(WinButtonType::Close, [this] {
		if ((tutorial_is(TUT_CLOSE_WINDOW) || tutorial_complete()) && onClose) onClose(this);
	});
	AddButton(WinButtonType::Minimize);
	int y = (CRUMB_BAR_H - 16) / 2;
	folderUp = IconButton("the_world_machine/window_icons/folder_up", Vec2(4, y), [this] { OnFolderUp(); });
	del = IconButton("the_world_machine/window_icons/trash", Vec2(contents.x - 20, y), [this] { OnFileDelete(); });
	slider.reset(new Slider("", 0, 1, Vec2(343, CRUMB_BAR_H + 1), 200 - CRUMB_BAR_H - 2, true, true));
	slider->onValueChanged = [this](int v) { grid->displayRowStart = v; };
	slider->active = grid->Rows() > 2;
	if (slider->active) slider->max = grid->Rows() - 2;
}

BrowserWindow::~BrowserWindow() {}

void BrowserWindow::NavigateTo(const std::string &p) {
	path = p;
	// CreateBreadcrumbs
	crumbs.clear();
	int slashW = text_w("/");
	Vec2 v(24 + slashW + 2, 2);
	std::vector<std::string> parts;
	size_t a = 0;
	while (a < p.size()) {
		size_t b = p.find('/', a);
		if (b == std::string::npos) b = p.size();
		if (b > a) parts.push_back(p.substr(a, b - a));
		a = b + 1;
	}
	std::string t;
	for (auto &part : parts) {
		Crumb c;
		c.folder = part;
		c.display = loc::twm(part.c_str());
		c.bound = Rect(v.x, v.y, text_w(c.display) + 4, 16);
		v.x += c.bound.w + slashW + 2;
		crumbs.push_back(c);
		t += "/" + c.display;
	}
	crumbGen = loc::generation();
	SetTitle(t);
	// InitIconGrid
	if (grid) oldGrids.push_back(std::move(grid));
	grid.reset(new IconGrid(path, fs, Vec2(contents.x, contents.y - CRUMB_BAR_H), true, [this] { if (grabFocus) grabFocus(this); }));
	grid->onBeginDrag = [](DraggedItem *d) { begin_drag(d); };
	grid->folderOverride = [this](const std::string &to) { NavigateTo(to); };
	grid->createModal = [this](int type, const std::string &msg) { ShowModal(type, msg); };
	if (slider) {
		slider->SetValue(0);
		slider->active = grid->Rows() > 2;
		if (slider->active) slider->max = grid->Rows() - 2;
	}
}

bool BrowserWindow::Update(bool consumed) {
	oldGrids.clear();
	if (crumbGen != loc::generation()) NavigateTo(path);
	Vec2 v(pos.x + 2, pos.y + 26);
	bool can = !consumed && !IsModalOpen() && !minimized;
	IconGrid *g = grid.get();
	g->Update(v + Vec2(0, CRUMB_BAR_H), !can);
	if (minimized) return Window::Update(consumed);
	folderUp.Update(v, can);
	del.Update(v, can);
	g = grid.get();   // a double-clicked folder replaced it
	slider->active = g->Rows() > 2;
	if (slider->active) {
		slider->max = g->Rows() - 2;
		if (slider->Value() > slider->max) slider->SetValue(slider->max);
	}
	slider->Update(v, can);
	if (can && tutorial_complete()) {
		std::string p = "/";
		for (auto &c : crumbs) {
			p += c.folder + "/";
			c.hovered = in_rect(translated(c.bound, v), cursor().MousePos());
			if (c.hovered && cursor().Clicked()) {
				NavigateTo(p);
				break;
			}
		}
	}
	folderUp.disabled = crumbs.size() <= 1;
	del.disabled = grid->Selected() == nullptr;
	return Window::Update(consumed);
}

void BrowserWindow::DrawContents(const Theme &th, Vec2 sp, int alpha) {
	Color bg = th.Background(alpha), fg = th.Primary(alpha), var = th.Variant(alpha);
	box(Rect(sp.x, sp.y, contents.x, contents.y), bg);
	box(Rect(sp.x, sp.y + CRUMB_BAR_H - 2, contents.x, 2), fg);
	// DrawBreadcrumbs
	int slashW = text_w("/");
	for (auto &c : crumbs) {
		Color fill = c.hovered ? fg : bg, txt = c.hovered ? bg : fg;
		Rect b = translated(c.bound, sp);
		Vec2 t(b.x - slashW - 1, b.y - 1);
		text(t, "/", fg);
		box(b, fg);
		box(shrink(b, 1), fill);
		line(Vec2(b.x + 1 - 1, t.y + 4), c.display, txt);
	}
	if (path == Filesystem::WALLPAPERS || path == Filesystem::THEMES) {
		int have = path == Filesystem::WALLPAPERS ? (int)unlocks().wallpapers.size() : (int)unlocks().themes.size();
		int all = path == Filesystem::WALLPAPERS ? desktop_grid().GameWallpaperCount() : (int)themes().size();
		std::string s = std::to_string(have) + "/" + std::to_string(all);
		text(Vec2(sp.x + contents.x - 24 - text_w(s), sp.y + contents.y - 20), s, var);
	}
	folderUp.Draw(sp, th, alpha);
	del.Draw(sp, th, tutorial_is(TUT_DELETE_CLICK_DELETE) ? (int)(alpha * tutorial_pulse()) : alpha);
	slider->Draw(th, sp, alpha);
	grid->Draw(th, sp + Vec2(0, CRUMB_BAR_H), alpha);
}

bool BrowserWindow::IsSameContent(const Window *other) const {
	const BrowserWindow *b = other->Kind() == KIND_BROWSER ? static_cast<const BrowserWindow *>(other) : nullptr;
	return b && b->path == path && Filesystem::ParentPath(path) != "/";
}

void BrowserWindow::HandleDragDropped(DraggedItem *item, Vec2 cp) {
	if (cp.y <= CRUMB_BAR_H) {
		std::string p = "/";
		for (auto &c : crumbs) {
			p += c.folder + "/";
			if (in_rect(c.bound, cp)) {
				DropFolder(item, p);
				return;
			}
		}
	}
	cp.y = std::max(0, cp.y - CRUMB_BAR_H);
	grid->OnDragDropped(item, cp);
}

void BrowserWindow::DropFolder(DraggedItem *item, const std::string &p) {
	Node *n = item->icon ? item->icon->file.get() : nullptr;
	if (n && n->parentPath != p) {
		if (!n->IsFolder()) {
			File *f = static_cast<File *>(n);
			if (!f->moveRestricted && !fs->IsPathMoveRestricted(p)) {
				fs->MoveFile(f, p);
				if (item->onDropComplete) item->onDropComplete(true);
				return;
			}
			ShowModal(MODAL_ERROR, "error_move_restricted");
		} else {
			ShowModal(MODAL_ERROR, "error_move_folder");
		}
	}
	if (item->onDropComplete) item->onDropComplete(false);
}

void BrowserWindow::OnFolderUp() {
	if (tutorial_complete()) NavigateTo(Filesystem::ParentPath(path));
}

static bool is_program(FileIcon *ic, Program p) { return !ic->file->IsFolder() && static_cast<File *>(ic->file.get())->program == p; }

Vec2 BrowserWindow::IconPos(Program p) {
	return pos + grid->IconPosition([p](FileIcon *ic) { return is_program(ic, p); }, Vec2(0, 0));
}

bool BrowserWindow::IsIconSelected(Program p) {
	return grid->IsIconSelected([p](FileIcon *ic) { return is_program(ic, p); });
}

Vec2 BrowserWindow::DeleteButtonPos() const { return Vec2(contents.x - 20, (CRUMB_BAR_H - 16) / 2 + 26) + pos; }

bool BrowserWindow::IsShowingDeleteConfirmModal() const {
	return modal && static_cast<ModalWindow *>(modal.get())->type == MODAL_YESNO;
}

Vec2 BrowserWindow::ModalButtonsPos() const {
	return modal ? static_cast<ModalWindow *>(modal.get())->ButtonsPos() : Vec2(0, 0);
}

void BrowserWindow::OnFileDelete() {
	FileIcon *sel = grid->Selected();
	if (!sel) { ShowModal(MODAL_ERROR, "delete_no_file_selected_modal"); return; }
	std::shared_ptr<Node> n = sel->file;
	if (n->deleteRestricted) { ShowModal(MODAL_ERROR, "delete_restricted_modal"); return; }
	std::string shown = is_custom_wallpaper(n->name) ? custom_wallpaper_name(n->name) : loc::twm(n->name.c_str());
	ShowModal(MODAL_YESNO, format1(loc::twm("delete_confirmation_modal"), shown), [this, n](int r) {
		if (r == RESP_YES) {
			sys::log("twm: deleting %s%s\n", n->parentPath.c_str(), n->name.c_str());
			bool customWp = !n->IsFolder() && static_cast<File *>(n.get())->program == PROG_WALLPAPER && is_custom_wallpaper(n->name);
			fs->Delete(n.get());
			if (customWp) remove_custom_wallpaper(n->name);   // the picture itself goes too
		}
	});
}

// ===================================================================== DocumentWindow

static std::vector<std::string> g_randomLetters;

// The port's own Help document (English, like its other additions).
static const char *const WALLPAPERS_HELP =
	"CUSTOM WALLPAPERS\n"
	"\n"
	"You can use your own pictures as wallpapers.\n"
	"\n"
	"FROM A USB STICK\n"
	"1. On a USB stick, make a folder named OneShot, and inside it a folder named wallpapers.\n"
	"2. Put your pictures in OneShot/wallpapers.\n"
	"3. Plug the stick into the PS3, open Customize and press Import Wallpapers.\n"
	"The pictures are copied into the game, so the stick can be removed afterwards.\n"
	"\n"
	"BY FTP\n"
	"Copy pictures into /dev_hdd0/game/OSWM00001/USRDIR/wallpapers. They show up when the game next starts, "
	"or straight away if you press Import Wallpapers.\n"
	"\n"
	"USING THEM\n"
	"Your pictures are at the end of the Wallpapers folder, under their file names. Open one to use it.\n"
	"To remove one, select it and press the trash button.\n"
	"\n"
	"LIMITS\n"
	"- PNG, JPG and BMP pictures.\n"
	"- Up to 16 megapixels (a 4K picture is about 8).\n"
	"- Pictures fill the whole screen. One that isn't the screen's shape is scaled up and its edges are cropped evenly.\n"
	"- A picture that was already imported is skipped.\n"
	"- Big pictures take a moment to load the first time.\n"
	"- Erase All Data keeps your wallpapers.";

static std::string doc_text(const std::string &file) {
	if (file == "osps3_wallpapers") return WALLPAPERS_HELP;
	std::string p = "gamedata/loc/" + loc::code() + "/txt/" + file + ".txt";
	if (!sys::file_exists(p.c_str())) p = "gamedata/txt/" + file + ".txt";
	uint8_t *data;
	size_t size;
	if (!sys::read_file(p.c_str(), &data, &size)) {
		sys::log("twm: document %s missing\n", p.c_str());
		return "";
	}
	std::string s((const char *)data, size);
	free(data);
	if (s.size() >= 3 && (uint8_t)s[0] == 0xef && (uint8_t)s[1] == 0xbb && (uint8_t)s[2] == 0xbf) s.erase(0, 3);   // BOM
	s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
	return s;
}

static std::vector<std::string> utf8_chars(const std::string &s) {
	std::vector<std::string> out;
	for (size_t i = 0; i < s.size();) {
		int n = font::utf8_len((unsigned char)s[i]);
		if (n <= 0) n = 1;
		out.push_back(s.substr(i, n));
		i += n;
	}
	return out;
}

static const std::string &random_letter() {
	if (g_randomLetters.empty()) {
		for (auto &c : utf8_chars(doc_text("random_letters")))
			if (c != "\n") g_randomLetters.push_back(c);
		if (g_randomLetters.empty()) g_randomLetters.push_back("#");
	}
	return g_randomLetters[util::random(0, (int)g_randomLetters.size() - 1)];
}

void DocumentWindow::ClearRandomLetters() { g_randomLetters.clear(); }

void DocumentWindow::Letter::Scramble() {
	int n = util::random(1, 100);
	timer = util::random(15, 60);
	display = n <= 75 ? random_letter() : original;
}

DocumentWindow::DocumentWindow(const std::string &name, const std::string &file, const std::string &safeCode) {
	docFile = file == "credits" ? "credits_pc" : file;
	icon = "doc";
	SetTitle(loc::twm(name.c_str()));
	SetContents(Vec2(450, 232));
	AddButton(WinButtonType::Close);
	AddButton(WinButtonType::Minimize);
	slider.reset(new Slider("", 0, 1, Vec2(432, 2), 228, true, true));
	slider->onValueChanged = [this](int v) { firstLine = v; };
	slider->active = false;
	Load(safeCode);
	if (lines.size() > 12) {
		slider->active = true;
		slider->max = (int)lines.size() - 12;
	}
}

void DocumentWindow::AddLine(std::vector<Letter> &letters) {
	Line l;
	for (auto &x : letters) l.glitched |= x.glitched;
	if (l.glitched) l.letters = letters;
	else
		for (auto &x : letters) l.text += x.display;
	lines.push_back(l);
}

void DocumentWindow::Load(const std::string &safeCode) {
	std::string text = doc_text(docFile);
	if (docFile == "document_postgame" || docFile == "document") {
		size_t p;
		while ((p = text.find("{CODE}")) != std::string::npos) text.replace(p, 6, safeCode);
	}
	size_t a = 0;
	while (a <= text.size()) {
		size_t b = text.find('\n', a);
		std::string src = text.substr(a, b == std::string::npos ? std::string::npos : b - a);
		int width = 0;
		std::vector<Letter> list;
		bool glitched = false, escaped = false;
		for (auto &ch : utf8_chars(src)) {
			if (ch == "\\") { escaped = true; continue; }
			if (ch == "[" && !escaped) { glitched = true; escaped = false; continue; }
			if (ch == "]" && !escaped) { glitched = false; escaped = false; continue; }
			escaped = false;
			Letter l;
			l.original = l.display = ch;
			l.glitched = glitched && ch != " ";
			l.width = font::width(font::GameSmall, ch);
			if (l.glitched) l.Scramble();
			width += l.width;
			if (width > 424) {
				// Word wrap: carry the unfinished word to the next line.
				std::vector<Letter> carry;
				while (l.display != " " && !list.empty()) {
					carry.push_back(l);
					l = list.back();
					list.pop_back();
				}
				if (list.empty()) {
					carry.push_back(l);
					list = carry;
					std::reverse(list.begin(), list.end());
					l = list.back();
					list.pop_back();
					AddLine(list);
					list.clear();
					list.push_back(l);
					width = l.width;
					continue;
				}
				AddLine(list);
				list = carry;
				std::reverse(list.begin(), list.end());
				width = 0;
				for (auto &x : list) width += x.width;
			} else {
				list.push_back(l);
			}
		}
		AddLine(list);
		if (b == std::string::npos) break;
		a = b + 1;
	}
}

bool DocumentWindow::Update(bool consumed) {
	for (auto &l : lines)
		if (l.glitched)
			for (auto &x : l.letters) x.Update();
	slider->Update(Vec2(pos.x + 2, pos.y + 26), !consumed && !minimized);
	return Window::Update(consumed);
}

void DocumentWindow::DrawContents(const Theme &th, Vec2 sp, int alpha) {
	box(Rect(sp.x, sp.y, 450, 232), th.Background(alpha));
	Color var = th.Variant(alpha), pri = th.Primary(alpha);
	unsigned vr = (unsigned)(var.r << 16 | var.g << 8 | var.b), pr = (unsigned)(pri.r << 16 | pri.g << 8 | pri.b);
	float a = alpha / 255.0f;
	Vec2 v(sp.x + 4, sp.y + 8);
	for (int i = firstLine; i < firstLine + 12 && i < (int)lines.size(); i++) {
		const Line &l = lines[i];
		if (l.glitched) {
			for (auto &x : l.letters) {
				font::draw(font::GameSmall, v, x.display, x.glitched ? pr : vr, a, 2, font::Mode::OnlyText);
				v.x += x.width;
			}
			v.x = sp.x + 4;
		} else {
			font::draw(font::GameSmall, v, l.text, vr, a, 2);
		}
		v.y += 18;
	}
	slider->Draw(th, sp, alpha);
}

bool DocumentWindow::IsSameContent(const Window *other) const {
	const DocumentWindow *d = other->Kind() == KIND_DOCUMENT ? static_cast<const DocumentWindow *>(other) : nullptr;
	return d && d->docFile == docFile;
}

// ===================================================================== PhotoWindow

PhotoWindow::PhotoWindow(const std::string &source, const std::string &displayName, int s) : scale(s == 2 ? 2 : 1) {
	icon = "photo";
	SetTitle(loc::twm(displayName.c_str()));
	picture = "the_world_machine/photos/" + source;
	Vec2 sz = gm::texture_size(picture.c_str(), gm::Game);
	SetContents(Vec2(sz.x / (2 / scale), sz.y / (2 / scale)));
	if (contents.x <= 0) SetContents(Vec2(160, 120));
	AddButton(WinButtonType::Close);
	AddButton(WinButtonType::Minimize);
}

void PhotoWindow::DrawContents(const Theme &, Vec2 sp, int alpha) {
	tex(picture.c_str(), scale == 1 ? Vec2(sp.x * 2, sp.y * 2) : sp, Rect(), Color::White().withA(alpha), scale, gm::Game);
}

bool PhotoWindow::IsSameContent(const Window *other) const {
	const PhotoWindow *p = other->Kind() == KIND_PHOTO ? static_cast<const PhotoWindow *>(other) : nullptr;
	return p && p->picture == picture;
}

// Dragging a picture by any of its pixels (PhotoWindow / JournalWindow).
static bool drag_picture(Window *w, Vec2 &pos, bool &held, Vec2 &heldMouse) {
	MouseCursor &c = cursor();
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
		if (w->grabFocus) w->grabFocus(w);
	}
	return true;
}

bool PhotoWindow::Update(bool consumed) {
	consumed |= Window::Update(consumed);
	if (!consumed && !minimized) {
		Vec2 m = cursor().MousePos() - Vec2(pos.x + 2, pos.y + 26);
		if (m.x >= 0 && m.y >= 0 && m.x < contents.x && m.y < contents.y) consumed = drag_picture(this, pos, pictureHeld, heldMouse);
	} else {
		pictureHeld = false;
	}
	return consumed;
}

// ===================================================================== JournalWindow

JournalWindow::JournalWindow() {
	icon = "clover";
	SetPicture("default");
	showBackground = false;
	SetContents(Vec2(400, 300));
	AddButton(WinButtonType::Close);
	AddButton(WinButtonType::Minimize);
	if (cJSON *j = parse_json_file("gamedata/twm/journal_clover_data.json")) {
		const cJSON *it;
		cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(j, "cloverLocations")) {
			const cJSON *id = cJSON_GetObjectItemCaseSensitive(it, "id");
			if (!cJSON_IsString(id)) continue;
			auto v2 = [&](const char *k) {
				const cJSON *o = cJSON_GetObjectItemCaseSensitive(it, k);
				return Vec2((int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(o, "X")),
				            (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(o, "Y")));
			};
			bool dup = false;
			for (auto &c : clovers) dup |= c.first == id->valuestring;
			if (!dup) clovers.push_back({ id->valuestring, Clover{ v2("cloverPos"), v2("drawOffset") } });
		}
		cJSON_Delete(j);
	}
	// CheckForSaveInstructions: a save that has seen the journal's save page.
	if (filesystem().FileExists(std::string(Filesystem::MYGAMESONESHOT) + "fakesave_filename") && save::SavedFlag(147))
		SetPicture("save");
}

void JournalWindow::SetPicture(const std::string &id) {
	if (id.empty()) {
		if (onClose) onClose(this);
		return;
	}
	pictureId = id;
	picture = "the_world_machine/journal/" + id;
}

const JournalWindow::Clover *JournalWindow::CurrentClover() const {
	for (auto &c : clovers)
		if (c.first == pictureId) return &c.second;
	return nullptr;
}

void JournalWindow::DrawContents(const Theme &, Vec2 sp, int alpha) {
	Vec2 p(sp.x * 2, sp.y * 2);
	if (const Clover *c = CurrentClover()) p = p + c->drawOffset;
	tex(picture.c_str(), p, Rect(), Color::White().withA(alpha), 1, gm::Game);
}

bool JournalWindow::Update(bool consumed) {
	consumed |= Window::Update(consumed);
	if (!consumed && !minimized) {
		Vec2 m = cursor().MousePos() - Vec2(pos.x + 2, pos.y + 26);
		if (gm::pixel_solid(picture.c_str(), Vec2(m.x * 2, m.y * 2), gm::Game)) consumed = drag_picture(this, pos, pictureHeld, heldMouse);
	}
	// With the clover equipped, the journal's clover snaps onto the one in
	// the game's window.
	const Clover *c = CurrentClover();
	if (is_window_focused(this) && c && clover_equipped()) {
		Vec2 cp(c->cloverPos.x / 2 + pos.x + 2, c->cloverPos.y / 2 + pos.y + 26);
		Vec2 op = game_window_pos() + Vec2(294, 239);
		if (cp.x >= op.x - 11 && cp.x <= op.x + 11 && cp.y >= op.y - 12 && cp.y <= op.y + 12) {
			if (cp.x < op.x) pos.x++;
			else if (cp.x > op.x) pos.x--;
			if (cp.y < op.y) pos.y++;
			else if (cp.y > op.y) pos.y--;
		}
	}
	return consumed;
}

bool JournalWindow::IsSameContent(const Window *other) const { return other->Kind() == KIND_JOURNAL; }

} // namespace twm

namespace twm {

// The d-pad scrolls the folder's icons and the document's text.
static void pad_scroll(Slider *s) {
	if (!s || !s->active) return;
	if (pad_repeat(input::BTN_UP)) s->ArrowLeft();
	else if (pad_repeat(input::BTN_DOWN)) s->ArrowRight();
}

void BrowserWindow::PadInput() { pad_scroll(slider.get()); }
void DocumentWindow::PadInput() { pad_scroll(slider.get()); }

} // namespace twm
