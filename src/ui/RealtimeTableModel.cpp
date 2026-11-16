#include "RealtimeTableModel.h"

#include <QBrush>
#include <QColor>
#include <QFont>

namespace mb {

RealtimeTableModel::RealtimeTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int RealtimeTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_values.size();
}

int RealtimeTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return ColumnCount;
}

quint16 RealtimeTableModel::addrAt(int row) const
{
    if (row < 0 || row >= m_values.size())
        return 0;
    return m_values.at(row).addr;
}

QVariant RealtimeTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_values.size())
        return {};

    const RegValue& v = m_values.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case ColAddr:  return QString::number(v.addr);
        case ColName:  return v.name;
        case ColRaw:   return v.valid ? QString::number(v.raw) : QStringLiteral("-");
        case ColValue: return v.valid ? QString::number(v.value, 'f', 2) : QStringLiteral("-");
        case ColUnit:  return v.unit;
        case ColStatus:
            if (!v.valid)  return QStringLiteral("离线");
            if (v.alarm)   return QStringLiteral("越限");
            return QStringLiteral("正常");
        default: break;
        }
        break;

    case Qt::TextAlignmentRole:
        if (index.column() == ColName || index.column() == ColUnit || index.column() == ColStatus)
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);

    case Qt::ForegroundRole:
        if (!v.valid)
            return QBrush(QColor(0x99, 0x99, 0x99));
        if (v.alarm)
            return QBrush(QColor(0xC0, 0x39, 0x2B));
        return QBrush(QColor(0x22, 0x22, 0x22));

    case Qt::BackgroundRole:
        if (v.alarm)
            return QBrush(QColor(0xFD, 0xEC, 0xEA));
        return {};

    case Qt::FontRole:
        if (index.column() == ColValue || v.alarm) {
            QFont f;
            f.setBold(v.alarm || index.column() == ColValue);
            return f;
        }
        return {};

    default:
        break;
    }
    return {};
}

QVariant RealtimeTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return {};

    if (orientation == Qt::Horizontal) {
        switch (section) {
        case ColAddr:   return QStringLiteral("地址");
        case ColName:   return QStringLiteral("测点");
        case ColRaw:    return QStringLiteral("原始值");
        case ColValue:  return QStringLiteral("工程值");
        case ColUnit:   return QStringLiteral("单位");
        case ColStatus: return QStringLiteral("状态");
        default: break;
        }
    }
    return QAbstractTableModel::headerData(section, orientation, role);
}

void RealtimeTableModel::setValues(const QVector<RegValue>& values)
{
    if (values.size() != m_values.size()) {
        beginResetModel();
        m_values = values;
        endResetModel();
        return;
    }

    const int rows = m_values.size();
    if (rows == 0)
        return;

    m_values = values;
    emit dataChanged(index(0, 0), index(rows - 1, ColumnCount - 1),
                     {Qt::DisplayRole, Qt::ForegroundRole,
                      Qt::BackgroundRole, Qt::FontRole});
}

} // namespace mb
