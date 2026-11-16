#include "MainWindow.h"

#include "RealtimeTableModel.h"
#include "TrendChart.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableView>
#include <QItemSelectionModel>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>

namespace mb {

namespace {

// --------------------------------------------------------------------------
//  AI 参数设置对话框（常用服务预设 / 端点 / Key / 模型）
// --------------------------------------------------------------------------
class AiSettingsDialog : public QDialog {
public:
    AiSettingsDialog(const QUrl& endpoint, const QString& key, const QString& model,
                     AiDiagnoser::ProxyMode proxy, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("AI 诊断设置"));
        setMinimumWidth(560);

        auto* form = new QFormLayout(this);

        // ---- 常用服务预设：避免用户自己拼错地址
        m_preset = new QComboBox(this);
        m_preset->addItems({
            QStringLiteral("自定义"),
            QStringLiteral("DeepSeek  (api.deepseek.com)"),
            QStringLiteral("OpenAI    (api.openai.com)"),
        });
        form->addRow(QStringLiteral("常用服务"), m_preset);

        m_endpoint = new QLineEdit(endpoint.toString(), this);
        m_endpoint->setPlaceholderText(
            QStringLiteral("https://api.deepseek.com/chat/completions"));
        form->addRow(QStringLiteral("接口地址"), m_endpoint);

        m_key = new QLineEdit(key, this);
        m_key->setPlaceholderText(QStringLiteral("留空则使用本地规则诊断"));
        m_key->setEchoMode(QLineEdit::Password);
        form->addRow(QStringLiteral("API Key"), m_key);

        m_model = new QLineEdit(model, this);
        form->addRow(QStringLiteral("模型"), m_model);

        m_proxy = new QComboBox(this);
        m_proxy->addItems({QStringLiteral("系统默认"), QStringLiteral("不使用代理")});
        m_proxy->setCurrentIndex(proxy == AiDiagnoser::ProxyMode::None ? 1 : 0);
        form->addRow(QStringLiteral("网络代理"), m_proxy);

        // ---- 实时校验提示
        m_hint = new QLabel(this);
        m_hint->setWordWrap(true);
        form->addRow(m_hint);

        auto* tip = new QLabel(
            QStringLiteral("说明：接口地址必须指向 API 端点（以 /chat/completions 结尾），"
                           "不是浏览器的控制台页面。\n"
                           "Key 在 https://platform.deepseek.com/api_keys 这类页面里创建，"
                           "但调用要发给 api.deepseek.com。\n"
                           "兼容任何 OpenAI 格式接口；留空 Key 时走本地规则诊断。"),
            this);
        tip->setWordWrap(true);
        tip->setStyleSheet(QStringLiteral("color:#666;"));
        form->addRow(tip);

        auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        form->addRow(box);
        connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

        connect(m_preset, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int idx) {
                    if (idx == 1) {
                        m_endpoint->setText(QStringLiteral("https://api.deepseek.com/chat/completions"));
                        m_model->setText(QStringLiteral("deepseek-chat"));
                    } else if (idx == 2) {
                        m_endpoint->setText(QStringLiteral("https://api.openai.com/v1/chat/completions"));
                        m_model->setText(QStringLiteral("gpt-4o-mini"));
                    }
                    refreshHint();
                });
        connect(m_endpoint, &QLineEdit::textChanged, this, [this] { refreshHint(); });

        refreshHint();
    }

    QUrl    endpoint() const { return QUrl(m_endpoint->text().trimmed()); }
    QString apiKey()   const { return m_key->text(); }
    QString model()    const { return m_model->text(); }
    AiDiagnoser::ProxyMode proxyMode() const {
        return m_proxy->currentIndex() == 1 ? AiDiagnoser::ProxyMode::None
                                           : AiDiagnoser::ProxyMode::System;
    }

private:
    void refreshHint()
    {
        const QUrl url(m_endpoint->text().trimmed());
        const QString problem = AiDiagnoser::endpointProblem(url);

        if (!problem.isEmpty()) {
            m_hint->setStyleSheet(QStringLiteral("color:#C0392B;"));
            m_hint->setText(QStringLiteral("⚠ %1").arg(problem));
            return;
        }
        const QUrl normalized = AiDiagnoser::normalizeUrl(url);
        m_hint->setStyleSheet(QStringLiteral("color:#1E6B3A;"));
        m_hint->setText(QStringLiteral("✓ 实际请求：%1").arg(normalized.toString()));
    }

