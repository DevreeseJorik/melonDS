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

#include "ControlServer.h"
#include "EmuInstance.h"
#include "EmuThread.h"
#include "Window.h"
#include "ARMDisassembler.h"
#include "NDS.h"
#include "ARM.h"

#include <QJsonDocument>
#include <QTimer>
#include <QHostAddress>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <string>

using namespace melonDS;

static constexpr int MaxMemoryTransferBytes = 1024 * 1024; // 1 MiB

ControlServer::ControlServer(EmuInstance* inst, QObject* parent)
    : QObject(parent), Inst(inst)
{
}

ControlServer::~ControlServer()
{
    stop();
}

bool ControlServer::start(quint16 port)
{
    Server = new QTcpServer(this);
    connect(Server, &QTcpServer::newConnection, this, &ControlServer::onNewConnection);
    if (!Server->listen(QHostAddress::LocalHost, port))
    {
        delete Server;
        Server = nullptr;
        return false;
    }
    return true;
}

void ControlServer::stop()
{
    if (TraceFile)
    {
        NDS* nds = Inst->getNDS();
        if (nds) { nds->TraceEnabled = false; nds->TraceLogger = nullptr; }
        fclose(TraceFile);
        TraceFile = nullptr;
    }
    if (Server)
    {
        Server->close();
        Server = nullptr;
    }
}

void ControlServer::broadcastEvent(const QJsonObject& evt)
{
    QByteArray line = QJsonDocument(evt).toJson(QJsonDocument::Compact) + '\n';
    for (QTcpSocket* client : RecvBuf.keys())
        client->write(line);
}

