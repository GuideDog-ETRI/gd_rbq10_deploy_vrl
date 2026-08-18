#pragma once

#include <QObject>
#include <QString>

// 연결 상태의 QML 파사드.
//
// RobotState 와 달리 폴링하지 않는다. CommunicationClient 가 이미 tcpConnected /
// tcpDisconnected 시그널을 이벤트로 내주므로 그대로 이어받는다.
// (DiffAssign.h 의 diff 방출은 "폴링 원본"에만 필요한 처리다.)
class ConnectionState : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool    connected  READ connected  NOTIFY changed)
    Q_PROPERTY(QString ip         READ ip         NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    // 마지막 해제 사유. 연결 실패 원인을 화면에 남기기 위한 것.
    Q_PROPERTY(QString lastReason READ lastReason NOTIFY changed)

public:
    explicit ConnectionState(QObject* parent = nullptr) : QObject(parent) {}

    bool    connected() const  { return m_connected; }
    QString ip() const         { return m_ip; }
    QString lastReason() const { return m_lastReason; }
    QString statusText() const
    {
        return m_connected ? QStringLiteral("CONNECTED  %1").arg(m_ip)
                           : QStringLiteral("DISCONNECTED");
    }

    void setConnected(const QString& ip)
    {
        m_connected = true;
        m_ip = ip;
        m_lastReason.clear();
        emit changed();
    }

    void setDisconnected(const QString& ip, const QString& reason)
    {
        m_connected = false;
        m_ip = ip;
        m_lastReason = reason;
        emit changed();
    }

Q_SIGNALS:
    // 필드 수가 적고 항상 함께 바뀌므로 signal 하나로 묶는다.
    void changed();

private:
    bool    m_connected = false;
    QString m_ip;
    QString m_lastReason;
};
