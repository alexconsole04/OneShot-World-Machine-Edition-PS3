#include "fs.h"
#include "../../platform/sys.h"
#include "cJSON.h"
#include <stdlib.h>
#include <algorithm>

namespace twm {

static const char *PROGRAM_NAMES[PROG_COUNT] = {
	"ONESHOT", "PHOTO", "DOCUMENT", "JOURNAL", "CUSTOMIZE", "JUKEBOX", "GALLERY", "CONTACTS", "ACHIEVEMENT", "AUTHOR_PROFILE",
	"NIKO_PROFILE", "FROMNIKO_WALLPAPER", "TUTORIAL_DELETE_FILE", "TUTORIAL_INTRO_FILE", "TUTORIAL_MOVE_FILE",
	"DUMMY_FILE_FOR_TUTORIALS", "DEBUG_CHEAT_APP", "WALLPAPER", "THEME", "TIMER", "CONTROLS", "SHUTDOWN",
};

const char *program_name(Program p) { return p >= 0 && p < PROG_COUNT ? PROGRAM_NAMES[p] : "ONESHOT"; }
bool program_from_name(const std::string &s, Program &p) {
	for (int i = 0; i < PROG_COUNT; i++)
		if (s == PROGRAM_NAMES[i]) { p = (Program)i; return true; }
	return false;
}

std::shared_ptr<File> make_file(const std::string &icon, const std::string &name, Program p, std::vector<std::string> args) {
	auto f = std::make_shared<File>();
	f->icon = icon;
	f->name = name;
	f->program = p;
	f->args = args;
	return f;
}

const char *Filesystem::DESKTOP = "/";
const char *Filesystem::MYDOCUMENTS = "/docs_foldername/";
const char *Filesystem::MYGAMES = "/docs_foldername/mygames_foldername/";
const char *Filesystem::MYGAMESONESHOT = "/docs_foldername/mygames_foldername/oneshot_foldername/";
const char *Filesystem::HELP = "/help_foldername/";
const char *Filesystem::PORTALS = "/portals_foldername/";
const char *Filesystem::WALLPAPERS = "/wallpapers_foldername/";
const char *Filesystem::THEMES = "/themes_foldername/";

std::string Filesystem::ParentPath(const std::string &path) {
	if (path.empty()) return "";
	std::string t = path.substr(0, path.size() - 1);
	size_t s = t.find_last_of('/');
	return s == std::string::npos ? "" : t.substr(0, s + 1);
}

std::string Filesystem::FileNameFromPath(const std::string &path) {
	size_t s = path.find_last_of('/');
	return s == std::string::npos ? path : path.substr(s + 1);
}

Folder *Filesystem::GetDir(const std::string &path) const {
	auto it = dirs.find(path);
	return it == dirs.end() ? nullptr : it->second.get();
}

bool Filesystem::FileExists(const std::string &path) const {
	Folder *d = GetDir(ParentPath(path));
	if (!d) return false;
	std::string n = FileNameFromPath(path);
	for (auto &c : d->contents)
		if (c->name == n) return true;
	return false;
}

bool Filesystem::IsPathMoveRestricted(const std::string &path) const { return path == WALLPAPERS || path == THEMES; }

void Filesystem::NotifyWrite(const std::string &path) {
	std::vector<Listener> l = listeners;
	for (auto &x : l)
		if (x.write) {
			x.write(path);
		}
	if (onAnyWriteOrDelete) onAnyWriteOrDelete();
}

void Filesystem::NotifyMove(const std::string &path) {
	std::vector<Listener> l = listeners;
	for (auto &x : l)
		if (x.move) x.move(path);
}

int Filesystem::Listen(Notify w, Notify m) {
	listeners.push_back({ nextId, w, m });
	return nextId++;
}

void Filesystem::Unlisten(int id) {
	listeners.erase(std::remove_if(listeners.begin(), listeners.end(), [id](const Listener &l) { return l.id == id; }),
	                listeners.end());
}

void Filesystem::Delete(const std::string &path) {
	std::shared_ptr<Folder> folder;
	auto it = dirs.find(path);
	if (it != dirs.end()) {
		folder = it->second;
		dirs.erase(it);
	}
	std::string parent = ParentPath(path);
	Folder *p = GetDir(parent);
	if (!p) return;
	std::string name = folder ? folder->name : FileNameFromPath(path);
	for (size_t i = 0; i < p->contents.size(); i++)
		if (p->contents[i]->name == name) {
			p->contents.erase(p->contents.begin() + i);
			NotifyWrite(parent);
			return;
		}
}

void Filesystem::Delete(Node *node) {
	std::string parent = node->parentPath;
	Folder *p = GetDir(parent);
	if (!p) return;
	for (size_t i = 0; i < p->contents.size(); i++)
		if (p->contents[i].get() == node) {
			p->contents.erase(p->contents.begin() + i);
			NotifyWrite(parent);
			return;
		}
}

void Filesystem::MoveFile(File *file, const std::string &to) {
	std::string from = file->parentPath;
	Folder *a = GetDir(from), *b = GetDir(to);
	if (!a || !b) return;
	for (size_t i = 0; i < a->contents.size(); i++)
		if (a->contents[i].get() == file) {
			std::shared_ptr<Node> keep = a->contents[i];
			a->contents.erase(a->contents.begin() + i);
			b->contents.push_back(keep);
			file->parentPath = to;
			NotifyMove(from);
			NotifyMove(to);
			if (onAnyWriteOrDelete) onAnyWriteOrDelete();
			return;
		}
}

void Filesystem::AddDir(const std::string &path) {
	if (dirs.count(path)) return;
	// The folder's name is its last path element ("desktop_foldername" for "/").
	std::vector<std::string> parts;
	size_t s = 0;
	while (s < path.size()) {
		size_t e = path.find('/', s);
		if (e == std::string::npos) e = path.size();
		if (e > s) parts.push_back(path.substr(s, e - s));
		s = e + 1;
	}
	auto f = std::make_shared<Folder>();
	f->name = parts.empty() ? "desktop_foldername" : parts.back();
	f->icon = f->name == "themes_foldername" ? "theme" : f->name == "wallpapers_foldername" ? "wallpapers_folder"
	        : f->name == "help_foldername" ? "help" : "folder";
	f->deleteRestricted = true;
	dirs[path] = f;
	std::string parent = ParentPath(path);
	if (!parent.empty()) AddDir(parent);
	if (Folder *p = GetDir(parent)) {
		p->contents.push_back(f);
		f->parentPath = parent;
		NotifyWrite(parent);
	}
}

int Filesystem::ItemCount(const std::string &path) const {
	Folder *d = GetDir(path);
	return d ? (int)d->contents.size() : 0;
}

void Filesystem::WriteFile(const std::string &path, std::shared_ptr<File> file) {
	File *old = FindNamedFile(*file);
	if (old) Delete(old);
	Folder *d = GetDir(path);
	if (!d) {
		sys::log("twm: could not find path when creating '%s': %s\n", file->name.c_str(), path.c_str());
		return;
	}
	d->contents.push_back(file);
	file->parentPath = path;
	NotifyWrite(path);
}

bool Filesystem::FileTypeExists(Program p) const {
	for (auto &d : dirs)
		for (auto &c : d.second->contents)
			if (!c->IsFolder() && static_cast<File *>(c.get())->program == p) return true;
	return false;
}

File *Filesystem::FindNamedFile(const File &f) const {
	for (auto &d : dirs) {
		for (auto &c : d.second->contents)
			if (!c->IsFolder()) {
				File *x = static_cast<File *>(c.get());
				if (x->program == f.program && x->name == f.name) return x;
			}
	}
	return nullptr;
}

void Filesystem::MoveFilesOffDesktop(int capacity) {
	Folder *desk = GetDir(DESKTOP);
	if (!desk) return;
	int n = (int)desk->contents.size();
	std::vector<File *> movable;
	for (auto &c : desk->contents) {
		if (c->IsFolder() || c->moveRestricted) continue;
		Program p = static_cast<File *>(c.get())->program;
		if (p == PROG_PHOTO || p == PROG_DOCUMENT || p == PROG_AUTHOR_PROFILE || p == PROG_NIKO_PROFILE ||
		    p == PROG_FROMNIKO_WALLPAPER)
			movable.push_back(static_cast<File *>(c.get()));
	}
	while (n > capacity && !movable.empty()) {
		MoveFile(movable.back(), MYDOCUMENTS);
		movable.pop_back();
		n--;
	}
}

// ------------------------------------------------------------------ the standard files

void Filesystem::CreateDefaultFiles() {
	dirs.clear();
	AddDir(DESKTOP);
}

void Filesystem::CreateDocumentsFolder() { AddDir(MYGAMESONESHOT); }

void Filesystem::CreateOneshotFile() {
	auto f = make_file("oneshot", "oneshot_appname", PROG_ONESHOT);
	f->deleteRestricted = true;
	WriteFile(DESKTOP, f);
}

void Filesystem::CreateTutorialCompleteFiles(const std::vector<std::string> &wallpapers, const std::vector<std::string> &themes) {
	auto f = make_file("customize", "customize_app_name", PROG_CUSTOMIZE);
	f->deleteRestricted = true;
	WriteFile(DESKTOP, f);
	CreateHelpFiles();
	CreateWallpaperFiles(wallpapers);
	CreateThemeFiles(themes);
	CreateExtraSystemFiles();
}

void Filesystem::CreateDeleteTutorialFiles() {
	AddDir(MYDOCUMENTS);
	auto f = make_file("doc", "tutorial_dummyfile_filename", PROG_DUMMY_FILE_FOR_TUTORIALS, { "readme" });
	f->moveRestricted = true;
	WriteFile(MYDOCUMENTS, f);
}

void Filesystem::CreateMoveTutorialFiles() {
	AddDir("/movefile_tutorial_folder1_name/");
	AddDir("/movefile_tutorial_folder2_name/");
	auto f = make_file("doc", "tutorial_dummyfile_filename", PROG_DUMMY_FILE_FOR_TUTORIALS, { "readme" });
	f->deleteRestricted = true;
	WriteFile("/movefile_tutorial_folder1_name/", f);
}

void Filesystem::LoadEmpty() {
	dirs.clear();
	AddDir(DESKTOP);
	NotifyWrite(DESKTOP);
}

void Filesystem::CreateExtraSystemFiles() {
	if (!FileTypeExists(PROG_TIMER)) {
		auto f = make_file("timer", "timer_app_filename", PROG_TIMER);
		f->deleteRestricted = true;
		AddDir(MYGAMESONESHOT);
		WriteFile(MYGAMESONESHOT, f);
	}
	if (!FileTypeExists(PROG_CONTROLS)) {
		auto f = make_file("controls", "controls_app_filename", PROG_CONTROLS);
		f->deleteRestricted = true;
		WriteFile(DESKTOP, f);
	}
	if (!FileTypeExists(PROG_SHUTDOWN)) {
		auto f = make_file("exit", "shutdown_app_filename", PROG_SHUTDOWN);
		f->deleteRestricted = true;
		f->moveRestricted = true;
		WriteFile(DESKTOP, f);
	}
}

void Filesystem::CreateHelpFiles() {
	AddDir(HELP);
	struct H { const char *icon, *name; Program p; const char *arg; };
	const H help[] = {
		{ "help", "tutorial_delete_file_filename", PROG_TUTORIAL_DELETE_FILE, nullptr },
		{ "help", "tutorial_move_file_filename", PROG_TUTORIAL_MOVE_FILE, nullptr },
		{ "help", "tutorial_introduction_filename", PROG_TUTORIAL_INTRO_FILE, nullptr },
	};
	for (auto &h : help) {
		auto f = make_file(h.icon, h.name, h.p);
		f->deleteRestricted = true;
		f->moveRestricted = true;
		WriteFile(HELP, f);
	}
	auto credits = make_file("doc", "credits_filename", PROG_DOCUMENT, { "credits" });
	credits->deleteRestricted = true;
	WriteFile(HELP, credits);
	AddDir(MYDOCUMENTS);
	auto readme = make_file("doc", "readme_filename", PROG_DOCUMENT, { "readme" });
	readme->deleteRestricted = true;
	WriteFile(MYDOCUMENTS, readme);
}

void Filesystem::CreateWallpaperFiles(const std::vector<std::string> &ids) {
	Delete(WALLPAPERS);
	AddDir(WALLPAPERS);
	for (auto &id : ids) CreateWallpaperFile(id);
	for (auto &id : customWallpapers) {
		CreateWallpaperFile(id);
		if (Folder *d = GetDir(WALLPAPERS))
			for (auto &c : d->contents)
				if (c->name == id) c->deleteRestricted = false;
	}
}

void Filesystem::CreateWallpaperFile(const std::string &id) {
	auto f = make_file("wallpaper_thumbs/" + id, id, PROG_WALLPAPER, { id });
	f->deleteRestricted = true;
	f->moveRestricted = true;
	WriteFile(WALLPAPERS, f);
}

void Filesystem::CreateThemeFiles(const std::vector<std::string> &ids) {
	Delete(THEMES);
	AddDir(THEMES);
	for (auto &id : ids) CreateThemeFile(id);
}

void Filesystem::CreateThemeFile(const std::string &id) {
	auto f = make_file("theme", id, PROG_THEME, { id });
	f->deleteRestricted = true;
	f->moveRestricted = true;
	WriteFile(THEMES, f);
}

// ------------------------------------------------------------------ fs.dat

std::string Filesystem::Save() const {
	cJSON *o = cJSON_CreateObject();
	cJSON *files = cJSON_AddArrayToObject(o, "files");
	cJSON *folders = cJSON_AddArrayToObject(o, "folders");
	for (auto &d : dirs) {
		cJSON_AddItemToArray(folders, cJSON_CreateString(d.first.c_str()));
		for (auto &c : d.second->contents) {
			if (c->IsFolder()) continue;
			const File *f = static_cast<const File *>(c.get());
			cJSON *j = cJSON_CreateObject();
			cJSON_AddStringToObject(j, "program", program_name(f->program));
			cJSON *a = cJSON_AddArrayToObject(j, "argument");
			for (auto &s : f->args) cJSON_AddItemToArray(a, cJSON_CreateString(s.c_str()));
			cJSON_AddStringToObject(j, "parentPath", f->parentPath.c_str());
			cJSON_AddStringToObject(j, "name", f->name.c_str());
			cJSON_AddStringToObject(j, "icon", f->icon.c_str());
			cJSON_AddBoolToObject(j, "deleteRestricted", f->deleteRestricted);
			cJSON_AddBoolToObject(j, "moveRestricted", f->moveRestricted);
			cJSON_AddItemToArray(files, j);
		}
	}
	char *s = cJSON_PrintUnformatted(o);
	std::string r = s ? s : "";
	if (s) free(s);
	cJSON_Delete(o);
	return r;
}

bool Filesystem::Load(const std::string &json) {
	cJSON *o = cJSON_Parse(json.c_str());
	if (!o) return false;
	const cJSON *files = cJSON_GetObjectItemCaseSensitive(o, "files"), *folders = cJSON_GetObjectItemCaseSensitive(o, "folders");
	if (!cJSON_IsArray(files) || !cJSON_IsArray(folders)) { cJSON_Delete(o); return false; }
	dirs.clear();
	const cJSON *it;
	cJSON_ArrayForEach(it, folders)
		if (cJSON_IsString(it)) AddDir(it->valuestring);
	cJSON_ArrayForEach(it, files) {
		auto str = [&](const char *k) {
			const cJSON *v = cJSON_GetObjectItemCaseSensitive(it, k);
			return std::string(cJSON_IsString(v) ? v->valuestring : "");
		};
		auto f = std::make_shared<File>();
		if (!program_from_name(str("program"), f->program)) continue;
		f->name = str("name");
		f->icon = str("icon");
		std::string parent = str("parentPath");
		f->deleteRestricted = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(it, "deleteRestricted"));
		f->moveRestricted = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(it, "moveRestricted"));
		const cJSON *a;
		cJSON_ArrayForEach(a, cJSON_GetObjectItemCaseSensitive(it, "argument"))
			if (cJSON_IsString(a)) f->args.push_back(a->valuestring);
		WriteFile(parent, f);
	}
	cJSON_Delete(o);
	if (!GetDir(DESKTOP)) AddDir(DESKTOP);
	NotifyWrite(DESKTOP);   // the desktop's icons follow a reload
	return true;
}

} // namespace twm
