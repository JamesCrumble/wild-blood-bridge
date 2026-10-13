# История версий

## 0.1 (бета) — 2026-10-13

Первая сборка.

- Wild Blood 1.1.5 (Unreal Engine 3) запускается на OnePlus 13s и показывает главное меню, 60 fps при лимите 60.
  Дальше меню не проверялось.
- Слой игры под точки входа `Game` / `GameRenderer` / `GameGLSurfaceView`, 64 обратных вызова класса `Game`,
  классы `SUtils`, `Device`, `GameInstaller`, `C2DMAndroidUtils`, `PortingJNI`, `KeyEvent`.
- GDRM: память для четырёх `lockPointer`. Собственный stdio библиотеки перенаправлен на мост загрузчика.
- Данные читаются из папки кэша (путь движка `/data/data/<пакет>` отображён на неё); OBB не нужен.
- Кадр показывается после каждого `nativeRender` (в этой сборке движка `PlatformSwapBuffers` пуст).
- В GL-мост добавлены `glDetachShader`, `glGetBooleanv`, `glIsShader`, `glFinish`.
- Без сети: разрешение имён отвечает отказом. Ролики пропускаются.
- Иконка приложения берётся из APK игры при сборке.