    QComboBox* m_preset = nullptr;
    QLineEdit* m_endpoint = nullptr;
    QLineEdit* m_key = nullptr;
    QLineEdit* m_model = nullptr;
    QComboBox* m_proxy = nullptr;
    QLabel*    m_hint = nullptr;
};

} // namespace

// ---------------------------------------------------------------------------
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Modbus 设备数据采集与监控平台"));
    resize(1280, 800);

    buildRegisters();
    buildUi();

    m_ai = new AiDiagnoser(this);
    m_ai->setHistoryProvider([this](quint16 addr, int count) {
        return m_collector.history(addr, count);
    });
    connect(m_ai, &AiDiagnoser::diagnosisReady,
            this, &MainWindow::onDiagnosisReady);

    // 数据库放在用户数据目录，避免污染工程目录
    const QString dataDir = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataDir);
    m_dbPath = QDir(dataDir).filePath(QStringLiteral("modbus_monitor.db"));

    QString err;
    if (m_storage.open(m_dbPath, &err)) {
        m_storage.setSource(&m_collector.storeQueue());
        m_storage.start();
        m_dbLabel->setText(QStringLiteral("数据库: %1").arg(m_dbPath));
        m_dbLabel->setToolTip(m_dbPath);
    } else {
        m_dbLabel->setText(QStringLiteral("数据库打开失败: %1").arg(err));
    }

    m_uiTimer = new QTimer(this);
    m_uiTimer->setInterval(100);          // 界面节流刷新：10 Hz
    connect(m_uiTimer, &QTimer::timeout, this, &MainWindow::onRefreshUi);
    m_uiTimer->start();

    updateStatus();
}

MainWindow::~MainWindow()
{
    if (m_uiTimer)
        m_uiTimer->stop();
    m_collector.stop();
    m_storage.stop();
    m_storage.close();
}

// --------------------------------------------------------------------------- UI
void MainWindow::buildRegisters()
{
    // 地址 / 名称 / 单位 / 比例 / 下限 / 上限
    m_collector.setRegisters({
        {0, QStringLiteral("温度"), QStringLiteral("℃"),   0.1,  20.0,   80.0, true},
        {1, QStringLiteral("压力"), QStringLiteral("kPa"),  0.1,   0.0,  100.0, true},
        {2, QStringLiteral("液位"), QStringLiteral("%"),    0.1,  10.0,   90.0, true},
        {3, QStringLiteral("转速"), QStringLiteral("rpm"),  1.0,   0.0, 3000.0, true},
        {4, QStringLiteral("电流"), QStringLiteral("A"),    0.01,  0.0,   20.0, true},
        {5, QStringLiteral("电压"), QStringLiteral("V"),    0.1, 180.0,  260.0, true},
    });

    CollectorConfig cfg;
    cfg.mode           = LinkMode::Tcp;
    cfg.host           = QStringLiteral("127.0.0.1");
    cfg.port           = 5020;
    cfg.unitId         = 1;
    cfg.pollIntervalMs = 500;
    cfg.timeoutMs      = 800;
    m_collector.setConfig(cfg);
}

LinkMode MainWindow::currentMode() const
{
    return m_modeCombo->currentIndex() == 0 ? LinkMode::Tcp : LinkMode::Rtu;
}

