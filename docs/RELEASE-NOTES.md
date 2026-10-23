# Assassin's Creed IV: Black Flag — PhysX & High FPS Fix 0.3.2 alpha

## Русский

Бесплатное обновление: исправление зависшего крена камеры теперь включается автоматически, F4 для обычной игры не нужен. Добавлены защищённое чтение записей тел и очистка отметок групп без повторного обращения к недоступному объекту. Нативный установщик сохранил прежнее оформление RU/EN. Лимит физики трупов остаётся 20, подробные CSV-журналы по умолчанию выключены.

**Известная проблема:** редкий рывок камеры при корабельном бое/прыжке пока не исправлен. Это не полная переработка Havok. Проверенный длительный заезд частной сборки и стендовые проверки упакованного варианта описаны в docs/VALIDATION.md; полный отдельный игровой заезд варианта с автоматическим включением ещё не проведён.

Выберите **установщик EXE или ручной Nexus ZIP**. Для ZIP копируйте содержимое `GAME_FILES` рядом с `AC4BFSP.exe`. Не перезаписывайте чужие DLL. Для обновления с 0.3.1 предпочтителен установщик. Source.zip — исходники для разработчиков, не ручная установка.

## English

Free update: the scoped retired CameraFX.roll fix is enabled automatically; F4 is not required in normal play. Adds guarded corpse reads and cached collision-group cleanup without rereading removed objects. Retains the native RU/EN installer design. CorpseLimit stays 20; detailed CSV diagnostics default off. Only dinput8.dll changed among the 404 payload files.

**Known issue:** a rare camera jerk during ship combat/jumping remains unresolved. This is not a complete Havok rewrite. Local checks and the distinction between the reviewed private game build and packaged default-on variant are documented in docs/VALIDATION.md. CI checks installation/packaging, not complete gameplay or Havok lifetimes.

Choose **setup EXE or manual Nexus ZIP**. Copy the contents of `GAME_FILES` next to the game EXE for manual installation. Do not overwrite another mod's DLL. Setup is preferred for validated updates. Source.zip is developer source, not the manual game package.

## Supported build / Совместимость

Only Steam (2023) AC4BFSP.exe SHA256:
`bc11ad7924e1eca7363ee86aa3332c05386d0a05896c0b7370131daee9ab5150`.

Unsigned alpha. Assets include setup, manual ZIP, source ZIP and SHA256SUMS.txt. The previous 0.3.1 release remains available.
