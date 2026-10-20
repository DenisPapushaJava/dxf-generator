#include "desktop/main_window.hpp"
#include "desktop/parts_model.hpp"
#include "io/export_service.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIdentityProxyModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void chooseFile(const QString& path) {
    auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
    if (!dialog)
        qFatal("Expected a file dialog");
    if (dialog->fileMode() == QFileDialog::Directory)
        dialog->setDirectory(path);
    dialog->selectFile(path);
    static_cast<QDialog*>(dialog)->accept();
}
void checkTableEditing(const QString& root) {
    using namespace dxfgen;
    MainWindow window;
    window.show();
    window.activateWindow();
    QApplication::processEvents();
    const auto example = root + "/examples/Детали.xlsx";
    QFile source(example);
    require(source.open(QIODevice::ReadOnly), "cannot read source workbook");
    const auto original_bytes = source.readAll();
    source.close();
    require(window.loadExcel(example, false), "cannot prepare editable table");
    auto* table = window.findChild<QTableView*>("partsTable");
    auto* model = static_cast<PartsModel*>(table->model());
    auto* save = window.findChild<QPushButton*>("exportButton");
    auto begin_edit = [&](PartsModel::Column column, const QString& value) {
        window.activateWindow();
        QApplication::processEvents();
        const auto index = model->index(0, column);
        table->setCurrentIndex(index);
        table->setFocus();
        QKeyEvent edit(QEvent::KeyPress, Qt::Key_F2, Qt::NoModifier);
        QApplication::sendEvent(table, &edit);
        QApplication::processEvents();
        auto* editor = qobject_cast<QLineEdit*>(QApplication::focusWidget());
        if (!editor || !editor->isVisible())
            throw std::runtime_error(
                QStringLiteral("F2 failed for column %1, value %2; focus: %3")
                    .arg(column)
                    .arg(value)
                    .arg(QApplication::focusWidget()
                             ? QApplication::focusWidget()->metaObject()->className()
                             : "none")
                    .toStdString());
        require(!editor->acceptDrops(), "cell editor intercepted whole-window file drops");
        editor->setText(value);
        return editor;
    };
    auto commit = [&](QLineEdit* editor) {
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(editor, &enter);
        QApplication::processEvents();
    };
    auto answer = [&](QMessageBox::StandardButton button) {
        QTimer::singleShot(0, &window, [button] {
            auto* question = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!question)
                qFatal("Expected an unexported-changes question");
            question->button(button)->click();
        });
    };
    require(!window.isWindowModified(), "fresh import marked the window as modified");
    commit(begin_edit(PartsModel::quantity, "0"));
    require(!save->isEnabled() && !model->part(0) &&
                window.findChild<QLabel*>("validationLabel")->text().contains("F2"),
            "invalid edit did not refresh validation and block export");
    auto* quantity_editor = begin_edit(PartsModel::quantity, "12");
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QApplication::sendEvent(quantity_editor, &tab);
    QApplication::processEvents();
    require(model->part(0) && model->part(0)->quantity == 12, "Tab did not commit the cell");
    commit(begin_edit(PartsModel::first, "250,5"));
    auto* cancelled_editor = begin_edit(PartsModel::first, "9999");
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(cancelled_editor, &escape);
    QApplication::processEvents();
    require(std::get<Rectangle>(model->part(0)->shape).width == 250.5,
            "Escape committed a cancelled edit");
    require(
        save->isEnabled() && window.isWindowModified() &&
            window.findChild<QWidget*>("partPreview")->accessibleDescription().contains("250,5"),
        "corrected dimensions did not update preview and pending state");
    const auto type_index = model->index(0, PartsModel::type);
    table->setCurrentIndex(type_index);
    table->edit(type_index);
    QApplication::processEvents();
    auto* combo = qobject_cast<QComboBox*>(QApplication::focusWidget());
    require(combo && combo->objectName() == "partTypeEditor", "shape editor is not a combo box");
    const int circle = combo->findText("Круг");
    combo->setCurrentIndex(circle);
    QMetaObject::invokeMethod(combo, "activated", Qt::DirectConnection, Q_ARG(int, circle));
    QApplication::processEvents();
    require(model->fileName(0) == "К_250,5_6мм_12шт.dxf" &&
                !model->flags(model->index(0, PartsModel::second)).testFlag(Qt::ItemIsEditable),
            "shape combo did not commit immediately or hide the second dimension");
    commit(begin_edit(PartsModel::name, "Заказ №7"));
    require(model->fileName(0) == "Заказ №7_6мм_12шт.dxf" && model->sourceRow(0) == 2,
            "edited source name or source row changed unexpectedly");
    require(!window.loadExcel(example, false), "noninteractive reload discarded Excel edits");
    answer(QMessageBox::No);
    require(!window.loadExcel(example), "reload ignored a cancelled changes question");

    // Clicking Export must commit the active editor even when it has not lost
    // focus through a mouse click (keyboard shortcuts and programmatic actions).
    begin_edit(PartsModel::first, "275.25");
    QTemporaryDir output;
    QTimer::singleShot(0, &window, [&output] { chooseFile(output.path()); });
    save->click();
    const auto path = output.filePath("Заказ №7_6мм_12шт.dxf");
    QFile drawing(path);
    require(drawing.open(QIODevice::ReadOnly) && drawing.readAll().contains("40\n137.625\n"),
            "export used stale editor geometry");
    require(!model->hasPendingChanges() && !window.isWindowModified(),
            "exported edits still count as pending");
    require(source.open(QIODevice::ReadOnly) && source.readAll() == original_bytes,
            "editing or exporting overwrote the source Excel");
    source.close();
    begin_edit(PartsModel::quantity, "13");
    answer(QMessageBox::No);
    require(!window.close() && window.isVisible() && model->part(0)->quantity == 13 &&
                window.isWindowModified(),
            "close did not commit and protect the active cell edit");
    answer(QMessageBox::Yes);
    require(window.close(), "explicitly accepted close was blocked");
}
void check(const QString& root) {
    using namespace dxfgen;
    MainWindow window;
    window.show();
    auto* table = window.findChild<QTableView*>("partsTable");
    auto* model = static_cast<PartsModel*>(table->model());
    auto* open = window.findChild<QPushButton*>("openExcelButton");
    auto* save = window.findChild<QPushButton*>("exportButton");
    auto* overlay = window.findChild<QWidget*>("dropOverlay");
    require(model->rowCount() == 0 && !save->isEnabled() &&
                !window.findChild<QPushButton*>("partsGalleryButton")->isEnabled(),
            "startup must be empty");
    require(window.findChild<QLabel*>("sourceLabel")->isHidden() && overlay->isHidden(),
            "startup must not show a permanent drop banner");
    const auto example = root + "/examples/Детали.xlsx";
    QTimer::singleShot(0, &window, [example] { chooseFile(example); });
    open->click();
    require(model->rowCount() == 5 && save->isEnabled(), "open-file action failed");
    auto* preview = window.findChild<QWidget*>("partPreview");
    require(preview->visibleRegion().contains(preview->rect()),
            "preview is clipped by the side panel");
    auto* gallery = window.findChild<QPushButton*>("partsGalleryButton");
    require(gallery->isEnabled() && preview->accessibleDescription().contains("Длина: 200 мм") &&
                preview->accessibleDescription().contains("ширина: 100 мм"),
            "rectangle preview must expose both dimensions");
    table->selectRow(2);
    require(preview->accessibleDescription().contains("Наружный диаметр: 100 мм") &&
                preview->accessibleDescription().contains("внутренний диаметр: 50 мм"),
            "washer preview must expose both diameters");
    int expanded_count = 0;
    auto close_expanded = [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "expandedPreviewDialog")
            qFatal("Expected expanded preview dialog");
        auto* large_preview = dialog->findChild<QWidget*>("partPreview");
        require(large_preview &&
                    large_preview->accessibleDescription() == preview->accessibleDescription(),
                "expanded preview changed the selected dimensions");
        dialog->resize(900, 700);
        QApplication::processEvents();
        require(large_preview->width() > preview->width() && large_preview->height() > 300,
                "expanded preview does not grow with its dialog");
        ++expanded_count;
        dialog->reject();
    };
    QTimer::singleShot(0, &window, close_expanded);
    QMouseEvent double_click(QEvent::MouseButtonDblClick, QPointF(30, 30),
                             QPointF(preview->mapToGlobal(QPoint(30, 30))), Qt::LeftButton,
                             Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(preview, &double_click);
    require(expanded_count == 1 && model->rowCount() == 5 && model->included().size() == 5,
            "preview expansion changed the current list or selection");
    table->selectRow(0);
    QTemporaryDir templates;
    const auto template_base = templates.filePath("Шаблон");
    bool template_saved = false;
    bool template_cancelled = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "excelHelpDialog")
            qFatal("Expected Excel help dialog");
        auto* save_template = dialog->findChild<QPushButton*>("saveTemplateButton");
        QTimer::singleShot(0, dialog, [] {
            auto* chooser = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (!chooser)
                qFatal("Expected template save dialog");
            chooser->reject();
        });
        save_template->click();
        template_cancelled = QDir(templates.path()).entryList(QDir::Files).isEmpty() &&
                             dialog->findChild<QLabel*>("templateSavedLabel")->isHidden();
        QTimer::singleShot(0, dialog, [template_base] { chooseFile(template_base); });
        save_template->click();
        template_saved = dialog->findChild<QLabel*>("templateSavedLabel")->isVisible();
        dialog->reject();
    });
    window.findChild<QPushButton*>("helpButton")->click();
    require(template_cancelled, "cancelled template save wrote a file or reported success");
    require(template_saved && QFileInfo::exists(template_base + ".xlsx"),
            "embedded Excel template not saved with the default extension");
    const auto template_book = import_xlsx(template_base + ".xlsx");
    require(template_book.error.isEmpty() && template_book.rows.size() == 5,
            "saved template is not an importable workbook");
    for (const auto& row : template_book.rows)
        require(row_errors(row).isEmpty(), "saved template contains invalid example rows");
    require(cam_file_name(*template_book.rows[0].part).name == "П_200_100_6мм_8шт.dxf" &&
                cam_file_name(*template_book.rows[1].part).name == "К_150_8мм_3шт.dxf",
            "template examples do not follow the current CAM naming contract");
    require(model->rowCount() == 5 && save->isEnabled(), "help changed the current part list");
    QTemporaryDir output;
    QTimer::singleShot(0, &window, [&output] { chooseFile(output.path()); });
    save->click();
    require(QDir(output.path()).entryList(QDir::Files).size() == 5,
            "export button did not save files");
    require(QFileInfo::exists(output.filePath("К_150_8мм_3шт(1).dxf")),
            "UI duplicate naming differs");

    // A cloud mount can expose an extensionless path. File drags anywhere in the
    // client area must show feedback and must never ask the source to move a file.
    const auto cloud_path = output.filePath("cloud-file-id");
    require(QFile::copy(example, cloud_path), "cannot prepare cloud file");
    QMimeData cloud;
    cloud.setUrls({QUrl::fromLocalFile(cloud_path)});
    QWidget* surfaces[] = {open, table->viewport(), window.findChild<QLabel*>("detailsLabel"),
                           save};
    for (auto* surface : surfaces) {
        const auto local_pos = surface->rect().center();
        const auto pos = surface->mapTo(&window, local_pos);
        QDragEnterEvent hover(local_pos, Qt::CopyAction | Qt::MoveAction, &cloud, Qt::LeftButton,
                              Qt::ShiftModifier);
        QApplication::sendEvent(surface, &hover);
        require(hover.isAccepted() && overlay->isVisible() && overlay->property("valid").toBool(),
                "valid workbook must show an overlay across the window");
        QDragMoveEvent move(local_pos, Qt::CopyAction | Qt::MoveAction, &cloud, Qt::LeftButton,
                            Qt::ShiftModifier);
        QApplication::sendEvent(surface, &move);
        require(move.isAccepted() && move.dropAction() == Qt::CopyAction,
                "import must not move the source file");
        require(overlay->geometry().contains(pos), "drop overlay does not cover client area");
        QDragLeaveEvent leave;
        QApplication::sendEvent(surface, &leave);
        require(overlay->isHidden() && model->rowCount() == 5,
                "drag leave must clear feedback without replacing data");
    }
    QMimeData rejected;
    const QList<QUrl> bad_urls[] = {{QUrl::fromLocalFile(root + "/tests/fixtures/not-zip.xlsx")},
                                    {QUrl::fromLocalFile(root + "/tests/fixtures/damaged.xlsx")},
                                    {QUrl::fromLocalFile(output.path())},
                                    {QUrl::fromLocalFile(example), QUrl::fromLocalFile(cloud_path)},
                                    {QUrl("https://example.com/list.xlsx")}};
    for (const auto& urls : bad_urls) {
        rejected.setUrls(urls);
        QDragEnterEvent hover(QPoint(30, 30), Qt::CopyAction, &rejected, Qt::LeftButton,
                              Qt::NoModifier);
        QApplication::sendEvent(&window, &hover);
        require(hover.isAccepted() && overlay->isVisible() &&
                    !overlay->property("valid").toBool() &&
                    !window.findChild<QLabel*>("dropMessage")->text().isEmpty(),
                "invalid drop needs visible error feedback");
        QDragMoveEvent move(QPoint(30, 30), Qt::CopyAction, &rejected, Qt::LeftButton,
                            Qt::NoModifier);
        QApplication::sendEvent(&window, &move);
        require(!move.isAccepted(), "invalid file must have a forbidden cursor");
        QDragLeaveEvent leave;
        QApplication::sendEvent(&window, &leave);
        require(overlay->isHidden() && model->rowCount() == 5,
                "invalid drag left an overlay or changed current data");
    }

    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(root + "/examples/Детали-с-ошибками.xlsx")});
    QDragEnterEvent enter(QPoint(30, 30), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &enter);
    require(enter.isAccepted(), "file drag rejected");
    window.resize(1200, 700);
    QApplication::processEvents();
    require(overlay->geometry() == window.rect().adjusted(8, 8, -8, -8),
            "overlay did not follow window resize");
    QDropEvent drop(QPointF(30, 30), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &drop);
    require(drop.isAccepted() && model->rowCount() == 10, "drop did not load Excel");
    require(overlay->isHidden(), "overlay remained after import");
    require(!save->isEnabled(), "invalid selected rows must block export");
    table->selectRow(3);
    QApplication::processEvents();
    require(window.findChild<QLabel*>("validationLabel")->text().contains("отверстие"),
            "selected row error missing");
    window.findChild<QPushButton*>("selectReadyButton")->click();
    require(save->isEnabled() && model->included().size() == 3, "select-ready action failed");
    const auto source = window.findChild<QLabel*>("sourceLabel")->text();
    require(!window.loadExcel(root + "/tests/fixtures/not-zip.xlsx", false),
            "bad workbook accepted");
    require(model->rowCount() == 10 && source == window.findChild<QLabel*>("sourceLabel")->text(),
            "failed load replaced current data");

    QTimer::singleShot(0, &window, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog)
            qFatal("Expected manual part dialog");
        auto* shape = dialog->findChild<QComboBox*>("partShape");
        auto* first = dialog->findChild<QDoubleSpinBox*>("firstDimension");
        auto* second = dialog->findChild<QDoubleSpinBox*>("secondDimension");
        auto* thickness = dialog->findChild<QDoubleSpinBox*>("partThickness");
        auto* add = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        if (!first->text().trimmed().isEmpty() || !second->text().trimmed().isEmpty() ||
            !thickness->text().trimmed().isEmpty() || add->isEnabled())
            qFatal("New part must start with empty dimensions, independent of selected table row");
        first->setValue(200);
        if (add->isEnabled())
            qFatal("Rectangle accepted without a width");
        second->setValue(100);
        if (!add->isEnabled())
            qFatal("Complete rectangle not accepted");
        shape->setCurrentIndex(2);
        second->setValue(250);
        if (add->isEnabled())
            qFatal("Washer accepted with oversized opening");
        shape->setCurrentIndex(1);
        if (!second->isHidden() || !add->isEnabled())
            qFatal("Circle still requires the hidden second dimension");
        auto* editor = first->findChild<QLineEdit*>();
        editor->selectAll();
        QKeyEvent clear(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
        QApplication::sendEvent(editor, &clear);
        if (add->isEnabled() || first->value() != 0)
            qFatal("Clearing a dimension left the previous value exportable");
        first->setValue(150);
        dialog->findChild<QDoubleSpinBox*>("partThickness")->setValue(8);
        dialog->findChild<QSpinBox*>("partQuantity")->setValue(3);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    window.findChild<QPushButton*>("manualButton")->click();
    require(model->rowCount() == 11 && model->sourceRow(10) == 0, "manual creation failed");
    require(model->fileName(10) == "К_150_8мм_3шт(1).dxf", "manual circle naming regression");
    const auto prior_name = model->fileName(10);
    QTimer::singleShot(0, &window, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog ||
            !dialog->findChild<QDoubleSpinBox*>("firstDimension")->text().trimmed().isEmpty())
            qFatal("Creating another manual part reused the selected manual row");
        dialog->reject();
    });
    window.findChild<QPushButton*>("manualButton")->click();
    require(model->rowCount() == 11 && model->fileName(10) == prior_name,
            "Cancelling a new part changed an existing manual row");
    require(!window.loadExcel(example, false) && model->rowCount() == 11,
            "noninteractive load discarded manual edits");
}
void checkExitConfirmation() {
    dxfgen::MainWindow window;
    window.show();
    QTimer::singleShot(0, &window, [] {
        auto* question = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!question || question->objectName() != "exitConfirmation" ||
            question->text() != "Закрыть программу?" ||
            question->button(QMessageBox::Yes)->text() != "Да" ||
            question->button(QMessageBox::No)->text() != "Нет" ||
            question->defaultButton() != question->button(QMessageBox::No))
            qFatal("Expected a simple Russian exit question with No as default");
        question->button(QMessageBox::No)->click();
    });
    require(!window.close() && window.isVisible(), "No did not cancel closing an empty window");
    QTimer::singleShot(0, &window, [] {
        auto* question = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!question || question->objectName() != "exitConfirmation")
            qFatal("Expected one exit question");
        question->button(QMessageBox::Yes)->click();
    });
    require(window.close(), "Yes did not close an empty window");
}

