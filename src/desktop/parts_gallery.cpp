#include "desktop/parts_gallery.hpp"
#include "desktop/parts_model.hpp"
#include "desktop/preview_widget.hpp"

#include <QApplication>
#include <QCache>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QIdentityProxyModel>
#include <QLabel>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QStyleOptionButton>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace dxfgen {
namespace {
class GalleryModel final : public QIdentityProxyModel {
  public:
    GalleryModel(PartsModel& source, QObject* parent) : QIdentityProxyModel(parent) {
        setSourceModel(&source);
    }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (index.isValid() && index.column() == PartsModel::filename) {
            const auto source = sourceModel()->index(index.row(), PartsModel::source);
            if (role == Qt::CheckStateRole)
                return sourceModel()->data(source, role);
            if (role == Qt::AccessibleTextRole)
                return QIdentityProxyModel::data(index, role).toString() +
                       (sourceModel()->data(source, Qt::CheckStateRole).toInt() == Qt::Checked
                            ? QStringLiteral("; в экспорт")
                            : QStringLiteral("; не в экспорт"));
        }
        return QIdentityProxyModel::data(index, role);
    }
    Qt::ItemFlags flags(const QModelIndex& index) const override {
        auto result = QIdentityProxyModel::flags(index);
        if (index.isValid() && index.column() == PartsModel::filename)
            result |= Qt::ItemIsUserCheckable;
        return result & ~Qt::ItemIsEditable;
    }
    bool setData(const QModelIndex& index, const QVariant& value, int role) override {
        if (!index.isValid() || index.column() != PartsModel::filename ||
            role != Qt::CheckStateRole)
            return false;
        return sourceModel()->setData(sourceModel()->index(index.row(), PartsModel::source), value,
                                      role);
    }
};

struct CardLayout {
    QRect card, heading, picture, status, check;
    int line;
};
CardLayout card_layout(const QStyleOptionViewItem& option) {
    CardLayout layout;
    layout.card = option.rect.adjusted(4, 4, -4, -4);
    layout.line = std::max(20, option.fontMetrics.height() + 4);
    layout.heading = {layout.card.x() + 12, layout.card.y() + 8, layout.card.width() - 24,
                      layout.line};
    layout.picture = {layout.heading.x(), layout.heading.bottom() + layout.line + 6,
                      layout.heading.width(), 250};
    layout.status = {layout.heading.x(), layout.picture.bottom() + 6, layout.heading.width(),
                     layout.line};
    layout.check = layout.status.translated(0, layout.line);
    return layout;
}

