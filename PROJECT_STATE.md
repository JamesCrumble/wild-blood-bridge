# Wild Blood Bridge (Wild Blood -> Android APK для OnePlus 13s): состояние проекта

Цель: повторить для этой игры то, что сделано в `../nova3probe` и `../the-dark-knight-rises-probe`
(APK с qemu-user + мост GLES на GPU телефона). Каркас взят из `../the-dark-knight-rises-probe`
(коммит 9d57877), слой игры написан заново: движок здесь другой.
Архитектура обёртки: `port/android/README.md`.

Обновлено: 2026-10-13.

Рабочая папка: `/home/james/probe/wild-blood-probe`. Всё исполняется в WSL (Ubuntu-20.04).
Телефон: OnePlus 13s (CPH2723, Snapdragon 8 Elite, только AArch64), виден через Windows `adb.exe` (обёртка `tools/adb`).

## Правила работы
- По-русски, кратко. Писать, что делаю, перед каждым шагом; долгие прогоны только в фоне.
- Глобально ничего не ставить: тулчейны копировать из соседних проектов или ставить локально в папку проекта.
- git: пока только локальный репозиторий (ветка master), удалённого нет.
- Телефон: устанавливать APK, копировать данные, читать лог и перезапускать игру после установки можно
  (пользователь разрешил 13.10; `tools/build_install.sh` делает это сам).
- Мок-сервер Gameloft не делать, пока не найдена прямая зависимость игры от сервера (решение пользователя 13.10).
  Если понадобится: искать сервер в локальной сети на фиксированном порту 0,5 с, иначе поднимать свой.

## Игра
- `gamebin/WildBlood_v1.1.5_MaxGraphics.apk` (пакет `com.gameloft.android.ANMP.GloftWBHM`; в манифесте
  versionName 1.1.4, код 11423, сам движок пишет в лог «1.1.4d») и `gamebin/2014-Wild-Blood-v1.1.5-cache1.zip`
  (2,4 ГБ в распакованном виде).
- Библиотека одна: `lib/armeabi-v7a/libUnrealEngine3.so` (57 МБ, SHA1 `a03e390c718bb1f2ae04e8cb66700e33eb3ceb72`,
  ARMv7 + VFPv3, SONAME `libAOWGame-Android-Release.so`). Это **Unreal Engine 3** под Android-обвязкой
  Gameloft; экспортированы C++-символы движка (76 тыс.), так что функции ищутся по именам.
- Данные — плоские файлы: `files/AOWGame/CookedAndroid_ETC/*.xxx`, `files/AOWGame/Audio/sound.pack`,
  `files/Engine/`. Рядом в архиве лежит `main.1110.*.obb` (760 МБ) — те же файлы одним zip-ом, игре не нужен.
- Текстуры только ETC1 (`CookedAndroid_ETC`); Adreno их принимает как есть.

### Отличия от N.O.V.A. 3 и TDKR, из-за которых слой игры переписан
| что | TDKR | Wild Blood |
|---|---|---|
| движок | Glitch (Gameloft) | Unreal Engine 3 |
| JNI-классы с точками входа | `com/gameloft/glf/GL2JNILib` | `<пакет>/Game`, `GameRenderer`, `GameGLSurfaceView`, `MyVideoView` |
| обратные вызовы в Java | `GL2JNIActivity`, `GL2JNILib` | все — статические методы `<пакет>/Game` (64 штуки); ещё `SUtils`, `Device`, `GameInstaller`, `PushNotification/C2DMAndroidUtils`, `com/gameloft/glot/PortingJNI` |
| порядок запуска | `init` -> `initGL` -> `InitViewSettings` -> `resize` -> `step` | `JNI_OnLoad` -> `Game.nativeInit` -> `GameRenderer.nativeInit(2, ш, в, "1.1.4")` -> `nativeResize` -> `nativeRender` в цикле |
| кадр | порт сам делает swap после `step` | движок сам зовёт `Game.EglSwapR()`; swap в `wb_present()` |
| инициализация | в `init` | первый `nativeRender` = `appInit`, дальше ещё несколько кадров `FEngineLoop::InitStepbyStep` |
| пути | `setPaths` из Java | движок строит всё от `/data/data/<пакет>`, Java не спрашивает; весь этот путь отображён на папку кэша |
| DRM | нет | GDRM: четыре `lockPointer`, как в N.O.V.A. 3 (`port/game/patch.cpp`) |
| stdio | импорт из libc | в библиотеке своя копия (CrystaX): `fopen`, `fread`, `fseek`... перенаправлены на мост по именам (`patch.cpp`) |
| сеть | — | `getaddrinfo`/`gethostbyname` отвечают «нет сети» (раскладка `addrinfo` у bionic и glibc разная) |
| GL | GLES2 | GLES2; в мост добавлены `glDetachShader`, `glGetBooleanv`, `glIsShader`, `glFinish` |
| звук | Java `AudioTrack` (vox) | то же, `audio_track.cpp` без изменений |
| ролики | нет | `Game.JavaCallback_StartVideo/StopVideo` (mp4 в `AOWGame/Movies`); плеера нет, сразу «ролик закончился» |
| ввод | `touchEvent(action, x, y, id)` | `GameGLSurfaceView.nativeOnTouch(action, x, y, id, времяМс)`; коды те же: 1 вниз, 2 движение, 0 вверх |

