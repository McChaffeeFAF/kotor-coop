KotOR co-op test build (Linux / Steam Deck)
===========================================

This folder holds the game engine only. It contains no game files: it
plays from YOUR copies of Star Wars: Knights of the Old Republic and
Knights of the Old Republic II (the Steam versions, which Steam installs
for Proton; they are Windows installs and that is fine, only the data
files are read), and never changes them.

1. Run ./play.sh (double-click it, or run it in a terminal). The first time
   it finds your KotOR and KotOR II installs by itself: Steam in
   ~/.local/share/Steam, ~/.steam/steam, the Flatpak Steam, other Steam
   libraries listed in libraryfolders.vdf, and the Steam Deck SD card. If it
   can't find one it asks for the folder with swkotor.exe / swkotor2.exe.
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

4. Steam Deck: in Desktop Mode, add play.sh as a non-Steam game (Steam >
   Add a Non-Steam Game, browse to play.sh), then you can start it from
   Gaming Mode. Without a terminal the questions appear as dialogs
   (zenity or kdialog, both present on SteamOS). Set Proton off for this
   entry: the engine is a native Linux program. Use "./play.sh host",
   "./play.sh join CODE" or "./play.sh solo" as the launch option arguments
   to skip the menu.

5. Something went wrong? ./report-bug.sh packs the game's log, the co-op
   recording and your settings (no game files, no saves) into reports/ as a
   .tar.gz. Send that to the developer with a line about what happened.

Needs: a 64-bit Linux with OpenGL 3.3+ (Mesa or a vendor driver), glibc 2.38
or newer (VERSION.txt names the exact need). Other libraries (SDL3,
networking, FFmpeg, OpenAL) are bundled in lib/.
