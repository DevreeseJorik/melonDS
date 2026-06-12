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

#include "DebuggerDialog.h"
#include "ARMDisassembler.h"
#include "EmuInstance.h"
#include "EmuThread.h"
#include "NDS.h"
#include "ARM.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFont>
#include <QMenu>
#include <QStatusBar>
#include <algorithm>
#include <string_view>

using namespace melonDS;

DebuggerDialog* DebuggerDialog::currentDlg = nullptr;

DebuggerDialog::DebuggerDialog(EmuInstance* inst, QWidget* parent)
    : QMainWindow(parent), Inst(inst), Thread(inst->getEmuThread())
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("melonDS Debugger");
    resize(900, 600);
    BuildUI();

    connect(Thread, &EmuThread::debugBreakHit,
            this, &DebuggerDialog::OnBreakHit,
            Qt::QueuedConnection);
    connect(Thread, &EmuThread::windowEmuPause,
            this, &DebuggerDialog::OnEmuPause,
            Qt::QueuedConnection);
    connect(Thread, &EmuThread::windowEmuStop,
            this, &DebuggerDialog::OnEmuStop,
            Qt::QueuedConnection);

    RefreshTimer = new QTimer(this);
    RefreshTimer->setInterval(100);
    connect(RefreshTimer, &QTimer::timeout, this, &DebuggerDialog::OnRefreshTimer);
    RefreshTimer->start();
}

DebuggerDialog::~DebuggerDialog()
{
    NDS* nds = Inst->getNDS();
    if (nds) nds->NativeDebugEnabled = false;
    closeDlg();
}