## Что есть в папке
| путь | что |
|---|---|
| `port/game/` | слой игры: `main.cpp`, `io_paths.cpp`, `input_bridge.cpp`, `patch.cpp`, `tweaks.cpp`, `jni/` (классы игры, `AudioTrack`) |
| `port/portbase/` | база порта из TDKR Bridge; правки для этой игры: `symtab_net.cpp`, `symtab_io.cpp` (mkdir), `thunks/libc` (stdio, bitmap), `jni/classes/string.cpp` (`getBytes`), `gl_bridge.cpp` |
| `port/android/` | APK-обёртка (пакет `com.jamescrumble.wildblood`), `build_apk.sh` |
| `qemu/` | патчи и сборочные файлы qemu; бинарник `qemu/out/qemu-arm-F.stripped` скопирован, здесь не пересобирался |
| `jdk/ android-sdk/ sdl-arm/` | локальные тулчейны, скопированы из TDKR (в git не входят) |
| `port/android/build/sysroot/` | armhf sysroot, скопирован из TDKR |
| `wbdata/` | распакованная игра: `lib/armeabi-v7a/libUnrealEngine3.so`, пакет кэша, `files` — ссылка на `<пакет>/files` |
| `extracted/` | разбор APK: `classes.txt` (dexdump), `strings.txt`, `imports.txt`, `syms.txt` |
| `tools/` | `build_install.sh`, `run_pc.sh`, `push_data.sh`, `pull_log.sh`, `adb`; для разбора: `dexmethod.py` (метод из dex), `dis.sh` (дизассемблер диапазона) |

## Сделано (2026-10-13)
- Порт собирается (`./build.sh` -> `port/build/wb`), APK собирается (`tools/build_install.sh noinstall` ->
  `port/WildBlood-Bridge-debug.apk`, 57 МБ). Иконка берётся из APK игры (`res/drawable/icon.png`).
- На телефоне: данные скопированы `tools/push_data.sh` (1,6 ГБ без OBB), папки открыты `chmod 2777`.
  Игра запускается, **главное меню, 60 fps** при лимите 60 (снимок `logs/phone_main_menu.png`, лог `logs/phone.log`).
  До меню около минуты: движок компилирует шейдеры.
- На ПК (`tools/run_pc.sh`, qemu-arm + llvmpipe): 0 неразрешённых импортов, движок проходит `appInit` и начинает
  компилировать шейдеры; до картинки не доходит за разумное время. ПК годится только ловить падения на старте.
- На телефоне установлена сборка без класса `KeyEvent`; собранный APK с ним лежит в `port/`, не установлен.

## Как искалось (по порядку; у каждой правки — где она)
1. **`libjnigraphics.so` не загружается.** Загрузчик считает модулем всё из DT_NEEDED, чего нет в его списке.
   Добавлено в `so_builtin_libs` (`portbase/src/symtab.cpp`).
2. **11 неразрешённых импортов**: `__sfp`, `__sinit`, `__sflags`, `__smakebuf`, `__swsetup`, `__swbuf`, `__sglue`,
   `__sdidinit`, `AndroidBitmap_*`. Реализации в `portbase/thunks/libc/stdio.cpp` и `libc_table.cpp`
   (`__swbuf` -> `fputc`, остальные сообщают в лог, откуда вызваны).
3. **SIGSEGV в `Game.nativeInit` +0x14c8950**, запись по адресу 0. `tools/dis.sh 14c8908`: `str r2,[r3]`, где r3
   читается через GOT. Слот GOT (0x1e61538) -> 0x1f78afc, в таблице символов это `lockPointer4`. То же, что в
   N.O.V.A. 3: взят `patch.cpp` оттуда (`wb_unlock_drm`).
4. **Методы `Game.Exit/Pause/isHTCDevice/isAmazonDevice`, `String.getBytes`** — по строкам лога «does not have».
   Добавлены в `game/jni/game_classes.cpp` и `portbase/jni/classes/string.cpp`.
