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

#include "TraceLoggerDialog.h"
#include "ARMDisassembler.h"
#include "EmuInstance.h"
#include "EmuThread.h"
#include "NDS.h"
#include "ARM.h"
#include "MemConstants.h"
#include "main.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QFileDialog>
#include <QFileInfo>
#include <QStatusBar>
#include <QDateTime>
#include <QDir>
#include <QFont>
#include <string_view>

using namespace melonDS;

TraceLoggerDialog* TraceLoggerDialog::currentDlg = nullptr;

static std::string_view TraceRegionName(int cpu, uint32_t addr)
{
    using namespace melonDS;
    if (cpu == 0)
    {
        switch (addr & 0xFF000000)
        {
        case 0x00000000:
        case 0x01000000: return "ITCM";
        case 0x02000000: return "MainRAM";
        case 0x03000000: return "SharedWRAM";
        case 0x04000000: return "IO";
        case 0x05000000: return "Palette";
        case 0x06000000: return "VRAM";
        case 0x07000000: return "OAM";
        case 0xFF000000: return "BIOS9";
        default:         return "---";
        }
    }
    else
    {
        switch (addr & 0xFF000000)
        {
        case 0x00000000: return addr < ARM7BIOSSize ? "BIOS7" : "---";
        case 0x02000000: return "MainRAM";
        case 0x03000000: return addr < ARM7WRAMBase ? "SharedWRAM" : "WRAM7";
        case 0x04000000: return "IO";
        case 0x06000000: return "VRAM";
        case 0xFF000000: return "BIOS7";
        default:         return "---";
        }
    }
}

TraceLoggerDialog::TraceLoggerDialog(EmuInstance* inst, QWidget* parent)
    : QMainWindow(parent), Inst(inst), Thread(inst->getEmuThread())
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("melonDS Trace Logger");
    BuildUI();
    resize(500, 360);

    connect(Thread, &EmuThread::windowEmuStop, this, [this]{
        StopTrace();
        statusBar()->showMessage("Trace stopped (emulation ended).");
    }, Qt::QueuedConnection);
}

TraceLoggerDialog::~TraceLoggerDialog()
{
    NDS* nds = Inst->getNDS();
    if (nds)
    {
        nds->TraceEnabled = false;
        nds->TraceLogger  = nullptr;
    }
    if (TraceFileHandle) { fclose(TraceFileHandle); TraceFileHandle = nullptr; }
    closeDlg();
}