void checkGallery(const QString& root) {
    using namespace dxfgen;
    MainWindow window;
    window.show();
    QApplication::processEvents();
    require(!window.findChild<QPushButton*>("enlargePreviewButton"), "old enlarge button remains");
    require(window.loadExcel(root + "/examples/Детали-с-ошибками.xlsx", false),
            "prepare gallery rows");
    auto* table = window.findChild<QTableView*>("partsTable");
    auto* model = static_cast<PartsModel*>(table->model());
    auto* button = window.findChild<QPushButton*>("partsGalleryButton");
    model->selectReady();
    table->selectRow(3);
    require(button->isEnabled(), "gallery disabled for an invalid selected row");
    const auto count = model->included().size();
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "partsGalleryDialog")
            qFatal("Expected parts gallery");
        auto* view = dialog->findChild<QListView*>("partsGalleryView");
        auto* proxy = view ? qobject_cast<QIdentityProxyModel*>(view->model()) : nullptr;
        if (!view || !proxy || proxy->sourceModel() != model || view->model()->rowCount() != 10 ||
            view->currentIndex().row() != 3 ||
            view->editTriggers() != QAbstractItemView::NoEditTriggers)
            qFatal("Gallery omitted rows, lost current selection or allowed editing");
        dialog->resize(450, 600);
        QApplication::processEvents();
        if (view->horizontalScrollBar()->maximum() != 0)
            qFatal("Gallery needs horizontal scrolling at one-card width");
        view->setCurrentIndex(view->model()->index(2, PartsModel::filename));
        dialog->findChild<QPushButton*>("galleryJumpButton")->click();
    });
    button->click();
    require(table->currentIndex().row() == 2 && model->included().size() == count &&
                model->rowCount() == 10 && !model->hasPendingChanges(),
            "gallery jump changed export selection or data");
    const auto source = import_xlsx(root + "/examples/Детали.xlsx");
    std::vector<InputRow> large(2000, source.rows.front());
    for (int i = 0; i < 2000; ++i) {
        auto& input = large[static_cast<std::size_t>(i)];
        input.source_row = i + 2;
        input.cells[0] = QStringLiteral("Деталь %1").arg(i);
        parse_row(input);
    }
    model->replace(std::move(large));
    require(model->setData(model->index(5, PartsModel::quantity), "9", Qt::EditRole),
            "prepare pending gallery edit");
    table->selectRow(5);
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "partsGalleryDialog")
            qFatal("Expected large gallery");
        auto* view = dialog->findChild<QListView*>("partsGalleryView");
        const auto last = view->model()->index(1999, PartsModel::filename);
        view->setCurrentIndex(last);
        view->scrollTo(last);
        QApplication::processEvents();
        if (!view->visualRect(last).intersects(view->viewport()->rect()) ||
            view->findChildren<QWidget*>().size() > 20)
            qFatal("Large gallery does not scroll to the end or creates widgets for every row");
        dialog->reject();
    });
    button->click();
    require(table->currentIndex().row() == 5 && model->rowCount() == 2000 &&
                model->part(5)->quantity == 9 && model->hasPendingChanges(),
            "closing gallery changed current row or pending edits");
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "partsGalleryDialog")
            qFatal("Expected gallery keyboard navigation");
        auto* view = dialog->findChild<QListView*>("partsGalleryView");
        view->setCurrentIndex(view->model()->index(12, PartsModel::filename));
        view->setFocus();
        dialog->activateWindow();
        QApplication::processEvents();
        QTimer::singleShot(300, dialog, &QDialog::reject);
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(view, &enter);
    });
    button->click();
    require(table->currentIndex().row() == 12 && model->hasPendingChanges(),
            "Enter did not jump from gallery to the selected source row");
}

