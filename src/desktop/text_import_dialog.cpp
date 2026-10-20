#include "desktop/text_import_dialog.hpp"
#include "io/list_import.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace dxfgen {
std::optional<ImportResult> configure_text_import(QWidget* parent, const QString& path,
                                                  const QString& reason) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("textImportDialog"));
    dialog.setWindowTitle(QStringLiteral("Параметры текстового файла"));
    dialog.resize(820, 450);
    auto* layout = new QVBoxLayout(&dialog);
    auto* heading = new QLabel(QFileInfo(path).fileName());
    heading->setTextFormat(Qt::PlainText);
    heading->setWordWrap(true);
    layout->addWidget(heading);
    auto* explanation = new QLabel(reason);
    explanation->setTextFormat(Qt::PlainText);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto* fields = new QFormLayout;
    auto* encoding = new QComboBox;
    encoding->setObjectName(QStringLiteral("textEncodingCombo"));
    encoding->addItems({QStringLiteral("Автоматически (UTF-8 / BOM)"), QStringLiteral("UTF-8"),
                        QStringLiteral("UTF-16 LE"), QStringLiteral("UTF-16 BE"),
                        QStringLiteral("Windows-1251")});
    if (reason.contains(QStringLiteral("UTF-8")))
        encoding->setCurrentIndex(4); // Explicit confirmation is required before using this guess.
    auto* delimiter = new QComboBox;
    delimiter->setObjectName(QStringLiteral("textSeparatorCombo"));
    delimiter->addItems({QStringLiteral("Автоматически"), QStringLiteral("Точка с запятой ;"),
                         QStringLiteral("Запятая ,"), QStringLiteral("Табуляция")});
    fields->addRow(QStringLiteral("Кодировка"), encoding);
    fields->addRow(QStringLiteral("Разделитель"), delimiter);
    layout->addLayout(fields);
    auto* preview = new QTableWidget(0, 6);
    preview->setObjectName(QStringLiteral("textImportPreview"));
    preview->setHorizontalHeaderLabels({QStringLiteral("Имя файла"), QStringLiteral("Тип"),
                                        QStringLiteral("Длина / Ø"),
                                        QStringLiteral("Ширина / Ø отв."),
                                        QStringLiteral("Толщина"), QStringLiteral("Количество")});
    preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    preview->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(preview, 1);
    auto* status = new QLabel;
    status->setObjectName(QStringLiteral("textImportStatus"));
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel);
    auto* open = buttons->button(QDialogButtonBox::Open);
    open->setText(QStringLiteral("Открыть"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
    layout->addWidget(buttons);
    ImportResult imported;
    auto refresh = [&] {
        const QChar separators[] = {QChar{}, QChar(';'), QChar(','), QChar('\t')};
        imported = import_list(path, {static_cast<TextEncoding>(encoding->currentIndex()),
                                      separators[delimiter->currentIndex()]});
        preview->setRowCount(static_cast<int>(std::min<std::size_t>(8, imported.rows.size())));
        for (int row = 0; row < preview->rowCount(); ++row) {
            const auto& input = imported.rows[static_cast<std::size_t>(row)];
            preview->setVerticalHeaderItem(row,
                                           new QTableWidgetItem(QString::number(input.source_row)));
            for (int col = 0; col < 6; ++col) {
                auto* item = new QTableWidgetItem(input.cells[static_cast<std::size_t>(col)]);
                item->setToolTip(row_errors(input).join('\n'));
                preview->setItem(row, col, item);
            }
        }
        const bool valid = imported.error.isEmpty();
        open->setEnabled(valid);
        status->setText(
            valid ? QStringLiteral("%1 · строк: %2. Показаны первые %3; проверьте текст и размеры.")
                        .arg(imported.source_description)
                        .arg(imported.rows.size())
                        .arg(preview->rowCount())
                  : imported.error);
        status->setStyleSheet(valid ? "" : "color: #b45309;");
    };
    QObject::connect(encoding, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(delimiter, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    refresh();
    if (dialog.exec() != QDialog::Accepted || !imported.error.isEmpty())
        return std::nullopt;
    return imported;
}
} // namespace dxfgen
