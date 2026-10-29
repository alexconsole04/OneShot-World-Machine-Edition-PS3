// fs.h - TWMFilesystem: the World Machine's folders and files (fs.dat).
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace twm {

// LaunchableWindowType
enum Program {
	PROG_ONESHOT, PROG_PHOTO, PROG_DOCUMENT, PROG_JOURNAL, PROG_CUSTOMIZE, PROG_JUKEBOX, PROG_GALLERY, PROG_CONTACTS,
	PROG_ACHIEVEMENT, PROG_AUTHOR_PROFILE, PROG_NIKO_PROFILE, PROG_FROMNIKO_WALLPAPER, PROG_TUTORIAL_DELETE_FILE,
	PROG_TUTORIAL_INTRO_FILE, PROG_TUTORIAL_MOVE_FILE, PROG_DUMMY_FILE_FOR_TUTORIALS, PROG_DEBUG_CHEAT_APP, PROG_WALLPAPER,
	PROG_THEME, PROG_TIMER, PROG_CONTROLS, PROG_SHUTDOWN, PROG_COUNT
};
const char *program_name(Program p);
bool program_from_name(const std::string &s, Program &p);

struct Node {
	virtual ~Node() {}
	std::string name, icon, parentPath;   // name: a string id
	bool deleteRestricted = false, moveRestricted = false;
	virtual bool IsFolder() const = 0;
};

struct File : Node {
	Program program = PROG_ONESHOT;
	std::vector<std::string> args;
	bool IsFolder() const override { return false; }
};

struct Folder : Node {
	std::vector<std::shared_ptr<Node>> contents;
	std::string Path() const { return parentPath + name + "/"; }
	bool IsFolder() const override { return true; }
};

std::shared_ptr<File> make_file(const std::string &icon, const std::string &name, Program p, std::vector<std::string> args = {});

class Filesystem {
public:
	typedef std::function<void(const std::string &path)> Notify;
	static const char *DESKTOP, *MYDOCUMENTS, *MYGAMES, *MYGAMESONESHOT, *HELP, *PORTALS, *WALLPAPERS, *THEMES;

	static std::string ParentPath(const std::string &path);
	static std::string FileNameFromPath(const std::string &path);

	Folder *GetDir(const std::string &path) const;
	bool FileExists(const std::string &path) const;
	bool IsPathMoveRestricted(const std::string &path) const;
	void Delete(const std::string &path);
	void Delete(Node *node);
	void MoveFile(File *file, const std::string &to);
	void AddDir(const std::string &path);
	int ItemCount(const std::string &path) const;
	void WriteFile(const std::string &path, std::shared_ptr<File> file);
	bool FileTypeExists(Program p) const;
	File *FindNamedFile(const File &f) const;
	void MoveFilesOffDesktop(int capacity);

	void CreateDefaultFiles();
	void CreateDocumentsFolder();
	void CreateOneshotFile();
	void CreateTutorialCompleteFiles(const std::vector<std::string> &wallpapers, const std::vector<std::string> &themes);
	void CreateExtraSystemFiles();
	void CreateDeleteTutorialFiles();
	void CreateMoveTutorialFiles();
	void LoadEmpty();   // the tutorials' empty desktop
	void CreateHelpFiles();
	void CreateWallpaperFiles(const std::vector<std::string> &ids);
	void CreateWallpaperFile(const std::string &id);
	void CreateThemeFiles(const std::vector<std::string> &ids);
	void CreateThemeFile(const std::string &id);

	// fs.dat (the PC build's JSON: folders and files)
	std::string Save() const;
	bool Load(const std::string &json);

	// Listeners (icon grids): called with the folder path that changed.
	int Listen(Notify onWriteOrDelete, Notify onMove);
	void Unlisten(int id);
	std::function<void()> onAnyWriteOrDelete;   // the window manager saves

private:
	void NotifyWrite(const std::string &path);
	void NotifyMove(const std::string &path);
	std::map<std::string, std::shared_ptr<Folder>> dirs;
	struct Listener { int id; Notify write, move; };
	std::vector<Listener> listeners;
	int nextId = 1;
};

} // namespace twm
