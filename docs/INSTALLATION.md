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


AC4 PHYSX FIX / MODULAR PATCH 0.3.2 ALPHA — РУЧНАЯ УСТАНОВКА
Бесплатный патч сообщества. ZIP заменяет установщик, а не дополняет его.

СОВМЕСТИМАЯ ВЕРСИЯ
Steam (2023), SHA256 файла AC4BFSP.exe:
bc11ad7924e1eca7363ee86aa3332c05386d0a05896c0b7370131daee9ab5150
Ручное копирование не проверяет версию игры и конфликты автоматически.

УСТАНОВКА
1. Закройте игру. Распакуйте архив в отдельную папку вне игры.
2. Если в игре уже есть dinput8.dll, nvcuda.dll, AC4Fix или AC4-PhysX-D3D, остановитесь:
   это может быть старая версия патча или другой мод. Используйте предусмотренное удаление/обновление.
   Чужие DLL не перезаписывайте. Для проверяемого обновления предпочтителен нативный установщик.
3. Скопируйте СОДЕРЖИМОЕ GAME_FILES рядом с AC4BFSP.exe, не саму папку GAME_FILES.
4. Запускайте игру как обычно. Фикс камеры включается автоматически; F4 нажимать не нужно.

НАСТРОЙКИ
Лимит CorpseLimit остаётся 20. Буферы и частота Havok в этом обновлении не увеличивались.
Подробная диагностика выключена. Автолимит FPS по-прежнему равен частоте монитора минус 3.
Личные изменения храните в AC4Fix/User.ini, с теми же разделами и ключами.
Для отключения фикса камеры добавьте [Physics] и CameraFix=0 в User.ini и перезапустите игру.
F10 по-прежнему переключает FPS/60 FPS. F4 доступна как диагностический переключатель при Diagnostics=1.

ИЗМЕНЕНИЯ
Подавляются подтверждённые завершённые вклады в крен CameraFX, живые эффекты не отключаются целиком.
Защищено чтение записей тел; очистка отметок групп не перечитывает уже недоступный объект.
Состав Direct3D 11 PhysX сохранён; из 404 файлов нагрузки изменён только dinput8.dll.

ОГРАНИЧЕНИЯ
Редкий рывок камеры при корабельном бое/прыжке ещё НЕ исправлен. Это не отключение всех поворотов
и не полная переработка Havok. Риски времени жизни объектов и параллельной обработки полностью не закрыты.
Длительный заезд частной CAMTEST09 разобран. Упакованный вариант с автоматическим включением и
выключенной по умолчанию диагностикой прошёл стенды, но собственного полного игрового заезда ещё не было.
Поддерживается только указанный EXE. Проверки на AMD/Intel пока неполные.

УДАЛЕНИЕ
Предпочтительно использовать нативный установщик: выбрать игру и «Удалить патч».
Сохраните AC4Fix/install-manifest.json. Без установщика удаляйте только собственные файлы патча
из FILELIST_SHA256.txt относительно GAME_FILES, предварительно сохранив User.ini и нужные логи.
Не трогайте файлы других модов, AC4BFSP.exe, архивы игры и сохранения.
Ручная установка не имеет автоматического отката. В ZIP нет установщика или скриптов.
