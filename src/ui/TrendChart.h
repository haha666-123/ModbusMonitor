#pragma once
// ============================================================================
//  实时趋势曲线
//  ---------------------------------------------------------------------------
//  每次刷新用环形缓冲的快照整段替换 series 数据（而非逐点追加），
//  这样既能自动滚动窗口，又不会无限增长。
// ============================================================================
#include "../common/ModbusDefs.h"

#include <QChartView>

#include <vector>

QT_BEGIN_NAMESPACE
class QChart;
class QLineSeries;
class QValueAxis;
class QDateTimeAxis;
QT_END_NAMESPACE

namespace mb {

class TrendChart : public QChartView {
    Q_OBJECT
public:
    explicit TrendChart(QWidget* parent = nullptr);

    void setActiveSeries(quint16 addr, const QString& name, const QString& unit);
    void setHistory(const std::vector<DataPoint>& points);
    void clear();

    [[nodiscard]] quint16 activeAddr() const { return m_addr; }

private:
    QChart*        m_chart = nullptr;
    QLineSeries*   m_series = nullptr;
    QValueAxis*    m_axisY = nullptr;
    QDateTimeAxis* m_axisX = nullptr;

    quint16 m_addr = 0;
    QString m_title;
    QString m_unit;
};

} // namespace mb
