# Положение окон: клиент 3.3.5a, build 12340

Кандидат реализует `SetUserPlaced` / `IsUserPlaced`, `SetDontSavePosition` /
`GetDontSavePosition` и локальную раскладку окон. UE-сборка, UE automation,
проверка в отрисованном клиенте и сравнение с запущенным оригиналом передаются
сессии приёмки. Здесь не заявляется, что эти проверки уже прошли.

## Правила оригинала

Использована локальная реконструкция `D:/Games/idle_wow/src/wowdecomp`, нацеленная
на оригинальный `Wow.exe` 12340. Проверенные функции и адреса:

- `CSimpleFrame_Scripts.cpp`: `Script_SetUserPlaced` 0x004A0C70,
  `Script_IsUserPlaced` 0x004A0D00; Set/GetDontSavePosition 0x004A08C0/0x004A0910.
- `CSimpleTop.cpp`: `StartMoveOrResizeFrame` 0x00494060,
  `StopMoveOrResizeFrame` 0x00493A10.
- `CLayoutFrame.cpp`: `FlattenFrame` 0x0048A5F0, `UnflattenFrame` 0x0048A840.
- `GameUI.cpp`: `PlacedFrameCallback` 0x00517E00, `PlaceFrame` 0x00518280,
  `LoadPlacedFrames` 0x005183A0, `SavePlacedFrames` 0x005234A0.
- `WowSvcsClient/AccountData.cpp`: слот 6 — `layout-local.txt`, локальный файл
  персонажа без серверного cache-файла. Загрузка следует за стартовыми аддонами
  и предшествует `VARIABLES_LOADED` и `PLAYER_LOGIN`.

`SetUserPlaced` проверяет movable/resizable даже при выключении флага. Без
аргумента включает флаг; `nil`, `false`, число 0 и строки false/off/disabled
выключают его по старому `StringToBOOL`. `IsUserPlaced` возвращает `1` или `nil`.
Принятый `StartMoving` или `StartSizing` включает флаг автоматически. Остановка
не включает его повторно: аддон может вызвать `SetUserPlaced(false)` прямо
после начала перемещения.

При начале перемещения прежние ограничения заменяются одним `TOPLEFT` к
экрану, сохраняются фактические размеры. При остановке выбирается из девяти
соответствующих экранных точек пара с минимальным квадратом расстояния.
Равные расстояния разрешаются в порядке TOPLEFT, TOP, TOPRIGHT, LEFT, CENTER,
RIGHT, BOTTOMLEFT, BOTTOM, BOTTOMRIGHT. Смещения делятся на полный масштаб окна.
Остановка происходит и при скрытии окна либо его родителя.

Сохраняются только именованные окна с userPlaced и movable/resizable,
без dontSavePosition, с определённым прямоугольником. Непосредственный родитель
не обязан быть UIParent. Анонимные внутренние имена движка не сохраняются.
Масштаб, видимость, родитель и произвольные Lua-поля не сохраняются.

Файл: `Saved/WTF/Account/<account>/<realm>/<character>/layout-local.txt`.
Формат оригинала — `Version: 1`, затем Frame/FrameLevel/Anchor/X/Y/W/H.
Координаты и размер округляются до целых локальных UI-единиц с ties-to-even.
Сохранение выполняется через центральный `SaveAll` для ReloadUI/Logout/Quit
и отдельно до отсоединения контекста при завершении UI. Путь закреплён за
экземпляром FrameManager, а не вычисляется заново при сохранении старой VM.

Восстановление меняет уровень окна; якорь применяется только к movable,
размер — только к resizable. Сохраняемый масштаб определяется XML/аддоном
заново. Восстанавливаются уже существующие стартовые окна; поздно созданным
окнам/аддонам автоматическая отложенная загрузка позиции не добавляется.

`SetUserPlaced(false)` отключает последующее сохранение, сохраняя текущее
положение до ReloadUI. Для немедленного сброса аддон отдельно задаёт исходные
якоря. После сохранения отключённой позиции запись исчезает из файла, и при
следующей загрузке остаются исходные якоря XML/аддона.

## Самостоятельные проверки

Запускать из корня приватного клона:

```powershell
cmake -S BuildSupport/UserPlacedLayout -B Intermediate/UserPlacedLayoutChecks -G 'Visual Studio 17 2022' -A x64 -DCMAKE_SUPPRESS_REGENERATION=ON
cmake --build Intermediate/UserPlacedLayoutChecks --config Release --target user-placed-layout-tests -- /m:1 /nr:false
cmake --build Intermediate/UserPlacedLayoutChecks --config Release --target layout-lua-check -- /m:1 /nr:false
ctest --test-dir Intermediate/UserPlacedLayoutChecks -C Release --output-on-failure
```

