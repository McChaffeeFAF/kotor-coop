KotOR co-op test build
======================

This folder holds the game engine only. It contains no game files: it
plays from YOUR copy of Star Wars: Knights of the Old Republic (intended
for the Steam version; other versions such as GOG or retail discs may work
but are untested), and never changes it. Star Wars: Knights of the Old
Republic II is OPTIONAL: with it you get the KotOR II look and its feats,
powers and classes; without it the game plays KotOR 1 with the KotOR look.

1. Double-click "Play KotOR Co-op.cmd". The first time, it finds your
   KotOR install by itself (Steam, including libraries on other drives,
   and GOG). If it can't find it, click Change... and pick the folder that
   has swkotor.exe. KotOR II is detected too and used if present. Your
   name defaults to your Windows user name; change it in the box.
   Display: "Fullscreen windowed" (the default: a borderless window at
   your desktop resolution, easy to Alt+Tab), "Fullscreen" (switches the
   screen to the Size you pick) or "Windowed" (a window of that Size).

2. To host: click "Host a co-op game", then New Game or Load Game as usual.
   You host on UDP port 27015. Forward UDP 27015 on your router to your
   PC, then send your friend your public IP and the port, like
   203.0.113.7:27015. When they connect you get an Accept / Decline prompt.
   (Builds made with a join-code server show a join code like K7QM-4TXR
   instead, on the main menu's Co-op screen and in game under Options >
   Co-op, with a Copy button. No port forwarding is needed then.)

3. To join: type the host's IP:port into the launcher and click
   "Join by IP:port" (or paste the join code and click Join, in a build
   with a join-code server).

   The first time, Windows may warn that engine.exe is an unrecognised app
   (it isn't code-signed): click "More info", then "Run anyway". Windows
   Firewall may ask whether engine.exe may use the network: allow it, or
   co-op cannot connect.

   Setup.cmd, Host.cmd and Join.cmd do the same from a console window if
   the launcher won't open.

   Optional: "Generate enhanced textures" builds extra surface-detail maps
   from your own KotOR install (used by the Enhanced graphics preset). It
   takes about 10 seconds to a minute and uses about 2 GB of disk space.
   The generated files stay on your PC (in the enhanced_textures folder
   here); nothing is uploaded.

4. Something went wrong? Click "Report a bug" in the launcher (or ReportBug.cmd) right after it happens.
   It makes a zip in the reports folder with the game's log, the co-op
   state recording and your settings (no game files, no saves), and opens
   the folder. Send that zip to the developer with a line about what
   happened.