void checkGalleryCheckboxes(const QString& root) {
    using namespace dxfgen;
    MainWindow window;
    window.show();
    require(window.loadExcel(root + "/examples/Детали.xlsx", false), "prepare export gallery");
    auto* table = window.findChild<QTableView*>("partsTable");
    auto* model = static_cast<PartsModel*>(table->model());
    auto* button = window.findChild<QPushButton*>("partsGalleryButton");
    auto* export_button = window.findChild<QPushButton*>("exportButton");
    auto click_checkbox = [](QListView* view, int row, bool double_click = false) {
        const auto index = view->model()->index(row, PartsModel::filename);
        view->setCurrentIndex(index);
        view->scrollTo(index);
        QApplication::processEvents();
        const auto rect = view->visualRect(index);
        const int line = std::max(20, view->fontMetrics().height() + 4);
        const QPointF pos(rect.left() + 24, rect.bottom() - 13 - line / 2);
        const QPointF global = view->viewport()->mapToGlobal(pos.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, pos, global, Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, pos, global, Qt::LeftButton, Qt::NoButton,
                            Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &press);
        QApplication::sendEvent(view->viewport(), &release);
        if (double_click) {
            QMouseEvent twice(QEvent::MouseButtonDblClick, pos, global, Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(view->viewport(), &twice);
            QApplication::sendEvent(view->viewport(), &release);
        }
    };
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "partsGalleryDialog")
            qFatal("Expected export checkboxes gallery");
        auto* view = dialog->findChild<QListView*>("partsGalleryView");
        const auto index = view->model()->index(1, PartsModel::filename);
        if (!view->model()->flags(index).testFlag(Qt::ItemIsUserCheckable))
            qFatal("Gallery checkbox not accessible as checkable");
        click_checkbox(view, 1);
        if (model->isIncluded(1) || model->fileName(4) != "К_150_8мм_3шт.dxf" ||
            view->model()->data(index, Qt::CheckStateRole).toInt() != Qt::Unchecked ||
            model->data(model->index(1, PartsModel::source), Qt::CheckStateRole).toInt() !=
                Qt::Unchecked)
            qFatal("Gallery checkbox did not update source selection and duplicate filename");
        click_checkbox(view, 1);
        if (!model->isIncluded(1) || model->fileName(4) != "К_150_8мм_3шт(1).dxf")
            qFatal("Gallery checkbox did not restore duplicate filename");
        click_checkbox(view, 1, true);
        if (QApplication::activeModalWidget() != dialog || model->isIncluded(1))
            qFatal("Double-click on checkbox jumped to the table or toggled twice");
        dialog->activateWindow();
        view->setFocus();
        QApplication::processEvents();
        QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        QApplication::sendEvent(view, &space);
        if (!model->isIncluded(1))
            qFatal("Space did not toggle the current gallery checkbox");
        for (int row = 0; row < model->rowCount(); ++row)
            click_checkbox(view, row);
        if (model->canExport() || export_button->isEnabled() ||
            !dialog->findChild<QLabel*>("gallerySummaryLabel")->text().contains("в экспорт: 0"))
            qFatal("Clearing gallery selection did not update export button and summary");
        click_checkbox(view, 2);
        dialog->reject();
    });
    button->click();
    require(model->included().size() == 1 && model->isIncluded(2) && export_button->isEnabled() &&
                table->currentIndex().row() == 0 && !model->hasPendingChanges(),
            "Closing gallery lost export selection, changed table row or marked parameters dirty");
    require(window.loadExcel(root + "/examples/Детали-с-ошибками.xlsx", false),
            "prepare gallery error guard");
    model->selectReady();
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* view = dialog->findChild<QListView*>("partsGalleryView");
        click_checkbox(view, 3);
        if (!model->isIncluded(3) || export_button->isEnabled() ||
            !dialog->findChild<QLabel*>("gallerySummaryLabel")->text().contains("с ошибками: 1"))
            qFatal("Selecting invalid gallery row bypassed export validation");
        click_checkbox(view, 3);
        if (!export_button->isEnabled())
            qFatal("Excluding invalid gallery row did not restore export");
        dialog->reject();
    });
    button->click();
}

