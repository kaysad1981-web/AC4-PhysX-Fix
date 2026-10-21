AC4 PHYSX FIX 0.3 ALPHA — MANUAL INSTALLATION
Native packaging release 0.3.1. This is an alternative to setup, not an additional component.

BEFORE INSTALLING
Only the original Steam (2023) AC4BFSP.exe is supported, with SHA-256:
bc11ad7924e1eca7363ee86aa3332c05386d0a05896c0b7370131daee9ab5150
This ZIP does not automatically check the game build, conflicts or permissions.
If uncertain, use the native setup, which validates the executable, instead of copying files manually.
Close the game. Back up your game folder or at least any files you intend to change.

WHERE TO COPY
1. Extract this ZIP to a separate location, such as Downloads/AC4Patch.
2. Open GAME_FILES inside the extracted archive.
3. Locate the installed game directory containing AC4BFSP.exe.
4. STOP if dinput8.dll, nvcuda.dll, AC4Fix or AC4-PhysX-D3D already exists in the game directory.
   It may belong to an old version or another mod. Do not overwrite it.
   Use the original uninstaller / native setup to remove an existing installation of this patch first.
5. Copy the CONTENTS of GAME_FILES into the game directory:
   Game/AC4BFSP.exe  (existing game file; not included in the ZIP)
   Game/dinput8.dll
   Game/nvcuda.dll
   Game/AC4Fix/Settings.ini
   Game/AC4Fix/install-manifest.json
   Game/AC4-PhysX-D3D/...
   Wrong: Game/GAME_FILES/dinput8.dll.
6. Start the game normally.

DEFAULTS
Unlocked-FPS game fixes, PhysX via Direct3D 11 and performance-core selection on supported Intel CPUs.
Automatic FPS cap = monitor refresh rate minus 3 (144 Hz -> 141 FPS). This is a ceiling, not guaranteed performance.
Detail distance: as in game. Diagnostics: off.
Personal changes belong in AC4Fix/User.ini, using the same sections and keys as Settings.ini.

REMOVE
Preferred: run AC4-PhysX-Fix-Setup.exe separately, choose the game directory and click Remove patch.
The manifest in this ZIP allows native setup to recognize the manual installation.
Close the game first. Changed settings/logs will be backed up outside the game.

Without setup: remove only patch files listed in FILELIST_SHA256.txt relative to GAME_FILES.
Ensure dinput8.dll / nvcuda.dll have not subsequently been replaced by another mod.
Remove patch-generated runtime*.log / EffectPackage*.apb in AC4Fix and *.log / *.csv / crash-*.txt in
AC4-PhysX-D3D if created after installation. Back up wanted logs and User.ini first.
Delete directories only when empty; preserve unknown files and other mods.
The original game EXE, game data and saves are not changed, so no original game files need restoring.

LIMITATIONS
Original unmodified game DLLs 0.3 alpha. Source notes document RTX 5090 + i9-14900K testing.
AMD/Intel D3D11 compatibility is claimed but still requires hardware validation.
ZIP packaging does not guarantee antivirus acceptance or Nexus quarantine clearance.
Do not disable Windows protection to install this mod.