class GalleryDelegate final : public QStyledItemDelegate {
  public:
    GalleryDelegate(PartsModel& model, QObject* parent)
        : QStyledItemDelegate(parent), model_(model), cache_(12 * 1024) {
        renderer_.setMinimumSize(0, 0);
        renderer_.setFooterVisible(false);
        connect(&model, &QAbstractItemModel::dataChanged, this, [this] { cache_.clear(); });
        connect(&model, &QAbstractItemModel::modelReset, this, [this] { cache_.clear(); });
    }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override {
        const int line = std::max(20, option.fontMetrics.height() + 4);
        return {340, 286 + 4 * line};
    }
    bool lastPressOnCheckbox() const { return last_check_press_; }
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override {
        if (event->type() != QEvent::MouseButtonPress &&
            event->type() != QEvent::MouseButtonRelease &&
            event->type() != QEvent::MouseButtonDblClick)
            return false;
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton)
            return false;
        const bool hit = card_layout(option).check.contains(mouse->position().toPoint());
        if (event->type() == QEvent::MouseButtonPress) {
            last_check_press_ = hit;
            pressed_ = hit ? QPersistentModelIndex(index) : QPersistentModelIndex{};
            return hit;
        }
        if (event->type() == QEvent::MouseButtonDblClick)
            return hit;
        const bool toggle = hit && pressed_ == index;
        pressed_ = QPersistentModelIndex{};
        if (toggle)
            return model->setData(index,
                                  model->data(index, Qt::CheckStateRole).toInt() == Qt::Checked
                                      ? Qt::Unchecked
                                      : Qt::Checked,
                                  Qt::CheckStateRole);
        return hit;
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        painter->save();
        painter->setClipRect(option.rect);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setFont(option.font);
        const auto layout = card_layout(option);
        const auto& card = layout.card;
        const int row = index.row();
        const bool included = model_.isIncluded(row);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        painter->setBrush(!included  ? QColor("#f1f5f9")
                          : selected ? QColor("#eff6ff")
                                     : QColor("#ffffff"));
        painter->setPen(QPen(QColor(selected ? "#2563eb" : "#dbe2ea"), selected ? 2 : 1));
        painter->drawRoundedRect(card, 8, 8);
        const int line = layout.line;
        const auto& heading = layout.heading;
        painter->setPen(QColor("#64748b"));
        painter->drawText(heading, Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("Деталь №%1").arg(row + 1));
        const auto file = model_.fileName(row);
        painter->setPen(QColor(included ? "#172337" : "#64748b"));
        painter->drawText(
            heading.translated(0, line), Qt::AlignLeft | Qt::AlignVCenter,
            option.fontMetrics.elidedText(file.isEmpty() ? QStringLiteral("Ошибка в данных") : file,
                                          Qt::ElideMiddle, heading.width()));
        const auto& picture = layout.picture;
        if (const auto* part = model_.part(row)) {
            painter->save();
            if (!included) {
                painter->fillRect(picture, QColor("#f1f5f9"));
                painter->setOpacity(0.35);
            }
            painter->drawPixmap(picture.topLeft(), thumbnail(row, *part, picture.size(), option));
            painter->restore();
        } else {
            painter->fillRect(picture, QColor("#f8fafc"));
            painter->setPen(QColor("#b45309"));
            painter->drawText(picture.adjusted(16, 16, -16, -16),
                              Qt::AlignCenter | Qt::TextWordWrap,
                              QStringLiteral("Нет чертежа\nИсправьте параметры в таблице"));
        }
        const auto errors = model_.errors(row);
        const auto& status = layout.status;
        painter->setPen(QColor(!errors.isEmpty() ? "#b45309" : included ? "#15803d" : "#64748b"));
        painter->drawText(status, Qt::AlignLeft | Qt::AlignVCenter,
                          option.fontMetrics.elidedText(
                              errors.isEmpty() ? (included ? QStringLiteral("Готово")
                                                           : QStringLiteral("Не в экспорте"))
                                               : QStringLiteral("Ошибка: %1").arg(errors.front()),
                              Qt::ElideRight, status.width()));
        QStyleOptionButton check;
        check.rect = layout.check;
        check.text = QStringLiteral("В экспорт");
        check.palette = option.palette;
        check.state = QStyle::State_Enabled | (included ? QStyle::State_On : QStyle::State_Off);
        if (selected && option.state.testFlag(QStyle::State_HasFocus))
            check.state |= QStyle::State_HasFocus;
        const auto* style = option.widget ? option.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_CheckBox, &check, painter, option.widget);
        painter->restore();
    }

  private:
    QPixmap thumbnail(int row, const Part& part, QSize size,
                      const QStyleOptionViewItem& option) const {
        const qreal dpr = option.widget ? option.widget->devicePixelRatioF() : 1.0;
        const auto key = QStringLiteral("%1/%2/%3/%4/%5")
                             .arg(row)
                             .arg(size.width())
                             .arg(size.height())
                             .arg(dpr)
                             .arg(option.font.toString());
        if (const auto* cached = cache_.object(key))
            return *cached;
        renderer_.resize(size);
        renderer_.setFont(option.font);
        renderer_.setPart(&part);
        QPixmap pixmap(qRound(size.width() * dpr), qRound(size.height() * dpr));
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);
        renderer_.render(&pixmap);
        const int cost = static_cast<int>(
            (static_cast<qint64>(pixmap.width()) * pixmap.height() * 4 + 1023) / 1024);
        if (cost <= cache_.maxCost())
            cache_.insert(key, new QPixmap(pixmap), cost);
        return pixmap;
    }
    PartsModel& model_;
    mutable PreviewWidget renderer_;
    mutable QCache<QString, QPixmap> cache_;
    QPersistentModelIndex pressed_;
    bool last_check_press_ = false;
};
} // namespace