void ControlServer::sendTo(QTcpSocket* client, const QJsonObject& obj)
{
    if (!client || !RecvBuf.contains(client)) return;
    client->write(QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n');
}

void ControlServer::onNewConnection()
{
    while (Server && Server->hasPendingConnections())
    {
        QTcpSocket* client = Server->nextPendingConnection();
        RecvBuf[client] = QByteArray();
        connect(client, &QTcpSocket::readyRead,    this, &ControlServer::onClientReadyRead);
        connect(client, &QTcpSocket::disconnected, this, &ControlServer::onClientDisconnected);
    }
}

void ControlServer::onClientReadyRead()
{
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;
    RecvBuf[client] += client->readAll();
    while (true)
    {
        int nl = RecvBuf[client].indexOf('\n');
        if (nl < 0) break;
        QByteArray line = RecvBuf[client].left(nl).trimmed();
        RecvBuf[client].remove(0, nl + 1);
        if (line.isEmpty()) continue;
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (doc.isObject())
            dispatchCommand(client, doc.object());
        else
        {
            QJsonObject resp;
            resp["id"]    = -1;
            resp["ok"]    = false;
            resp["error"] = "invalid JSON: " + err.errorString();
            sendTo(client, resp);
        }
    }
}

void ControlServer::onClientDisconnected()
{
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;
    if (BreakWaiter && BreakWaiter->client == client)
        BreakWaiter.reset();
    RecvBuf.remove(client);
    client->deleteLater();
}

void ControlServer::dispatchCommand(QTcpSocket* client, const QJsonObject& req)
{
    QString cmd = req["cmd"].toString();
    int id = req.contains("id") ? req["id"].toInt(-1) : -1;

    if (cmd == "wait_break") { cmdWaitBreak(client, req); return; }

    QJsonObject result;
    if      (cmd == "emu_status")        result = cmdEmuStatus(req);
    else if (cmd == "pause")             result = cmdPause(req);
    else if (cmd == "resume")            result = cmdResume(req);
    else if (cmd == "reset")             result = cmdReset(req);
    else if (cmd == "frame_step")        result = cmdFrameStep(req);
    else if (cmd == "boot_rom")          result = cmdBootRom(req);
    else if (cmd == "save_state")        result = cmdSaveState(req);
    else if (cmd == "load_state")        result = cmdLoadState(req);
    else if (cmd == "press_buttons")     result = cmdPressButtons(req);
    else if (cmd == "release_buttons")   result = cmdReleaseButtons(req);
    else if (cmd == "touch_screen")      result = cmdTouchScreen(req);
    else if (cmd == "release_screen")    result = cmdReleaseScreen(req);
    else if (cmd == "add_breakpoint")    result = cmdAddBreakpoint(req);
    else if (cmd == "remove_breakpoint") result = cmdRemoveBreakpoint(req);
    else if (cmd == "clear_breakpoints") result = cmdClearBreakpoints(req);
    else if (cmd == "list_breakpoints")  result = cmdListBreakpoints(req);
    else if (cmd == "get_registers")     result = cmdGetRegisters(req);
    else if (cmd == "read_memory")       result = cmdReadMemory(req);
    else if (cmd == "write_memory")      result = cmdWriteMemory(req);
    else if (cmd == "read_u8")           result = cmdReadU8(req);
    else if (cmd == "read_u16")          result = cmdReadU16(req);
    else if (cmd == "read_u32")          result = cmdReadU32(req);
    else if (cmd == "write_u8")          result = cmdWriteU8(req);
    else if (cmd == "write_u16")         result = cmdWriteU16(req);
    else if (cmd == "write_u32")         result = cmdWriteU32(req);
    else if (cmd == "read_multi")        result = cmdReadMulti(req);
    else if (cmd == "write_multi")       result = cmdWriteMulti(req);
    else if (cmd == "continue")          result = cmdContinue(req);
    else if (cmd == "step")              result = cmdStep(req);
    else if (cmd == "start_trace")       result = cmdStartTrace(req);
    else if (cmd == "stop_trace")        result = cmdStopTrace(req);
    else result = {{"error", "unknown command: " + cmd}};

    QJsonObject resp;
    resp["id"] = id;
    bool isErr = result.contains("error");
    resp["ok"] = !isErr;
    if (isErr) resp["error"] = result.take("error");
    for (auto it = result.constBegin(); it != result.constEnd(); ++it)
        resp[it.key()] = it.value();
    sendTo(client, resp);
}

uint32_t ControlServer::parseAddr(const QJsonValue& v)
{
    if (v.isString()) return (uint32_t)v.toString("0").toLongLong(nullptr, 0);
    return (uint32_t)(qint64)v.toDouble();
}

uint32_t ControlServer::parseButtonBits(const QJsonArray& buttons)
{
    static const struct { const char* name; int bit; } kMap[] = {
        {"A",      0}, {"B",     1}, {"SELECT", 2}, {"START", 3},
        {"RIGHT",  4}, {"LEFT",  5}, {"UP",     6}, {"DOWN",  7},
        {"R",      8}, {"L",     9}, {"X",     10}, {"Y",    11},
    };
    uint32_t bits = 0;
    for (const QJsonValue& v : buttons)
    {
        QByteArray s = v.toString().toUpper().toLatin1();
        for (const auto& e : kMap)
            if (s == e.name) { bits |= (1u << e.bit); break; }
    }
    return bits;
}

QJsonObject ControlServer::cmdEmuStatus(const QJsonObject&)
{
    EmuThread* t = Inst->getEmuThread();
    QString s;
    if      (!Inst->emuIsActive()) s = "stopped";
    else if (t->emuIsRunning())    s = "running";
    else                           s = "paused";
    return {{"status", s}};
}

QJsonObject ControlServer::cmdPause(const QJsonObject&)
{
    Inst->getEmuThread()->emuPause();
    return {};
}

QJsonObject ControlServer::cmdResume(const QJsonObject&)
{
    Inst->getEmuThread()->emuUnpause();
    return {};
}

QJsonObject ControlServer::cmdReset(const QJsonObject&)
{
    Inst->getEmuThread()->emuReset();
    return {};
}

QJsonObject ControlServer::cmdFrameStep(const QJsonObject&)
{
    Inst->getEmuThread()->emuFrameStep();
    return {};
}

QJsonObject ControlServer::cmdBootRom(const QJsonObject& req)
{
    QString path = req["path"].toString();
    if (path.isEmpty()) return {{"error", "missing path"}};
    QString errorstr;
    // melonDS convention: msgResult 0 = fail, 1 = success
    int ret = Inst->getEmuThread()->bootROM({path}, errorstr);
    if (ret == 0) return {{"error", errorstr.isEmpty() ? "bootROM failed" : errorstr}};
    Inst->doOnAllWindows([](MainWindow* win) { win->updateCartInserted(false); });
    return {};
}

QJsonObject ControlServer::cmdSaveState(const QJsonObject& req)
{
    QString path = req["path"].toString();
    if (path.isEmpty()) return {{"error", "missing path"}};
    if (Inst->getEmuThread()->saveState(path) == 0) return {{"error", "save state failed"}};
    return {};
}

QJsonObject ControlServer::cmdLoadState(const QJsonObject& req)
{
    QString path = req["path"].toString();
    if (path.isEmpty()) return {{"error", "missing path"}};
    if (Inst->getEmuThread()->loadState(path) == 0) return {{"error", "load state failed"}};
    return {};
}

QJsonObject ControlServer::cmdPressButtons(const QJsonObject& req)
{
    uint32_t bits = parseButtonBits(req["buttons"].toArray());
    Inst->remoteButtonMask.fetch_and(~bits, std::memory_order_relaxed);
    return {};
}

QJsonObject ControlServer::cmdReleaseButtons(const QJsonObject& req)
{
    uint32_t bits = parseButtonBits(req["buttons"].toArray());
    Inst->remoteButtonMask.fetch_or(bits, std::memory_order_relaxed);
    return {};
}

QJsonObject ControlServer::cmdTouchScreen(const QJsonObject& req)
{
    int x = req["x"].toInt(-1);
    int y = req["y"].toInt(-1);
    if (x < 0 || x > 255 || y < 0 || y > 191)
        return {{"error", "x must be 0-255, y must be 0-191"}};
    Inst->remoteTouchX.store((uint16_t)x, std::memory_order_relaxed);
    Inst->remoteTouchY.store((uint16_t)y, std::memory_order_relaxed);
    Inst->remoteTouchActive.store(true, std::memory_order_relaxed);
    return {};
}

QJsonObject ControlServer::cmdReleaseScreen(const QJsonObject&)
{
    Inst->remoteTouchActive.store(false, std::memory_order_relaxed);
    return {};
}

QJsonObject ControlServer::cmdAddBreakpoint(const QJsonObject& req)
{
    NDS* nds = Inst->getNDS();
    if (!nds) return {{"error", "emulator not active"}};

    uint32_t addr = parseAddr(req["addr"]);
    QString typeStr = req["type"].toString("exec").toLower();
    NativeBpType type;
    if      (typeStr == "exec")  type = nativeBp_Execute;
    else if (typeStr == "read")  type = nativeBp_Read;
    else if (typeStr == "write") type = nativeBp_Write;
    else if (typeStr == "rw")    type = nativeBp_ReadWrite;
    else return {{"error", "type must be exec/read/write/rw"}};

    auto& bps = nds->NativeBreakpoints;
    bps.erase(std::remove_if(bps.begin(), bps.end(),
        [addr, type](const NativeBpEntry& e) { return e.Addr == addr && e.Type == type; }),
        bps.end());
    bps.push_back({addr, type});
    nds->NativeDebugEnabled = true;
    return {};
}

QJsonObject ControlServer::cmdRemoveBreakpoint(const QJsonObject& req)
{
    NDS* nds = Inst->getNDS();
    if (!nds) return {{"error", "emulator not active"}};

    uint32_t addr = parseAddr(req["addr"]);
    auto& bps = nds->NativeBreakpoints;
    bps.erase(std::remove_if(bps.begin(), bps.end(),
        [addr](const NativeBpEntry& e) { return e.Addr == addr; }),
        bps.end());
    nds->NativeDebugEnabled = !bps.empty();
    return {};
}

QJsonObject ControlServer::cmdClearBreakpoints(const QJsonObject&)
{
    NDS* nds = Inst->getNDS();
    if (!nds) return {{"error", "emulator not active"}};
    nds->NativeBreakpoints.clear();
    nds->NativeDebugEnabled = false;
    return {};
}

QJsonObject ControlServer::cmdListBreakpoints(const QJsonObject&)
{
    NDS* nds = Inst->getNDS();
    QJsonArray arr;
    if (nds)
    {
        for (const auto& bp : nds->NativeBreakpoints)
        {
            QJsonObject o;
            o["addr"] = QString("0x%1").arg(bp.Addr, 8, 16, QChar('0'));
            o["type"] = bp.Type == nativeBp_Execute  ? "exec"  :
                        bp.Type == nativeBp_Read      ? "read"  :
                        bp.Type == nativeBp_Write     ? "write" : "rw";
            arr.append(o);
        }
    }
    return {{"bps", arr}};
}

QJsonObject ControlServer::cmdGetRegisters(const QJsonObject& req)
{
    NDS* nds = Inst->getNDS();
    if (!nds) return {{"error", "emulator not active"}};
    if (Inst->getEmuThread()->emuIsRunning()) return {{"error", "emulator must be paused"}};

    int cpu = req["cpu"].toInt(0);
    ARM* arm = (cpu == 0) ? (ARM*)&nds->ARM9 : (ARM*)&nds->ARM7;

    QJsonObject regs;
    for (int i = 0; i < 16; i++)
        regs[QString("r%1").arg(i)] = QString("0x%1").arg(arm->R[i], 8, 16, QChar('0'));
    regs["cpsr"] = QString("0x%1").arg(arm->CPSR, 8, 16, QChar('0'));
    return {{"regs", regs}};
}

QJsonObject ControlServer::cmdReadMemory(const QJsonObject& req)
{
    NDS* nds = Inst->getNDS();
    if (!nds) return {{"error", "emulator not active"}};
    if (Inst->getEmuThread()->emuIsRunning()) return {{"error", "emulator must be paused"}};

    uint32_t addr = parseAddr(req["addr"]);
    int len  = req["len"].toInt(0);
    int cpu  = req["cpu"].toInt(0);
    if (len <= 0 || len > MaxMemoryTransferBytes) return {{"error", "len must be 1-MaxMemoryTransferBytes"}};

    QByteArray data(len, 0);
    for (int i = 0; i < len; i++)
        data[i] = (char)(cpu == 0 ? nds->ARM9Read8(addr + i) : nds->ARM7Read8(addr + i));

    return {{"addr", QString("0x%1").arg(addr, 8, 16, QChar('0'))},
            {"data", QString(data.toHex())}};
}

QJsonObject ControlServer::cmdWriteMemory(const QJsonObject& req)
{
    NDS* nds = Inst->getNDS();
    if (!nds) return {{"error", "emulator not active"}};
    if (Inst->getEmuThread()->emuIsRunning()) return {{"error", "emulator must be paused"}};

    uint32_t addr    = parseAddr(req["addr"]);
    QByteArray data  = QByteArray::fromHex(req["data"].toString().toLatin1());
    int cpu          = req["cpu"].toInt(0);
    if (data.isEmpty())          return {{"error", "empty data"}};
    if (data.size() > MaxMemoryTransferBytes)   return {{"error", "data too large (max MaxMemoryTransferBytes bytes)"}};

    for (int i = 0; i < data.size(); i++)
    {
        if (cpu == 0) nds->ARM9Write8(addr + i, (uint8_t)data[i]);
        else          nds->ARM7Write8(addr + i, (uint8_t)data[i]);
    }
    return {};
}

static QString fmtHex32(uint32_t v) { return QString("0x%1").arg(v, 8, 16, QChar('0')); }

QJsonObject ControlServer::cmdReadU8(const QJsonObject& req)
{
    NDS* nds = Inst->getNDS();
    if (!nds) return {{"error", "emulator not active"}};
    if (Inst->getEmuThread()->emuIsRunning()) return {{"error", "emulator must be paused"}};
    uint32_t addr = parseAddr(req["addr"]);
    int cpu = req["cpu"].toInt(0);
    uint8_t v = cpu == 0 ? nds->ARM9Read8(addr) : nds->ARM7Read8(addr);
    return {{"addr", fmtHex32(addr)}, {"value", (int)v}};
}

QJsonObject ControlServer::cmdReadU16(const QJsonObject& req)
{
    NDS* nds = Inst->getNDS();
    if (!nds) return {{"error", "emulator not active"}};
    if (Inst->getEmuThread()->emuIsRunning()) return {{"error", "emulator must be paused"}};
    uint32_t addr = parseAddr(req["addr"]);
    int cpu = req["cpu"].toInt(0);
    uint16_t v = cpu == 0 ? nds->ARM9Read16(addr) : nds->ARM7Read16(addr);
    return {{"addr", fmtHex32(addr)}, {"value", (int)v}};
}

QJsonObject ControlServer::cmdReadU32(const QJsonObject& req)
{
    NDS* nds = Inst->getNDS();
    if (!nds) return {{"error", "emulator not active"}};
    if (Inst->getEmuThread()->emuIsRunning()) return {{"error", "emulator must be paused"}};
    uint32_t addr = parseAddr(req["addr"]);
    int cpu = req["cpu"].toInt(0);
    uint32_t v = cpu == 0 ? nds->ARM9Read32(addr) : nds->ARM7Read32(addr);
    return {{"addr", fmtHex32(addr)}, {"value", fmtHex32(v)}};
}

QJsonObject ControlServer::cmdWriteU8(const QJsonObject& req)
{
    NDS* nds = Inst->getNDS();
    if (!nds) return {{"error", "emulator not active"}};
    if (Inst->getEmuThread()->emuIsRunning()) return {{"error", "emulator must be paused"}};
    uint32_t addr = parseAddr(req["addr"]);
    uint8_t  val  = (uint8_t)parseAddr(req["value"]);
    int cpu = req["cpu"].toInt(0);
    if (cpu == 0) nds->ARM9Write8(addr, val); else nds->ARM7Write8(addr, val);
    return {};
}

QJsonObject ControlServer::cmdWriteU16(const QJsonObject& req)
{
    NDS* nds = Inst->getNDS();
    if (!nds) return {{"error", "emulator not active"}};
    if (Inst->getEmuThread()->emuIsRunning()) return {{"error", "emulator must be paused"}};
    uint32_t addr = parseAddr(req["addr"]);
    uint16_t val  = (uint16_t)parseAddr(req["value"]);
    int cpu = req["cpu"].toInt(0);
    if (cpu == 0) nds->ARM9Write16(addr, val); else nds->ARM7Write16(addr, val);
    return {};
}

QJsonObject ControlServer::cmdWriteU32(const QJsonObject& req)
{
    NDS* nds = Inst->getNDS();
    if (!nds) return {{"error", "emulator not active"}};
    if (Inst->getEmuThread()->emuIsRunning()) return {{"error", "emulator must be paused"}};
    uint32_t addr = parseAddr(req["addr"]);
    uint32_t val  = parseAddr(req["value"]);
    int cpu = req["cpu"].toInt(0);
    if (cpu == 0) nds->ARM9Write32(addr, val); else nds->ARM7Write32(addr, val);
    return {};
}

QJsonObject ControlServer::cmdReadMulti(const QJsonObject& req)
{
    NDS* nds = Inst->getNDS();
    if (!nds) return {{"error", "emulator not active"}};
    if (Inst->getEmuThread()->emuIsRunning()) return {{"error", "emulator must be paused"}};

    QJsonArray regionsIn  = req["regions"].toArray();
    int defaultCpu = req["cpu"].toInt(0);
    QJsonArray regionsOut;

    for (const QJsonValue& rv : regionsIn)
    {
        QJsonObject ro  = rv.toObject();
        uint32_t addr   = parseAddr(ro["addr"]);
        int len         = ro["len"].toInt(0);
        int cpu         = ro.contains("cpu") ? ro["cpu"].toInt(0) : defaultCpu;

        if (len <= 0 || len > MaxMemoryTransferBytes)
            return {{"error", QString("region addr=%1: len must be 1-MaxMemoryTransferBytes").arg(fmtHex32(addr))}};

        QByteArray data(len, 0);
        for (int i = 0; i < len; i++)
            data[i] = (char)(cpu == 0 ? nds->ARM9Read8(addr + i) : nds->ARM7Read8(addr + i));

        QJsonObject out;
        out["addr"] = fmtHex32(addr);
        out["data"] = QString(data.toHex());
        regionsOut.append(out);
    }

    return {{"regions", regionsOut}};
}

QJsonObject ControlServer::cmdWriteMulti(const QJsonObject& req)
{
    NDS* nds = Inst->getNDS();
    if (!nds) return {{"error", "emulator not active"}};
    if (Inst->getEmuThread()->emuIsRunning()) return {{"error", "emulator must be paused"}};

    QJsonArray regionsIn = req["regions"].toArray();
    int defaultCpu = req["cpu"].toInt(0);

    for (const QJsonValue& rv : regionsIn)
    {
        QJsonObject ro  = rv.toObject();
        uint32_t addr   = parseAddr(ro["addr"]);
        QByteArray data = QByteArray::fromHex(ro["data"].toString().toLatin1());
        int cpu         = ro.contains("cpu") ? ro["cpu"].toInt(0) : defaultCpu;

        if (data.isEmpty())        return {{"error", QString("region addr=%1: empty data").arg(fmtHex32(addr))}};
        if (data.size() > MaxMemoryTransferBytes) return {{"error", QString("region addr=%1: data too large").arg(fmtHex32(addr))}};

        for (int i = 0; i < data.size(); i++)
        {
            if (cpu == 0) nds->ARM9Write8(addr + i, (uint8_t)data[i]);
            else          nds->ARM7Write8(addr + i, (uint8_t)data[i]);
        }
    }

    return {};
}

QJsonObject ControlServer::cmdContinue(const QJsonObject&)
{
    Inst->getEmuThread()->emuUnpause();
    return {};
}

QJsonObject ControlServer::cmdStep(const QJsonObject&)
{
    Inst->getEmuThread()->emuNativeDbgStep();
    return {};
}

void ControlServer::cmdWaitBreak(QTcpSocket* client, const QJsonObject& req)
{
    int id         = req.contains("id") ? req["id"].toInt(-1) : -1;
    int timeout_ms = req["timeout_ms"].toInt(10000);

    // If already at a breakpoint, respond immediately
    NDS* nds = Inst->getNDS();
    if (nds && nds->NativeBreakHit)
    {
        QJsonObject resp;
        resp["id"]  = id;
        resp["ok"]  = true;
        resp["addr"] = QString("0x%1").arg(nds->NativeBreakPC, 8, 16, QChar('0'));
        resp["cpu"] = nds->NativeBreakCPU;
        sendTo(client, resp);
        return;
    }

    BreakWaiter = PendingBreak{client, id};

    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, client, id, timer]() {
        if (BreakWaiter && BreakWaiter->client == client && BreakWaiter->id == id)
        {
            BreakWaiter.reset();
            QJsonObject resp;
            resp["id"]    = id;
            resp["ok"]    = false;
            resp["error"] = "timeout";
            sendTo(client, resp);
        }
        timer->deleteLater();
    });
    timer->start(timeout_ms);
}

