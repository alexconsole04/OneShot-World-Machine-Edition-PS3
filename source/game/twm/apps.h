// apps.h - the World Machine's story apps: BrowserWindow (folders),
// DocumentWindow, PhotoWindow, JournalWindow.
#pragma once
#include "icons.h"
#include "window.h"
#include <memory>

namespace twm {

class BrowserWindow : public Window {
public:
	int Kind() const override { return KIND_BROWSER; }
	BrowserWindow(Filesystem *fs, const std::string &path);
	~BrowserWindow() override;
	bool Update(bool consumed) override;
	void DrawContents(const Theme &th, Vec2 screenPos, int alpha) override;
	bool IsSameContent(const Window *other) const override;
	const std::string &Path() const { return path; }
protected:
	void HandleDragDropped(DraggedItem *item, Vec2 contentPos) override;
private:
	struct Crumb { std::string folder, display; Rect bound; bool hovered = false; };
	void NavigateTo(const std::string &p);
	void DropFolder(DraggedItem *item, const std::string &p);
	void OnFolderUp();
	void OnFileDelete();
	Filesystem *fs;
	std::string path;
	std::unique_ptr<IconGrid> grid;
	std::vector<std::unique_ptr<IconGrid>> oldGrids;   // replaced while their update may still run
	std::vector<Crumb> crumbs;
	IconButton folderUp, del;
	std::unique_ptr<Slider> slider;
	int crumbGen = -1;
};

class DocumentWindow : public Window {
public:
	int Kind() const override { return KIND_DOCUMENT; }
	DocumentWindow(const std::string &name, const std::string &file, const std::string &safeCode);
	bool Update(bool consumed) override;
	void DrawContents(const Theme &th, Vec2 screenPos, int alpha) override;
	bool IsSameContent(const Window *other) const override;
	static void ClearRandomLetters();
private:
	struct Letter {
		std::string original, display;
		int width = 0, timer = 0;
		bool glitched = false;
		void Scramble();
		void Update() { if (glitched && --timer <= 0) Scramble(); }
	};
	struct Line { bool glitched = false; std::string text; std::vector<Letter> letters; };
	void Load(const std::string &safeCode);
	void AddLine(std::vector<Letter> &letters);
	std::string docFile;
	std::vector<Line> lines;
	std::unique_ptr<Slider> slider;
	int firstLine = 0;
	friend struct Letter;
};

class PhotoWindow : public Window {
public:
	int Kind() const override { return KIND_PHOTO; }
	PhotoWindow(const std::string &source, const std::string &displayName, int scale = 1);
	bool Update(bool consumed) override;
	void DrawContents(const Theme &th, Vec2 screenPos, int alpha) override;
	bool IsSameContent(const Window *other) const override;
private:
	std::string picture;
	int scale;
	bool pictureHeld = false;
	Vec2 heldMouse;
};

class JournalWindow : public Window {
public:
	int Kind() const override { return KIND_JOURNAL; }
	JournalWindow();
	bool Update(bool consumed) override;
	void DrawContents(const Theme &th, Vec2 screenPos, int alpha) override;
	bool IsSameContent(const Window *other) const override;
	void SetPicture(const std::string &id);
private:
	struct Clover { Vec2 cloverPos, drawOffset; };
	const Clover *CurrentClover() const;
	std::string pictureId, picture;
	std::vector<std::pair<std::string, Clover>> clovers;
	bool pictureHeld = false;
	Vec2 heldMouse;
};

} // namespace twm
