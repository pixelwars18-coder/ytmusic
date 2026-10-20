# YT Music

A lightweight personal YouTube music player, in two parts.

- `ytmusic/` – desktop app (C++ / Qt 6 Widgets). Plays through mpv, searches with yt-dlp, MPRIS + tray icon,
  follows the Noctalia colour palette. Build: `cmake -B build -G Ninja && ninja -C build`.
  Needs `mpv`, `yt-dlp` and `curl` at runtime.
- `ytmusic-android/` – standalone Android app (Kotlin, plain Views, Media3, NewPipeExtractor) with offline downloads,
  made to run on old Fire tablets. Build: see its README. Releases are signed with a private key that is not in
  this repository; without it the build falls back to the debug key.
- `tools/` – the self-update server and publish script used to deliver APKs over a home network.

Both apps need no account and keep likes, playlists and history locally.
