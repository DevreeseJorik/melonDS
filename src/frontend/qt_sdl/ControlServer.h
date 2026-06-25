/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef CONTROLSERVER_H
#define CONTROLSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include <QByteArray>
#include <QJsonObject>
#include <QJsonArray>

#include <optional>
#include <cstdint>
#include <vector>
#include <stdio.h>

class EmuInstance;

struct CtrlTraceRange
{
    uint32_t addrMin;
    uint32_t addrMax;
    bool captureExec;
    bool captureRead;
    bool captureWrite;
};

// TCP control server — listens on 127.0.0.1:63742 by default.
// Protocol: newline-delimited JSON.
//   Request:  {"cmd": "pause", "id": 1}\n
//   Response: {"id": 1, "ok": true}\n
//   Event:    {"event": "break_hit", "addr": "0x020000b4", "cpu": 0}\n
class ControlServer : public QObject
{
    Q_OBJECT
public:
    explicit ControlServer(EmuInstance* inst, QObject* parent = nullptr);
    ~ControlServer() override;

    bool start(quint16 port = 63742);
    void stop();

    void broadcastEvent(const QJsonObject& evt);

public slots:
    void onBreakHit(quint32 addr, int cpu);
    void onEmuStart();
    void onEmuStop();
    void onEmuPause(bool paused);

private slots:
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();

private:
    void dispatchCommand(QTcpSocket* client, const QJsonObject& req);
    void sendTo(QTcpSocket* client, const QJsonObject& obj);

    static uint32_t parseAddr(const QJsonValue& v);
    static uint32_t parseButtonBits(const QJsonArray& buttons);

    QJsonObject cmdEmuStatus      (const QJsonObject& req);
    QJsonObject cmdPause          (const QJsonObject& req);
    QJsonObject cmdResume         (const QJsonObject& req);
    QJsonObject cmdReset          (const QJsonObject& req);
    QJsonObject cmdFrameStep      (const QJsonObject& req);
    QJsonObject cmdBootRom        (const QJsonObject& req);
    QJsonObject cmdSaveState      (const QJsonObject& req);
    QJsonObject cmdLoadState      (const QJsonObject& req);
    QJsonObject cmdPressButtons   (const QJsonObject& req);
    QJsonObject cmdReleaseButtons (const QJsonObject& req);
    QJsonObject cmdTouchScreen    (const QJsonObject& req);
    QJsonObject cmdReleaseScreen  (const QJsonObject& req);
    QJsonObject cmdAddBreakpoint  (const QJsonObject& req);
    QJsonObject cmdRemoveBreakpoint(const QJsonObject& req);
    QJsonObject cmdClearBreakpoints(const QJsonObject& req);
    QJsonObject cmdListBreakpoints (const QJsonObject& req);
    QJsonObject cmdGetRegisters   (const QJsonObject& req);
    QJsonObject cmdReadMemory     (const QJsonObject& req);
    QJsonObject cmdWriteMemory    (const QJsonObject& req);
    QJsonObject cmdReadU8         (const QJsonObject& req);
    QJsonObject cmdReadU16        (const QJsonObject& req);
    QJsonObject cmdReadU32        (const QJsonObject& req);
    QJsonObject cmdWriteU8        (const QJsonObject& req);
    QJsonObject cmdWriteU16       (const QJsonObject& req);
    QJsonObject cmdWriteU32       (const QJsonObject& req);
    QJsonObject cmdReadMulti      (const QJsonObject& req);
    QJsonObject cmdWriteMulti     (const QJsonObject& req);
    QJsonObject cmdContinue       (const QJsonObject& req);
    QJsonObject cmdStep           (const QJsonObject& req);
    void        cmdWaitBreak      (QTcpSocket* client, const QJsonObject& req);
    QJsonObject cmdStartTrace     (const QJsonObject& req);
    QJsonObject cmdStopTrace      (const QJsonObject& req);

    EmuInstance*  Inst;
    QTcpServer*   Server = nullptr;
    QHash<QTcpSocket*, QByteArray> RecvBuf;

    struct PendingBreak { QTcpSocket* client; int id; };
    std::optional<PendingBreak> BreakWaiter;

    FILE* TraceFile = nullptr;
};

#endif // CONTROLSERVER_H
