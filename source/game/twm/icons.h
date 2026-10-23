// icons.h - FileIcon, IconGrid (the desktop and folder windows), DraggedItem.
#pragma once
#include "fs.h"
#include "twm.h"
#include <memory>

namespace twm {

class FileIcon {
public:
	explicit FileIcon(std::shared_ptr<Node> f);
	std::shared_ptr<Node> file;
	void Update();
	void Draw(const Theme &th, Vec2 pos, bool focus, bool canHover, float alpha);
	Rect ClickArea(Vec2 pos);
	bool IsWallpaper() const;
	bool IsTheme() const;
private:
	void Layout();
	int layoutGen = -1;
	std::string text1, text2;
	Rect r1, r2;
	int textHeight = 12;
};

class IconGrid;
struct DraggedItem {
	FileIcon *icon = nullptr;   // owned by `source`
	std::function<void(bool accepted)> onDropComplete;
	IconGrid *source = nullptr;
	void Draw(const Theme &th, Vec2 mouse);
};

class IconGrid {
public:
	IconGrid(const std::string &path, Filesystem *fs, Vec2 size, bool browser = false, std::function<void()> onItemDropped = nullptr);
	virtual ~IconGrid();
	virtual void Update(Vec2 offset, bool consumed);
	virtual void Draw(const Theme &th, Vec2 offset, int alpha = 255);
	virtual void OnDragDropped(DraggedItem *item, Vec2 pos);
	void SetIconArea(Vec2 size);
	FileIcon *Selected();
	void Unselect() { clickGridPos = Vec2(-1, -1); }
	int Rows() const { return rows; }
	int displayRowStart = 0;
	std::function<void(const std::string &)> folderOverride;   // folder windows navigate instead of opening
	std::function<void(int type, const std::string &msg)> createModal;
	std::function<void(DraggedItem *)> onBeginDrag;   // takes ownership
	void DragEnded() { heldIcon = nullptr; }
	// Hands an icon over to another grid (a file dragged onto the desktop).
	std::unique_ptr<FileIcon> Release(FileIcon *icon);
	const std::string &Path() const { return fsPath; }

protected:
	virtual void OnFsChange(const std::string &path);
	FileIcon *&Slot(int x, int y) { return grid[x * rows + y]; }
	Vec2 GridPosition(int gx, int gy) const { return Vec2(gx * 84 + 4, (gy - displayRowStart) * 72 + 4); }
	Vec2 PointToGrid(Vec2 p) const;
	void AddIcon(std::shared_ptr<Node> n);
	void InsertIcon(FileIcon *icon, Vec2 at);
	bool CheckPreventMoveFolder(DraggedItem *item);
	bool CheckPreventMoveRestricted(DraggedItem *item);
	bool CheckPathMoveRestricted(DraggedItem *item);
	bool CheckForFolderDrop(DraggedItem *item, Vec2 cursorPos);
	void Error(const char *key) { if (createModal) createModal(1 /* MODAL_ERROR */, key); }

	Filesystem *fs;
	std::string fsPath;
	bool browser;
	std::vector<FileIcon *> grid;   // cols * rows, column-major
	std::vector<std::unique_ptr<FileIcon>> owned;
	int cols = 0, rows = 0, displayHeight = 0;
	Vec2 bounds;
	FileIcon *clickedIcon = nullptr, *heldIcon = nullptr;
	int framesSinceClick = 30;
	Vec2 clickGridPos = Vec2(-1, -1), clickPos;
	bool cursorWasFree = false;
	int listenId = 0;
	std::function<void()> onItemDropped;
	std::vector<std::unique_ptr<FileIcon>> graveyard;   // removed icons a drag may still point at
};

// Wallpapers (WallpaperInfo + wallpapers_metadata.json).
struct WallpaperInfo {
	enum Mode { CENTERED, STRETCH, TILE_CORNER, TILE_CENTER };
	std::string imageFile, displayName;
	int mode = STRETCH;
	Color bg = Color(0, 0, 0);
	void Draw(Rect area, Rect hole) const;   // hole: skip what the game's picture will cover
};

// TWMDesktopManager: the desktop's icon grid and wallpaper.
class DesktopGrid : public IconGrid {
public:
	DesktopGrid(Filesystem *fs, Vec2 size);
	void DrawDesktop(const Theme &th, Rect hole, bool drawIcons = true);
	void OnDragDropped(DraggedItem *item, Vec2 pos) override;
	// Layout: icon positions by file name.
	std::vector<std::pair<std::string, Vec2>> Layout() const;
	void SetLayout(const std::vector<std::pair<std::string, Vec2>> &positions);
	void MoveDocumentsToTopLeft();
	bool IsFull() const { return fs->ItemCount("/") >= cols * rows; }

	const WallpaperInfo *WallpaperMeta(const std::string &id) const;
	const std::vector<WallpaperInfo> &AllWallpapers() const { return metadata; }
	void SetWallpaper(const std::string &id);
	void OverrideWallpaper(const WallpaperInfo &w);
	void RestoreWallpaper();
	bool IsOverridden() const { return hasRemembered; }
	const WallpaperInfo &Current() const { return hasRemembered ? remembered : wallpaper; }   // GetCurrentWallpaper
	const WallpaperInfo &Showing() const { return wallpaper; }
	bool inSolstice = false;
	std::function<void()> onDesktopChange;   // a file on the desktop came or went: save
protected:
	void OnFsChange(const std::string &path) override;
private:
	std::vector<WallpaperInfo> metadata;
	WallpaperInfo wallpaper, remembered;
	bool hasRemembered = false;
};

} // namespace twm