void MainWindow::buildUi()
{
    // ---------------------------------------------------- 顶部连接参数栏
    auto* toolbar = addToolBar(QStringLiteral("连接"));
    toolbar->setMovable(false);

    toolbar->addWidget(new QLabel(QStringLiteral(" 链路 "), this));
    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItems({QStringLiteral("Modbus TCP"), QStringLiteral("Modbus RTU")});
    toolbar->addWidget(m_modeCombo);
    toolbar->addSeparator();

    toolbar->addWidget(new QLabel(QStringLiteral(" 主机 "), this));
    m_hostEdit = new QLineEdit(QStringLiteral("127.0.0.1"), this);
    m_hostEdit->setFixedWidth(120);
    toolbar->addWidget(m_hostEdit);

    toolbar->addWidget(new QLabel(QStringLiteral(" 端口 "), this));
    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(5020);
    m_portSpin->setFixedWidth(80);
    toolbar->addWidget(m_portSpin);

    toolbar->addWidget(new QLabel(QStringLiteral(" 串口 "), this));
    m_serialEdit = new QLineEdit(QStringLiteral("COM3"), this);
    m_serialEdit->setFixedWidth(80);
    m_serialEdit->setEnabled(false);
    toolbar->addWidget(m_serialEdit);

    m_baudCombo = new QComboBox(this);
    m_baudCombo->addItems({QStringLiteral("9600"), QStringLiteral("19200"),
                           QStringLiteral("38400"), QStringLiteral("57600"),
                           QStringLiteral("115200")});
    m_baudCombo->setEnabled(false);
    toolbar->addWidget(m_baudCombo);

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral(" 从站 "), this));
    m_unitSpin = new QSpinBox(this);
    m_unitSpin->setRange(1, 247);
    m_unitSpin->setValue(1);
    m_unitSpin->setFixedWidth(60);
    toolbar->addWidget(m_unitSpin);

    toolbar->addWidget(new QLabel(QStringLiteral(" 周期(ms) "), this));
    m_pollSpin = new QSpinBox(this);
    m_pollSpin->setRange(50, 10000);
    m_pollSpin->setSingleStep(50);
    m_pollSpin->setValue(500);
    m_pollSpin->setFixedWidth(80);
    toolbar->addWidget(m_pollSpin);

    toolbar->addSeparator();
    m_startBtn = new QPushButton(QStringLiteral("开始采集"), this);
    m_startBtn->setMinimumWidth(100);
    toolbar->addWidget(m_startBtn);

    toolbar->addSeparator();
    auto* aiBtn = new QPushButton(QStringLiteral("AI 设置"), this);
    toolbar->addWidget(aiBtn);

    connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::onStartStop);
    connect(aiBtn, &QPushButton::clicked, this, &MainWindow::onAiSettings);
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                const bool tcp = (idx == 0);
                m_hostEdit->setEnabled(tcp);
                m_portSpin->setEnabled(tcp);
                m_serialEdit->setEnabled(!tcp);
                m_baudCombo->setEnabled(!tcp);
            });

    // ---------------------------------------------------- 中央区域
    m_model = new RealtimeTableModel(this);
    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setColumnWidth(RealtimeTableModel::ColAddr, 60);
    m_table->setColumnWidth(RealtimeTableModel::ColName, 90);
    m_table->setColumnWidth(RealtimeTableModel::ColRaw, 80);
    m_table->setColumnWidth(RealtimeTableModel::ColValue, 100);
    m_table->setColumnWidth(RealtimeTableModel::ColUnit, 60);

    auto* tableBox = new QGroupBox(QStringLiteral("实时数据（点击行查看趋势）"), this);
    auto* tableLayout = new QVBoxLayout(tableBox);
    tableLayout->addWidget(m_table);

    m_chart = new TrendChart(this);
    m_chart->setMinimumHeight(240);

    m_alarmTable = new QTableWidget(0, 5, this);
    m_alarmTable->setHorizontalHeaderLabels(
        {QStringLiteral("时间"), QStringLiteral("测点"), QStringLiteral("值"),
         QStringLiteral("限值"), QStringLiteral("诊断建议")});
    m_alarmTable->horizontalHeader()->setStretchLastSection(true);
    m_alarmTable->verticalHeader()->setVisible(false);
    m_alarmTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_alarmTable->setColumnWidth(0, 90);
    m_alarmTable->setColumnWidth(1, 80);
    m_alarmTable->setColumnWidth(2, 80);
    m_alarmTable->setColumnWidth(3, 80);

    auto* alarmBox = new QGroupBox(QStringLiteral("告警与 AI 诊断"), this);
    auto* alarmLayout = new QVBoxLayout(alarmBox);
    alarmLayout->addWidget(m_alarmTable);

    auto* rightSplitter = new QSplitter(Qt::Vertical, this);
    rightSplitter->addWidget(m_chart);
    rightSplitter->addWidget(alarmBox);
    rightSplitter->setStretchFactor(0, 3);
    rightSplitter->setStretchFactor(1, 2);

    auto* mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->addWidget(tableBox);
    mainSplitter->addWidget(rightSplitter);
    mainSplitter->setStretchFactor(0, 2);
    mainSplitter->setStretchFactor(1, 3);

    setCentralWidget(mainSplitter);

    // ---------------------------------------------------- 状态栏
    m_statusLabel = new QLabel(QStringLiteral("未连接"), this);
    m_statsLabel  = new QLabel(QStringLiteral("-"), this);
    m_dbLabel     = new QLabel(QStringLiteral("数据库: -"), this);
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addWidget(m_statsLabel);
    statusBar()->addPermanentWidget(m_dbLabel);

    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onSelectionChanged);
}

