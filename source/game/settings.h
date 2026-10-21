// settings.h - SettingsMenu values (settings.conf), shared by the game.
// The PC build's window options (fullscreen, scaling, border) have no
// meaning on a console and are left out.
#pragma once

struct Settings {
	bool defaultMovementRun = true;
	bool colorblindMode = false;
	bool automash = false;           // hold L2 to fast-forward text
	bool vibration = true;
	bool screenTear = true;
	bool chromaAberration = true;
	void Load();                     // settings.conf; defaults if missing
	void Save() const;
};

Settings &settings();

// InputManager.IsAutoMashing: L2 held with the setting on.
bool auto_mashing();