void TraceLoggerDialog::BuildUI()
{
    QWidget* w = new QWidget;
    QVBoxLayout* vl = new QVBoxLayout(w);
    vl->setContentsMargins(8, 8, 8, 8);
    vl->setSpacing(6);

    // CPU selector
    QHBoxLayout* hl1 = new QHBoxLayout;
    hl1->addWidget(new QLabel("CPU:"));
    TraceCpuCombo = new QComboBox;
    TraceCpuCombo->addItem("ARM9");
    TraceCpuCombo->addItem("ARM7");
    TraceCpuCombo->addItem("Both");
    hl1->addWidget(TraceCpuCombo);
    hl1->addStretch();
    vl->addLayout(hl1);

    // Range table
    RangeList = new QTableWidget(0, 3);
    RangeList->setHorizontalHeaderLabels({"Start", "End", "Type"});
    RangeList->setFont(QFont("Courier New", 10));
    RangeList->setSelectionBehavior(QAbstractItemView::SelectRows);
    RangeList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    RangeList->verticalHeader()->setVisible(false);
    RangeList->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    RangeList->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    RangeList->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    RangeList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(RangeList, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        int row = RangeList->rowAt(pos.y());
        if (row < 0 || row >= (int)Ranges.size()) return;

        TraceRange& r = Ranges[row];
        QMenu menu(this);

        auto makeTypeStr = [](bool e, bool rd, bool wr) -> QString {
            QString s;
            if (e)  s += "X";
            if (rd) s += "R";
            if (wr) s += "W";
            return s.isEmpty() ? "---" : s;
        };

        auto addCheck = [&](const QString& label, bool checked) -> QAction* {
            QAction* act = menu.addAction(label);
            act->setCheckable(true);
            act->setChecked(checked);
            return act;
        };

        QAction* execAct  = addCheck("Execute (X)", r.captureExec);
        QAction* readAct  = addCheck("Read (R)",    r.captureRead);
        QAction* writeAct = addCheck("Write (W)",   r.captureWrite);

        QAction* chosen = menu.exec(RangeList->mapToGlobal(pos));
        if (!chosen) return;

        if (chosen == execAct)  r.captureExec  = !r.captureExec;
        if (chosen == readAct)  r.captureRead  = !r.captureRead;
        if (chosen == writeAct) r.captureWrite = !r.captureWrite;

        RangeList->item(row, 2)->setText(makeTypeStr(r.captureExec, r.captureRead, r.captureWrite));
    });
    vl->addWidget(RangeList);

    // Add-range controls
    QHBoxLayout* hl2 = new QHBoxLayout;
    hl2->addWidget(new QLabel("Start:"));
    RangeMinEdit = new QLineEdit("00000000");
    RangeMinEdit->setMaximumWidth(80);
    hl2->addWidget(RangeMinEdit);
    hl2->addWidget(new QLabel("End:"));
    RangeMaxEdit = new QLineEdit("FFFFFFFF");
    RangeMaxEdit->setMaximumWidth(80);
    hl2->addWidget(RangeMaxEdit);
    RangeExecCheck  = new QCheckBox("X");
    RangeReadCheck  = new QCheckBox("R");
    RangeWriteCheck = new QCheckBox("W");
    RangeExecCheck->setChecked(true);
    hl2->addWidget(RangeExecCheck);
    hl2->addWidget(RangeReadCheck);
    hl2->addWidget(RangeWriteCheck);
    RangeAddBtn = new QPushButton("Add");
    connect(RangeAddBtn,  &QPushButton::clicked,     this, &TraceLoggerDialog::OnAddRange);
    connect(RangeMinEdit, &QLineEdit::returnPressed, this, &TraceLoggerDialog::OnAddRange);
    connect(RangeMaxEdit, &QLineEdit::returnPressed, this, &TraceLoggerDialog::OnAddRange);
    hl2->addWidget(RangeAddBtn);
    vl->addLayout(hl2);

    // Remove / Clear buttons
    QHBoxLayout* hl3 = new QHBoxLayout;
    RangeRemBtn = new QPushButton("Remove");
    RangeClrBtn = new QPushButton("Clear all");
    connect(RangeRemBtn, &QPushButton::clicked, this, &TraceLoggerDialog::OnRemoveRange);
    connect(RangeClrBtn, &QPushButton::clicked, this, &TraceLoggerDialog::OnClearRanges);
    hl3->addWidget(RangeRemBtn);
    hl3->addWidget(RangeClrBtn);
    hl3->addStretch();
    vl->addLayout(hl3);

    // File output
    QHBoxLayout* hl4 = new QHBoxLayout;
    hl4->addWidget(new QLabel("File:"));
    TracePathEdit = new QLineEdit;
    TracePathEdit->setText(DefaultTracePath());
    hl4->addWidget(TracePathEdit, 1);
    TraceBrowseBtn = new QPushButton("…");
    TraceBrowseBtn->setMaximumWidth(28);
    connect(TraceBrowseBtn, &QPushButton::clicked, this, &TraceLoggerDialog::OnTraceBrowse);
    hl4->addWidget(TraceBrowseBtn);
    vl->addLayout(hl4);

    TraceStartBtn = new QPushButton("Start Logging");
    connect(TraceStartBtn, &QPushButton::clicked, this, &TraceLoggerDialog::OnTraceToggle);
    vl->addWidget(TraceStartBtn);
    vl->addStretch();

    setCentralWidget(w);

    // Start with a default full-range execute entry
    Ranges.push_back({0x00000000, 0xFFFFFFFF, true, false, false});
    RebuildRangeList();
}

