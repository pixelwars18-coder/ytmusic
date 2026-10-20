# YT Music for Android

Standalone Android app (Kotlin, plain Views, Media3). Search YouTube songs and playlists, play in the
background, like songs, keep your own playlists, download for offline listening.

Build:   JAVA_HOME=<jdk21> ANDROID_HOME=~/android-sdk ~/.local/gradle-8.9/bin/gradle :app:assembleRelease
Output:  app/build/outputs/apk/release/app-release.apk  (signed with ytmusic.keystore, password "ytmusic")

Install:
  Allow installs from your browser / file manager ("Install unknown apps"),
  then open the APK.  Or:  adb install -r ytmusic-fire.apk

Layout:  core/  YouTube access (NewPipeExtractor): search, playlists, stream URLs  (plain JVM, testable on a PC)
         app/   Android UI, playback service, downloads
