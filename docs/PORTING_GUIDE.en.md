# How Wild Blood was ported: a step-by-step account

Русская версия: [PORTING_GUIDE.md](PORTING_GUIDE.md).

This file records how the game was taken from "an APK and a cache archive" to the main menu on the phone,
written as a guide for the next port of this kind. Every step has the same shape: what was seen, how the
cause was found, what was changed and where. The commands are given so they can be repeated.

Status on 2026-10-13: main menu, 60 fps on a OnePlus 13s. Nothing past the menu has been tested.
The short summary is in [README.md](../README.md); the current state and the list of what is unfinished are
in [PROJECT_STATE.md](../PROJECT_STATE.md) (Russian).

## Where to start

The frame (qemu, the APK wrapper, the GL bridge, the loader) is taken whole from the closest finished
project and renamed. Here that was TDKR Bridge. Only the game layer in `port/game/` is rewritten.

The first thing to establish is what game is inside — that decides how much of the frame fits unchanged:

```sh
unzip -l gamebin/<game>.apk | grep -v res/          # which libraries the APK holds
readelf -hd lib.so | grep -E 'NEEDED|SONAME'        # what it depends on and what it calls itself
readelf --dyn-syms -W lib.so | awk '$7=="UND"{print $8}' | sort > extracted/imports.txt
readelf --dyn-syms -W lib.so | grep -E 'Java_|JNI_OnLoad'      # entry points
dexdump -d classes.dex > extracted/classes.txt      # the Java side
strings -n 6 lib.so > extracted/strings.txt
```

Wild Blood's library is called `libUnrealEngine3.so` and its SONAME is `libAOWGame-Android-Release.so`.
This is Unreal Engine 3, not Gameloft's own engine as in N.O.V.A. 3 and TDKR, so the game layer had to be
written from scratch.

Three questions to answer before the first line of code:

1. **In what order Java calls the native functions.** Read from the dex. `tools/dexmethod.py <class>
   [method] --calls` prints only the calls of a method. Here the order is: `Game.nativeInit` →
   `GameRenderer.nativeInit(manufacturer, width, height, version)` → `nativeResize` → `nativeRender` in a
   loop.
2. **Which Java methods the library itself calls.** The names sit in the library's strings as one group;
   the signatures come from the dex. The fake JNI matches name and signature exactly, so signatures must
   not be written from memory.
3. **What each entry point does.** `tools/dis.sh <address> <length>` disassembles a range. This library
   keeps its function names, so it is visible that `nativeRender` is `appUpdate`, which runs `appInit` on
   its first call and `FEngineLoop::Tick` after that.

## How to find the cause of a crash

The procedure is always the same:

1. Run, and take the crash address from the log: `pc = … (libUnrealEngine3.so+0x…)`.
2. Find the function by address in `extracted/syms.txt` and disassemble the spot:
   `tools/dis.sh <address> 60`.
3. If the pointer is read through the GOT, work out the slot and find it in `readelf -rW`; look the slot's
   value up in the symbol table. That is how global variables are found by name. An example is worked
   through below.
4. Fix the cause where it arises, not the place where it crashes.

A run on the PC comes first (`tools/run_pc.sh <frames>`): it catches start-up crashes in seconds. This game
does not reach a picture on the PC — compiling its shaders under software rendering takes tens of minutes.
Everything about the picture and speed is checked on the phone only.

Long runs are started in the background with a time limit, and the log is read with short one-off commands.

### Example: from a crash address to a variable's name

The crash of step 2: `pc = +0x014c8950`, the instruction `str r2, [r3]`, zero in `r3`. Higher up:

```
14c890c  ldr r8, [pc, #2880]    ; offset to the GOT, stored at 0x14c9454
14c8918  ldr r3, [pc, #2876]    ; offset of the slot inside the GOT, stored at 0x14c945c
14c891c  add r8, pc, r8         ; r8 = 0x14c891c + 8 + [0x14c9454]
14c8934  ldr r3, [r8, r3]       ; r3 = the slot's contents = the variable's address
14c8940  ldr r3, [r3]           ; r3 = the variable's value (zero)
14c8950  str r2, [r3]           ; store through zero
```

The slot is `0x14c891c + 8 + word_at_0x14c9454 + word_at_0x14c945c`, which comes to `0x1e61538`. Then two
commands:

```sh
readelf -rW libUnrealEngine3.so | grep -i '^01e61538'   # R_ARM_RELATIVE: the slot holds an address
grep -i '^01f78afc' extracted/syms.txt                  # 01f78afc 4 OBJECT lockPointer4
```

