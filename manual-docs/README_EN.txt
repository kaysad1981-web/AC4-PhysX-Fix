AC4 PHYSX FIX / MODULAR PATCH 0.3.2 ALPHA — MANUAL INSTALLATION
Free community patch. This ZIP is an alternative to setup, not an extra component.

SUPPORTED BUILD
Steam (2023), AC4BFSP.exe SHA256:
bc11ad7924e1eca7363ee86aa3332c05386d0a05896c0b7370131daee9ab5150
Manual copying does not automatically validate your game or detect conflicts.

INSTALL
1. Close the game. Extract this archive outside the game folder.
2. If dinput8.dll, nvcuda.dll, AC4Fix or AC4-PhysX-D3D already exists, stop:
   it may belong to an earlier patch or another mod. Use its supported removal/update method.
   Do not overwrite a foreign DLL. The native installer is preferred for validated updates.
3. Copy the CONTENTS of GAME_FILES next to AC4BFSP.exe, not the GAME_FILES folder itself.
4. Launch the game normally. The camera fix is ON automatically; F4 is not required.

DEFAULTS AND OPTIONS
CorpseLimit remains 20; no buffer expansion or Havok-rate change in this update.
Detailed diagnostics are OFF. Automatic FPS ceiling remains monitor refresh minus 3.
Personal overrides belong in AC4Fix/User.ini, using the same sections and keys.
To disable the camera fix, add [Physics] and CameraFix=0 in User.ini, then restart the game.
F10 retains the FPS/60-FPS switch. F4 is only a diagnostic toggle when Diagnostics=1.

WHAT CHANGED
Scoped suppression of confirmed retired CameraFX roll contributions, preserving live effects.
Safer reads of corpse records and collision-group bookkeeping without rereading removed objects.
The Direct3D 11 PhysX payload is retained; only dinput8.dll changed among the 404 payload files.

KNOWN LIMITATIONS
A rare camera jerk during ship combat/jumping is NOT fixed. The update does not eliminate all
camera rotation or rewrite Havok. Some native object lifetime and concurrency risks remain.
A long private CAMTEST09 game session was reviewed; this packaged auto-on/default-diagnostics-off
variant passed standalone checks but has not yet received its own full in-game test.
Only the stated EXE hash is supported. AMD/Intel validation remains incomplete.

REMOVE
Prefer the native installer: select the game directory and Remove patch. Keep install-manifest.json.
Without setup, back up personal files and remove only the patch-owned files in FILELIST_SHA256.txt,
relative to GAME_FILES. Preserve files replaced by other mods and unknown files. Never remove
AC4BFSP.exe, game archives or saves. Back up User.ini and wanted logs first.
Manual copying has no automatic rollback. No installer or script is included in this ZIP.
