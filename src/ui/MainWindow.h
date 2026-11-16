#pragma once
#include "../core/AiDiagnoser.h"
#include "../core/Collector.h"
#include "../core/Storage.h"

#include <QMainWindow>
#include <QUrl>
#include <QMap>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableView;
class QTableWidget;
class QTimer;
QT_END_NAMESPACE

namespace mb {

class RealtimeTableModel;
class TrendChart;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onStartStop();
    void onRefreshUi();
    void onSelectionChanged();
    void onAiSettings();
    void onDiagnosisReady(qint64 alarmId, const QString& text);

private:
    void buildUi();
    void buildRegisters();
    void appendAlarmRow(const Alarm& alarm);
    void updateStatus();
    [[nodiscard]] CollectorConfig collectConfigFromUi() const;
    [[nodiscard]] LinkMode currentMode() const;

    // 数据链路
    Collector  m_collector;
    Storage    m_storage;
    AiDiagnoser* m_ai = nullptr;

    // 视图
    RealtimeTableModel* m_model = nullptr;
    TrendChart*         m_chart = nullptr;
    QTableView*         m_table = nullptr;
    QTableWidget*       m_alarmTable = nullptr;

    // 控件
    QComboBox*   m_modeCombo  = nullptr;
    QLineEdit*   m_hostEdit   = nullptr;
    QSpinBox*    m_portSpin   = nullptr;
    QLineEdit*   m_serialEdit = nullptr;
    QComboBox*   m_baudCombo  = nullptr;
    QSpinBox*    m_unitSpin   = nullptr;
    QSpinBox*    m_pollSpin   = nullptr;
    QPushButton* m_startBtn   = nullptr;

    QLabel*      m_statusLabel = nullptr;
    QLabel*      m_statsLabel  = nullptr;
    QLabel*      m_dbLabel     = nullptr;

    QTimer* m_uiTimer = nullptr;

    QMap<qint64, int> m_alarmRows;   // alarmId -> 表格行号
    QString m_dbPath;
    QUrl    m_aiEndpoint;
    QString m_aiKey;
    QString m_aiModel    = QStringLiteral("gpt-4o-mini");
    AiDiagnoser::ProxyMode m_aiProxy = AiDiagnoser::ProxyMode::System;
    bool    m_running = false;
};

} // namespace mb
