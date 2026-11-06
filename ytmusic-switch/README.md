# YT Music for Nintendo Switch

A homebrew port of the YT Music player: search YouTube, play, queue, like, playlists and downloads to the SD card,
in the same Noctalia look as the desktop app. Talks to YouTube directly (no PC needed).

## Install
1. Copy `ytmusic.nro` to `sdmc:/switch/ytmusic/ytmusic.nro` (any folder under `switch/` works).
2. Open the Homebrew Menu and start **YT Music**. For the most memory, start the Homebrew Menu by holding **R**
   while launching a game (title takeover) rather than from the Album.

Needs Atmosphère + the Homebrew Menu and an internet connection (downloaded songs play offline).

## Controls
| Button | Action |
| --- | --- |
| A | Play the selected song (or open a playlist / run a search) |
| B | Back (close a playlist, clear the search) |
| X | Like / unlike |
| Y | Search (on-screen keyboard; paste a YouTube link to play it) |
| − | Menu: play next, add to queue, radio, add to playlist, download, play all… |
| + | Play / pause (hold to exit) |
| ZL / ZR | Previous / next song |
| L / R | Switch tabs |
| D-pad / left stick | Move; ← → jump a page |
| Right stick | ↔ seek 10 s, ↕ volume |
| Touch | Tap to select, tap again to play; drag to scroll; tap the player bar |

## Files (on the SD card)
- `switch/ytmusic/state.json` – likes, playlists, queue
- `switch/ytmusic/downloads/` – downloaded songs (`<videoId>.m4a`) + `index.json`
- `switch/ytmusic/ytmusic-backup.json` – put a backup exported from the PC or Android app here, then Settings → Import
- `switch/ytmusic/clients.json` – optional override of the YouTube client list if YouTube changes something
- `switch/ytmusic/log.txt` – log of the last run

## Background playback
Settings → **Background playback: On**. The app installs a small background service (an Atmosphère sysmodule,
`atmosphere/contents/420000000059544D/`) and starts it; from then on the service plays the queue, so music keeps
going on the HOME menu and in games, and moves on to the next song by itself. The app becomes its remote control.
Turning it off stops the service and hands the song back to the app. After a reboot the app restarts the service
the next time you open it (nothing runs at boot). Files: `switch/ytmusic/bg/` (control files, cache, `log.txt`).


## Build
devkitPro with `switch-dev switch-sdl2 switch-sdl2_ttf switch-sdl2_image switch-curl switch-ffmpeg`, then `make`.
CI (`.github/workflows/switch.yml`) builds the .nro and runs `tests/host_test.cpp` (core against real YouTube on Linux).
