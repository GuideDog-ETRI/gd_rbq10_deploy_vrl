#include "CommunicationClient.h"
#include <QtEndian>
#include <cstring>
#include <algorithm>

// ===== ctor =====
CommunicationClient::CommunicationClient(QObject* parent)
    : QObject(parent)
{
    tcpSocket = new QTcpSocket(this);
    tcpPort_ = 18000;
    connect(tcpSocket, &QTcpSocket::connected, this, &CommunicationClient::onConnected);
    connect(tcpSocket, &QTcpSocket::disconnected, this, &CommunicationClient::onDisconnected);
    connect(tcpSocket, &QTcpSocket::readyRead, this, &CommunicationClient::onReadyRead);
    connect(tcpSocket,
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, &CommunicationClient::onSocketError);

    lastReceivedTime = QDateTime::currentDateTime();
    nextAttemptAt_ = QDateTime::currentDateTime();

    // 수신 타임아웃
    readTimeoutTimer = new QTimer(this);
    readTimeoutTimer->setInterval(1000);
    readTimeoutTimer->setTimerType(Qt::CoarseTimer);
    connect(readTimeoutTimer, &QTimer::timeout, this, &CommunicationClient::onReadTimeoutCheck);
    readTimeoutTimer->start();

    // 비콘 리스너
    m_beacon = new QUdpSocket(this);
    connect(m_beacon, &QUdpSocket::readyRead, this, &CommunicationClient::onBeaconReceived);

    // 연결 워치독
    connectWatchdog_ = new QTimer(this);
    connectWatchdog_->setInterval(200);
    connectWatchdog_->setTimerType(Qt::PreciseTimer);
    connect(connectWatchdog_, &QTimer::timeout, this, [this]()
    {
        if (tcpSocket && tcpSocket->state() == QAbstractSocket::ConnectingState)
        {
            if (connectStartAt_.msecsTo(QDateTime::currentDateTime()) > connectTimeoutMs_)
            {
                FILE_LOG_AS(logWARNING, "TCP") << "connect watchdog timeout -> abort";
                tcpSocket->abort();
                markDisconnected("connect timeout");
            }
        }
    });
}

// ===== 비콘 기반 자동 연결 =====
void CommunicationClient::startAutoConnect(quint16 beaconPort, bool simMode)
{
    if (m_beacon->state() == QAbstractSocket::BoundState) return;

    m_simMode = simMode;
    const QHostAddress bindAddr = simMode ? QHostAddress(QHostAddress::LocalHost)
                                          : QHostAddress(QHostAddress::Any);

    if (!m_beacon->bind(bindAddr, beaconPort,
                        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
    {
        FILE_LOG_AS(logERROR, "BEACON") << "Failed to bind on port " << beaconPort;
    }
    else
    {
        FILE_LOG_AS(logSUCCESS, "BEACON") << "Listening on port " << beaconPort
                             << (simMode ? " (SIM: localhost only)" : "");
    }
}

void CommunicationClient::stopAutoConnect()
{
    m_beacon->close();
}

void CommunicationClient::onBeaconReceived()
{
    while (m_beacon->hasPendingDatagrams())
    {
        QByteArray data;
        data.resize(int(m_beacon->pendingDatagramSize()));
        QHostAddress sender;
        m_beacon->readDatagram(data.data(), data.size(), &sender);

        // 이미 연결됨 → 무시
        if (bIsConnect_) continue;

        // 연결 시도 중 → 무시
        if (tcpSocket && tcpSocket->state() == QAbstractSocket::ConnectingState) continue;

        // 패킷 검증: magic(0xCA 0xFE) + port(2)
        if (data.size() < 4) continue;
        if (data[0] != '\xCA' || data[1] != '\xFE') continue;

        // 백오프 체크
        if (QDateTime::currentDateTime() < nextAttemptAt_) continue;

        // TCP 포트 추출
        quint16 port;
        memcpy(&port, data.constData() + 2, 2);
        tcpPort_ = qFromBigEndian(port);

        // 발신자 IP 정규화 (::ffff:127.0.0.1 → 127.0.0.1)
        QString ip = sender.toString();
        if (ip.startsWith("::ffff:")) ip = ip.mid(7);

        // SIM 모드: loopback 발신자만 수락 (LAN 상의 다른 인스턴스 차단)
        if (m_simMode && ip != "127.0.0.1") {
            continue;
        }

        FILE_LOG_AS(logINFO, "BEACON") << "Server found at " << ip.toStdString() << ":" << tcpPort_;
        Connect(ip);
    }
}

// ===== 연결 =====
void CommunicationClient::Connect(const QString& ip)
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::UnconnectedState) return;

    currentIp_ = ip;
    FILE_LOG_AS(logINFO, "TCP") << "Connecting to " << ip.toStdString() << ":" << tcpPort_;
    connectStartAt_ = QDateTime::currentDateTime();
    tcpSocket->connectToHost(ip, tcpPort_);
    if (!connectWatchdog_->isActive()) connectWatchdog_->start();
}

