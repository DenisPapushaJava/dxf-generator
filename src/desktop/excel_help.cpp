#include "desktop/excel_help.hpp"
#include "desktop/update_dialog.hpp"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResource>
#include <QSaveFile>
#include <QVBoxLayout>

// Refer to the resource explicitly so it is retained when linking the widgets
// static library, including in an offline single-EXE build.
static void init_excel_template() {
    static const bool initialized = [] {
        Q_INIT_RESOURCE(excel_template);
        Q_INIT_RESOURCE(legal_notices);
        return true;
    }();
    (void)initialized;
}

namespace dxfgen {
namespace {
QLabel* text(const QString& value) {
    auto* label = new QLabel(value);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    return label;
}
} // namespace

void show_excel_help(QWidget* parent, const std::function<bool()>& before_restart) {
    init_excel_template();
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("excelHelpDialog"));
    dialog.setWindowTitle(QStringLiteral("Справка — DXF Generator"));
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog.resize(700, 680);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);
    auto* heading = text(QStringLiteral("Из списка — в DXF"));
    heading->setStyleSheet("font-size: 22px; font-weight: bold;");
    layout->addWidget(heading);
    layout->addWidget(
        text(QStringLiteral("1. Сохраните шаблон и замените примеры своими деталями.\n"
                            "2. Откройте файл кнопкой «Открыть Excel…» или перетащите в окно.\n"
                            "3. Проверьте ячейки и экспортируйте выбранные детали в папку.")));
    auto* structure =
        text(QStringLiteral("Первый лист · заголовки в строке 1 · детали со строки 2"));
    structure->setStyleSheet("font-weight: bold;");
    layout->addWidget(structure);
    auto* columns = new QGridLayout;
    columns->setHorizontalSpacing(16);
    columns->setVerticalSpacing(10);
    columns->setColumnStretch(2, 1);
    const QString fields[][3] = {
        {QStringLiteral("A"), QStringLiteral("Имя файла"),
         QStringLiteral("Необязательно. Пусто — имя DXF формируется по размерам.")},
        {QStringLiteral("B"), QStringLiteral("Тип"),
         QStringLiteral("П — прямоугольник, К — круг, Ш — шайба. Полные названия тоже подходят.")},
        {QStringLiteral("C"), QStringLiteral("Длина"),
         QStringLiteral("Длина прямоугольника или диаметр круга / наружный диаметр шайбы.")},
        {QStringLiteral("D"), QStringLiteral("Ширина"),
         QStringLiteral("Ширина прямоугольника или внутренний диаметр шайбы. У круга — пусто.")},
        {QStringLiteral("E"), QStringLiteral("Толщина"),
         QStringLiteral("Необязательно. Пусто или 0 — без толщины в имени DXF.")},
        {QStringLiteral("F"), QStringLiteral("Количество"),
         QStringLiteral("Целое положительное число. Пусто — 1 штука.")}};
    for (int row = 0; row < 6; ++row)
        for (int column = 0; column < 3; ++column) {
            auto* cell = text(fields[row][column]);
            if (column < 2)
                cell->setStyleSheet("font-weight: bold;");
            columns->addWidget(cell, row, column, Qt::AlignTop);
        }
    layout->addLayout(columns);
    layout->addWidget(text(QStringLiteral(
        "Размеры — в миллиметрах. Дробные числа — с запятой или точкой, без «мм» в ячейках. "
        "Порядок столбцов A–F одинаков для XLSX, CSV и TSV. Старый XLS пересохраните в XLSX.\n\n"
        "В шаблоне пять корректных примеров. Удалите ненужные строки с примерами. "
        "Подсказки в строки с деталями не добавляйте: они будут прочитаны как данные.\n\n"
        "Ячейки программы можно исправить двойным щелчком или F2; Enter / Tab — применить. "
        "Исходный файл при этом не изменяется.")));
    layout->addStretch();
    auto* saved = text({});
    saved->setObjectName(QStringLiteral("templateSavedLabel"));
    saved->setStyleSheet("color: #15803d;");
    saved->hide();
    layout->addWidget(saved);
    auto* legal_row = new QHBoxLayout;
    legal_row->addWidget(text(QStringLiteral("MIT · Qt (LGPLv3) · QXlsx (MIT)")));
    legal_row->addStretch();
    auto* licenses = new QPushButton(QStringLiteral("Лицензии…"));
    licenses->setAutoDefault(false);
    legal_row->addWidget(licenses);
    layout->addLayout(legal_row);
    QObject::connect(licenses, &QPushButton::clicked, &dialog, [&dialog] {
        QDialog notice(&dialog);
        notice.setWindowTitle(QStringLiteral("Лицензии — DXF Generator"));
        notice.resize(760, 600);
        auto* content = new QVBoxLayout(&notice);
        auto* body = new QPlainTextEdit;
        body->setReadOnly(true);
        QFile file(QStringLiteral(":/licenses/THIRD-PARTY-NOTICES.txt"));
        body->setPlainText(file.open(QIODevice::ReadOnly)
                               ? QString::fromUtf8(file.readAll())
                               : QStringLiteral("Не удалось открыть лицензии."));
        content->addWidget(body);
        auto* close = new QDialogButtonBox(QDialogButtonBox::Close);
        close->button(QDialogButtonBox::Close)->setText(QStringLiteral("Закрыть"));
        QObject::connect(close, &QDialogButtonBox::rejected, &notice, &QDialog::reject);
        content->addWidget(close);
        notice.exec();
    });
    auto* version_row = new QHBoxLayout;
    version_row->addWidget(text(
        QStringLiteral("DXF Generator · версия %1").arg(QCoreApplication::applicationVersion())));
    version_row->addStretch();
    auto* update = new QPushButton(QStringLiteral("Проверить обновления…"));
    update->setObjectName(QStringLiteral("checkUpdatesButton"));
    update->setAutoDefault(false);
    version_row->addWidget(update);
    layout->addLayout(version_row);
    QObject::connect(update, &QPushButton::clicked, &dialog,
                     [&dialog, before_restart] { show_update_dialog(&dialog, before_restart); });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("Закрыть"));
    auto* save =
        buttons->addButton(QStringLiteral("Сохранить шаблон Excel…"), QDialogButtonBox::ActionRole);
    save->setObjectName(QStringLiteral("saveTemplateButton"));
    save->setDefault(true);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(save, &QPushButton::clicked, &dialog, [&dialog, saved] {
        QFileDialog chooser(&dialog, QStringLiteral("Сохранить шаблон Excel"),
                            QStringLiteral("Шаблон деталей.xlsx"),
                            QStringLiteral("Книга Excel (*.xlsx)"));
        chooser.setAcceptMode(QFileDialog::AcceptSave);
        chooser.setDefaultSuffix(QStringLiteral("xlsx"));
        if (chooser.exec() != QDialog::Accepted)
            return;
        const auto path = chooser.selectedFiles().value(0);
        if (path.isEmpty())
            return;
        QFile source(QStringLiteral(":/templates/Детали.xlsx"));
        if (!source.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(&dialog, QStringLiteral("Не удалось сохранить шаблон"),
                                 QStringLiteral("Не удалось прочитать встроенный шаблон Excel."));
            return;
        }
        const auto bytes = source.readAll();
        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
            !output.commit()) {
            QMessageBox::warning(&dialog, QStringLiteral("Не удалось сохранить шаблон"),
                                 output.errorString());
            return;
        }
        saved->setText(QStringLiteral("Шаблон сохранён: %1").arg(QFileInfo(path).fileName()));
        saved->setToolTip(path);
        saved->show();
    });
    dialog.exec();
}
} // namespace dxfgen