int show_parts_gallery(QWidget* parent, PartsModel& model, int current_row) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("partsGalleryDialog"));
    dialog.setWindowTitle(QStringLiteral("Все детали — DXF Generator"));
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog.setMinimumSize(400, 450);
    const auto* screen = parent ? parent->screen() : QGuiApplication::primaryScreen();
    const auto available = screen ? screen->availableGeometry().size() : QSize(1200, 900);
    dialog.resize(std::min(1120, available.width() - 40), std::min(840, available.height() - 80));
    auto* layout = new QVBoxLayout(&dialog);
    auto* heading = new QLabel;
    heading->setObjectName(QStringLiteral("gallerySummaryLabel"));
    heading->setWordWrap(true);
    heading->setStyleSheet("font-size: 18px; font-weight: bold;");
    auto refresh_summary = [&] {
        int included = 0, errors = 0;
        for (int row = 0; row < model.rowCount(); ++row)
            if (model.isIncluded(row)) {
                ++included;
                errors += !model.errors(row).isEmpty();
            }
        heading->setText(
            QStringLiteral("Все детали: %1 · в экспорт: %2%3")
                .arg(model.rowCount())
                .arg(included)
                .arg(errors ? QStringLiteral(" · с ошибками: %1").arg(errors) : QString{}));
    };
    QObject::connect(&model, &QAbstractItemModel::dataChanged, &dialog, refresh_summary);
    refresh_summary();
    layout->addWidget(heading);
    auto* hint = new QLabel(QStringLiteral("Флажок или Пробел — включить в экспорт. Двойной щелчок "
                                           "по детали или Enter — к строке в списке."));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* view = new QListView;
    view->setObjectName(QStringLiteral("partsGalleryView"));
    view->setAccessibleName(
        QStringLiteral("Галерея всех деталей, включая ошибочные и не отмеченные для экспорта"));
    auto* gallery_model = new GalleryModel(model, view);
    view->setModel(gallery_model);
    view->setModelColumn(PartsModel::filename);
    auto* delegate = new GalleryDelegate(model, view);
    view->setItemDelegate(delegate);
    view->setViewMode(QListView::IconMode);
    view->setMovement(QListView::Static);
    view->setResizeMode(QListView::Adjust);
    // Uniform sizes keep layout cheap and allow an immediate jump to any row.
    view->setUniformItemSizes(true);
    view->setSpacing(4);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setStyleSheet("QListView { background: #f1f5f9; border: 0; }");
    layout->addWidget(view, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("Закрыть"));
    auto* jump =
        buttons->addButton(QStringLiteral("К строке в списке"), QDialogButtonBox::ActionRole);
    jump->setObjectName(QStringLiteral("galleryJumpButton"));
    jump->setDefault(true);
    layout->addWidget(buttons);
    int selected_row = -1;
    auto choose = [&] {
        if (view->currentIndex().isValid()) {
            selected_row = view->currentIndex().row();
            dialog.accept();
        }
    };
    QObject::connect(jump, &QPushButton::clicked, &dialog, choose);
    QObject::connect(view, &QListView::doubleClicked, &dialog, [&] {
        if (!delegate->lastPressOnCheckbox())
            choose();
    });
    auto* toggle = new QShortcut(QKeySequence(Qt::Key_Space), view);
    toggle->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(toggle, &QShortcut::activated, &dialog, [&] {
        const auto index = view->currentIndex();
        if (index.isValid())
            gallery_model->setData(index,
                                   model.isIncluded(index.row()) ? Qt::Unchecked : Qt::Checked,
                                   Qt::CheckStateRole);
    });
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        auto* shortcut = new QShortcut(QKeySequence(key), view);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        QObject::connect(shortcut, &QShortcut::activated, &dialog, choose);
    }
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (model.rowCount())
        view->setCurrentIndex(gallery_model->index(std::clamp(current_row, 0, model.rowCount() - 1),
                                                   PartsModel::filename));
    QTimer::singleShot(0, &dialog, [&] {
        view->scrollTo(view->currentIndex());
        view->setFocus();
    });
    return dialog.exec() == QDialog::Accepted ? selected_row : -1;
}
} // namespace dxfgen
