#pragma once

#include <QMainWindow>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QTableView;

namespace dxfgen {
class PartsModel;
class PreviewWidget;
class MainWindow final : public QMainWindow {
  public:
    MainWindow();

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    void loadSelection();
    void updatePart();
    void refreshSummary();
    void exportFiles();
    int currentRow() const;
    PartsModel* model_;
    QTableView* table_;
    PreviewWidget* preview_;
    QLineEdit* name_;
    QComboBox* shape_;
    QDoubleSpinBox* first_;
    QDoubleSpinBox* second_;
    QDoubleSpinBox* thickness_;
    QSpinBox* quantity_;
    QLabel* firstLabel_;
    QLabel* secondLabel_;
    QLabel* validation_;
    QLabel* summary_;
    QPushButton* export_;
    QPushButton* remove_;
    QWidget* editor_;
    bool loading_ = false;
    bool dirty_ = false;
};
} // namespace dxfgen
