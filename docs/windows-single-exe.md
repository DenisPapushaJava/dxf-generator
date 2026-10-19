# Windows: один EXE

Целевая система — Windows 10 (1809+) / 11 x64, согласно [поддержке Qt 6.10](https://doc.qt.io/qt-6.10/windows.html). Разрабатывать UI и ядро можно на Linux с системным Qt. Проверять Windows-приложение и получать основной Windows-артефакт — на Windows runner, чтобы использовать штатные Qt build tools и платформенный плагин.

## Что подготовлено

- `release-windows-static`: требует статический `Qt6::Core`, включает статический MSVC runtime, оптимизацию размера и IPO.
- `qt_import_plugins(... NO_DEFAULT INCLUDE Qt6::QWindowsIntegrationPlugin)`: Windows-плагин внутри EXE, без папки `platforms`.
- Manifest: обычные права пользователя, Windows 10 compatibility, PerMonitorV2 DPI и long paths.
- `scripts/verify_windows_exe.py`: проверка PE32+ x64, обычных и отложенных DLL-импортов. Неизвестные внешние DLL завершают проверку ошибкой. Список системных DLL намеренно явный; расширять его только после проверки источника зависимости.
- Ручной workflow `.github/workflows/windows-static.yml`: QtBase 6.10.3 из commit `7ddbc87d8e14ce51d2957ea72d0a6077593d5ff4`, MSVC 2022, static runtime, кеш SDK, сборка/CTest/аудит, тестовый EXE в Actions artifact на 7 дней. Обычные push не пересобирают Qt.

Qt использует системную `Authz.dll` для проверки файловых разрешений Windows; эта зависимость разрешена аудитом ([Microsoft: AuthzAccessCheck](https://learn.microsoft.com/en-us/windows/win32/api/authz/nf-authz-authzaccesscheck)). SDK кешируется сразу после установки, чтобы ошибка приложения или аудита не требовала повторной сборки Qt.

## Локальная Windows-сборка

1. Установить MSVC 2022 x64, Windows SDK, CMake и Ninja. Открыть x64 Developer PowerShell.
2. Собрать QtBase 6.10.3 из исходников. Точные параметры находятся в ручном workflow. Обычный установленный динамический SDK для этого профиля не подходит.
3. Указать prefix установленного статического SDK:

```powershell
$env:QT_STATIC_ROOT = 'C:/sdk/qt-6.10.3-static'
cmake --preset release-windows-static
cmake --build --preset release-windows-static
ctest --preset release-windows-static
python scripts/verify_windows_exe.py build/release-windows-static/dxf-generator.exe
```

Финальный EXE находится в `build/release-windows-static`. PDB/obj, CMake cache и исходники Qt в пользовательскую папку приложения не копируются. Материалы для соблюдения выбранной лицензии готовятся отдельно, см. [licensing.md](licensing.md).

После успешного аудита нужна проверка на чистых Windows 10/11: запуск без Qt/Visual Studio, DPI 100/150/200%, русский путь, отсутствие прав администратора, экспорт и открытие DXF в целевой CAD/CAM. PE-аудит подтверждает зависимости, но не заменяет такой прогон.

## Кросс-компиляция на Linux

Архитектура не содержит Windows-специфичной бизнес-логики. MinGW поддерживается настройками runtime, но готовый кросс-SDK в репозиторий не входит. Для него необходимы статический Windows Qt и host tools **той же версии Qt**, toolchain-файл и отдельный build directory. Смешивать Linux Qt 6.4 host tools и Windows Qt 6.10 не следует. Поддерживаемый воспроизводимый путь в этом каркасе — Windows CI.

Справка: [Qt static plugins](https://doc.qt.io/qt-6.10/plugins-howto.html), [qt_import_plugins](https://doc.qt.io/qt-6/qt-import-plugins.html), [Qt configure options](https://doc.qt.io/qt-6.10/configure-options.html).