void CommunicationClient::Disconnect()
{
    buffer.clear();
    if (tcpSocket) tcpSocket->disconnectFromHost();
}

// ===== 송신 =====
bool CommunicationClient::sendCommand(const COMMAND_STRUCT& cmd)
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
    {
        FILE_LOG_AS(logWARNING, "TCP") << "sendCommand dropped (not connected)";
        return false;
    }
    const char* raw = reinterpret_cast<const char*>(&cmd);
    const int len = static_cast<int>(sizeof(COMMAND_STRUCT));
    QByteArray bytes = QByteArray::fromRawData(raw, len);
    const qint64 n = tcpSocket->write(bytes);
    if (n < 0)
    {
        FILE_LOG_AS(logERROR, "TCP") << "write() failed";
        return false;
    }
    emit tcpBytesWritten(n);
    FILE_LOG_AS(logINFO, "TCP") << "sendCommand queued " << n << " bytes";
    return true;
}

bool CommunicationClient::sendRaw(const QByteArray& bytes)
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
    {
        FILE_LOG_AS(logWARNING, "TCP") << "sendRaw dropped (not connected)";
        return false;
    }
    const qint64 n = tcpSocket->write(bytes);
    if (n < 0)
    {
        FILE_LOG_AS(logERROR, "TCP") << "write() failed";
        return false;
    }
    emit tcpBytesWritten(n);
    FILE_LOG_AS(logINFO, "TCP") << "sendRaw queued " << n << " bytes";
    return true;
}

// ===== 연결 이벤트 =====
void CommunicationClient::onConnected()
{
    FILE_LOG_AS(logSUCCESS, "TCP") << "Connected to " << currentIp_.toStdString();
    bIsConnect_ = true;
    if (sharedConsole) sharedConsole->bIsConnect = true;
    resetBackoffOnSuccess();
    connectWatchdog_->stop();
    emit tcpConnected(currentIp_);
}

void CommunicationClient::onDisconnected()
{
    if (sender() && sender() != tcpSocket) return;
    markDisconnected("QTcpSocket disconnected()");
}

void CommunicationClient::onSocketError(QAbstractSocket::SocketError)
{
    if (sender() && sender() != tcpSocket) return;
    FILE_LOG_AS(logWARNING, "TCP") << "socket error "
        << (tcpSocket ? tcpSocket->errorString().toStdString() : std::string("no socket"));
    markDisconnected("socket error");
}

// ===== 공통 =====
void CommunicationClient::onReadTimeoutCheck()
{
    if (bIsConnect_)
    {
        if (lastReceivedTime.msecsTo(QDateTime::currentDateTime()) > readTimeoutMs_)
        {
            FILE_LOG_AS(logWARNING, "TCP") << "Read timeout (> " << readTimeoutMs_ << " ms). Aborting socket.";
            if (tcpSocket) tcpSocket->abort();
        }
    }
}

void CommunicationClient::markDisconnected(const char* reason)
{
    if (bIsConnect_)
        FILE_LOG_AS(logWARNING, "TCP") << "Disconnected from " << currentIp_.toStdString()
            << " (" << (reason ? reason : "") << ")";
    bIsConnect_ = false;
    if (sharedConsole) sharedConsole->bIsConnect = false;
    emit tcpDisconnected(currentIp_, reason ? QString::fromUtf8(reason) : QString());
    currentIp_.clear();

    connectWatchdog_->stop();

    // 백오프: 다음 비콘까지 대기
    nextAttemptAt_ = QDateTime::currentDateTime().addMSecs(backoffMs_);
    backoffMs_ = std::min(backoffMs_ * 2, backoffMaxMs_);
}

void CommunicationClient::resetBackoffOnSuccess()
{
    backoffMs_ = 1000;
    nextAttemptAt_ = QDateTime::currentDateTime();
}

