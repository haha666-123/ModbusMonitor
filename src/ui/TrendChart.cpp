#include "TrendChart.h"

#include <QDateTime>
#include <QDateTimeAxis>
#include <QLineSeries>
#include <QValueAxis>

#include <QPointF>
#include <QtCharts/QChart>

#include <algorithm>

namespace mb {

TrendChart::TrendChart(QWidget* parent)
    : QChartView(parent)
{
    m_series = new QLineSeries(this);

    m_chart = new QChart();
    m_chart->addSeries(m_series);
    m_chart->setAnimationOptions(QChart::NoAnimation);
    m_chart->legend()->hide();
    m_chart->setMargins(QMargins(4, 4, 4, 4));
    m_chart->setBackgroundRoundness(0);

    m_axisX = new QDateTimeAxis(this);
    m_axisX->setFormat(QStringLiteral("HH:mm:ss"));
    m_axisX->setTickCount(5);
    m_axisX->setTitleText(QStringLiteral("时间"));

    m_axisY = new QValueAxis(this);
    m_axisY->setLabelFormat(QStringLiteral("%.1f"));
    m_axisY->setTickCount(5);

    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);
    m_series->attachAxis(m_axisX);
    m_series->attachAxis(m_axisY);

    setChart(m_chart);
    setRenderHint(QPainter::Antialiasing, true);
    clear();
}

void TrendChart::setActiveSeries(quint16 addr, const QString& name, const QString& unit)
{
    if (m_addr == addr && m_title == name)
        return;

    m_addr  = addr;
    m_title = name;
    m_unit  = unit;

    m_axisY->setTitleText(unit.isEmpty() ? name : QStringLiteral("%1 (%2)").arg(name, unit));
    m_chart->setTitle(name.isEmpty()
                          ? QStringLiteral("实时趋势")
                          : QStringLiteral("实时趋势 - %1").arg(name));

    m_series->clear();
    m_axisX->setRange(QDateTime::currentDateTime().addSecs(-60),
                      QDateTime::currentDateTime());
}

void TrendChart::clear()
{
    m_series->clear();
    m_chart->setTitle(QStringLiteral("实时趋势（在左侧表格中选中一行）"));
    m_axisY->setTitleText(QString());
}

void TrendChart::setHistory(const std::vector<DataPoint>& points)
{
    if (points.empty()) {
        m_series->clear();
        return;
    }

    double lo = points.front().value;
    double hi = lo;

    QVector<QPointF> pts;
    pts.reserve(static_cast<int>(points.size()));

    for (const DataPoint& p : points) {
        pts.push_back(QPointF(static_cast<qreal>(p.ts), p.value));
        lo = std::min(lo, p.value);
        hi = std::max(hi, p.value);
    }

    m_series->replace(pts);

    // X 轴跟随数据时间范围
    const qint64 first = points.front().ts;
    const qint64 last  = points.back().ts;
    if (last > first) {
        m_axisX->setRange(QDateTime::fromMSecsSinceEpoch(first),
                          QDateTime::fromMSecsSinceEpoch(last));
    }

    // Y 轴留 10% 余量，避免曲线贴边；若数值不变则给一个固定跨度
    if (qFuzzyCompare(lo, hi)) {
        lo -= 1.0;
        hi += 1.0;
    } else {
        const double pad = (hi - lo) * 0.1;
        lo -= pad;
        hi += pad;
    }
    m_axisY->setRange(lo, hi);
}

} // namespace mb
