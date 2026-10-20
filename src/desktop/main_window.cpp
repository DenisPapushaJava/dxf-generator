#include "desktop/main_window.hpp"
#include "desktop/excel_help.hpp"
#include "desktop/part_dialog.hpp"
#include "desktop/parts_delegate.hpp"
#include "desktop/parts_gallery.hpp"
#include "desktop/parts_model.hpp"
#include "desktop/preview_widget.hpp"
#include "desktop/text_import_dialog.hpp"
#include "io/excel_import.hpp"
#include "io/export_service.hpp"
#include "io/list_import.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QResizeEvent>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace dxfgen {
namespace {
QLabel* label(const QString& text = {}) {
    auto* result = new QLabel(text);
    result->setTextFormat(Qt::PlainText);
    result->setWordWrap(true);
    return result;
}
} // namespace
MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("DXF Generator[*]"));
    setAcceptDrops(true);
    resize(1440, 800);
    setMinimumSize(1100, 620);
    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(12);
    auto* toolbar = new QHBoxLayout;
    auto* title = label(QStringLiteral("DXF Generator"));
    title->setStyleSheet("font-size: 18px; font-weight: bold;");
    toolbar->addWidget(title);
    toolbar->addStretch();
    auto* manual = new QPushButton(QStringLiteral("Создать &вручную…"));
    manual->setObjectName(QStringLiteral("manualButton"));
    toolbar->addWidget(manual);
    auto* open = new QPushButton(QStringLiteral("&Открыть Excel…"));
    open->setObjectName(QStringLiteral("openExcelButton"));
    open->setToolTip(
        QStringLiteral("Открыть Excel (.xlsx) или текстовую таблицу CSV / TSV / TXT (Ctrl+O)"));
    open->setShortcut(QKeySequence::Open);
    open->setMinimumHeight(34);
    open->setStyleSheet(
        "background: #2563eb; color: white; padding: 6px 18px; border-radius: 4px;");
    toolbar->addWidget(open);
    auto* help = new QPushButton(QStringLiteral("?"));
    help->setObjectName(QStringLiteral("helpButton"));
    help->setAccessibleName(QStringLiteral("Справка, шаблон Excel и обновления"));
    help->setToolTip(QStringLiteral("Справка, шаблон Excel и обновления (F1)"));
    help->setShortcut(QKeySequence::HelpContents);
    help->setFixedSize(34, 34);
    toolbar->addWidget(help);
    connect(help, &QPushButton::clicked, this, [this] {
        show_excel_help(this, [this] {
            finishTableEdit();
            return discardChanges();
        });
    });
    layout->addLayout(toolbar);
    source_ = label();
    source_->setObjectName(QStringLiteral("sourceLabel"));
    source_->hide();
    layout->addWidget(source_);
    auto* splitter = new QSplitter;
    splitter->setChildrenCollapsible(false);
    auto* list_pane = new QWidget;
    auto* list_layout = new QVBoxLayout(list_pane);
    list_layout->setContentsMargins(0, 0, 0, 0);
    auto* list_toolbar = new QHBoxLayout;
    list_toolbar->addWidget(label(QStringLiteral("Детали")));
    list_toolbar->addStretch();
    auto* ready = new QPushButton(QStringLiteral("Выбрать &готовые"));
    ready->setObjectName(QStringLiteral("selectReadyButton"));
    ready->setToolTip(
        QStringLiteral("Выбрать только строки без ошибок. Остальные останутся видны в таблице."));
    list_toolbar->addWidget(ready);
    list_layout->addLayout(list_toolbar);
    model_ = new PartsModel(this);
    table_ = new QTableView;
    table_->setObjectName(QStringLiteral("partsTable"));
    table_->setModel(model_);
    table_->setItemDelegate(new PartsDelegate(table_));
    table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                            QAbstractItemView::AnyKeyPressed);
    table_->setTabKeyNavigation(true);
    table_->setAcceptDrops(false);
    table_->viewport()->setAcceptDrops(false);
    table_->setAccessibleName(
        QStringLiteral("Редактируемые строки списка. Двойной щелчок или F2 — изменить параметры. "
                       "Флажок выбирает строку для экспорта."));
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(true);
    table_->setWordWrap(false);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(36);
    const int widths[] = {65, 105, 125, 80, 110, 70, 65, 230, 150};
    for (int column = 0; column < PartsModel::columns; ++column)
        table_->setColumnWidth(column, widths[column]);
    table_->horizontalHeader()->setStretchLastSection(true);
    list_layout->addWidget(table_, 1);
    auto* row_toolbar = new QHBoxLayout;
    remove_ = new QPushButton(QStringLiteral("Удалить ручную"));
    row_toolbar->addWidget(remove_);
    row_toolbar->addStretch();
    auto* hint = label(QStringLiteral(
        "Двойной щелчок / F2 — изменить · Enter / Tab — применить. Исходный файл не изменяется."));
    hint->setStyleSheet("color: #64748b;");
    row_toolbar->addWidget(hint, 1);
    list_layout->addLayout(row_toolbar);
    splitter->addWidget(list_pane);
    auto* preview_pane = new QWidget;
    auto* preview_layout = new QVBoxLayout(preview_pane);
    preview_layout->setContentsMargins(4, 0, 0, 0);
    auto* preview_toolbar = new QHBoxLayout;
    preview_toolbar->addWidget(label(QStringLiteral("Предпросмотр")), 1);
    gallery_ = new QPushButton(QStringLiteral("Все детали…"));
    gallery_->setObjectName(QStringLiteral("partsGalleryButton"));
    gallery_->setToolTip(QStringLiteral("Открыть галерею всех деталей из списка"));
    preview_toolbar->addWidget(gallery_);
    preview_layout->addLayout(preview_toolbar);
    preview_ = new PreviewWidget;
    preview_->setMaximumHeight(300);
    preview_layout->addWidget(preview_);
    connect(gallery_, &QPushButton::clicked, this, [this] {
        finishTableEdit();
        const int row = show_parts_gallery(this, *model_, currentRow());
        if (row >= 0) {
            table_->selectRow(row);
            table_->scrollTo(model_->index(row, PartsModel::source));
            table_->setFocus();
        }
    });
    details_ = label(QStringLiteral("Выберите строку таблицы."));
    details_->setObjectName(QStringLiteral("detailsLabel"));
    details_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    preview_layout->addWidget(details_);
    validation_ = label();
    validation_->setObjectName(QStringLiteral("validationLabel"));
    validation_->setAccessibleName(QStringLiteral("Ошибки выбранной строки"));
    preview_layout->addWidget(validation_);
    preview_layout->addStretch();
    preview_pane->setMinimumWidth(preview_->minimumWidth() +
                                  preview_layout->contentsMargins().left() +
                                  preview_layout->contentsMargins().right());
    splitter->addWidget(preview_pane);
    splitter->setSizes({1080, 300});
    splitter->setStretchFactor(0, 1);
    layout->addWidget(splitter, 1);
    auto* footer = new QHBoxLayout;
    summary_ = label();
    summary_->setObjectName(QStringLiteral("summaryLabel"));
    footer->addWidget(summary_, 1);
    export_ = new QPushButton(QStringLiteral("&Экспортировать DXF…"));
    export_->setObjectName(QStringLiteral("exportButton"));
    export_->setMinimumHeight(38);
    export_->setStyleSheet(
        "QPushButton { background: #2563eb; color: white; padding: 6px 20px; border-radius: 4px; } "
        "QPushButton:disabled { background: #94a3b8; }");
    export_->setToolTip(QStringLiteral("Выбрать папку и сохранить отмеченные строки. "
                                       "Существующие файлы не перезаписываются."));
    footer->addWidget(export_);
    layout->addLayout(footer);
    setCentralWidget(central);
    statusBar()->showMessage(
        QStringLiteral(".xlsx / .csv / .tsv → DXF в папку · размеры в миллиметрах"));
    drop_overlay_ = new QFrame(this);
    drop_overlay_->setObjectName(QStringLiteral("dropOverlay"));
    drop_overlay_->setAttribute(Qt::WA_TransparentForMouseEvents);
    drop_overlay_->setFocusPolicy(Qt::NoFocus);
    auto* overlay_layout = new QVBoxLayout(drop_overlay_);
    overlay_layout->setContentsMargins(40, 40, 40, 40);
    overlay_layout->addStretch();
    drop_title_ = label();
    drop_title_->setAlignment(Qt::AlignCenter);
    drop_title_->setStyleSheet("font-size: 26px; font-weight: bold; border: none;");
    overlay_layout->addWidget(drop_title_);
    drop_message_ = label();
    drop_message_->setObjectName(QStringLiteral("dropMessage"));
    drop_message_->setAlignment(Qt::AlignCenter);
    drop_message_->setStyleSheet("font-size: 16px; border: none;");
    overlay_layout->addWidget(drop_message_);
    overlay_layout->addStretch();
    drop_overlay_->hide();
    drop_timer_ = new QTimer(this);
    drop_timer_->setSingleShot(true);
    connect(drop_timer_, &QTimer::timeout, this, &MainWindow::clearDrop);
    // All child surfaces, including the table viewport and selectable labels, route
    // file drags to this window. The overlay never becomes a separate drop target.
    for (auto* child : findChildren<QWidget*>())
        child->setAcceptDrops(false);
    connect(open, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Открыть список деталей"), {},
            QStringLiteral("Списки деталей (*.xlsx *.csv *.tsv *.txt);;Книга Excel "
                           "(*.xlsx);;Текстовая таблица (*.csv *.tsv *.txt);;Все файлы (*)"));
        if (!path.isEmpty())
            loadExcel(path);
    });
    connect(manual, &QPushButton::clicked, this, &MainWindow::createManual);
    connect(remove_, &QPushButton::clicked, this, [this] {
        const int row = currentRow();
        if (model_->sourceRow(row) != 0)
            return;
        model_->remove(row);
        if (model_->rowCount())
            table_->selectRow(std::min(row, model_->rowCount() - 1));
        refreshSummary();
        loadSelection();
    });
    connect(ready, &QPushButton::clicked, model_, &PartsModel::selectReady);
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this] { loadSelection(); });
    connect(model_, &QAbstractItemModel::dataChanged, this, [this] {
        refreshSummary();
        loadSelection();
    });
    connect(export_, &QPushButton::clicked, this, [this] { exportFiles(); });
    refreshSummary();
    loadSelection();
}
int MainWindow::currentRow() const { return table_->currentIndex().row(); }
void MainWindow::finishTableEdit() {
    if (auto* focus = QApplication::focusWidget(); focus && table_->isAncestorOf(focus))
        table_->setFocus(Qt::OtherFocusReason);
}
bool MainWindow::discardChanges() {
    return !model_->hasPendingChanges() ||
           QMessageBox::question(
               this, QStringLiteral("Изменения списка"),
               QStringLiteral("В списке есть изменения, ещё не экспортированные в DXF. "
                              "При продолжении они будут потеряны. "
                              "Исходный файл не изменён. Продолжить?"),
               QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}
bool MainWindow::loadExcel(const QString& path, bool interactive) {
    finishTableEdit();
    auto imported = import_list(path);
    if (!imported.error.isEmpty() && !imported.needs_text_options) {
        if (interactive)
            QMessageBox::warning(this, QStringLiteral("Не удалось открыть список"), imported.error);
        statusBar()->showMessage(imported.error);
        return false;
    }
    return applyImport(path, std::move(imported), interactive);
}
bool MainWindow::applyImport(const QString& path, ImportResult imported, bool interactive) {
    finishTableEdit();
    if (imported.needs_text_options) {
        if (!interactive)
            return false;
        auto configured = configure_text_import(this, path, imported.error);
        if (!configured)
            return false;
        imported = std::move(*configured);
    }
    if (model_->hasPendingChanges() && (!interactive || !discardChanges()))
        return false;
    model_->replace(std::move(imported.rows));
    source_->setText(QStringLiteral("%1 · %2 · строк: %3")
                         .arg(QFileInfo(path).fileName(), imported.source_description)
                         .arg(model_->rowCount()));
    source_->setToolTip(QFileInfo(path).absoluteFilePath());
    source_->show();
    setWindowTitle(QStringLiteral("DXF Generator — %1[*]").arg(QFileInfo(path).fileName()));
    table_->selectRow(0);
    refreshSummary();
    loadSelection();
    statusBar()->showMessage(QStringLiteral("Выберите строки без ошибок для экспорта DXF."));
    return true;
}
void MainWindow::loadSelection() {
    const int row = currentRow();
    const auto* part = model_->part(row);
    const bool included = model_->isIncluded(row);
    preview_->setPart(part, included);
    const bool manual = model_->sourceRow(row) == 0;
    remove_->setEnabled(manual);
    remove_->setVisible(manual);
    if (row < 0 || row >= model_->rowCount()) {
        details_->setText(QStringLiteral("Выберите строку таблицы."));
        validation_->clear();
        return;
    }
    const auto row_label = model_->sourceRow(row)
                               ? QStringLiteral("Деталь №%1 · строка файла %2")
                                     .arg(row + 1)
                                     .arg(model_->sourceRow(row))
                               : QStringLiteral("Деталь №%1 · добавлена вручную").arg(row + 1);
    details_->setText(row_label + (model_->fileName(row).isEmpty()
                                       ? QString{}
                                       : QStringLiteral("\n\nИмя DXF:\n") + model_->fileName(row)));
    const auto errors = model_->errors(row);
    validation_->setText(
        errors.isEmpty()
            ? (included ? QStringLiteral("✓ Готово к экспорту") : QStringLiteral("Не в экспорте"))
            : (included ? QString{} : QStringLiteral("Не в экспорте\n")) + errors.join('\n'));
    validation_->setStyleSheet(!errors.isEmpty() ? "color: #b45309;"
                               : included        ? "color: #15803d;"
                                                 : "color: #64748b;");
}
void MainWindow::refreshSummary() {
    setWindowModified(model_->hasPendingChanges());
    int errors = 0;
    int selected = 0;
    int selected_errors = 0;
    for (int row = 0; row < model_->rowCount(); ++row) {
        const bool invalid = !model_->errors(row).isEmpty();
        errors += invalid;
        selected += model_->isIncluded(row);
        selected_errors += model_->isIncluded(row) && invalid;
    }
    summary_->setText(QStringLiteral("Строк: %1 · ошибок: %2 · выбрано: %3%4")
                          .arg(model_->rowCount())
                          .arg(errors)
                          .arg(selected)
                          .arg(selected_errors
                                   ? QStringLiteral(" · исправьте или снимите флажки с %1 строк")
                                         .arg(selected_errors)
                                   : QString{}));
    export_->setEnabled(model_->canExport());
    gallery_->setEnabled(model_->rowCount() > 0);
}
void MainWindow::createManual() {
    finishTableEdit();
    auto created = create_part(this);
    if (!created)
        return;
    table_->selectRow(model_->add(std::move(*created)));
    refreshSummary();
    loadSelection();
}
void MainWindow::exportFiles() {
    finishTableEdit();
    if (!model_->canExport())
        return;
    const auto directory = QFileDialog::getExistingDirectory(this, QStringLiteral("Папка для DXF"));
    if (directory.isEmpty())
        return;
    const auto result = export_parts(directory, model_->included());
    if (!result.error.isEmpty()) {
        QMessageBox::warning(
            this, QStringLiteral("Экспорт не завершён"),
            result.error + QStringLiteral("\nУже сохранено файлов: %1").arg(result.files.size()));
        return;
    }
    model_->markExported();
    statusBar()->showMessage(QStringLiteral("Сохранено DXF: %1 · %2")
                                 .arg(result.files.size())
                                 .arg(QDir::toNativeSeparators(directory)));
}
void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    prepareDrop(event->mimeData());
    // Accept entry even for invalid files so we receive leave/move events and
    // can keep the explanation visible while displaying the forbidden cursor.
    event->setDropAction(Qt::CopyAction);
    event->accept();
}
void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
    if (drop_import_ && drop_error_.isEmpty()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else
        event->ignore();
}
void MainWindow::dragLeaveEvent(QDragLeaveEvent* event) {
    clearDrop();
    event->accept();
}
void MainWindow::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (!drop_import_ || urls.size() != 1 || urls[0].toLocalFile() != drop_path_)
        prepareDrop(event->mimeData());
    if (!drop_error_.isEmpty()) {
        event->ignore();
        statusBar()->showMessage(drop_error_, 5000);
        drop_timer_->start(3000);
        return;
    }
    const auto path = drop_path_;
    auto imported = std::move(*drop_import_);
    clearDrop();
    if (applyImport(path, std::move(imported), true)) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else
        event->ignore();
}
void MainWindow::prepareDrop(const QMimeData* mime) {
    clearDrop();
    const auto urls = mime->urls();
    if (urls.size() != 1) {
        drop_error_ = QStringLiteral("Перетащите один список деталей: XLSX, CSV или TSV.");
    } else if (!urls[0].isLocalFile()) {
        drop_error_ = QStringLiteral("Перетащите файл с диска, а не ссылку на сайт.");
    } else if (QFileInfo(urls[0].toLocalFile()).isDir()) {
        drop_error_ = QStringLiteral("Это папка. Выберите файл XLSX, CSV или TSV.");
    } else {
        drop_path_ = urls[0].toLocalFile();
        drop_import_ = import_list(drop_path_);
        if (!drop_import_->needs_text_options)
            drop_error_ = drop_import_->error;
    }
    showDropOverlay(drop_error_);
}
void MainWindow::showDropOverlay(const QString& error) {
    const bool valid = error.isEmpty();
    drop_overlay_->setProperty("valid", valid);
    drop_overlay_->setStyleSheet(
        valid ? "QFrame#dropOverlay { background: rgba(239,246,255,240); "
                "border: 2px dashed #2563eb; border-radius: 10px; color: #1e40af; }"
              : "QFrame#dropOverlay { background: rgba(254,242,242,240); "
                "border: 2px dashed #dc2626; border-radius: 10px; color: #991b1b; }");
    drop_title_->setText(valid ? QStringLiteral("Отпустите, чтобы открыть список")
                               : QStringLiteral("Не удалось принять файл"));
    drop_message_->setText(
        valid ? (drop_import_->needs_text_options
                     ? QStringLiteral("После отпускания выберите кодировку и разделитель файла.")
                     : QStringLiteral("%1 · строк: %2\n%3")
                           .arg(drop_import_->source_description)
                           .arg(drop_import_->rows.size())
                           .arg(model_->rowCount() ? QStringLiteral("Текущий список будет заменён")
                                                   : QStringLiteral("Детали появятся в списке")))
              : error);
    drop_overlay_->setGeometry(rect().adjusted(8, 8, -8, -8));
    drop_overlay_->show();
    drop_overlay_->raise();
}
void MainWindow::clearDrop() {
    drop_timer_->stop();
    drop_overlay_->hide();
    drop_path_.clear();
    drop_error_.clear();
    drop_import_.reset();
}
void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    // resize() in the constructor can run before the overlay is constructed.
    if (drop_overlay_)
        drop_overlay_->setGeometry(rect().adjusted(8, 8, -8, -8));
}
void MainWindow::closeEvent(QCloseEvent* event) {
    finishTableEdit();
    QMessageBox question(QMessageBox::Question, QStringLiteral("Выход"),
                         QStringLiteral("Закрыть программу?"), QMessageBox::Yes | QMessageBox::No,
                         this);
    question.setObjectName(QStringLiteral("exitConfirmation"));
    question.button(QMessageBox::Yes)->setText(QStringLiteral("Да"));
    question.button(QMessageBox::No)->setText(QStringLiteral("Нет"));
    question.setDefaultButton(QMessageBox::No);
    question.setEscapeButton(QMessageBox::No);
    if (question.exec() != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    event->accept();
}
} // namespace dxfgen
