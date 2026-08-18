#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

// 로봇이 보내온 로그 줄 (Qt Widgets 콘솔 Log 탭의 이식).
//
// 로그는 상태와 달리 폴링이 아니라 **이벤트**로 온다. CommunicationClient 가
// [F0 EE] 프레임을 풀어 logMessageReceived() 로 올려주면 여기 쌓인다.
//
// 원본 대비 사라진 것:
//   - HTML 연속 공백이 접히는 문제 → &nbsp; 치환 루프. QML Text 는 PlainText 로
//     두면 애초에 안 생긴다.
//   - 스크롤바 QSS 폭 조정 / 터치 드래그 스크롤(LogDragScroll, ~100줄).
//     ListView + ScrollBar 가 기본 제공한다.
class LogModel : public QAbstractListModel
{
    Q_OBJECT

    // 멈춰도 버퍼에는 계속 쌓인다. 재개하면 그 사이 것까지 한꺼번에 보인다.
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    // TLogLevel 이 심각도 오름차순(0=ERROR … 4=DEBUG)이라 "이 값 이하만 표시"다.
    Q_PROPERTY(int filterLevel READ filterLevel WRITE setFilterLevel NOTIFY filterLevelChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countsChanged)
    Q_PROPERTY(int shownCount READ shownCount NOTIFY countsChanged)
    // "N lines" 또는 필터가 걸렸으면 "shown / total lines".
    Q_PROPERTY(QString countText READ countText NOTIFY countsChanged)

public:
    enum Roles {
        LevelRole = Qt::UserRole + 1,
        MessageRole,   // "text" 로 두면 QML Label.text 와 이름이 겹친다
    };

    explicit LogModel(QObject* parent = nullptr);

    Q_INVOKABLE void clear();

    // CommunicationClient::logMessageReceived 에 연결된다.
    void append(quint8 level, const QString& text);

    bool paused() const { return m_paused; }
    void setPaused(bool v);
    int filterLevel() const { return m_filterLevel; }
    void setFilterLevel(int v);

    int totalCount() const { return int(m_all.size()); }
    int shownCount() const { return int(m_shown.size()); }
    QString countText() const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void pausedChanged();
    void filterLevelChanged();
    void countsChanged();

private:
    struct Entry { quint8 level; QString text; };

    bool passes(quint8 level) const { return int(level) <= m_filterLevel; }
    void rebuild();

    // 링 버퍼 상한. 50Hz 로 흘러드는 로그에 메모리가 무한정 차는 것을 막는다
    // (원본 kLogRingMax 와 동일).
    static constexpr int kRingMax = 5000;

    QVector<Entry> m_all;     // 필터와 무관하게 전부 (Pause 중에도 쌓인다)
    QVector<int> m_shown;     // 필터를 통과한 m_all 인덱스 — 모델의 행
    bool m_paused = false;
    int m_filterLevel = 4;    // logDEBUG = 전부 표시
};