void checkTextLists(const QString& root) {
    using namespace dxfgen;
    MainWindow window;
    window.show();
    window.activateWindow();
    QApplication::processEvents();
    auto* table = window.findChild<QTableView*>("partsTable");
    auto* model = static_cast<PartsModel*>(table->model());
    require(window.loadExcel(root + "/examples/Детали.csv", false) && model->rowCount() == 5,
            "CSV did not load through the main window");
    require(model->data(model->index(1, PartsModel::type)).toString() == "Круг",
            "short circle name not expanded in the table");
    table->setCurrentIndex(model->index(1, PartsModel::type));
    table->setFocus();
    QKeyEvent edit(QEvent::KeyPress, Qt::Key_F2, Qt::NoModifier);
    QApplication::sendEvent(table, &edit);
    QApplication::processEvents();
    auto* combo = qobject_cast<QComboBox*>(QApplication::focusWidget());
    require(combo && combo->currentText() == "Круг" && combo->count() == 3,
            "alias became an unknown combo entry");
    table->setFocus();
    QApplication::processEvents();
    require(!model->hasPendingChanges() &&
                model->data(model->index(1, PartsModel::type), Qt::EditRole).toString() == "К",
            "opening the type editor changed a short source value");
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(root + "/examples/Детали.tsv")});
    QDragEnterEvent enter(QPoint(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &enter);
    QDropEvent drop(QPointF(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &drop);
    require(drop.isAccepted() && model->rowCount() == 5 &&
                model->fileName(1) == "К_150_8мм_3шт.dxf",
            "TSV drop failed");
    QTemporaryDir dir;
    const auto path = dir.filePath("legacy.csv");
    const QString source = QStringLiteral("Имя файла;Тип;Длина;Ширина;Толщина;Количество\r\n"
                                          "Пластина;К;150;;2,5;3\r\n");
    QByteArray bytes;
    for (const auto ch : source) {
        const auto u = ch.unicode();
        bytes += static_cast<char>(u < 128 ? u : u - 0x410 + 0xc0);
    }
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
            "write legacy CSV");
    file.close();
    require(!window.loadExcel(path, false) && model->rowCount() == 5,
            "noninteractive import guessed a legacy encoding");
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "textImportDialog")
            qFatal("Expected text import parameters");
        auto* encoding = dialog->findChild<QComboBox*>("textEncodingCombo");
        auto* preview = dialog->findChild<QTableWidget*>("textImportPreview");
        if (encoding->currentText() != "Windows-1251" || preview->rowCount() != 1 ||
            preview->item(0, 0)->text() != "Пластина")
            qFatal("Legacy preview did not decode Cyrillic correctly");
        dialog->reject();
    });
    require(!window.loadExcel(path) && model->rowCount() == 5,
            "cancelling encoding selection replaced the list");
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != "textImportDialog")
            qFatal("Expected text import parameters");
        auto* delimiter = dialog->findChild<QComboBox*>("textSeparatorCombo");
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        delimiter->setCurrentIndex(2);
        if (buttons->button(QDialogButtonBox::Open)->isEnabled())
            qFatal("Wrong separator remained importable");
        delimiter->setCurrentIndex(0);
        if (!buttons->button(QDialogButtonBox::Open)->isEnabled())
            qFatal("Correct separator not recovered");
        buttons->button(QDialogButtonBox::Open)->click();
    });
    require(window.loadExcel(path) && model->rowCount() == 1 &&
                model->fileName(0) == "Пластина_2,5мм_3шт.dxf",
            "confirmed legacy import failed");
    require(file.open(QIODevice::ReadOnly) && file.readAll() == bytes, "source CSV overwritten");
}
} // namespace
int main(int argc, char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    try {
        require(app.arguments().size() == 2, "source directory argument required");
        check(app.arguments()[1]);
        checkTableEditing(app.arguments()[1]);
        checkExitConfirmation();
        checkTextLists(app.arguments()[1]);
        checkGallery(app.arguments()[1]);
        checkGalleryCheckboxes(app.arguments()[1]);
        std::cout << "Open, drag/drop, selection, export and manual dialog passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
