# 0.3.2 alpha — validation and limits

- Rebuilt x86 bridge from a private copy; only dinput8.dll differs from the 404-file 0.3.1 payload (403 files unchanged).
- Release auto-enable/default-trace-off behavior: standalone test passed. [Physics] CameraFix=0 skips queueing; UI component selection renders CameraFix=1/0 through the settings template.
- Exact release camera module: 30 original-writeback cases, 2048 CPU-state comparisons, two lifecycle chains and delayed-binding regression passed. Synthetic object bodies remain; no full native lifetime/concurrency proof.
- Corpse fixes are unchanged from CAMTEST09: eight cleanup integration and 15 guarded-read tests previously passed. The long reviewed CAMTEST09 session had about 49 minutes with filter ON, no recorded exceptions, and balanced 96 acquired/released group marks. A rare ship-combat/jump camera jerk was confirmed by the user and is NOT fixed.
- The packaged release variant enables the camera fix automatically and disables detailed diagnostics by default. This exact startup configuration passed standalone checks, but has not received a separate full in-game session. Current private game installation was not replaced during packaging.
- Native installer: transaction-tests passed; 417 real-package checks passed, including defaults/component-off camera settings, full install, component update, early/middle/late rollback, clean removal and recognition/removal of a manual package in an isolated fixture. No real game launch.
- Final installer --verify-package returned 0. UI renderer returned 0 for 5 screens, RU/EN, 100/150/200% scaling (30 images); representative screens visually reviewed.
- Nexus package: 406 game files, per-file hashes and manifest checked; archive contains no .exe/.ps1/.cmd/.bat/.py or nested archives. Game EXE and saves are not included.
- Unsigned alpha; only Steam (2023) EXE SHA256 bc11ad7924e1eca7363ee86aa3332c05386d0a05896c0b7370131daee9ab5150 is supported. Other hardware/builds, clean-machine deployment, all cinematics, all native Havok lifetimes and parallel step state are not fully validated.
- No 40-corpse default, 2048-buffer expansion or complete Havok rewrite. CorpseLimit stays 20. Nothing uploaded to GitHub or Nexus by the assistant.