void DebuggerDialog::BuildUI()
{
    setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);

    {
        QWidget* w = new QWidget;
        QVBoxLayout* vl = new QVBoxLayout(w);
        vl->setContentsMargins(4, 4, 4, 4);

        QHBoxLayout* hl = new QHBoxLayout;
        BpAddrEdit = new QLineEdit;
        BpAddrEdit->setPlaceholderText("Address (hex)");
        BpCpuCombo = new QComboBox;
        BpCpuCombo->addItem("ARM9");
        BpCpuCombo->addItem("ARM7");
        BpTypeCombo = new QComboBox;
        BpTypeCombo->addItem("Execute");
        BpTypeCombo->addItem("Read");
        BpTypeCombo->addItem("Write");
        BpTypeCombo->addItem("Read+Write");
        BpAddBtn = new QPushButton("Add");
        connect(BpAddBtn,   &QPushButton::clicked,      this, &DebuggerDialog::OnAddBreakpoint);
        connect(BpAddrEdit, &QLineEdit::returnPressed,  this, &DebuggerDialog::OnAddBreakpoint);
        hl->addWidget(BpAddrEdit, 2);
        hl->addWidget(BpCpuCombo, 1);
        hl->addWidget(BpTypeCombo, 1);
        hl->addWidget(BpAddBtn);
        vl->addLayout(hl);

        BpList = new QTableWidget(0, 2);
        BpList->setHorizontalHeaderLabels({"Type", "Address"});
        BpList->setFont(QFont("Courier New", 10));
        BpList->setSelectionBehavior(QAbstractItemView::SelectRows);
        BpList->setEditTriggers(QAbstractItemView::NoEditTriggers);
        BpList->verticalHeader()->setVisible(false);
        BpList->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        BpList->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        BpList->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(BpList, &QTableWidget::customContextMenuRequested,
                this, &DebuggerDialog::OnBreakpointContextMenu);
        vl->addWidget(BpList);

        QHBoxLayout* hl2 = new QHBoxLayout;
        BpRemBtn = new QPushButton("Remove");
        BpClrBtn = new QPushButton("Clear all");
        connect(BpRemBtn, &QPushButton::clicked, this, &DebuggerDialog::OnRemoveBreakpoint);
        connect(BpClrBtn, &QPushButton::clicked, this, &DebuggerDialog::OnClearBreakpoints);
        hl2->addWidget(BpRemBtn);
        hl2->addWidget(BpClrBtn);
        vl->addLayout(hl2);

        BpDock = new QDockWidget("Breakpoints", this);
        BpDock->setFeatures(QDockWidget::DockWidgetMovable);
        BpDock->setWidget(w);
        addDockWidget(Qt::LeftDockWidgetArea, BpDock);
    }

    {
        QWidget* w = new QWidget;
        QVBoxLayout* vl = new QVBoxLayout(w);
        vl->setContentsMargins(4, 4, 4, 4);

        QHBoxLayout* hl = new QHBoxLayout;
        PcLabel      = new QLabel("PC: --");
        ContinueBtn  = new QPushButton("Continue  [F5]");
        ContinueBtn->setShortcut(QKeySequence("F5"));
        connect(ContinueBtn, &QPushButton::clicked, this, &DebuggerDialog::OnContinue);
        StepBtn = new QPushButton("Step  [F10]");
        StepBtn->setShortcut(QKeySequence("F10"));
        connect(StepBtn, &QPushButton::clicked, this, &DebuggerDialog::OnStep);
        hl->addWidget(PcLabel, 2);
        hl->addWidget(ContinueBtn);
        hl->addWidget(StepBtn);
        vl->addLayout(hl);

        DisasmTable = new QTableWidget(0, 3);
        DisasmTable->setHorizontalHeaderLabels({"Address", "Bytes", "Instruction"});
        DisasmTable->setFont(QFont("Courier New", 10));
        DisasmTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        DisasmTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        DisasmTable->verticalHeader()->setVisible(false);
        DisasmTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        DisasmTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        DisasmTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        DisasmTable->setAlternatingRowColors(true);
        vl->addWidget(DisasmTable);

        DisasmDock = new QDockWidget("Disassembly", this);
        DisasmDock->setFeatures(QDockWidget::DockWidgetMovable);
        DisasmDock->setWidget(w);
        addDockWidget(Qt::RightDockWidgetArea, DisasmDock);
    }

    {
        RegTable = new QTableWidget(18, 2);
        RegTable->setHorizontalHeaderLabels({"Register", "Value"});
        RegTable->setFont(QFont("Courier New", 10));
        RegTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        RegTable->verticalHeader()->setVisible(false);
        RegTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        RegTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        RegTable->setAlternatingRowColors(true);
        constexpr std::string_view rnames[] = {
            "R0","R1","R2","R3","R4","R5","R6","R7",
            "R8","R9","R10","R11","R12","SP","LR","PC","CPSR","SPSR"
        };
        for (int i = 0; i < 18; i++)
        {
            RegTable->setItem(i, 0, new QTableWidgetItem(QLatin1String(rnames[i].data(), rnames[i].size())));
            RegTable->setItem(i, 1, new QTableWidgetItem("--------"));
        }

        RegDock = new QDockWidget("Registers", this);
        RegDock->setFeatures(QDockWidget::DockWidgetMovable);
        RegDock->setWidget(RegTable);
        addDockWidget(Qt::RightDockWidgetArea, RegDock);
        splitDockWidget(DisasmDock, RegDock, Qt::Horizontal);
    }

    statusBar()->showMessage("Waiting for breakpoint…");
    SetPaused(false);
}

void DebuggerDialog::OnAddBreakpoint()
{
    bool ok;
    uint32_t addr = BpAddrEdit->text().toUInt(&ok, 16);
    if (!ok)
    {
        statusBar()->showMessage("Invalid address");
        return;
    }

    NDS* nds = Inst->getNDS();
    if (!nds) return;

    NativeBpType type = (NativeBpType)BpTypeCombo->currentIndex();
    for (auto& bp : nds->NativeBreakpoints)
        if (bp.Addr == addr && bp.Type == type) return;

    nds->NativeBreakpoints.push_back({addr, type});
    nds->NativeDebugEnabled = true;

    RebuildBreakpointList();
    BpAddrEdit->clear();
    statusBar()->showMessage(QString("Breakpoint added: 0x%1").arg(addr, 8, 16, QLatin1Char('0')));
}

