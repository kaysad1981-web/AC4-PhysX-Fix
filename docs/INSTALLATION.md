# Installation / Установка

Use setup or the manual ZIP, not both. / Используй EXE либо ручной ZIP, не оба подряд.

The manual package instructions are maintained in [Русский](../manual-docs/README_RU.txt) and [English](../manual-docs/README_EN.txt).

## Native installer

Keep setup outside the game folder. Close the game before any operation. On the folder screen, setup validates the supported original Steam (2023) executable. If a manifest exists, the same screen offers **Remove patch / Удалить патч**, while Next opens component changes.

Original game files and saves are not replaced. Foreign `dinput8.dll`, `nvcuda.dll` or conflicting managed files cause setup to stop. Do not remove another mod to force installation unless you understand that mod's own uninstall instructions.

## Removal and recovery

The installation manifest is `AC4Fix/install-manifest.json`. Retain it until uninstall. Setup verifies tracked file hashes; a DLL subsequently replaced by another mod is not silently deleted.

Changed settings, newly created User.ini and patch logs may be backed up under `%LOCALAPPDATA%/AC4-Modular-Patch/Removed/…`. A pre-existing User.ini is retained. If importing an older manifest with no reliable history, User.ini is also retained conservatively. Unknown files are kept; only empty owned directories are removed.

If setup terminates during a transaction, do not delete `.AC4Patch-transaction` or start the game. Reopen setup, select the folder and accept recovery. A corrupted backup or conflicting external modification stops recovery with an error rather than overwriting the conflict.

The remembered path is stored in `%LOCALAPPDATA%/AC4-Modular-Patch/native-preferences.json`. Preferences and recovery backups can remain outside the game after uninstall. Removal restores the patch's file changes; it does not reset other mods, saves, game settings or unrelated changes.

## Русское пояснение

Храни установщик отдельно от игры и запускай его повторно для удаления. На экране папки игры появится «Удалить патч». Исходный EXE и игровые данные не перезаписываются, сохранения не затрагиваются. При конфликте с чужим файлом установка/удаление останавливается.

Манифест и журнал незавершённой операции нельзя удалять вручную до восстановления. Изменённые настройки и журналы сохраняются вне игры; неизвестные файлы и существовавший ранее User.ini остаются. Это возврат изменений нашего патча, а не сброс всех остальных модов и пользовательских данных.
