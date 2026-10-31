// settings.h - SettingsMenu values (settings.conf), shared by the game.
// "fullscreen" is the OneShot window's maximized state (as on the PC);
// "scaling" (sharp/smooth, for the maximized window) is this port's own.
#pragma once

struct Settings {
	bool defaultMovementRun = true;
	bool colorblindMode = false;
	bool automash = false;           // hold L2 to fast-forward text
	bool vibration = true;
	bool screenTear = true;
	bool chromaAberration = true;
	int scaling = 0;                 // gm::SCALE_SHARP / SMOOTH (maximized window)
	bool fullscreen = false;         // the OneShot window opens maximized
	bool fullscreenBorder = true;    // FullscreenBorderManager's side art when maximized
	// The Controls app (InputManager): stick dead zones (0..50) and cursor speed (2..8).
	int moveDeadZone = 20, mouseDeadZone = 20, cursorSpeed = 5;
	void ApplyInput() const;         // hands them to the pad and the cursor
	void Load();                     // settings.conf; defaults if missing
	void Save() const;
};

Settings &settings();

// InputManager.IsAutoMashing: L2 held with the setting on.
bool auto_mashing();
