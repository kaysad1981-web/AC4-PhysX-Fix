# Validation status

Packaging/installer: 0.3.1 native. Supplied game payload: unchanged 0.3 alpha, 404 files.

| Check | Local result |
|---|---|
| Release x86 compilation, static CRT | Passed |
| Engine assertions | 99 passed |
| Original payload + manual ZIP assertions | 415 passed |
| Native UI rendering | 30 states: RU/EN × five screens × 100/150/200% |
| Repeated renderer completion | Five consecutive passes |
| Embedded package verification | Exit code 0 |
| Installer and three DLLs: ordinary PE imports | Windows system libraries; no mandatory VC++ Redistributable/.NET imports |
| Local Defender scan of the local release candidate | Completed without detections |
| Publisher signature | None |

Engine tests cover install/update/remove, component changes, foreign-file conflicts, edited settings, pre-existing and new User.ini, legacy manifest migration, Unicode paths, unsupported EXEs, running-game rejection, cancellation before/during commit, hard links, manifest traversal, preparation/commit/removal errors and recovery after forcibly terminating a separate test process. Snapshots compare paths and file SHA-256 values. These are process-failure tests, not physical power-loss or disk-failure certification.

Original-package tests copy the real payload into an isolated fixture. They do not load the DLLs or launch the game. Synthetic game hashes/process checks belong to the test harness, not public setup arguments. The manual ZIP manifest is recognized and removed by the same engine.

The screenshots use the application's native rendering code; this is not a claim that every interactive Windows dialog has been automatically exercised.

## Still required

- Supported Steam (2023) EXE gameplay testing; the exact required hash is in the README.
- Clean Windows 10/11 VM tests, including dynamic dependency paths and protected-directory permissions.
- Real NVIDIA/AMD/Intel hardware coverage; source notes document RTX 5090 + i9-14900K only.
- Signing and distribution review as applicable. A local clean scan does not guarantee SmartScreen reputation, acceptance by other antivirus engines or Nexus/Steam approval.

GitHub Actions results are separate evidence from these local checks. Do not describe a pending CI run as passed.