void DebuggerDialog::OnRemoveBreakpoint()
{
    NDS* nds = Inst->getNDS();
    if (!nds) return;

    int row = BpList->currentRow();
    if (row < 0 || row >= (int)nds->NativeBreakpoints.size()) return;

    nds->NativeBreakpoints.erase(nds->NativeBreakpoints.begin() + row);
    if (nds->NativeBreakpoints.empty())
        nds->NativeDebugEnabled = false;
    RebuildBreakpointList();
}

void DebuggerDialog::OnClearBreakpoints()
{
    NDS* nds = Inst->getNDS();
    if (!nds) return;
    nds->NativeBreakpoints.clear();
    nds->NativeDebugEnabled = false;
    RebuildBreakpointList();
}

void DebuggerDialog::RebuildBreakpointList()
{
    constexpr std::string_view kTypeTag[] = { "[X]", "[R]", "[W]", "[RW]" };
    NDS* nds = Inst->getNDS();
    BpList->setRowCount(0);
    if (!nds) return;
    nds->NativeDebugEnabled = !nds->NativeBreakpoints.empty();
    BpList->setRowCount((int)nds->NativeBreakpoints.size());
    for (int i = 0; i < (int)nds->NativeBreakpoints.size(); i++)
    {
        auto& bp = nds->NativeBreakpoints[i];
        int t = (int)bp.Type;
        if (t < 0 || t > 3) t = 0;
        BpList->setItem(i, 0, new QTableWidgetItem(QLatin1String(kTypeTag[t].data(), kTypeTag[t].size())));
        BpList->setItem(i, 1, new QTableWidgetItem(
            QString("0x%1").arg(bp.Addr, 8, 16, QLatin1Char('0')).toUpper()));
    }
}

void DebuggerDialog::OnContinue()
{
    Thread->emuUnpause();
}

void DebuggerDialog::OnStep()
{
    Thread->emuNativeDbgStep();
}

void DebuggerDialog::UpdateDisassembly(uint32_t pc, bool isThumb, int cpu)
{
    NDS* nds = Inst->getNDS();
    if (!nds) return;

    PcLabel->setText(QString("PC: 0x%1  [%2]  %3")
        .arg(pc, 8, 16, QLatin1Char('0'))
        .arg(isThumb ? "Thumb" : "ARM")
        .arg(cpu == 0 ? "ARM9" : "ARM7"));

    const int kCount = 100;

    auto readWord = [&](uint32_t a) -> uint32_t {
        return cpu == 0 ? nds->ARM9Read32(a) : nds->ARM7Read32(a);
    };
    auto readHalf = [&](uint32_t a) -> uint16_t {
        return cpu == 0 ? nds->ARM9Read16(a) : nds->ARM7Read16(a);
    };

    auto results = ARMDisassembler::DisassembleRange(pc, kCount, isThumb, readWord, readHalf);

    DisasmTable->setRowCount((int)results.size());
    DisasmTable->verticalHeader()->setDefaultSectionSize(18);

    int highlightRow = -1;
    for (int i = 0; i < (int)results.size(); i++)
    {
        auto& dr = results[i];

        auto* addrItem = new QTableWidgetItem(
            QString("0x%1").arg(dr.Address, 8, 16, QLatin1Char('0')));

        QString bytesStr;
        if (dr.IsThumb && dr.Size == 2)
            bytesStr = QString("%1").arg(dr.Encoding & 0xFFFF, 4, 16, QLatin1Char('0')).toUpper();
        else
            bytesStr = QString("%1").arg(dr.Encoding, 8, 16, QLatin1Char('0')).toUpper();

        auto* bytesItem = new QTableWidgetItem(bytesStr);
        auto* instrItem = new QTableWidgetItem(QString::fromStdString(dr.Instruction));

        bool hasBkpt = std::any_of(nds->NativeBreakpoints.begin(), nds->NativeBreakpoints.end(),
            [&](const NativeBpEntry& bp){ return bp.Addr == dr.Address && bp.Type == nativeBp_Execute; });

        if (hasBkpt)
            addrItem->setBackground(QColor(0xFF, 0x60, 0x60));

        DisasmTable->setItem(i, 0, addrItem);
        DisasmTable->setItem(i, 1, bytesItem);
        DisasmTable->setItem(i, 2, instrItem);

        if (dr.Address == pc) highlightRow = i;
    }

    if (highlightRow >= 0)
    {
        for (int col = 0; col < 3; col++)
        {
            auto* it = DisasmTable->item(highlightRow, col);
            if (it) it->setBackground(QColor(0xFF, 0xFF, 0x60));
        }
        DisasmTable->scrollToItem(DisasmTable->item(highlightRow, 0));
    }
}