The slot's value (`0x1f78afc`) is read from the file at the same offset: in this library the addresses of
the first segment equal file offsets, and so do the second's (check with `readelf -lW`).

The full symbol table, local symbols included, is produced by `readelf -sW`. It has 280 thousand entries
against 76 thousand in the dynamic one, and that is where `callSwapBuffers` and `PlatformSwapBuffers` were
found (step 8).

## The steps

### 1. The library does not load

- **Seen:** `Failed to load module 'libjnigraphics.so'`, then 11 `unresolved symbol` lines.
- **Cause:** the loader treats everything in DT_NEEDED that is not on its list of system libraries as a
  module to map. The unresolved symbols are bionic's stdio internals (`__sfp`, `__sinit`, `__sflags`,
  `__smakebuf`, `__swsetup`, `__swbuf`, `__sglue`, `__sdidinit`) and three `AndroidBitmap_*` functions.
- **Fix:** the library was added to `so_builtin_libs` in [symtab.cpp](../port/portbase/src/symtab.cpp); the
  symbols got implementations in [stdio.cpp](../port/portbase/thunks/libc/stdio.cpp) and
  [libc_table.cpp](../port/portbase/thunks/libc/libc_table.cpp). The stubs log the caller's address — that
  paid off in step 4.

### 2. The crash in `Game.nativeInit`: GDRM protection

- **Seen:** `SIGSEGV at 0x00000000`, `pc = +0x014c8950`, the function's ninth instruction group.
- **How it was found:** the disassembly shows `str r2, [r3]` with `r3` read through the GOT. GOT slot
  `0x1e61538` holds `0x1f78afc`, which the symbol table calls `lockPointer4`; `lockPointer1..3` sit next
  to it.
- **Cause:** GDRM is Gameloft's licence check. On Android the installer activity performs it before the
  game starts and leaves the result in four global pointers. The installer is not run here (the licence
  server was switched off long ago), the pointers stay null, and the engine writes through them without
  checking.
- **Fix:** the four pointers are each given a word of storage — `wb_unlock_drm` in
  [patch.cpp](../port/game/patch.cpp). The game's code is not patched. N.O.V.A. 3 uses the same solution.
- **Not checked:** whether the engine later reads those words as a sign of a licence.

### 3. Missing Java methods

- **Seen:** lines `Class Game does not have static method …`, `Class String does not have method getBytes`.
- **Fix:** 64 static methods of the class `Game` and the classes `SUtils`, `Device`, `GameInstaller`,
  `C2DMAndroidUtils`, `PortingJNI` — in [game_classes.cpp](../port/game/jni/game_classes.cpp);
  `String.getBytes(charset)` — in [string.cpp](../port/portbase/jni/classes/string.cpp).
- **Rule:** nearly all methods answer with the quietest sensible constant. Few do real work:
  `GetTextureFormat` (always ETC: the cache holds `CookedAndroid_ETC` only), `getConfig` (the quality
  profile), `EglSwapR`, the movies.

### 4. The crash in the library's own `fopen`

- **Seen:** `bionic stdio internal __sflags() was called from 0x…`, then `SIGSEGV` at `+0x01a0ecf4`.
- **How it was found:** the caller's address fell inside a function `fopen` defined in the library itself
  (`0x1a0f26c`). The list of such functions came from selecting the symbols in the range
  `0x1a0a000–0x1a4c000` of `extracted/syms.txt`.
- **Cause:** the library has CrystaX's libc compiled in, with its own copy of stdio. Its `fopen` takes a
  `FILE` from bionic through `__sfp()` and fills the fields in itself. The functions the library still
  imports (`fprintf`, `getc`, `ferror`, `setvbuf`) arrive at the loader's bridge, where a `FILE` is a
  handle on a host stream. One file would be opened by one family of functions and read by the other.
- **Fix:** `wb_bridge_private_stdio` in [patch.cpp](../port/game/patch.cpp) hooks 17 exported functions
  (`fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell`, `fgets` and others) and sends them where an
  import of the same name would go. It is called before the static initialisers.
- **Left alone:** the wide-character functions and the internals (`__sfvwrite`, `__vfwprintf`): `vswprintf`
  and `swscanf` work on string buffers and never meet a real file.

### 5. The crash in the network thread

- **Seen:** `SIGSEGV at 0x00000004` in a background thread, `pc` in `FSocketSubsystemBSD::GetHostByName`.
- **How it was found:** the function reads `[r3, #24]` as the host's address. In bionic that offset is
  `ai_addr`; in glibc it is `ai_canonname`, which is NULL. The two libraries order the fields of
  `struct addrinfo` differently.
