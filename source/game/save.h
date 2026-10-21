// save.h - GameSaveManager: save.dat (the playthrough), p-settings.dat
// (permanent flags 151-175, variables 76-100 and the player's name, which
// survive a finished playthrough) and settings.conf (settings.h).
//
// The files are JSON in our own layout (the PC build's save files are not
// read). They live in the "saves" folder next to the game data.
#pragma once

class TileMap;
struct GameState;

namespace save {

bool Exists();
// MakeSave: writes save.dat, p-settings.dat and settings.conf.
void Make(TileMap *map, GameState *state);
// LoadSave: applies save.dat, then the permanent data. False if the file
// is unreadable (the caller clears it, like ClearCorruptSave).
bool Load(TileMap *map, GameState *state);
// EraseSave: deletes save.dat (the journal is part of the desktop port).
void Erase();
void ClearCorrupt();
// LoadPermaSave / KillPermaFlags.
bool LoadPerma(GameState *state);
void KillPermaFlags(GameState *state);

} // namespace save
