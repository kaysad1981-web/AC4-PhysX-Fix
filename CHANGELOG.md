# 0.3.2 alpha — 22 October 2026

- Integrates the scoped retired CameraFX.roll fix. Enabled automatically via [Physics] CameraFix=1.
- Detailed render/effect CSV diagnostics are disabled by default; F4 is not required in normal play.
- Adds guarded corpse-data reads and cached collision-group ownership cleanup, including shared group IDs and limit eviction.
- Preserves the 0.3.1 native installer design and Direct3D 11 PhysX payload (403 of 404 payload files unchanged).
- CorpseLimit stays 20. No 2048-buffer expansion, Havok-rate change or complete physics rewrite.
- Known issue: rare camera jerk during ship combat/jumping remains unresolved.

Validation distinguishes the reviewed private game build from the newly packaged default-on variant; see VALIDATION.md. Nothing was uploaded by the assistant.