- **Fix:** `getaddrinfo`, `freeaddrinfo` and `gethostbyname` answer "no network" —
  [symtab_net.cpp](../port/portbase/src/symtab_net.cpp). The game takes its "no internet" branch.
- **Decision on a server:** a mock Gameloft server is not built until a direct dependency of the game on a
  server is found. If one is needed: look for a server on the local network at a fixed port for 0.5 s,
  otherwise bring one up inside the app.

### 6. The game does not find its data

- **Seen:** no crashes, frames tick, but nothing loads: `textures=0`.
- **How it was found:** in the file-access log (`grep 'io: open' logs/pc_run.log`) the engine opens
  `/data/data/<package>/AOWGame/CookedAndroid_ETC/Coalesced_INT.bin` and gets ENOENT. It does not call the
  Java method `getSDFolder` at all.
- **Fix:** a rule in `port_fix_path` — the whole of `/data/data/<package>` maps to the cache folder
  ([io_paths.cpp](../port/game/io_paths.cpp)). Saves, settings and the engine's log end up there too.
- **What it gave for diagnosis:** the engine started writing its own log, `AOWGame/Logs/Launch.log`. It
  shows the chosen texture format, the version ("1.1.4d", although the APK file is named 1.1.5), and that
  initialisation is spread over several frames — a ten-frame run is not enough for it.

### 7. On the phone the game exits with code 1

- **Seen:** `unresolved symbol` in `wb.log` on the phone for `glDetachShader`, `glGetBooleanv`,
  `glIsShader`, `glFinish`.
- **Cause:** on the PC a real driver provides those functions, while on the phone every GL call goes
  through the bridge, whose table lacked them.
- **Fix:** `glDetachShader` was added to the generator [gen_glbridge.py](../port/android/tools/gen_glbridge.py)
  (it writes both sides of the bridge); the other three are implemented on the game side in
  [gl_bridge.cpp](../port/portbase/src/gl_bridge.cpp).
- **Checking ahead:** all of the game's GL imports are compared with the bridge's table in one command, so
  that gaps are not met one at a time:

```sh
for f in $(grep '^gl' extracted/imports.txt); do
  grep -qE "E\($f\)|X\($f\)|\"$f\"" port/portbase/src/gl_bridge.cpp port/portbase/src/gl_bridge_gen.inc \
    || echo "MISSING $f"
done
```

### 8. A black screen and 0 frames

- **Seen:** the `frames` counter grows, `presented` stays at 0 or 1; the wrapper shows «Подготовка»
  ("Preparing").
- **False lead:** the loading movie was suspected first: the game asks Java to play `LevelMovie1`, and
  there is no player. A confirmation of the movie being stopped was added
  (`JavaCallback_StopVideo` → `videoFinished`). The change was kept, but it did not bring the screen to life.
- **How it was found:** the full symbol table (`readelf -sW`, local symbols included) holds
  `callSwapBuffers`, `callSwapBuffersR` and `PlatformSwapBuffers(FES2Viewport*)`. The last one consists of
  a single `bx lr` instruction (`+0x014aa05c`).
- **Cause:** Gameloft took presenting the frame out of the engine. On Android the frame is shown by the
  `GLSurfaceView`: it swaps after every return from `onDrawFrame`, that is after every `nativeRender`.
  `Game.EglSwap()` is empty in the Java, and `EglSwapR()` is an extra swap for loading screens. The port
  was waiting for a swap through `EglSwapR` only.
- **Fix:** the frame is shown after every `nativeRender` unless the engine showed it itself inside the
  frame — [main.cpp](../port/game/main.cpp).
- **Takeaway:** who does the swap has to be established at the very start. In TDKR the port does it after
  `step`; here on Android the view does it, not the game.

### 9. The class `android/view/KeyEvent` is not found

- **Seen:** `FindClass: no fake class registered for 'android/view/KeyEvent'` and a run of
  `GetStaticFieldID: NULL class`.
- **Cause:** the engine's `initMap()` reads 72 `KEYCODE_*` constants with `GetStaticIntField` to build its
  key table.
- **Fix:** a class with those fields was added in [wb_classes.h](../port/game/jni/wb_classes.h) and
  [game_classes.cpp](../port/game/jni/game_classes.cpp). Reading static numeric fields was fixed along the
  way in [jni.cpp](../port/portbase/jni/jni.cpp): it returned the field's address instead of its value.
- **Not checked:** the change is built into the APK but has not been run on the phone yet.

## Reference for this game

