#pragma once

#include "dxfgen/part.hpp"
#include <QAbstractTableModel>

namespace dxfgen {
class PartsModel final : public QAbstractTableModel {
  public:
    explicit PartsModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    const Part* part(int row) const;
    void update(int row, Part part);
    int add(Shape shape);
    void remove(int row);
    std::vector<Part> included() const;

  private:
    struct Row {
        Part part;
        bool included = true;
    };
    std::vector<Row> rows_;
};
} // namespace dxfgen
