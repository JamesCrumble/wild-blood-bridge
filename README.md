# Wild Blood Bridge

**English:** run **Wild Blood 1.1.5** (Gameloft, Unreal Engine 3, 32-bit ARMv7) on 64-bit-only Android phones
that have no AArch32 support. The game runs under a patched `qemu-user`, and its OpenGL ES calls are forwarded to
the phone's real GPU. Beta: on a OnePlus 13s (Snapdragon 8 Elite) the game starts and shows its main menu at
60 fps; gameplay has not been tested yet.
No game files are included: you need your own copy. The rest of this page is in Russian.

Запуск 32-битной **Wild Blood** (Gameloft, v1.1.5, Unreal Engine 3, только `armeabi-v7a`) на 64-битных
Android-телефонах, в которых нет поддержки AArch32.

Игра исполняется в пропатченном `qemu-arm` (linux-user), а её вызовы OpenGL ES пробрасываются на настоящий
GPU телефона. Способ тот же, что в NOVA3 Bridge, TDKR Bridge, Shadowgun Bridge и Mass Effect Infiltrator Bridge;
каркас взят из TDKR Bridge, слой игры написан под Unreal Engine 3.

**Игра не распространяется.** Нужны ваши собственные APK версии 1.1.5 и кэш к ней.

[Состояние](#состояние) · [Установка](#установка) · [Настройки](#настройки) · [Как это устроено](#как-это-устроено) ·
[Что пришлось сделать для этой игры](#что-пришлось-сделать-для-этой-игры) · [Структура](#структура) ·
[Сборка](#сборка) · [Диагностика](#диагностика) · [Другие игры](#другие-игры) ·
[Лицензия и авторство](#лицензия-и-авторство)

## Состояние

Бета.

- На OnePlus 13s (Snapdragon 8 Elite, Android 15) игра запускается и показывает главное меню, 60 fps при лимите 60.
- Дальше меню не проверялось: кампания, бой, звук на слух, сохранения, долгая игра.
- Касания подключены, в игре не проверены. Геймпад: кнопки передаются, стики не подключены.
- Ролики (вступление, концовка) не проигрываются — пропускаются.
- Сети нет: игра работает по ветке «нет интернета». Мультиплеер, магазин и GLLive недоступны.
- Проверено на одном устройстве.

## Установка

1. Установите `WildBlood-Bridge-<версия>.apk` (пакет `com.jamescrumble.wildblood`).
2. Запустите приложение один раз и закройте: появится папка `/sdcard/Android/data/com.jamescrumble.wildblood/files/`.
3. Положите в неё два файла как есть: оригинальный APK игры 1.1.5 и zip-архив с её кэшем. Имена не важны.
   Приложение само распакует архив при первом запуске игры (около 1,5 ГБ).
4. Откройте приложение, при необходимости поменяйте настройки и нажмите «Играть».

Распаковка вручную тоже подходит: папку `com.gameloft.android.ANMP.GloftWBHM` из архива кэша (внутри
`files/AOWGame`, `files/Engine`) положить в ту же папку целиком; файл `main.*.obb` из архива не нужен.
Если копировали по adb, откройте папки приложению (adb создаёт их от имени `shell`):
`adb shell "find /sdcard/Android/data/com.jamescrumble.wildblood -type d -exec chmod 2777 {} +"`

Из папки проекта ручную раскладку делает `tools/push_data.sh`. Если чего-то не хватает, причина пишется на экране и в
`wb.log` в папке с файлами игры. Первый запуск дольше: распаковывается sysroot (~100 МБ), а игра компилирует
свои шейдеры (около минуты до меню).

Сохранения, настройки и лог самой игры (`AOWGame/Logs/Launch.log`) лежат в той же папке кэша.

## Настройки

Экран настроек открывается при старте приложения. Значения хранятся в `settings.txt` рядом с файлами игры.

| Настройка | Ключ | Что делает |
|---|---|---|
| Разрешение рендера (высота) | `RENDER_HEIGHT` | 480 / 540 / 720, по умолчанию 720 |
| Лимит кадров | `FPS_LIMIT` | 30 / 60 / 90 / 120, по умолчанию 60 |
| Счётчик FPS | `FPS_COUNTER` | Показывает fps поверх игры |
| Буфер звука | `AUDIO_LATENCY_MS` | Размер буфера AudioTrack |
| Кадров «в полёте» | `WB_GL_INFLIGHT` | Сколько кадров мост может держать в очереди к GPU, по умолчанию 2 |
| Только быстрые ядра (6–7) | `CPU_PRIME` | Привязывает qemu к двум самым быстрым ядрам; по умолчанию включено |
| Подсказки производительности (ADPF) | `PERF_HINT` | Сообщает системе длительность кадров; по умолчанию выключено |
| Профилирование в логе | `QEMU_GUEST_PROF` | Диагностика |

Настройки унаследованы от TDKR Bridge; на этой игре проверен только запуск со значениями по умолчанию.

## Как это устроено

```
 APK-обёртка (Java, AArch64)                    дочерний процесс
 ┌──────────────────────────────┐   pipe   ┌─────────────────────────────────────────┐
 │ SettingsActivity             │ ◄──────► │ qemu-arm (наш патч, статический AArch64) │
 │ MainActivity + SurfaceView   │  команды │  └ wb (порт-лоадер, ARM32, glibc)        │
 │ GlBridge: исполняет GLES     │   GLES   │      ├ bionic ELF loader                 │
 │ AudioTrack, касания, геймпад │          │      ├ libUnrealEngine3.so (игра)        │
 │ PerfHint (ADPF, телеметрия)  │          │      ├ JNI и Android API на заглушках    │
 └──────────────────────────────┘          │      └ GL-мост: сериализует вызовы GLES  │
                                           └─────────────────────────────────────────┘
```

- **Порт-лоадер** (`port/game/`, `port/portbase/`) загружает библиотеку игры, собранную под bionic, в процесс
  с glibc и подменяет libc, JNI и Java-классы игры (`port/game/jni/`).
- **GL-мост** (`port/portbase/src/gl_bridge.cpp` ↔ `port/android/src/.../GlBridge.java`): вызовы GLES пишутся
  в pipe и исполняются на стороне Java в контексте GLES 3.
- **Звук**: игра выводит звук через Java-класс `AudioTrack`; порт подставляет свой класс
  (`port/game/jni/audio_track.cpp`), тот пишет в FIFO, а обёртка играет его настоящим `AudioTrack`.
- **qemu** (`qemu/`): QEMU 11.1.2 с патчами из NOVA3 Bridge (вариант F), взят готовым.

## Что пришлось сделать для этой игры

Игра — Unreal Engine 3 под Android-обвязкой Gameloft. В порядке, в котором это всплывало:

| Что мешало | Как решено | Где |
|---|---|---|
| Загрузчик не знал `libjnigraphics.so`, в библиотеке — импорт внутренностей stdio из bionic | библиотека добавлена во встроенные, символы и `AndroidBitmap_*` получили реализации | `portbase/src/symtab.cpp`, `portbase/thunks/libc/` |
| GDRM: `Game.nativeInit` пишет по нулевому `lockPointer4` | четырём `lockPointer` выдана память (как в N.O.V.A. 3) | `game/patch.cpp` |
| Игра требует методы Java, которых нет | 64 статических метода класса `Game` и ещё пять классов | `game/jni/game_classes.cpp` |
| `String.getBytes(charset)` | добавлен в фиктивный `String` | `portbase/jni/classes/string.cpp` |
| В библиотеке своя копия stdio (CrystaX): её `fopen` строит `FILE` через `__sfp()` из bionic | 17 функций перенаправлены на мост загрузчика по именам | `game/patch.cpp` |
| Падение при поиске адреса сервера: раскладка `struct addrinfo` в bionic и glibc разная | разрешение имён отвечает «нет сети» | `portbase/src/symtab_net.cpp` |
| Движок ищет данные в `/data/data/<пакет>`, а не в папке кэша | весь путь отображён на папку кэша | `game/io_paths.cpp` |
| В мосте не было `glDetachShader`, `glGetBooleanv`, `glIsShader`, `glFinish` | добавлены | `portbase/src/gl_bridge.cpp`, генератор |
| Чёрный экран: `PlatformSwapBuffers` движка — пустая функция, кадр на Android показывает `GLSurfaceView` | порт показывает кадр после каждого `nativeRender` | `game/main.cpp` |
| Движок читает `KeyEvent.KEYCODE_*` статическими int-полями | класс `KeyEvent`; чтение статических числовых полей возвращает значение | `game/jni/`, `portbase/jni/jni.cpp` |

Пошаговый разбор — что было видно, как нашли причину, что исправили — в
[docs/PORTING_GUIDE.md](docs/PORTING_GUIDE.md) ([English](docs/PORTING_GUIDE.en.md)); текущее состояние — в [PROJECT_STATE.md](PROJECT_STATE.md). История версий — в
[CHANGELOG.md](CHANGELOG.md). Архитектура обёртки — в [port/android/README.md](port/android/README.md).

## Структура

| Папка | Что |
|---|---|
| `port/game/` | слой этой игры: точка входа, пути, Java-классы игры, ввод, GDRM и stdio |
| `port/portbase/` | общая база порта: загрузчик bionic-ELF, JNI, переходники libc, GL-мост |
| `port/android/` | APK-обёртка: Java-код, манифест, сборка APK и sysroot |
| `qemu/` | патчи qemu 11.1.2, Dockerfile для кросс-сборки, тесты точности, бенчмарк |
| `tools/` | сборка и установка, запуск на ПК, логи, разбор APK |

В репозитории нет файлов игры, тулчейнов и сборочных каталогов (см. `.gitignore`).

## Сборка

Сборка идёт в WSL (Ubuntu). Глобально ничего ставить не нужно, кроме уже имеющихся в системе
`gcc-arm-linux-gnueabihf`/`g++-arm-linux-gnueabihf`, `qemu-arm` и Go (для `mstrace`).

Кладутся в корень проекта и в git не входят:

- `jdk/` — JDK 17;
- `android-sdk/` — build-tools 34.0.0, platform 34, platform-tools;
- `sdl-arm/` — SDL2 под armhf;
- `qemu/out/qemu-arm-F.stripped` — собранный qemu;
- `port/android/build/sysroot/` — armhf-библиотеки (или `port/android/make_sysroot.sh`);
- `gamebin/` — ваш APK игры и архив кэша, `wbdata/` — распакованная игра
  (`lib/armeabi-v7a/libUnrealEngine3.so`, `com.gameloft.android.ANMP.GloftWBHM/files`, ссылка `files` на неё).

```sh
tools/build_install.sh noinstall   # порт + APK -> port/WildBlood-Bridge-debug.apk
tools/build_install.sh             # то же, установка на телефон по adb и перезапуск игры
tools/push_data.sh                 # APK игры и кэш на телефон, права на папки
tools/run_pc.sh 600                # запуск на ПК под qemu-arm, 600 кадров, лог в logs/pc_run.log
tools/pull_log.sh                  # лог с телефона -> logs/phone.log
tools/make_release.sh              # релизный APK, архив исходников и INSTALL.txt в release/<версия>/
```

Телефон из WSL виден через виндовый `adb.exe`: обёртка `tools/adb`.

## Диагностика

- Лог порта: `tools/pull_log.sh` → `logs/phone.log`. Строка `[gl] frames` раз в несколько секунд показывает fps,
  число GL-команд, загрузку моста, частоту ядра; `frames=N presented=N movie=M` — счётчик кадров порта.
- Лог самого движка: `AOWGame/Logs/Launch.log` в папке кэша.
- `engine_env.txt` рядом с логом (строки `KEY=VALUE`): `WB_CONFIG` — профиль качества, который родная Java
  выбирала по GPU (3, 4, 5, 6, 8, 10; по умолчанию 3), `WB_GPU_NAME` — имя GPU, которое видит игра.
- На ПК `tools/run_pc.sh` годится, чтобы поймать падение на старте; до картинки под программным рендером
  игра идёт очень долго (компиляция шейдеров).
- Разбор APK: `tools/dexmethod.py <класс> [метод] [--calls]` печатает метод из dex,
  `tools/dis.sh <адрес> <длина>` — дизассемблер участка библиотеки.

## Другие игры

Тот же способ запуска — qemu-user и мост GLES на GPU телефона — для других 32-битных игр:

- [NOVA3 Bridge](https://github.com/JamesCrumble/NOVA3-Bridge) — N.O.V.A. 3 1.0.7 (Gameloft)
- [TDKR Bridge](https://github.com/JamesCrumble/the-dark-knight-rises-bridge) — The Dark Knight Rises 1.1.6 (Gameloft)
- [Shadowgun Bridge](https://github.com/JamesCrumble/shadowgun-bridge) — SHADOWGUN 1.7.0 (Unity 5.3)
- [Mass Effect Infiltrator Bridge](https://github.com/JamesCrumble/mass-effect-infiltrator-bridge) — Mass Effect Infiltrator 1.0.58

## Лицензия и авторство

GPL-3.0, см. [LICENSE](LICENSE). Происхождение заимствованных частей и их лицензии — в
[port/NOTICE.md](port/NOTICE.md). Патч qemu — GPL-2.0-or-later, как сам QEMU.

Порт N.O.V.A. 3 для PortMaster и его `portbase`, на которых основан проект, созданы EapRules.
Android-мост и слой этой игры написаны JamesCrumble; в работе над этим проектом участвовал Claude (Anthropic).

Wild Blood © Gameloft. Unreal Engine © Epic Games. Проект не связан с правообладателями и не содержит файлов игры.
