OpenSE4: making this copy portable
==================================

OpenSE4 keeps your saved games, settings, logs, mods and the history of the game you
play in your user folder:

  Windows   %APPDATA%\OpenSE4  (C:\Users\<your name>\AppData\Roaming\OpenSE4)
  Linux     ~/.local/share/OpenSE4  ($XDG_DATA_HOME/OpenSE4 when that is set)
  macOS     ~/Library/Application Support/OpenSE4

A portable copy keeps them in the folder "userdata" beside its programs instead, so
that they go wherever this folder goes: a copy on a USB stick, or several copies side
by side. This copy is not portable until you make it so, in either of two ways:

  - Start OpenSE4, open Settings (at the top right of the title screen, or Ctrl+Comma
    in a game), choose the Files page and tick "Keep saves and settings in OpenSE4's
    folder". OpenSE4 offers to copy your saved games and settings across. It never
    deletes any: the files in your user folder stay where they are.
  - Or create an empty text file named portable.txt in this folder, beside opense4.exe
    (opense4 on Linux). In Windows Explorer with file name extensions hidden, name the
    new text document "portable".

To use your user folder again, untick the setting or delete portable.txt. The files in
userdata stay where they are.

This folder must be one you can write in: unpack the zip file or tarball into a folder
of your own, not into Program Files. A copy installed with the Windows installer cannot
be portable; OpenSE4 says so if you try.

The Files page also shows which folder is in use and can give your saved games a
folder of their own. When the environment variable OPENSE4_USER_DIR is set, the folder
it names is used, portable copy or not. OpenSE4 never writes in the original game's
folder. The game's manual (Settings, Files) and docs/SETUP.md, "Where OpenSE4 keeps its
own files", on the project's page tell more.
