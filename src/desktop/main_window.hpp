#pragma once

#include "io/excel_import.hpp"
#include <QMainWindow>
class QFrame;
class QLabel;
class QMimeData;
class QPushButton;
class QTableView;
class QTimer;

namespace dxfgen {
class PartsModel;
class PreviewWidget;
class MainWindow final : public QMainWindow {
  public:
    MainWindow();
    bool loadExcel(const QString& path, bool interactive = true);

  protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    void loadSelection();
    void refreshSummary();
    void exportFiles();
    void createManual();
    bool discardChanges();
    void finishTableEdit();
    bool applyImport(const QString& path, ImportResult imported, bool interactive);
    void prepareDrop(const QMimeData* mime);
    void showDropOverlay(const QString& error);
    void clearDrop();
    int currentRow() const;
    PartsModel* model_;
    QTableView* table_;
    PreviewWidget* preview_;
    QLabel* source_;
    QLabel* details_;
    QLabel* validation_;
    QLabel* summary_;
    QPushButton* export_;
    QPushButton* remove_;
    QPushButton* gallery_;
    QFrame* drop_overlay_ = nullptr;
    QLabel* drop_title_;
    QLabel* drop_message_;
    QTimer* drop_timer_;
    QString drop_path_;
    QString drop_error_;
    std::optional<ImportResult> drop_import_;
};
} // namespace dxfgen
