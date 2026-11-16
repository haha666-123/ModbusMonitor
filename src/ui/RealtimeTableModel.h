#pragma once
// ============================================================================
//  实时数据表模型
//  ---------------------------------------------------------------------------
//  采用 Model/View 架构（QTableView + QAbstractTableModel），
//  数据与视图分离；只对变化的数据发 dataChanged，避免整表重绘。
// ============================================================================
#include "../common/ModbusDefs.h"

#include <QAbstractTableModel>

namespace mb {

class RealtimeTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColAddr = 0,
        ColName,
        ColRaw,
        ColValue,
        ColUnit,
        ColStatus,
        ColumnCount
    };

    explicit RealtimeTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setValues(const QVector<RegValue>& values);
    [[nodiscard]] const QVector<RegValue>& values() const { return m_values; }

    //! 某行对应的寄存器地址（用于趋势曲线选中）
    [[nodiscard]] quint16 addrAt(int row) const;

private:
    QVector<RegValue> m_values;
};

} // namespace mb