void ControlServer::onBreakHit(quint32 addr, int cpu)
{
    QJsonObject evt;
    evt["event"] = "break_hit";
    evt["addr"]  = QString("0x%1").arg(addr, 8, 16, QChar('0'));
    evt["cpu"]   = cpu;

    if (BreakWaiter)
    {
        QJsonObject resp;
        resp["id"]  = BreakWaiter->id;
        resp["ok"]  = true;
        resp["addr"] = evt["addr"];
        resp["cpu"] = cpu;
        sendTo(BreakWaiter->client, resp);
        BreakWaiter.reset();
    }

    broadcastEvent(evt);
}

void ControlServer::onEmuStart()
{
    broadcastEvent({{"event", "emu_start"}});
}

void ControlServer::onEmuStop()
{
    // Clear trace to avoid dangling NDS* in the lambda after emu stops
    if (TraceFile)
    {
        NDS* nds = Inst->getNDS();
        if (nds) { nds->TraceEnabled = false; nds->TraceLogger = nullptr; }
        fclose(TraceFile);
        TraceFile = nullptr;
    }
    broadcastEvent({{"event", "emu_stop"}});
}

void ControlServer::onEmuPause(bool paused)
{
    broadcastEvent({{"event", paused ? "emu_pause" : "emu_resume"}});
}

