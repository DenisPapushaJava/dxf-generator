# Пересборка и перелинковка Windows EXE

Архив исходников релиза содержит `application/` (собственный код MIT и QXlsx MIT),
полный `qtbase-6.10.3-7ddbc87.tar.gz`, исходные лицензии и `source-manifest.json`.
Обычный пользователь скачивает только EXE. Для изменения Qt или приложения:

1. Установить Visual Studio 2022 Build Tools с C++ x64 и Windows SDK, CMake >= 3.25,
   Ninja и Python 3. Открыть x64 Native Tools Command Prompt.
2. Распаковать Qt-архив в `qt-source` и создать рядом `qt-build`, `qt-install`.
   Внести нужные изменения в исходники Qt. В `qt-build` выполнить команду ниже,
   подставив абсолютный путь к `qt-install` вместо `C:/work/qt-install`.

```bat
..\qt-source\configure.bat -prefix C:/work/qt-install -static -static-runtime -release -optimize-size -opensource -confirm-license -nomake tests -nomake examples -no-opengl -no-feature-vulkan -no-feature-network -no-feature-sql -no-feature-dbus -no-feature-printsupport -no-feature-testlib -no-icu -qt-zlib -qt-pcre -qt-freetype -qt-harfbuzz -qt-libpng -no-libjpeg
cmake --build . --parallel 4
cmake --install .
```

3. Перейти в `application`, при необходимости изменить код приложения. QXlsx уже
   включён исходниками, скачивать его при CMake configure не требуется.

```bat
set QT_STATIC_ROOT=C:/work/qt-install
cmake --preset release-windows-static
cmake --build --preset release-windows-static --parallel 2
ctest --preset release-windows-static
python scripts/verify_windows_exe.py build/release-windows-static/dxf-generator.exe
```

Полученный `build/release-windows-static/dxf-generator.exe` можно запускать прямо
из папки, в том числе со своим изменённым Qt. Ограничений подписью/ключом установки
нет. Статическая сборка не требует отдельных Qt DLL. Изменения могут изменить
размер EXE, поэтому аудит выше не навязывает исходный бюджет 20 МиБ.

Релизная конфигурация, закреплённый Qt commit и действия Windows runner записаны в
`.github/workflows/windows-static.yml`. Архив приложения содержит все собственные
исходники, CMake, ресурсы, тесты, QXlsx и инструкции; лицензия MIT разрешает их
изменение и пересборку. Qt и его встроенные компоненты сохраняют собственные
лицензии. Для обновления уведомлений после изменения состава Qt использовать
`scripts/generate_legal_notices.py` с соответствующим исходным Qt-архивом.