### Entry points the port calls

| Function | What it is in the library | When |
|---|---|---|
| `JNI_OnLoad(vm)` | stores the `JavaVM`, initialises audio (`VoxSetJavaVM`) | once |
| `Game.nativeInit()` | looks every Java method up by name, writes to the GDRM words | once |
| `GameRenderer.nativeInit(manufacturer, width, height, version)` | stores the sizes and the version string | once |
| `GameRenderer.nativeResize(width, height)` | `glViewport` | once |
| `GameRenderer.nativeRender()` | `appUpdate`: `appInit` the first time, `FEngineLoop::Tick` after | every frame |
| `GameGLSurfaceView.nativeOnTouch(action, x, y, finger, timeMs)` | `appOnTouch`: a queue under a mutex | per touch |
| `Game.nativeOnKeyDown/Up(code)` | keys | per press |
| `MyVideoView.videoFinished()` | clears the "a movie is playing" flag | after a movie |

The manufacturer is passed as a number, as `Game.getManufacture()` in the dex computes it: 0 — other,
1 — motorola, 2 — samsung, 3 — htc, 4 — sony, 5 — sharp. The port passes 2.

Touch action: 1 is a finger down, 2 a move, 0 a finger up. Inside `appOnTouch` they are turned into the
engine's codes by a three-entry table: `1 → Began`, `2 → Moved`, `0 → Ended`.

### What the Java methods answer

There are 64 static methods of the class `Game`. The ones that matter:

| Method | The port's answer | Why |
|---|---|---|
| `GetTextureFormat()` | 4 (ETC) | the game's Java picks the first of PVRTC (1), ATC (2), DXT (3), ETC (4) the driver advertises. Adreno advertises ATC, while the cache holds `CookedAndroid_ETC` only |
| `getConfig()` | 3 | the quality profile; the game's Java sets 3, 4, 5, 6, 8 or 10 by GPU model and screen |
| `getSDFolder()` | the cache folder | the engine does not call it (step 6); kept for completeness |
| `isWifiEnabled()`, `NetIsEnable()` | 0 | no network |
| `GetPhoneLanguage()` | 0 | index of the language in the game's table |
| `JavaCallback_StartVideo(name)` | "the movie has finished" at once | there is no player |
| `JavaCallback_StopVideo()` | "the movie has finished" | the game's Java closes the player, which reports the end |
| `EglSwap()` | nothing | the method is empty in the game's Java |
| `EglSwapR()` | show the frame now | loading screens |
| `GetBitmapFromText(…)` | empty | there is no text in a system font |

The values can be overridden through `engine_env.txt`: `WB_TEXTURE_FORMAT`, `WB_CONFIG`, `WB_LANG_INDEX`,
`WB_GPU_NAME`.

### Initialisation is spread over frames

`appInit` calls `FEngineLoop::InitStepbyStep`, and start-up continues for several `nativeRender` calls. On
the phone it is about a minute from start to the main menu: the engine reads its shader cache
(`AOWGame/CookedAndroid_ETC/Shaders/…`) and compiles the programs. In that time the bridge executes about
68 thousand uniform-location queries. A run of 10–20 frames shows nothing.

## The phone

- The data is copied by `tools/push_data.sh`: the game's APK and the unpacked cache without `main.*.obb`
  (that is the same files as one zip; the game does not need it), 1.6 GB.
- Folders made by `adb push` and `adb shell mkdir` belong to the `shell` user and are closed to the app.
  The script sets `2777` on every folder, including the app's own folder when it was made before the APK
  was installed.
- Folders the engine itself makes get mode `0744` and are out of adb's reach. The `mkdir` thunk in
  [symtab_io.cpp](../port/portbase/src/symtab_io.cpp) opens them; otherwise the engine's log cannot be
  read from the phone.
- `tools/build_install.sh` builds the port and the APK, installs it and restarts the game straight into
  the game, past the settings screen. `tools/pull_log.sh` fetches the log, `adb exec-out screencap -p` a
  screenshot.
- In the phone log the first things to look at are the `frames=N presented=N` line and the `[gl] frames`
  line.

## What is left

- Test everything past the main menu: the campaign, touches in the game, audio, saves, pause and return
  to the app.
- Movies are not played: the intro and the ending are skipped.
- The gamepad's sticks are not wired (`Game.nativeOnMotionEventPowerA`).
- The meaning of the `Game.getConfig()` values (3, 4, 5, 6, 8, 10) has not been worked out; 3 is returned.
- `GetBitmapFromText` answers with an empty result — text in a system font is not drawn.