void DebuggerDialog::UpdateRegisters(int cpu)
{
    NDS* nds = Inst->getNDS();
    if (!nds) return;

    ARM* arm = (cpu == 0) ? (ARM*)&nds->ARM9 : (ARM*)&nds->ARM7;

    for (int i = 0; i < 16; i++)
    {
        auto* it = RegTable->item(i, 1);
        if (it) it->setText(QString("0x%1").arg(arm->R[i], 8, 16, QLatin1Char('0')).toUpper());
    }

    auto* cpsrItem = RegTable->item(16, 1);
    if (cpsrItem) cpsrItem->setText(QString("0x%1").arg(arm->CPSR, 8, 16, QLatin1Char('0')).toUpper());

    auto* spsrItem = RegTable->item(17, 1);
    if (spsrItem) spsrItem->setText("(banked)");
}

void DebuggerDialog::OnBreakHit(uint32_t addr, int cpu)
{
    BreakCPU = cpu;
    NDS* nds = Inst->getNDS();
    bool isThumb = nds && nds->NativeBreakThumb;

    SetPaused(true);
    UpdateDisassembly(addr, isThumb, cpu);
    UpdateRegisters(cpu);
    show();
    raise();
    activateWindow();
    statusBar()->showMessage(QString("Breakpoint hit at 0x%1 (%2)")
        .arg(addr, 8, 16, QLatin1Char('0'))
        .arg(cpu == 0 ? "ARM9" : "ARM7"));
}

void DebuggerDialog::OnEmuPause(bool paused)
{
    if (!paused) SetPaused(false);
}

void DebuggerDialog::OnEmuStop()
{
    SetPaused(false);
    PcLabel->setText("PC: --");
    statusBar()->showMessage("Emulation stopped");
}

void DebuggerDialog::SetPaused(bool p)
{
    Paused = p;
    ContinueBtn->setEnabled(p);
    StepBtn->setEnabled(p);
}

void DebuggerDialog::OnRefreshTimer()
{
    NDS* nds = Inst->getNDS();
    if (!nds) return;

    if (Nds != nds)
    {
        Nds = nds;
        RebuildBreakpointList();
    }

    if (Paused)
        UpdateRegisters(BreakCPU);
}

void DebuggerDialog::OnBreakpointContextMenu(const QPoint& pos)
{
    int row = BpList->rowAt(pos.y());
    if (row < 0) return;
    NDS* nds = Inst->getNDS();
    if (!nds || row >= (int)nds->NativeBreakpoints.size()) return;

    QMenu menu(this);
    QAction* execAct  = menu.addAction("Execute [X]");
    QAction* readAct  = menu.addAction("Read [R]");
    QAction* writeAct = menu.addAction("Write [W]");
    QAction* rwAct    = menu.addAction("Read+Write [RW]");

    QAction* chosen = menu.exec(BpList->mapToGlobal(pos));
    if (!chosen) return;

    NativeBpType newType = nativeBp_Execute;
    if      (chosen == readAct)  newType = nativeBp_Read;
    else if (chosen == writeAct) newType = nativeBp_Write;
    else if (chosen == rwAct)    newType = nativeBp_ReadWrite;

    nds->NativeBreakpoints[row].Type = newType;
    RebuildBreakpointList();
}
