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

#ifndef DEBUGGERDIALOG_H
#define DEBUGGERDIALOG_H

#include <QMainWindow>
#include <QDockWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QTimer>
#include <QAction>

#include <stdint.h>
#include <vector>

#include "types.h"

class EmuInstance;
class EmuThread;
namespace melonDS { class NDS; }

class DebuggerDialog : public QMainWindow
{
    Q_OBJECT
public:
    explicit DebuggerDialog(EmuInstance* inst, QWidget* parent = nullptr);
    ~DebuggerDialog();

    static DebuggerDialog* currentDlg;
    static DebuggerDialog* openDlg(EmuInstance* inst, QWidget* parent)
    {
        if (currentDlg) { currentDlg->activateWindow(); return currentDlg; }
        currentDlg = new DebuggerDialog(inst, parent);
        currentDlg->show();
        return currentDlg;
    }
    static void closeDlg() { currentDlg = nullptr; }

public slots:
    void OnBreakHit(uint32_t addr, int cpu);
    void OnEmuPause(bool paused);
    void OnEmuStop();

private slots:
    void OnAddBreakpoint();
    void OnRemoveBreakpoint();
    void OnClearBreakpoints();
    void OnBreakpointContextMenu(const QPoint& pos);
    void OnContinue();
    void OnStep();
    void OnRefreshTimer();

private:
    void BuildUI();
    void RebuildBreakpointList();
    void UpdateDisassembly(uint32_t pc, bool isThumb, int cpu);
    void UpdateRegisters(int cpu);
    void SetPaused(bool p);

    EmuInstance*  Inst;
    EmuThread*    Thread;
    melonDS::NDS* Nds = nullptr;

    bool Paused   = false;
    int  BreakCPU = 0;

    QDockWidget*  BpDock;
    QTableWidget* BpList;
    QLineEdit*    BpAddrEdit;
    QComboBox*    BpCpuCombo;
    QComboBox*    BpTypeCombo;
    QPushButton*  BpAddBtn;
    QPushButton*  BpRemBtn;
    QPushButton*  BpClrBtn;

    QDockWidget*  DisasmDock;
    QTableWidget* DisasmTable;
    QLabel*       PcLabel;
    QPushButton*  ContinueBtn;
    QPushButton*  StepBtn;

    QDockWidget*  RegDock;
    QTableWidget* RegTable;

    QTimer* RefreshTimer;
};

#endif // DEBUGGERDIALOG_H