Проверяются точные координаты, масштаб, ближайший якорь, порядок равных
расстояний, округление, 20 000 случайных восстановлений координат, смена
разрешения, строки StringToBOOL, синтаксис аддона и всех Lua-шагов сценариев
на встроенной Lua 5.1.5, JSON/XML/TOC и уникальность четырёх регистраций API.
Lua-выражения новых UE-тестов также проходят отдельную синтаксическую проверку.

UE automation для приёмки:
`Automation RunTests WowUnreal.UI.UserPlaced` — два теста в отдельном
`IdleWowUserPlacedTests.cpp`: ApiAndMovement и CharacterPersistence. Они
проверяют реальный Lua API, drag scripts, координаты, скрытие, resize, файлы,
новую VM, сброс, XML dontSavePosition, анонимные/неподвижные окна, масштаб
именованного потомка, раздельное применение movable/resizable и пути
account/realm/character. Общий WowUnreal regression также обязателен.

## Сценарий ввода для приёмки

Использовать выделенный QA UserDir и персонажа. До запуска установить только
каталог `Scripts/Verification/UserPlacedLayout/IdleWowLayoutProbe` как
`<QA wowdata>/Interface/AddOns/IdleWowLayoutProbe`. В этом проекте файловый
fallback аддонов находится под **DataPath**, а не рядом с DataPath. MPQ
используются из пользовательского набора без изменения. Аддон не объявляет
SavedVariables. Для оригинального WoW каталог устанавливается в обычный
`<WoW>/Interface/AddOns`.

Приёмка добавляет `output` в копию каждого JSON и передаёт эту копию через
`-gameplayprobe=<absolute JSON>`. Каждый запуск — новый процесс с тем же
выделенным UserDir, аккаунтом, realm и персонажем; завершение должно быть
штатным. Не очищать профиль между фазами.

1. `01-drag-reload.json`: исходное окно 160x80, левый верхний угол (80,60).
   Реальный путь drag-скриптов с dx=60/dy=40 даёт (140,100). Второе окно с
   dontSavePosition перемещается, но после ReloadUI возвращается к (300,60),
   основное остаётся на (140,100), анонимное не получает сохранённый флаг.
   Правой кнопкой выполняется resize на 20x10: размер 180x90. Левый drag на
   644 даёт (784,100) и `TOPRIGHT, UIParent, TOPRIGHT, -60, -100`.
   При 1280x720 логическая ширина UI равна 1365 1/3, координата x=1125 1/3;
   физически левый верхний угол (1055,93.75), размер (168.75,84.375).
   Возврат к 1024x768 даёт (784,100). Три UE capture должны быть непустыми.
2. После полного выхода проверить запись основного окна: TOPRIGHT/-60/-100/
   W180/H90. NoSave и анонимного окна в `layout-local.txt` быть не должно.
   `02-full-restart-reset.json` в новом процессе сначала проверяет те же
   координаты и размеры после полного перезапуска. Затем SetUserPlaced(false)
   сохраняет положение (784,100) до ReloadUI; ReloadUI восстанавливает исходные
   (80,60), 160x80 и nil-флаг. Два UE capture должны быть непустыми.
3. `03-default-after-restart.json` в третьем процессе проверяет, что сброс
   пережил полный выход: (80,60), 160x80, nil-флаг. Один UE capture.

Допуск координат .05 UI-единицы; проверять `probe.json` каждой фазы и визуально
просмотреть все UE capture. Шаги `drag_frame` проходят hit-test и обычные
mouse/drag callbacks с четырьмя последовательными положениями указателя.
Они не подменяют перемещение вызовами SetPoint.

Для ручного сравнения с оригиналом 12340 повторить эти положения при
1024x768, проверить `LayoutProbeExpect` через `/run`, ближайший GetPoint,
файл, `/reload` и полный выход/вход. Функции аддона совместимы с Lua 5.1
оригинала. Изоляцию персонажей проверить тем же аддоном на втором выделенном
персонаже того же account/realm: до перемещения он должен иметь исходные
(80,60), 160x80; первый персонаж до фазы сброса должен сохранить TOPRIGHT/-60/
-100. Повторить на другом realm/account либо проверить отдельные каталоги.
Не обходить ограничения lifecycle-фикстуры, если она допускает лишь одного
персонажа: эту часть выполняет приёмка со своими разрешёнными QA-профилями.

После проверки удалить QA-аддон из выделенного DataPath; пользовательские
профили и существующие раскладки не затрагивать.

## Общие точки изменений

В общей таблице LuaFrameApi заменены два stub и SetDontSavePosition,
добавлен GetDontSavePosition. В WowUIManager добавлены загрузка до startup
events и сохранение до ClearContext. В SaveAll добавлен вызов FrameManager.
XML-парсеры и MergeTemplate получили только dontSavePosition. Математика,
формат файла, UE-тесты и сценарии вынесены в отдельные тематические файлы.
ClearFrameAnchors теперь использует общий SetFrameAnchors, чтобы после
ClearAllPoints сохранение не читало старый закешированный прямоугольник.
