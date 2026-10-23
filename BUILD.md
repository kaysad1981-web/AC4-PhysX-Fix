# Rebuild the native installer

Windows, Visual Studio 2022/2026 C++ desktop tools, a CMake version supporting that generator and a Windows 10 SDK are required **only on the build machine**. The resulting installer does not need these tools. GitHub Actions detects the installed Visual Studio C++ version on its Windows 2025 image.

The repository includes generated resource tables, the verified 404-file payload with updated dinput8.dll in `distribution/payload.zip`, the settings template and the supplied background/icon. CMake extracts the payload and verifies all hashes. It downloads nlohmann/json 3.12.0 only if absent and verifies its SHA-256: `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`. This build-time download does not occur on a player's machine.

From a terminal in this source folder:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build/Release/engine-tests.exe --package .
build/Release/AC4-PhysX-Fix-Setup.exe --verify-package
```

For Visual Studio 2022 use generator `Visual Studio 17 2022`. After cloning, network access is needed once to download the pinned JSON header, unless it is already supplied in `vendor/json.hpp`.

To generate the Nexus package:

```powershell
./package-manual.ps1 -Destination "$PWD/dist/manual"
./build/Release/engine-tests.exe --package "$PWD" "$PWD/dist/manual/GAME_FILES"
Compress-Archive -Path dist/manual/* -DestinationPath dist/AC4-PhysX-Fix-0.3.2-Nexus-Manual.zip
```

Release assets are separate from Git history. The workflow publishes a prerelease only after successful tests and a main-branch push whose commit message contains `[release]`. Existing release assets are preserved.

`render-tests.exe` is a separate development-only binary that renders all five screens in RU/EN at 100%, 150%, 200% scaling to `ui-previews/` in its working directory. It checks window/control bounds. It does not install the patch. Do not ship this executable as setup.

`engine-tests.exe` uses synthetic game fixtures and an injected test hash/process check; these are not available as setup arguments. It never launches a game or loads a patch DLL. The real-package test stages original payload bytes into an isolated fixture, verifies rollback at early/middle/late commit positions and checks a clean uninstall.

`prepare.ps1 -PackagePath <extracted-original-package>` is a developer-only resource generator. Setup itself does not call it. It validates every catalog hash before embedding.

The installer uses ordinary PE RCDATA resources, no executable packer, no PowerShell launcher, no external installer framework, no web engine, no network connection and no downloaded runtime. C++ runtime linkage is `/MT`. Resource compression is deliberately not used. System imports can be inspected with `dumpbin /dependents`.

Only the installer code is newly authored here. The original supplied game DLLs were not rebuilt and their provenance/licensing is not changed by this project. Asset ownership/licensing and third-party redistribution rights must be resolved by the project owner before public publication.

A code-signing certificate is not bundled; no self-signed certificate is silently installed. Sign the final EXE using a publisher identity under the project owner's control if available, then regenerate release checksums. Signing does not guarantee store approval or eliminate all warnings.

## Rebuilt bridge

`bridge/AC4-Camera-Candidate10-20261022/build-private-candidate.ps1` builds the release fork. Historical directory names preserve include paths. The release enables CameraFix automatically and gates detailed tracing behind Diagnostics. After rebuilding, update `distribution/payload.zip`, `payload-hashes.json` and generated resource hashes before packaging. The installer embeds a prebuilt bridge; CI currently builds/tests the installer, not the bridge ABI stands. See docs/VALIDATION.md for the local bridge checks.