// --------------------------------------------------------------------------- 逻辑
CollectorConfig MainWindow::collectConfigFromUi() const
{
    CollectorConfig cfg;
    cfg.mode           = currentMode();
    cfg.host           = m_hostEdit->text().trimmed();
    cfg.port           = static_cast<quint16>(m_portSpin->value());
    cfg.serialPort     = m_serialEdit->text().trimmed();
    cfg.baudRate       = m_baudCombo->currentText().toInt();
    cfg.unitId         = static_cast<quint8>(m_unitSpin->value());
    cfg.pollIntervalMs = m_pollSpin->value();
    cfg.timeoutMs      = std::max(200, m_pollSpin->value() * 2);
    cfg.autoReconnect  = true;
    return cfg;
}

void MainWindow::onStartStop()
{
    if (!m_running) {
        const CollectorConfig cfg = collectConfigFromUi();
        m_collector.setConfig(cfg);

        QString err;
        if (!m_collector.start(&err)) {
            QMessageBox::warning(this, QStringLiteral("启动失败"), err);
            return;
        }
        m_running = true;
        m_startBtn->setText(QStringLiteral("停止采集"));
        m_statusLabel->setText(QStringLiteral("采集中 · %1")
                                   .arg(cfg.mode == LinkMode::Tcp
                                            ? QStringLiteral("Modbus TCP %1:%2")
                                                  .arg(cfg.host).arg(cfg.port)
                                            : QStringLiteral("Modbus RTU %1@%2")
                                                  .arg(cfg.serialPort).arg(cfg.baudRate)));
    } else {
        m_collector.stop();
        m_running = false;
        m_startBtn->setText(QStringLiteral("开始采集"));
        m_statusLabel->setText(QStringLiteral("已停止"));
    }
    updateStatus();
}

void MainWindow::onRefreshUi()
{
    // 1) 实时值
    const QVector<RegValue> snap = m_collector.snapshot();
    if (!snap.isEmpty())
        m_model->setValues(snap);

    // 2) 新告警
    const QVector<Alarm> alarms = m_collector.takeNewAlarms();
    for (const Alarm& a : alarms) {
        appendAlarmRow(a);
        m_storage.saveAlarm(a);

        // 取最近数据作为 AI 上下文（旁路，不阻塞采集与界面）
        const std::vector<DataPoint> ctx = m_collector.history(a.addr, 20);
        m_ai->diagnose(a.id, a, ctx);
    }

    // 3) 趋势曲线
    if (m_table->selectionModel() && m_table->selectionModel()->hasSelection()) {
        const int row = m_table->selectionModel()->currentIndex().row();
        const quint16 addr = m_model->addrAt(row);
        const QVector<RegValue>& vals = m_model->values();
        QString name, unit;
        if (row >= 0 && row < vals.size()) {
            name = vals.at(row).name;
            unit = vals.at(row).unit;
        }
        m_chart->setActiveSeries(addr, name, unit);
        m_chart->setHistory(m_collector.history(addr, 120));
    }

    updateStatus();
}

