#include "LogModel.h"

LogModel::LogModel(QObject* parent) : QAbstractListModel(parent) {}

int LogModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_shown.size());
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    // 색은 내보내지 않는다 — 레벨만 주고 QML(Theme)이 색을 고른다.
    // 디자인 토큰이 한 곳에 모여 있어야 나중에 바꿀 때 C++ 을 안 건드린다.
    return { {LevelRole, "level"}, {MessageRole, "message"} };
}

QVariant LogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_shown.size()) return {};
    const Entry& e = m_all[m_shown[index.row()]];
    switch (role)
    {
    case LevelRole: return int(e.level);
    case MessageRole: return e.text;
    default:        return {};
    }
}

QString LogModel::countText() const
{
    return (m_shown.size() == m_all.size())
        ? tr("%1 lines").arg(m_all.size())
        : tr("%1 / %2 lines").arg(m_shown.size()).arg(m_all.size());
}

void LogModel::append(quint8 level, const QString& text)
{
    m_all.push_back(Entry{level, text});

    if (m_all.size() > kRingMax)
    {
        // 링을 넘겼다. m_shown 이 들고 있는 인덱스가 전부 밀리므로 다시 만든다.
        // 5,000줄에 한 번뿐이라 비용은 무시할 만하다.
        const int drop = int(m_all.size()) - kRingMax;
        m_all.remove(0, drop);
        rebuild();
        emit countsChanged();
        return;
    }

    // 멈춰 있어도 m_all 에는 쌓는다 — 재개하면 그 사이 것까지 보여준다.
    if (!m_paused && passes(level))
    {
        const int row = int(m_shown.size());
        beginInsertRows(QModelIndex(), row, row);
        m_shown.push_back(int(m_all.size()) - 1);
        endInsertRows();
    }
    emit countsChanged();
}

void LogModel::setPaused(bool v)
{
    if (m_paused == v) return;
    m_paused = v;
    // 재개할 때 멈춰 있던 동안 쌓인 것을 한꺼번에 반영한다.
    if (!m_paused) rebuild();
    emit pausedChanged();
    emit countsChanged();
}

void LogModel::setFilterLevel(int v)
{
    if (m_filterLevel == v) return;
    m_filterLevel = v;
    rebuild();
    emit filterLevelChanged();
    emit countsChanged();
}

void LogModel::clear()
{
    beginResetModel();
    m_all.clear();
    m_shown.clear();
    endResetModel();
    emit countsChanged();
}

void LogModel::rebuild()
{
    beginResetModel();
    m_shown.clear();
    m_shown.reserve(m_all.size());
    for (int i = 0; i < m_all.size(); ++i)
        if (passes(m_all[i].level)) m_shown.push_back(i);
    endResetModel();
}
