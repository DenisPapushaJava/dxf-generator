#include "desktop/part_dialog.hpp"
#include "io/export_service.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace dxfgen {
std::optional<Part> create_part(QWidget* parent) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("manualPartDialog"));
    dialog.setWindowTitle(QStringLiteral("Создать деталь"));
    dialog.setMinimumWidth(350);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* shape = new QComboBox;
    shape->setObjectName(QStringLiteral("partShape"));
    shape->addItems(
        {QStringLiteral("Прямоугольник"), QStringLiteral("Круг"), QStringLiteral("Шайба")});
    form->addRow(QStringLiteral("&Форма"), shape);
    auto dimension = [] {
        auto* input = new QDoubleSpinBox;
        input->setRange(0, 1'000'000);
        input->setDecimals(3);
        input->setSuffix(QStringLiteral(" мм"));
        // Zero is the empty sentinel; required dimensions are validated below.
        input->setSpecialValueText(QStringLiteral(" "));
        QObject::connect(input->findChild<QLineEdit*>(), &QLineEdit::textEdited, input, [input] {
            if (input->cleanText().trimmed().isEmpty())
                input->setValue(0);
        });
        return input;
    };
    auto* first = dimension();
    auto* second = dimension();
    auto* thickness = dimension();
    first->setObjectName(QStringLiteral("firstDimension"));
    second->setObjectName(QStringLiteral("secondDimension"));
    thickness->setObjectName(QStringLiteral("partThickness"));
    auto* first_label = new QLabel;
    first_label->setBuddy(first);
    auto* second_label = new QLabel;
    second_label->setBuddy(second);
    form->addRow(first_label, first);
    form->addRow(second_label, second);
    form->addRow(QStringLiteral("&Толщина (необязательно)"), thickness);
    auto* quantity = new QSpinBox;
    quantity->setObjectName(QStringLiteral("partQuantity"));
    quantity->setRange(1, 1'000'000);
    form->addRow(QStringLiteral("&Количество, шт."), quantity);
    quantity->setValue(1);
    layout->addLayout(form);
    auto* filename = new QLabel;
    filename->setObjectName(QStringLiteral("manualFilenameLabel"));
    filename->setTextFormat(Qt::PlainText);
    filename->setWordWrap(true);
    filename->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    layout->addWidget(filename);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Добавить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
    layout->addWidget(buttons);
    auto current = [&] {
        Part part;
        part.shape = shape->currentIndex() == 0 ? Shape(Rectangle{first->value(), second->value()})
                     : shape->currentIndex() == 1 ? Shape(Circle{first->value()})
                                                  : Shape(Washer{first->value(), second->value()});
        part.quantity = quantity->value();
        part.thickness =
            thickness->value() == 0 ? std::nullopt : std::optional<double>(thickness->value());
        return part;
    };
    auto refresh = [&] {
        const bool rectangle = shape->currentIndex() == 0;
        const bool washer = shape->currentIndex() == 2;
        first_label->setText(rectangle ? QStringLiteral("&Длина")
                             : washer  ? QStringLiteral("&Наружный диаметр")
                                       : QStringLiteral("&Диаметр"));
        second_label->setText(rectangle ? QStringLiteral("&Ширина")
                                        : QStringLiteral("&Внутренний диаметр"));
        second->setVisible(shape->currentIndex() != 1);
        second_label->setVisible(shape->currentIndex() != 1);
        if (first->value() == 0 || (shape->currentIndex() != 1 && second->value() == 0)) {
            filename->setText(QStringLiteral("Введите размеры в миллиметрах."));
            filename->setStyleSheet("color: #64748b;");
            buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
            return;
        }
        const auto name = cam_file_name(current());
        filename->setText(name.error.isEmpty() ? QStringLiteral("Имя DXF:\n%1").arg(name.name)
                                               : name.error);
        filename->setStyleSheet(name.error.isEmpty() ? "color: #15803d;" : "color: #b45309;");
        buttons->button(QDialogButtonBox::Ok)->setEnabled(name.error.isEmpty());
    };
    QObject::connect(shape, &QComboBox::currentIndexChanged, &dialog, refresh);
    for (auto* input : {first, second, thickness})
        QObject::connect(input, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(quantity, &QSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    refresh();
    first->setFocus();
    if (dialog.exec() == QDialog::Accepted)
        return current();
    return {};
}
} // namespace dxfgen