QJsonObject ControlServer::cmdStartTrace(const QJsonObject& req)
{
    NDS* nds = Inst->getNDS();
    if (!nds) return {{"error", "emulator not active"}};
    if (TraceFile) return {{"error", "trace already running, call stop_trace first"}};

    QString path = req["path"].toString();
    if (path.isEmpty()) return {{"error", "missing path"}};

    int cpuSel = req["cpu"].toInt(2); // 0=ARM9 1=ARM7 2=both
    if (cpuSel < 0 || cpuSel > 2) return {{"error", "cpu must be 0 (ARM9), 1 (ARM7), or 2 (both)"}};

    QJsonArray rangesArr = req["ranges"].toArray();
    std::vector<CtrlTraceRange> ranges;
    for (const QJsonValue& rv : rangesArr)
    {
        QJsonObject ro = rv.toObject();
        CtrlTraceRange r;
        r.addrMin      = parseAddr(ro["min"]);
        r.addrMax      = parseAddr(ro["max"]);
        r.captureExec  = ro["exec"].toBool(true);
        r.captureRead  = ro["read"].toBool(false);
        r.captureWrite = ro["write"].toBool(false);
        ranges.push_back(r);
    }
    if (ranges.empty()) return {{"error", "at least one range required in ranges array"}};

    QDir().mkpath(QFileInfo(path).absolutePath());
    TraceFile = fopen(path.toUtf8().constData(), "w");
    if (!TraceFile) return {{"error", "failed to open trace file: " + path}};

    bool logARM9 = (cpuSel == 0 || cpuSel == 2);
    bool logARM7 = (cpuSel == 1 || cpuSel == 2);
    FILE* f      = TraceFile;
    fprintf(f, "CPU\tRWX\tISA\tAddress\tInstruction\tRegisters\tDomain\n");

    nds->TraceLogger = [=, ranges = std::move(ranges)](int cpu, u32 addr, NativeBpType type, const u32* regs, u32 cpsr)
    {
        if (cpu == 0 && !logARM9) return;
        if (cpu == 1 && !logARM7) return;

        bool matched = false;
        for (const CtrlTraceRange& r : ranges)
        {
            if (addr < r.addrMin || addr > r.addrMax) continue;
            if (type == nativeBp_Execute && !r.captureExec)  continue;
            if (type == nativeBp_Read    && !r.captureRead)  continue;
            if (type == nativeBp_Write   && !r.captureWrite) continue;
            matched = true;
            break;
        }
        if (!matched) return;

        const char* cpuStr  = cpu ? "ARM7" : "ARM9";
        const char* typeStr = (type == nativeBp_Execute) ? "X" :
                              (type == nativeBp_Read)    ? "R" : "W";
        bool isThumb        = cpsr & 0x20;

        std::string instr;
        if (type == nativeBp_Execute)
        {
            if (isThumb)
            {
                uint16_t h0 = cpu ? nds->ARM7Read16(addr)   : nds->ARM9Read16(addr);
                uint16_t h1 = cpu ? nds->ARM7Read16(addr+2) : nds->ARM9Read16(addr+2);
                bool c32 = false;
                instr = ARMDisassembler::DisassembleThumb(addr, h0, h1, c32).Instruction;
            }
            else
            {
                uint32_t w = cpu ? nds->ARM7Read32(addr) : nds->ARM9Read32(addr);
                instr = ARMDisassembler::DisassembleARM(addr, w).Instruction;
            }
        }

        char regbuf[320];
        int off = 0;
        for (int i = 0; i <= 12; i++)
            off += snprintf(regbuf + off, (int)sizeof(regbuf) - off, "r%d=%08X ", i, regs[i]);
        snprintf(regbuf + off, (int)sizeof(regbuf) - off,
            "sp=%08X lr=%08X pc=%08X cpsr=%08X", regs[13], regs[14], regs[15], cpsr);

        fprintf(f, "%s\t%s\t%s\t%08X\t%-30s\t%s\t%s\n",
            cpuStr, typeStr, isThumb ? "THUMB" : "ARM  ", addr,
            instr.c_str(), regbuf,
            [cpu, addr]() -> const char* {
                if (cpu == 0) switch (addr & 0xFF000000) {
                    case 0x00000000: case 0x01000000: return "ITCM";
                    case 0x02000000: return "MainRAM";
                    case 0x03000000: return "SharedWRAM";
                    case 0x04000000: return "IO";
                    case 0x05000000: return "Palette";
                    case 0x06000000: return "VRAM";
                    case 0x07000000: return "OAM";
                    case 0xFF000000: return "BIOS9";
                    default:         return "---";
                }
                switch (addr & 0xFF000000) {
                    case 0x02000000: return "MainRAM";
                    case 0x03000000: return "WRAM";
                    case 0x04000000: return "IO";
                    case 0x06000000: return "VRAM";
                    case 0xFF000000: return "BIOS7";
                    default:         return "---";
                }
            }());
    };

    nds->TraceEnabled = true;
    return {};
}

QJsonObject ControlServer::cmdStopTrace(const QJsonObject&)
{
    NDS* nds = Inst->getNDS();
    if (nds) { nds->TraceEnabled = false; nds->TraceLogger = nullptr; }
    if (TraceFile) { fclose(TraceFile); TraceFile = nullptr; }
    return {};
}
