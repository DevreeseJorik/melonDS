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

#ifndef TRACELOGGERDIALOG_H
#define TRACELOGGERDIALOG_H

#include <QMainWindow>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QTableWidget>

#include <stdio.h>
#include <stdint.h>
#include <vector>

class EmuInstance;
class EmuThread;

struct TraceRange
{
    uint32_t addrMin;
    uint32_t addrMax;
    bool captureExec;
    bool captureRead;
    bool captureWrite;
};

class TraceLoggerDialog : public QMainWindow
{
    Q_OBJECT
public:
    explicit TraceLoggerDialog(EmuInstance* inst, QWidget* parent = nullptr);
    ~TraceLoggerDialog();

    static TraceLoggerDialog* currentDlg;
    static TraceLoggerDialog* openDlg(EmuInstance* inst, QWidget* parent)
    {
        if (currentDlg) { currentDlg->activateWindow(); return currentDlg; }
        currentDlg = new TraceLoggerDialog(inst, parent);
        currentDlg->show();
        return currentDlg;
    }
    static void closeDlg() { currentDlg = nullptr; }

private slots:
    void OnTraceBrowse();
    void OnTraceToggle();
    void OnAddRange();
    void OnRemoveRange();
    void OnClearRanges();

private:
    void BuildUI();
    QString DefaultTracePath() const;
    void StopTrace();
    void RebuildRangeList();
    void SetControlsEnabled(bool enabled);

    EmuInstance* Inst;
    EmuThread*   Thread;

    std::vector<TraceRange> Ranges;

    QComboBox*   TraceCpuCombo;
    QTableWidget* RangeList;
    QLineEdit*   RangeMinEdit;
    QLineEdit*   RangeMaxEdit;
    QCheckBox*   RangeExecCheck;
    QCheckBox*   RangeReadCheck;
    QCheckBox*   RangeWriteCheck;
    QPushButton* RangeAddBtn;
    QPushButton* RangeRemBtn;
    QPushButton* RangeClrBtn;
    QLineEdit*   TracePathEdit;
    QPushButton* TraceBrowseBtn;
    QPushButton* TraceStartBtn;
    FILE*        TraceFileHandle = nullptr;
};

#endif // TRACELOGGERDIALOG_H
