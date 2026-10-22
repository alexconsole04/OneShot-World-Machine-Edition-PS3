// settings.h - SettingsMenu values (settings.conf), shared by the game.
// The PC build's window options (fullscreen, border) have no meaning on a
// console and are left out; "scaling" is this port's own.
#pragma once

struct Settings {
	bool defaultMovementRun = true;
	bool colorblindMode = false;
	bool automash = false;           // hold L2 to fast-forward text
	bool vibration = true;
	bool screenTear = true;
	bool chromaAberration = true;
	int scaling = 0;                 // gm::SCALE_SHARP / SMOOTH / INTEGER
	void Load();                     // settings.conf; defaults if missing
	void Save() const;
};

Settings &settings();

// InputManager.IsAutoMashing: L2 held with the setting on.
bool auto_mashing();
