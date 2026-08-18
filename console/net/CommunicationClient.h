#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QDateTime>
#include <QByteArray>

#include <SharedMemory.h>

#include <common/ENumClasses.hpp>
#include <common/Log.hpp>

class CommunicationClient : public QObject
{
    Q_OBJECT
public:
    explicit CommunicationClient(QObject* parent = nullptr);

    void setTcpPort(quint16 port) { tcpPort_ = port; }

    // 비콘 기반 자동 연결
    void startAutoConnect(quint16 beaconPort = 18001, bool simMode = false);
    void stopAutoConnect();

    // 수동 연결/해제
    void Connect(const QString& ip);
    void Disconnect();

    // 송신
    bool sendCommand(const COMMAND_STRUCT& cmd);
    bool sendRaw(const QByteArray& bytes);

    // 상태
    bool isConnected() const { return bIsConnect_; }
    QString currentIp() const { return currentIp_; }

signals:
    void tcpConnected(const QString& ip);
    void tcpDisconnected(const QString& ip, const QString& reason);
    void tcpBytesWritten(qint64 n);

    // 로봇이 보낸 로그 한 줄. level 은 common/Log.hpp 의 TLogLevel
    // (0=ERROR, 1=WARNING, 2=SUCCESS, 3=INFO, 4=DEBUG).
    void logMessageReceived(quint8 level, const QString& text);

private slots:
    void onConnected();
    void onDisconnected();
    void onSocketError(QAbstractSocket::SocketError);
    void onReadyRead();
    void onReadTimeoutCheck();
    void onBeaconReceived();

private:
    void markDisconnected(const char* reason);
    void resetBackoffOnSuccess();

    // 메인 TCP 소켓
    QTcpSocket* tcpSocket = nullptr;

    // 비콘 리스너
    QUdpSocket* m_beacon = nullptr;

    // 버퍼/상태
    QByteArray buffer;
    QDateTime lastReceivedTime;
    bool bIsConnect_ = false;
    QString currentIp_;
    quint16 tcpPort_ = 18000;

    // 재연결 백오프
    int backoffMs_ = 1000;
    const int backoffMaxMs_ = 30 * 1000;
    QDateTime nextAttemptAt_;

    // 수신 타임아웃
    QTimer* readTimeoutTimer = nullptr;
    int readTimeoutMs_ = 10 * 1000;

    // 연결 워치독
    QTimer* connectWatchdog_ = nullptr;
    int connectTimeoutMs_ = 3000;
    QDateTime connectStartAt_;

    // SIM 모드: localhost 발신자만 수락
    bool m_simMode = false;
};
