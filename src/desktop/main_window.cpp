#include "desktop/main_window.hpp"
#include "desktop/parts_model.hpp"
#include "desktop/preview_widget.hpp"
#include "io/export_service.hpp"

#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QVBoxLayout>
#include <algorithm>

namespace dxfgen {
namespace {
QLabel* heading(const QString& text) {
    auto* label = new QLabel(text);
    auto font = label->font();
    font.setPointSize(font.pointSize() + 2);
    font.setBold(true);
    label->setFont(font);
    return label;
}
QDoubleSpinBox* dimension() {
    auto* input = new QDoubleSpinBox;
    input->setRange(0.0, 1'000'000.0);
    input->setDecimals(3);
    input->setSuffix(QStringLiteral(" мм"));
    input->setKeyboardTracking(false);
    return input;
}
} // namespace
MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("DXF Generator"));
    resize(1240, 760);
    setMinimumSize(1000, 600);
    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(14);
    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(heading(QStringLiteral("DXF Generator")));
    auto* subtitle = new QLabel(QStringLiteral("Плоские детали в DXF"));
    subtitle->setStyleSheet("color: #64748b;");
    toolbar->addWidget(subtitle);
    toolbar->addStretch();
    auto* add = new QPushButton(QStringLiteral("&Добавить деталь"));
    auto* menu = new QMenu(add);
    for (int i = 0; i < 3; ++i) {
        const Shape value = i == 0   ? Shape(Rectangle{})
                            : i == 1 ? Shape(Circle{})
                                     : Shape(Washer{});
        auto* action = menu->addAction(QString::fromUtf8(shape_name(value)));
        connect(action, &QAction::triggered, this, [this, value] {
            table_->selectRow(model_->add(value));
            dirty_ = true;
            refreshSummary();
        });
    }
    add->setMenu(menu);
    toolbar->addWidget(add);
    layout->addLayout(toolbar);
    auto* splitter = new QSplitter;
    splitter->setChildrenCollapsible(false);
    auto* listPane = new QWidget;
    auto* listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->addWidget(heading(QStringLiteral("Детали")));
    model_ = new PartsModel(this);
    table_ = new QTableView;
    table_->setModel(model_);
    table_->setAccessibleName(QStringLiteral("Список деталей. Флажок включает деталь в экспорт"));
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(40);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    listLayout->addWidget(table_);
    remove_ = new QPushButton(QStringLiteral("&Удалить выбранную"));
    listLayout->addWidget(remove_);
    splitter->addWidget(listPane);
    auto* previewPane = new QWidget;
    auto* previewLayout = new QVBoxLayout(previewPane);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->addWidget(heading(QStringLiteral("Предпросмотр")));
    preview_ = new PreviewWidget;
    previewLayout->addWidget(preview_, 1);
    splitter->addWidget(previewPane);
    editor_ = new QWidget;
    auto* editLayout = new QVBoxLayout(editor_);
    editLayout->setContentsMargins(8, 0, 0, 0);
    editLayout->addWidget(heading(QStringLiteral("Параметры детали")));
    auto* form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    form->setVerticalSpacing(10);
    name_ = new QLineEdit;
    name_->setPlaceholderText(QStringLiteral("По форме и размерам"));
    form->addRow(QStringLiteral("&Название (необязательно)"), name_);
    shape_ = new QComboBox;
    shape_->addItems(
        {QStringLiteral("Прямоугольник"), QStringLiteral("Круг"), QStringLiteral("Шайба")});
    form->addRow(QStringLiteral("&Форма"), shape_);
    first_ = dimension();
    second_ = dimension();
    firstLabel_ = new QLabel;
    firstLabel_->setBuddy(first_);
    secondLabel_ = new QLabel;
    secondLabel_->setBuddy(second_);
    form->addRow(firstLabel_, first_);
    form->addRow(secondLabel_, second_);
    thickness_ = dimension();
    thickness_->setSpecialValueText(QStringLiteral("Не указана"));
    form->addRow(QStringLiteral("&Толщина (для имени файла)"), thickness_);
    quantity_ = new QSpinBox;
    quantity_->setRange(1, 1'000'000);
    quantity_->setKeyboardTracking(false);
    form->addRow(QStringLiteral("&Количество, шт."), quantity_);
    editLayout->addLayout(form);
    validation_ = new QLabel;
    validation_->setWordWrap(true);
    validation_->setStyleSheet("color: #b45309;");
    validation_->setAccessibleName(QStringLiteral("Результат проверки детали"));
    editLayout->addWidget(validation_);
    filename_ = new QLabel;
    filename_->setWordWrap(true);
    filename_->setTextFormat(Qt::PlainText);
    filename_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    filename_->setAccessibleName(QStringLiteral("Имя DXF для CAM"));
    filename_->setToolTip(QStringLiteral("Повторы позиций получат (1), (2) перед .dxf"));
    editLayout->addWidget(filename_);
    editLayout->addStretch();
    auto* hint = new QLabel(QStringLiteral(
        "Один DXF на позицию. Количество и толщина попадут в имя файла; в DXF — только контуры."));
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #64748b;");
    editLayout->addWidget(hint);
    editor_->setMinimumWidth(230);
    splitter->addWidget(editor_);
    splitter->setSizes({340, 580, 270});
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);
    auto* footer = new QHBoxLayout;
    summary_ = new QLabel;
    footer->addWidget(summary_);
    footer->addStretch();
    export_ = new QPushButton(QStringLiteral("&Экспорт DXF…"));
    export_->setMinimumHeight(36);
    export_->setStyleSheet("QPushButton { background: #2563eb; color: white; padding: 6px 20px; "
                           "border-radius: 4px; } QPushButton:disabled { background: #94a3b8; }");
    export_->setToolTip(
        QStringLiteral("Выбрать папку и сохранить отмеченные позиции. Повторы получат (1), (2). "
                       "Существующие файлы остановят экспорт."));
    footer->addWidget(export_);
    layout->addLayout(footer);
    setCentralWidget(central);
    statusBar()->showMessage(
        QStringLiteral("Прототип %1 · данные хранятся в памяти до закрытия").arg(DXFGEN_VERSION));
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this] { loadSelection(); });
    connect(model_, &QAbstractItemModel::dataChanged, this, [this] { refreshSummary(); });
    connect(remove_, &QPushButton::clicked, this, [this] {
        const int row = currentRow();
        model_->remove(row);
        dirty_ = true;
        if (model_->rowCount())
            table_->selectRow(std::min(row, model_->rowCount() - 1));
        else
            loadSelection();
        refreshSummary();
    });
    connect(name_, &QLineEdit::textEdited, this, [this] { updatePart(); });
    connect(shape_, &QComboBox::currentIndexChanged, this, [this] {
        if (loading_ || !model_->part(currentRow()))
            return;
        Part value = *model_->part(currentRow());
        value.shape = shape_->currentIndex() == 0   ? Shape(Rectangle{})
                      : shape_->currentIndex() == 1 ? Shape(Circle{})
                                                    : Shape(Washer{});
        model_->update(currentRow(), std::move(value));
        dirty_ = true;
        loadSelection();
    });
    for (auto* input : {first_, second_, thickness_})
        connect(input, &QDoubleSpinBox::valueChanged, this, [this] { updatePart(); });
    connect(quantity_, &QSpinBox::valueChanged, this, [this] { updatePart(); });
    connect(export_, &QPushButton::clicked, this, [this] { exportFiles(); });
    table_->selectRow(0);
    refreshSummary();
}
int MainWindow::currentRow() const { return table_->currentIndex().row(); }
void MainWindow::loadSelection() {
    const auto* value = model_->part(currentRow());
    editor_->setEnabled(value != nullptr);
    remove_->setEnabled(value != nullptr);
    preview_->setPart(value);
    if (!value) {
        validation_->clear();
        filename_->clear();
        return;
    }
    loading_ = true;
    name_->setText(QString::fromStdString(value->name));
    shape_->setCurrentIndex(static_cast<int>(value->shape.index()));
    const bool rectangle = std::holds_alternative<Rectangle>(value->shape);
    const bool washer = std::holds_alternative<Washer>(value->shape);
    firstLabel_->setText(rectangle ? QStringLiteral("&Ширина")
                         : washer  ? QStringLiteral("&Наружный диаметр")
                                   : QStringLiteral("&Диаметр"));
    secondLabel_->setText(rectangle ? QStringLiteral("&Высота")
                                    : QStringLiteral("&Внутренний диаметр"));
    second_->setVisible(rectangle || washer);
    secondLabel_->setVisible(rectangle || washer);
    if (const auto* plate = std::get_if<Rectangle>(&value->shape)) {
        first_->setValue(plate->width);
        second_->setValue(plate->height);
    } else if (const auto* disc = std::get_if<Circle>(&value->shape))
        first_->setValue(disc->diameter);
    else {
        const auto& ring = std::get<Washer>(value->shape);
        first_->setValue(ring.outer_diameter);
        second_->setValue(ring.inner_diameter);
    }
    thickness_->setValue(value->thickness.value_or(0.0));
    quantity_->setValue(value->quantity);
    const auto filename = cam_file_name(*value);
    validation_->setText(filename.error.isEmpty() ? QStringLiteral("✓ Деталь готова к экспорту")
                                                  : filename.error);
    validation_->setStyleSheet(filename.error.isEmpty() ? "color: #15803d;" : "color: #b45309;");
    filename_->setText(filename.name.isEmpty() ? QString{}
                                               : QStringLiteral("Имя DXF:\n%1").arg(filename.name));
    loading_ = false;
}
void MainWindow::updatePart() {
    if (loading_ || !model_->part(currentRow()))
        return;
    Part value = *model_->part(currentRow());
    value.name = name_->text().toStdString();
    value.quantity = quantity_->value();
    value.thickness =
        thickness_->value() > 0 ? std::optional<double>(thickness_->value()) : std::nullopt;
    if (shape_->currentIndex() == 0)
        value.shape = Rectangle{first_->value(), second_->value()};
    else if (shape_->currentIndex() == 1)
        value.shape = Circle{first_->value()};
    else
        value.shape = Washer{first_->value(), second_->value()};
    model_->update(currentRow(), value);
    dirty_ = true;
    preview_->setPart(&value);
    const auto filename = cam_file_name(value);
    validation_->setText(filename.error.isEmpty() ? QStringLiteral("✓ Деталь готова к экспорту")
                                                  : filename.error);
    validation_->setStyleSheet(filename.error.isEmpty() ? "color: #15803d;" : "color: #b45309;");
    filename_->setText(filename.name.isEmpty() ? QString{}
                                               : QStringLiteral("Имя DXF:\n%1").arg(filename.name));
}
void MainWindow::refreshSummary() {
    const auto included = model_->included();
    int invalid = 0;
    qint64 count = 0;
    for (const auto& part : included) {
        if (!cam_file_name(part).error.isEmpty())
            ++invalid;
        count += part.quantity;
    }
    summary_->setText(
        QStringLiteral("К экспорту: %1 поз. · %2 шт.%3")
            .arg(static_cast<qulonglong>(included.size()))
            .arg(count)
            .arg(invalid ? QStringLiteral(" · исправьте ошибок: %1").arg(invalid) : QString{}));
    export_->setEnabled(!included.empty() && !invalid);
}
void MainWindow::exportFiles() {
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
    statusBar()->showMessage(QStringLiteral("Сохранено DXF: %1 · %2")
                                 .arg(result.files.size())
                                 .arg(QDir::toNativeSeparators(directory)));
}
void MainWindow::closeEvent(QCloseEvent* event) {
    if (dirty_ && QMessageBox::question(
                      this, QStringLiteral("Закрыть окно?"),
                      QStringLiteral("Список деталей хранится только в памяти. Закрыть окно? "
                                     "Экспортированные DXF останутся в выбранной папке."),
                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    event->accept();
}
} // namespace dxfgen