void MainWindow::onSelectionChanged()
{
    if (!m_table->selectionModel())
        return;
    const QModelIndex idx = m_table->selectionModel()->currentIndex();
    if (!idx.isValid())
        return;
    const quint16 addr = m_model->addrAt(idx.row());
    const QVector<RegValue>& vals = m_model->values();
    if (idx.row() < vals.size())
        m_chart->setActiveSeries(addr, vals.at(idx.row()).name, vals.at(idx.row()).unit);
}

void MainWindow::appendAlarmRow(const Alarm& alarm)
{
    const int row = m_alarmTable->rowCount();
    m_alarmTable->insertRow(row);

    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(alarm.ts);

    m_alarmTable->setItem(row, 0, new QTableWidgetItem(
        dt.toString(QStringLiteral("HH:mm:ss"))));
    m_alarmTable->setItem(row, 1, new QTableWidgetItem(alarm.name));
    m_alarmTable->setItem(row, 2, new QTableWidgetItem(
        QStringLiteral("%1%2").arg(alarm.value, 0, 'f', 2).arg(alarm.unit)));
    m_alarmTable->setItem(row, 3, new QTableWidgetItem(
        QStringLiteral("%1%2%3").arg(alarm.isHigh ? QStringLiteral("≥")
                                                  : QStringLiteral("≤"))
                                .arg(alarm.limit, 0, 'f', 2)
                                .arg(alarm.unit)));
    m_alarmTable->setItem(row, 4, new QTableWidgetItem(QStringLiteral("诊断中…")));

    m_alarmRows.insert(alarm.id, row);
    m_alarmTable->scrollToBottom();
}

void MainWindow::onDiagnosisReady(qint64 alarmId, const QString& text)
{
    const auto it = m_alarmRows.constFind(alarmId);
    if (it == m_alarmRows.constEnd())
        return;
    const int row = it.value();
    if (row < m_alarmTable->rowCount()) {
        if (auto* item = m_alarmTable->item(row, 4))
            item->setText(text);
    }
}

void MainWindow::onAiSettings()
{
    AiSettingsDialog dlg(m_aiEndpoint, m_aiKey, m_aiModel, m_aiProxy, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_aiEndpoint = dlg.endpoint();
    m_aiKey      = dlg.apiKey();
    m_aiModel    = dlg.model();
    m_aiProxy    = dlg.proxyMode();
    m_ai->configure(m_aiEndpoint, m_aiKey, m_aiModel);
    m_ai->setProxyMode(m_aiProxy);

    const QString problem = AiDiagnoser::endpointProblem(m_aiEndpoint);
    if (!problem.isEmpty()) {
        statusBar()->showMessage(
            QStringLiteral("AI 配置有误，已降级为本地诊断：%1").arg(problem), 10000);
        return;
    }
    if (!m_ai->enabled()) {
        statusBar()->showMessage(
            QStringLiteral("AI 诊断使用本地规则兜底（未填写 API Key）"), 5000);
        return;
    }
    statusBar()->showMessage(
        QStringLiteral("AI 诊断已启用，请求发往：%1").arg(m_ai->endpoint().toString()),
        8000);
}

void MainWindow::updateStatus()
{
    const CollectorStats st = m_collector.stats();

    m_statusLabel->setText(
        QStringLiteral("%1 | %2").arg(
            st.connected ? QStringLiteral("已连接") : QStringLiteral("未连接"),
            st.lastError.isEmpty() ? QStringLiteral("正常") : st.lastError));

    m_statsLabel->setText(
        QStringLiteral("轮询 %1 | 成功 %2 | 失败 %3 | 采样点 %4 | 队列 %5 | 入库 %6")
            .arg(st.polls)
            .arg(st.succeeded)
            .arg(st.failed)
            .arg(st.points)
            .arg(m_collector.storeQueue().size())
            .arg(m_storage.inserted()));
}

} // namespace mb
