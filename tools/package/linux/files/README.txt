KotOR co-op test build (Linux / Steam Deck)
===========================================

This folder holds the game engine only. It contains no game files: it
plays from YOUR copy of Star Wars: Knights of the Old Republic (the Steam
version, which Steam installs for Proton; it is a Windows install and that
is fine, only the data files are read), and never changes it. Star Wars:
Knights of the Old Republic II is OPTIONAL: with it you get the KotOR II
look and its feats, powers and classes; without it the game plays KotOR 1
with the KotOR look.

1. Run ./play.sh (double-click it, or run it in a terminal). The first time
   it finds your KotOR install by itself: Steam in
   ~/.local/share/Steam, ~/.steam/steam, the Flatpak Steam, other Steam
   libraries listed in libraryfolders.vdf, and the Steam Deck SD card; GOG
   copies in ~/GOG Games, ~/Games and the default Heroic and Lutris Wine
   prefixes (also their Flatpaks). Folder names may be in any capitalisation
   (Override or override). KotOR II is detected too and used if present. If it
   can't find KotOR 1 it asks for the folder with swkotor.exe.
   Run ./setup.sh to choose again.

2. To host: choose "Host a co-op game" (or ./host.sh), then New Game or Load
   Game as usual. The host listens on UDP port 27015. Forward UDP 27015 on
   your router to this machine, then send your friend your public IP and
   the port, like 203.0.113.7:27015.
   (Builds made with a join-code server show a join code like K7QM-4TXR
   instead, on the main menu's Co-op screen and in game under Options >
   Co-op. Send that to your friend; no port forwarding is needed.)

3. To join: choose "Join a co-op game" and type the host's IP:port (or
   ./join.sh 203.0.113.7:27015). In a build with a join-code server, type
   the code instead.

4. Steam Deck: the engine notices a Deck (Gaming Mode, or the user "deck") and
   starts at the panel's 1280x800, fullscreen, with a larger interface. To
   make the interface bigger or smaller, put guiscale=1.5 (and, for the text,
   guitextscale=0.75) in reone.cfg; --width, --height and --fullscreen work
   the same way.

   To start it from Gaming Mode, add it as a non-Steam game in Desktop Mode:
     a. Steam > Games > Add a Non-Steam Game to My Library > Browse, choose
        play.sh (set the file filter to "All files"), Add.
     b. Right-click the new entry > Properties. Launch options: leave empty for
        the menu, or use "host", "join CODE" or "solo" to skip the menu.
     c. Properties > Compatibility: tick "Force the use of a specific Steam Play
        compatibility tool" and choose "Steam Linux Runtime 3.0 (sniper)". This
        runs the native Linux engine inside Valve's container of current
        libraries, so it behaves the same on every SteamOS version. (If it is
        not in the list, install it once: Library > filter "Tools" >
        "Steam Linux Runtime 3.0 (sniper)" > Install.) Do NOT choose Proton:
        the engine is a native Linux program, not a Windows one.
     d. Questions (name, game folders) appear as dialogs when there is no
        terminal (zenity or kdialog, both present on SteamOS). Run play.sh once
        from Desktop Mode first so setup finds your games.
   Your KotOR installs on the Deck's SD card are found too (Steam libraries on
   /run/media/...). They are Windows installs run through Proton; only the data
   files are read.

5. Something went wrong? ./report-bug.sh packs the game's log, the co-op
   recording and your settings (no game files, no saves) into reports/ as a
   .tar.gz. Send that to the developer with a line about what happened.

Dependencies. The package carries its own SDL3, networking, FFmpeg and OpenAL
libraries in lib/. It relies on the system for the C and C++ runtimes and the
graphics stack (OpenGL via Mesa or a vendor driver, X11 or Wayland, ALSA/
PulseAudio/PipeWire); nothing needs installing on a normal desktop or a Deck.
If the engine does not start, run "ldd bin/engine" and look for "not found".

Needs: a 64-bit Linux with OpenGL 3.3+ (Mesa or a vendor driver), glibc 2.38
or newer (VERSION.txt names the exact need). Other libraries (SDL3,
networking, FFmpeg, OpenAL) are bundled in lib/.