QString TraceLoggerDialog::DefaultTracePath() const
{
    std::string romName = Inst->getBaseROMName();
    QString stem;
    if (romName.empty())
    {
        stem = "TRACE";
    }
    else
    {
        QString rn = QString::fromStdString(romName);
        int dot = rn.lastIndexOf('.');
        if (dot > 0) rn = rn.left(dot);
        stem = "TRACE_" + rn;
    }
    QString ts  = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString dir = emuDirectory + QDir::separator() + "tracelogs";
    return dir + QDir::separator() + stem + "_" + ts + ".log";
}

void TraceLoggerDialog::RebuildRangeList()
{
    RangeList->setRowCount(0);
    RangeList->setRowCount((int)Ranges.size());
    for (int i = 0; i < (int)Ranges.size(); i++)
    {
        const TraceRange& r = Ranges[i];
        QString typeStr;
        if (r.captureExec)  typeStr += "X";
        if (r.captureRead)  typeStr += "R";
        if (r.captureWrite) typeStr += "W";
        if (typeStr.isEmpty()) typeStr = "---";

        RangeList->setItem(i, 0, new QTableWidgetItem(
            QString("0x%1").arg(r.addrMin, 8, 16, QLatin1Char('0')).toUpper()));
        RangeList->setItem(i, 1, new QTableWidgetItem(
            QString("0x%1").arg(r.addrMax, 8, 16, QLatin1Char('0')).toUpper()));
        RangeList->setItem(i, 2, new QTableWidgetItem(typeStr));
    }
}

void TraceLoggerDialog::OnAddRange()
{
    bool ok;
    uint32_t addrMin = RangeMinEdit->text().toUInt(&ok, 16);
    if (!ok) { statusBar()->showMessage("Invalid start address"); return; }
    uint32_t addrMax = RangeMaxEdit->text().toUInt(&ok, 16);
    if (!ok) { statusBar()->showMessage("Invalid end address"); return; }
    if (addrMin > addrMax)
    {
        statusBar()->showMessage("Start address must be <= end address");
        return;
    }
    bool exec  = RangeExecCheck->isChecked();
    bool read  = RangeReadCheck->isChecked();
    bool write = RangeWriteCheck->isChecked();
    if (!exec && !read && !write)
    {
        statusBar()->showMessage("Select at least one type (X/R/W)");
        return;
    }
    Ranges.push_back({addrMin, addrMax, exec, read, write});
    RebuildRangeList();
    statusBar()->showMessage(
        QString("Range added: 0x%1–0x%2")
            .arg(addrMin, 8, 16, QLatin1Char('0'))
            .arg(addrMax, 8, 16, QLatin1Char('0')));
}

void TraceLoggerDialog::OnRemoveRange()
{
    int row = RangeList->currentRow();
    if (row < 0 || row >= (int)Ranges.size()) return;
    Ranges.erase(Ranges.begin() + row);
    RebuildRangeList();
}

void TraceLoggerDialog::OnClearRanges()
{
    Ranges.clear();
    RebuildRangeList();
}

void TraceLoggerDialog::SetControlsEnabled(bool enabled)
{
    TraceCpuCombo->setEnabled(enabled);
    RangeMinEdit->setEnabled(enabled);
    RangeMaxEdit->setEnabled(enabled);
    RangeExecCheck->setEnabled(enabled);
    RangeReadCheck->setEnabled(enabled);
    RangeWriteCheck->setEnabled(enabled);
    RangeAddBtn->setEnabled(enabled);
    RangeRemBtn->setEnabled(enabled);
    RangeClrBtn->setEnabled(enabled);
    TracePathEdit->setEnabled(enabled);
    TraceBrowseBtn->setEnabled(enabled);
}