// ===== 수신 처리 =====
void CommunicationClient::onReadyRead()
{
    if (auto* s = qobject_cast<QTcpSocket*>(sender()))
    {
        if (tcpSocket && s != tcpSocket)
        {
            s->readAll();
            return;
        }
    }
    else
    {
        if (!tcpSocket) return;
    }

    lastReceivedTime = QDateTime::currentDateTime();
    buffer.append(tcpSocket->readAll());

    // 버퍼 상한
    constexpr int kMaxBuf = 1 << 20; // 1MB
    if (buffer.size() > kMaxBuf)
    {
        FILE_LOG_AS(logWARNING, "TCP") << "buffer overflow (" << buffer.size() << "), clearing.";
        buffer.clear();
        return;
    }

    // 한 소켓에 두 가지 패킷이 섞여 온다.
    //   텔레메트리 [F0 EF][payload(sizeof(TELEMETRY_FRAME))][00 0F]          — 고정 길이, 50Hz
    //   로그       [F0 EE][level:u8][len:u16 LE][text(len)][00 0F]         — 가변 길이, 있을 때만
    //
    // 로그를 TELEMETRY_FRAME 안에 넣지 않은 이유 셋:
    //   1) 구조체 크기가 바뀌면 로봇과 콘솔을 반드시 같이 배포해야 한다(SharedMemory.hpp 주석).
    //      매직을 따로 쓰면 구버전 콘솔은 이 패킷을 그냥 건너뛰므로 한쪽만 먼저 올려도 안 죽는다.
    //   2) 50Hz 라 메시지가 없는 동안에도 빈 버퍼가 매 틱 나간다.
    //   3) 한 틱에 두 줄 이상 생기면 유실된다 — 하필 문제가 터질 때 로그가 몰린다.
    constexpr int headerSize = 2;
    constexpr int tailSize   = 2;
    constexpr int kLogPrefix = headerSize + 1 + 2;   // 매직 + level + len
    constexpr int kLogMaxLen = 4096;                 // 길이 필드가 깨졌을 때의 방어선
    const int payloadSize = static_cast<int>(sizeof(TELEMETRY_FRAME));
    const int packetSize  = headerSize + payloadSize + tailSize;

    int processed = 0;
    constexpr int kMaxProcessPerRead = 64;
    while (processed < kMaxProcessPerRead)
    {
        const int idxTele = buffer.indexOf(QByteArray::fromRawData("\xF0\xEF", 2));
        const int idxLog  = buffer.indexOf(QByteArray::fromRawData("\xF0\xEE", 2));
        int idx;
        if (idxTele < 0)      idx = idxLog;
        else if (idxLog < 0)  idx = idxTele;
        else                  idx = std::min(idxTele, idxLog);

        if (idx < 0)
        {
            // 매직이 하나도 없다. 마지막 1바이트는 남긴다 — 경계에 걸친 0xF0 일 수 있다.
            if (buffer.size() > 1) buffer.remove(0, buffer.size() - 1);
            break;
        }
        if (idx > 0) buffer.remove(0, idx);
        if (buffer.size() < headerSize) break;

        const bool isLog = static_cast<uchar>(buffer.at(1)) == 0xEE;

        if (!isLog)
        {
            if (buffer.size() < packetSize) break;
            const uchar* raw = reinterpret_cast<const uchar*>(buffer.constData());
            if (raw[packetSize - 2] == 0x00 && raw[packetSize - 1] == 0x0F)
            {
                TELEMETRY_FRAME newCamelData;
                std::memcpy(&newCamelData, buffer.constData() + headerSize, payloadSize);
                buffer.remove(0, packetSize);

                if (sharedConsole) sharedConsole->telemetry = newCamelData;
            }
            else
            {
                buffer.remove(0, headerSize);
            }
            ++processed;
            continue;
        }

        if (buffer.size() < kLogPrefix) break;      // level/len 이 아직 안 왔다
        const uchar* raw = reinterpret_cast<const uchar*>(buffer.constData());
        const quint8  level = raw[2];
        const int     len   = raw[3] | (raw[4] << 8);
        if (len > kLogMaxLen)
        {
            FILE_LOG_AS(logWARNING, "TCP") << "log packet length " << len << " out of range, skipping.";
            buffer.remove(0, headerSize);
            ++processed;
            continue;
        }
        const int total = kLogPrefix + len + tailSize;
        if (buffer.size() < total) break;           // 본문이 아직 다 안 왔다

        if (raw[total - 2] == 0x00 && raw[total - 1] == 0x0F)
        {
            const QString text = QString::fromUtf8(buffer.constData() + kLogPrefix, len);
            buffer.remove(0, total);
            emit logMessageReceived(level, text);
        }
        else
        {
            buffer.remove(0, headerSize);
        }
        ++processed;
    }
}