5. **SIGSEGV в +0x1a0ecf4** после «`__sflags() was called from …`». Адрес вызова попал в функцию `fopen`
   самой библиотеки (0x1a0f26c): у неё своя копия BSD-stdio из CrystaX (список: `awk` по `extracted/syms.txt`
   в диапазоне 0x1a0a000–0x1a4c000). Её `fopen` берёт `FILE` у bionic через `__sfp()` и сам заполняет поля, а
   импортируемые `fprintf/getc/ferror` идут на мост загрузчика — два несовместимых `FILE`. Решение:
   `wb_bridge_private_stdio` (`game/patch.cpp`) ставит хук на 17 экспортированных функций и ведёт их туда же,
   куда пошёл бы импорт с тем же именем. Вызывается из `so_after_relocate`, до статических инициализаторов.
6. **SIGSEGV в `FSocketSubsystemBSD::GetHostByName` +0x14c4b64** (фоновый поток). Читает `[r3,#24]` как
   `ai_addr`; в glibc по этому смещению `ai_canonname` (NULL). `getaddrinfo`, `freeaddrinfo`, `gethostbyname`
   в `portbase/src/symtab_net.cpp` отвечают «нет сети».
7. **Ничего не грузится**: в логе `io:` движок открывает `/data/data/<пакет>/AOWGame/CookedAndroid_ETC/Coalesced_INT.bin`
   (ENOENT), `getSDFolder` из Java не вызывает. Правило в `port_fix_path` (`game/io_paths.cpp`): весь
   `/data/data/<пакет>` -> папка кэша. После этого движок пишет свой `AOWGame/Logs/Launch.log`, по нему видно
   версию «1.1.4d», выбор ETC и то, что инициализация растянута на несколько кадров.
8. **Код 1 на телефоне**: «unresolved symbol» для `glDetachShader`, `glGetBooleanv`, `glIsShader`, `glFinish`
   (на ПК их даёт настоящий драйвер). `glDetachShader` добавлен в генератор (`android/tools/gen_glbridge.py`),
   остальные три — на стороне гостя в `portbase/src/gl_bridge.cpp`. Проверка всех 77 GL-импортов игры по
   таблице моста — цикл по `extracted/imports.txt`.
9. **Чёрный экран, 0 кадров**: `frames` растёт, `presented` стоит. Сначала подозревался ролик-заставка
   (`JavaCallback_StartVideo/StopVideo`; подтверждение остановки добавлено, не помогло). Затем в полной таблице
   символов (`readelf -s`, `extracted/symtab_all.txt`) найдены `callSwapBuffers`, `callSwapBuffersR` и
   `PlatformSwapBuffers(FES2Viewport*)` — последняя состоит из одной `bx lr` (+0x14aa05c). Кадр на Android
   показывает `GLSurfaceView` после каждого `onDrawFrame`; `EglSwapR` — только дополнительный показ на загрузке.
   В `game/main.cpp` кадр показывается после каждого `nativeRender`, если `EglSwapR` не сделал этого внутри кадра.
10. **`FindClass: android/view/KeyEvent`**: `initMap()` движка читает 72 константы `KEYCODE_*` через
    `GetStaticIntField`. Класс добавлен (`game/jni/`), а `GetStatic<числовой>Field` в `portbase/jni/jni.cpp`
    теперь возвращает значение поля, а не его адрес.

Попутно: папки, которые создаёт движок (0744), открываются через `chmod` в переходнике `mkdir`
(`portbase/src/symtab_io.cpp`) — иначе adb не читает лог движка.

## Не проверено / не сделано
- Геймпад: кнопки идут в `Game.nativeOnKeyDown/Up` с обычными Android-кодами, какие из них игра понимает — не выяснено;
  стики (`Game.nativeOnMotionEventPowerA`) не подключены.
- Всё после главного меню: кампания, бой, касания в игре, звук на слух, сохранения, пауза/возврат в приложение.
- Ролики не проигрываются (вступление и концовка пропускаются).
- `Game.getConfig()` (профиль качества, который Java выбирает по GPU: 3, 4, 5, 6, 8, 10) отдаёт 3; смысл значений
  не разобран. Меняется через `WB_CONFIG` в `engine_env.txt`.
- `GetBitmapFromText` (текст системным шрифтом в Bitmap) отвечает пустым результатом.
- Что читает движок из слов GDRM после записи — не проверялось.
- Сборка qemu из исходников в этой папке (исходников `qemu-src/` здесь нет).
- Настройки из меню (лимит кадров, кадров «в полёте», ядра 6–7, ADPF) на этой игре.