void TraceLoggerDialog::OnTraceBrowse()
{
    QString cur = TracePathEdit->text();
    if (cur.isEmpty()) cur = DefaultTracePath();
    QString path = QFileDialog::getSaveFileName(this, "Save Trace Log", cur,
                                                "Log files (*.log);;All files (*)");
    if (!path.isEmpty())
        TracePathEdit->setText(path);
}

void TraceLoggerDialog::StopTrace()
{
    NDS* nds = Inst->getNDS();
    if (nds)
    {
        nds->TraceEnabled = false;
        nds->TraceLogger  = nullptr;
    }
    if (TraceFileHandle) { fclose(TraceFileHandle); TraceFileHandle = nullptr; }
    TraceStartBtn->setText("Start Logging");
    SetControlsEnabled(true);
}

void TraceLoggerDialog::OnTraceToggle()
{
    NDS* nds = Inst->getNDS();
    if (!nds) return;

    if (TraceFileHandle)
    {
        StopTrace();
        statusBar()->showMessage("Trace stopped.");
        return;
    }

    if (Ranges.empty())
    {
        statusBar()->showMessage("Add at least one address range before starting.");
        return;
    }

    QString path = TracePathEdit->text();
    QDir().mkpath(QFileInfo(path).absolutePath());

    TraceFileHandle = fopen(path.toUtf8().constData(), "w");
    if (!TraceFileHandle)
    {
        statusBar()->showMessage("Failed to open trace file: " + path);
        return;
    }

    int cpuSel   = TraceCpuCombo->currentIndex();
    bool logARM9 = (cpuSel == 0 || cpuSel == 2);
    bool logARM7 = (cpuSel == 1 || cpuSel == 2);

    // Snapshot ranges for the lambda (captured by value)
    std::vector<TraceRange> ranges = Ranges;

    FILE* f = TraceFileHandle;
    fprintf(f, "CPU\tRWX\tISA\tAddress\tInstruction\tRegisters\tDomain\n");

    nds->TraceLogger = [=](int cpu, u32 addr, NativeBpType type, const u32* regs, u32 cpsr)
    {
        if (cpu == 0 && !logARM9) return;
        if (cpu == 1 && !logARM7) return;

        bool matched = false;
        for (const TraceRange& r : ranges)
        {
            if (addr < r.addrMin || addr > r.addrMax) continue;
            if (type == nativeBp_Execute && !r.captureExec)  continue;
            if (type == nativeBp_Read    && !r.captureRead)  continue;
            if (type == nativeBp_Write   && !r.captureWrite) continue;
            matched = true;
            break;
        }
        if (!matched) return;

        std::string_view cpuStr  = cpu ? "ARM7" : "ARM9";
        std::string_view typeStr = (type == nativeBp_Execute) ? "X"
                                 : (type == nativeBp_Read)    ? "R" : "W";
        bool isThumb = cpsr & 0x20;
        std::string_view isaStr = isThumb ? "THUMB" : "ARM";

        std::string instr;
        if (type == nativeBp_Execute)
        {
            if (isThumb)
            {
                uint16_t h0 = cpu ? nds->ARM7Read16(addr) : nds->ARM9Read16(addr);
                if ((h0 & 0xF800) == 0xF800 || (h0 & 0xF800) == 0xE800)
                    return;
                uint16_t h1 = cpu ? nds->ARM7Read16(addr+2) : nds->ARM9Read16(addr+2);
                bool consumed32 = false;
                instr = ARMDisassembler::DisassembleThumb(addr, h0, h1, consumed32).Instruction;
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
            cpuStr.data(), typeStr.data(), isaStr.data(), addr, instr.c_str(), regbuf,
            TraceRegionName(cpu, addr).data());
    };

    nds->TraceEnabled = true;

    TraceStartBtn->setText("Stop Logging");
    SetControlsEnabled(false);
    statusBar()->showMessage("Trace logging to: " + path);
}
