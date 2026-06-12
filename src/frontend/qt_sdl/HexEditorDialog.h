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

#ifndef HEXEDITORDIALOG_H
#define HEXEDITORDIALOG_H

#include <QMainWindow>
#include <QWidget>
#include <QScrollBar>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QFont>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QTimer>

#include <stdint.h>
#include <vector>

#include "types.h"

class EmuInstance;
class EmuThread;
namespace melonDS { class NDS; }

class MemoryViewerWidget : public QWidget
{
    Q_OBJECT
public:
    struct FrozenEntry { uint32_t Addr; uint32_t Value; int Size; bool IsARM9; };

    explicit MemoryViewerWidget(EmuInstance* inst, QWidget* parent = nullptr);

    void SetAddress(uint32_t addr);
    void JumpTo(uint32_t addr);
    void SetDomain(int domain);
    void SetRegion(uint32_t start, uint32_t size, int domain);
    void SetDataSize(int ds);
    void Refresh();

    void CopySelection();
    void PasteHexStream();
    void DumpRegionToFile();

    bool IsFrozen(uint32_t addr) const;
    void Freeze(uint32_t addr, uint32_t value, int size, bool arm9);
    void Unfreeze(uint32_t addr);
    const std::vector<FrozenEntry>& FrozenEntries() const { return Frozen; }
    void SyncFrozenToNDS();
    void ClearAllFrozen();

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    EmuInstance* Inst;
    uint32_t Domain      = 0;
    uint32_t RegionStart = 0x00000000;
    uint32_t RegionSize  = 0;

    static constexpr int kBytesPerRow = 16;
    int DataSize = 1;       // bytes per cell: 1, 2, or 4
    int CharW = 10, CharH = 18;

    QScrollBar* ScrollBar;
    QFont Font;

    std::vector<FrozenEntry> Frozen;

    bool     HasSel     = false;
    uint32_t SelAddr    = 0;    // cursor cell byte address
    uint32_t AnchorAddr = 0;    // drag-anchor byte address
    bool     Dragging   = false;
    int      EditNibble = 0;    // nibble within cell being typed (0 = MSN)

    int  CellsPerRow()  const { return kBytesPerRow / DataSize; }
    int  CellPxWidth()  const { return CharW * (2 * DataSize + 1); }

    uint8_t  ReadByte(uint32_t addr) const;
    void     WriteByte(uint32_t addr, uint8_t val);
    uint32_t ReadCell(uint32_t addr) const;
    void     WriteCell(uint32_t addr, uint32_t val);

    int  RowsVisible() const;
    void UpdateScrollRange();
    void EnsureAddrVisible(uint32_t addr);
    bool HitTestCell(QPoint pos, uint32_t& outAddr) const;
};

class HexEditorDialog : public QMainWindow
{
    Q_OBJECT
public:
    explicit HexEditorDialog(EmuInstance* inst, QWidget* parent = nullptr);
    ~HexEditorDialog();

    static HexEditorDialog* currentDlg;
    static HexEditorDialog* openDlg(EmuInstance* inst, QWidget* parent)
    {
        if (currentDlg) { currentDlg->activateWindow(); return currentDlg; }
        currentDlg = new HexEditorDialog(inst, parent);
        currentDlg->show();
        return currentDlg;
    }
    static void closeDlg() { currentDlg = nullptr; }

private slots:
    void OnMemJump();
    void OnMemRegionChanged(int idx);
    void OnDataSizeChanged(int idx);
    void OnRefreshTimer();

private:
    void BuildUI();

    EmuInstance*  Inst;
    EmuThread*    Thread;
    melonDS::NDS* Nds = nullptr;

    uint32_t LastDtcmBase = 0xFFFFFFFF;
    int      DtcmComboIdx = -1;

    MemoryViewerWidget* MemView;
    QLineEdit*          MemAddrEdit;
    QComboBox*          MemRegionCombo;
    QComboBox*          DataSizeCombo;
    QPushButton*        MemJumpBtn;
    QPushButton*        DumpBtn;

    QTimer* RefreshTimer;
};

#endif // HEXEDITORDIALOG_H
