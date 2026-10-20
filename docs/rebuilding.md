# Пересборка и перелинковка Windows EXE

Архив исходников релиза содержит `application/` (собственный код MIT и QXlsx MIT),
полный `qtbase-6.10.3-7ddbc87.tar.gz`, исходные лицензии и `source-manifest.json`.
Обычный пользователь скачивает только EXE. Для изменения Qt или приложения:

1. Установить Visual Studio 2022 Build Tools с C++ x64 и Windows SDK, CMake >= 3.25,
   Ninja и Python 3. Открыть Developer PowerShell с окружением x64.
2. Распаковать Qt-архив в `qt-source` и создать рядом `qt-build`, `qt-install`.
   Внести нужные изменения в исходники Qt. В `qt-build` выполнить команду ниже,
   подставив абсолютный путь к `qt-install` вместо `C:/work/qt-install`.

```powershell
$qtArgs = (Get-Content ../application/cmake/qt-static.json -Raw | ConvertFrom-Json).configure_args
& ../qt-source/configure.bat -prefix C:/work/qt-install @qtArgs
cmake --build . --parallel 4
cmake --install .
```

3. Перейти в `application`, при необходимости изменить код приложения. QXlsx уже
   включён исходниками, скачивать его при CMake configure не требуется.

```powershell
$env:QT_STATIC_ROOT = "C:/work/qt-install"
cmake --preset release-windows-static
cmake --build --preset release-windows-static --parallel 2
ctest --preset release-windows-static
python scripts/verify_windows_exe.py build/release-windows-static/dxf-generator.exe
```

Полученный `build/release-windows-static/dxf-generator.exe` можно запускать прямо
из папки, в том числе со своим изменённым Qt. Ограничений подписью/ключом установки
нет. Статическая сборка не требует отдельных Qt DLL. Изменения могут изменить
размер EXE, поэтому аудит выше не навязывает исходный бюджет 20 МиБ.

Релизная конфигурация Qt записана в `cmake/qt-static.json`, действия Windows runner —
в `.github/workflows/windows-static.yml`. Архив приложения содержит все собственные
исходники, CMake, ресурсы, тесты, QXlsx и инструкции; лицензия MIT разрешает их
изменение и пересборку. Qt и его встроенные компоненты сохраняют собственные
лицензии. Для обновления уведомлений после изменения состава Qt использовать
`scripts/generate_legal_notices.py` с соответствующим исходным Qt-архивом.
