# Значок приложения

Плоская версия старого значка: синий прямоугольник, зелёный круг и красная шайба.
Фигуры разделены, фон и отверстие шайбы прозрачные. Без осей, текста, теней и фасок.

Общий код рисования — `src/desktop/app_icon.cpp`. Qt получает из него размеры
16, 20, 24, 32, 40, 48, 64, 128 и 256 px; прямоугольник выравнивается по пикселям.
`app.svg` — векторный образец той же композиции. После изменения геометрии или
цветов нужно обновить образец и перегенерировать `app.ico` тем же кодом рисования.

Генератор использует только уже имеющиеся Qt Core/Gui, не входит в приложение и
не запускается при обычной сборке. Из корня проекта на Linux:

```bash
mkdir -p build/tools
c++ -std=c++20 -fPIC -Isrc scripts/generate_app_icon.cpp src/desktop/app_icon.cpp \
    $(pkg-config --cflags --libs Qt6Gui Qt6Core) -o build/tools/generate-app-icon
QT_QPA_PLATFORM=offscreen build/tools/generate-app-icon resources/app.ico
```

Windows встраивает `app.ico` через `windows.rc`. Qt рисует тот же значок в памяти;
SVG/ICO plugins, дополнительные библиотеки и внешние файлы для этого не нужны.
