#!/usr/bin/env python3
"""make_game_file_list.py - writes pkg_files/USRDIR/port/game_files.txt: the
paths (no contents) of every file in a complete copy of the PC game's
content/ and gamedata/ folders. The port checks a player's copy against it
at start-up (source/game/core/data_check.cpp).

    python3 tools/assets/make_game_file_list.py <folder with content/ and gamedata/> [output]

Run it again when the port moves to a newer version of the game."""
import os
import sys


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    root = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(__file__), '..', '..', 'pkg_files', 'USRDIR', 'port', 'game_files.txt')
    paths = []
    for top in ('content', 'gamedata'):
        for d, _, files in os.walk(os.path.join(root, top), followlinks=True):
            for f in files:
                paths.append(os.path.relpath(os.path.join(d, f), root).replace(os.sep, '/'))
    if not paths:
        sys.exit('no content/ or gamedata/ files under ' + root)
    paths.sort()
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# Every file of the PC game the port expects (made by tools/assets/make_game_file_list.py)\n')
        f.write('\n'.join(paths) + '\n')
    print(len(paths), 'files ->', out)


if __name__ == '__main__':
    main()
