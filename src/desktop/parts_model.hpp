#pragma once

#include "io/excel_import.hpp"
#include <QAbstractTableModel>

namespace dxfgen {
class PartsModel final : public QAbstractTableModel {
  public:
    enum Column {
        source,
        name,
        type,
        first,
        second,
        thickness,
        quantity,
        filename,
        status,
        columns
    };
    explicit PartsModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    const Part* part(int row) const;
    int sourceRow(int row) const;
    QStringList errors(int row) const;
    QString fileName(int row) const;
    bool isIncluded(int row) const;
    bool canExport() const;
    bool hasPendingChanges() const;
    void markExported();
    void replace(std::vector<InputRow> rows);
    void update(int row, Part part);
    int add(Part part);
    void remove(int row);
    void selectReady();
    std::vector<Part> included() const;

  private:
    struct Row {
        InputRow input;
        bool included = true;
        QString filename;
        QStringList issues;
        bool modified = false;
        bool pending_export = false;
    };
    std::vector<Row> rows_;
    void rebuild(bool notify = true);
    bool validRow(int row) const;
};
} // namespace dxfgen
