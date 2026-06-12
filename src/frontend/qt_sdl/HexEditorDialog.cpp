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

#include "HexEditorDialog.h"
#include "EmuInstance.h"
#include "EmuThread.h"
#include "NDS.h"
#include "ARM.h"
#include "MemConstants.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QFileDialog>
#include <QFile>
#include <QStatusBar>
#include <QApplication>
#include <QClipboard>
#include <QRegularExpression>
#include <algorithm>
#include <string_view>

using namespace melonDS;

HexEditorDialog* HexEditorDialog::currentDlg = nullptr;

MemoryViewerWidget::MemoryViewerWidget(EmuInstance* inst, QWidget* parent)
    : QWidget(parent), Inst(inst)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    Font = QFont("Courier New", 10);
    Font.setStyleHint(QFont::Monospace);
    Font.setFixedPitch(true);
    QFontMetrics fm(Font);
    CharW = fm.horizontalAdvance('0');
    CharH = fm.height() + 2;

    ScrollBar = new QScrollBar(Qt::Vertical, this);
    connect(ScrollBar, &QScrollBar::valueChanged, this, [this](int){ update(); });

    RegionStart = 0x02000000;
    RegionSize  = 0x00400000;
    Domain      = 0;

    setMinimumSize(300, 120);
}

uint8_t MemoryViewerWidget::ReadByte(uint32_t addr) const
{
    NDS* nds = Inst ? Inst->getNDS() : nullptr;
    if (!nds) return 0;
    if (Domain == 0)
    {
        if (addr < 0x02000000)
            return nds->ARM9.ITCM[addr & (ITCMPhysicalSize - 1)];
        if ((addr & nds->ARM9.DTCMMask) == nds->ARM9.DTCMBase)
            return nds->ARM9.DTCM[addr & (DTCMPhysicalSize - 1)];
        return nds->ARM9Read8(addr);
    }
    return nds->ARM7Read8(addr);
}

void MemoryViewerWidget::WriteByte(uint32_t addr, uint8_t val)
{
    NDS* nds = Inst ? Inst->getNDS() : nullptr;
    if (!nds) return;
    if (Domain == 0)
    {
        if (addr < 0x02000000)
            nds->ARM9.ITCM[addr & (ITCMPhysicalSize - 1)] = val;
        else if ((addr & nds->ARM9.DTCMMask) == nds->ARM9.DTCMBase)
            nds->ARM9.DTCM[addr & (DTCMPhysicalSize - 1)] = val;
        else
            nds->ARM9Write8(addr, val);
    }
    else
    {
        nds->ARM7Write8(addr, val);
    }
}

uint32_t MemoryViewerWidget::ReadCell(uint32_t addr) const
{
    uint32_t val = 0;
    for (int i = 0; i < DataSize; i++)
        val |= (uint32_t)ReadByte(addr + i) << (8 * i);
    return val;
}

void MemoryViewerWidget::WriteCell(uint32_t addr, uint32_t val)
{
    for (int i = 0; i < DataSize; i++)
        WriteByte(addr + i, (uint8_t)(val >> (8 * i)));
}

bool MemoryViewerWidget::IsFrozen(uint32_t addr) const
{
    for (const auto& f : Frozen)
        if (addr >= f.Addr && addr < f.Addr + (uint32_t)f.Size)
            return true;
    return false;
}

void MemoryViewerWidget::Freeze(uint32_t addr, uint32_t value, int size, bool arm9)
{
    Unfreeze(addr);
    Frozen.push_back({addr, value, size, arm9});
    SyncFrozenToNDS();
    update();
}

void MemoryViewerWidget::Unfreeze(uint32_t addr)
{
    Frozen.erase(
        std::remove_if(Frozen.begin(), Frozen.end(),
            [addr](const FrozenEntry& e){ return e.Addr == addr; }),
        Frozen.end());
    SyncFrozenToNDS();
    update();
}

void MemoryViewerWidget::ClearAllFrozen()
{
    Frozen.clear();
    SyncFrozenToNDS();
    update();
}

void MemoryViewerWidget::SyncFrozenToNDS()
{
    NDS* nds = Inst ? Inst->getNDS() : nullptr;
    if (!nds) return;
    nds->FrozenValues.clear();
    for (const auto& fe : Frozen)
        nds->FrozenValues.push_back({fe.Addr, fe.Value, fe.Size, fe.IsARM9, true});
}

int MemoryViewerWidget::RowsVisible() const
{
    return std::max(1, (height() - 4 - CharH) / CharH);
}

void MemoryViewerWidget::UpdateScrollRange()
{
    int visRows = RowsVisible();
    int minRow, maxRow;
    if (RegionSize == 0)
    {
        minRow = 0;
        maxRow = std::max(0, (int)(0xFFFFFFFF / kBytesPerRow) - visRows + 1);
    }
    else
    {
        minRow = (int)(RegionStart / kBytesPerRow);
        int totalRows = (int)((RegionSize + kBytesPerRow - 1) / kBytesPerRow);
        maxRow = minRow + std::max(0, totalRows - visRows);
    }
    ScrollBar->setRange(minRow, maxRow);
    ScrollBar->setPageStep(visRows);
    int cur = ScrollBar->value();
    if (cur < minRow) ScrollBar->setValue(minRow);
    else if (cur > maxRow) ScrollBar->setValue(maxRow);
}

void MemoryViewerWidget::EnsureAddrVisible(uint32_t addr)
{
    int row    = (int)(addr / kBytesPerRow);
    int topRow = ScrollBar->value();
    int botRow = topRow + RowsVisible() - 1;
    if (row < topRow)
        ScrollBar->setValue(row);
    else if (row > botRow)
        ScrollBar->setValue(row - RowsVisible() + 1);
}

void MemoryViewerWidget::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    int sbW = ScrollBar->sizeHint().width();
    ScrollBar->setGeometry(width() - sbW, 0, sbW, height());
    UpdateScrollRange();
    update();
}

void MemoryViewerWidget::SetRegion(uint32_t start, uint32_t size, int domain)
{
    RegionStart = start;
    RegionSize  = size;
    Domain      = domain;
    UpdateScrollRange();
    SetAddress(start);
}

void MemoryViewerWidget::SetAddress(uint32_t addr)
{
    addr &= ~(uint32_t)(DataSize - 1);
    int row = (int)(addr / kBytesPerRow);
    row = std::max(ScrollBar->minimum(), std::min(ScrollBar->maximum(), row));
    ScrollBar->setValue(row);
    update();
}

void MemoryViewerWidget::JumpTo(uint32_t addr)
{
    addr &= ~(uint32_t)(DataSize - 1);
    SetAddress(addr);
    SelAddr    = addr;
    AnchorAddr = addr;
    HasSel     = true;
    EditNibble = 0;
    setFocus();
    update();
}

void MemoryViewerWidget::SetDomain(int domain)
{
    Domain = domain;
    update();
}

void MemoryViewerWidget::SetDataSize(int ds)
{
    DataSize   = ds;
    HasSel     = false;
    EditNibble = 0;
    UpdateScrollRange();
    update();
}

void MemoryViewerWidget::Refresh() { update(); }

bool MemoryViewerWidget::HitTestCell(QPoint pos, uint32_t& outAddr) const
{
    int addrW  = CharW * 8 + 6;
    int hexX   = 2 + addrW;
    int cellW  = CellPxWidth();
    int cpr    = CellsPerRow();
    int hexW   = cellW * cpr;

    int row = (pos.y() - 2 - CharH) / CharH;
    if (row < 0 || row >= RowsVisible()) return false;

    int col = -1;
    if (pos.x() >= hexX && pos.x() < hexX + hexW)
    {
        col = (pos.x() - hexX) / cellW;
    }
    else
    {
        int ascX = hexX + hexW + 4;
        int ascW = CharW * kBytesPerRow;
        if (pos.x() >= ascX && pos.x() < ascX + ascW)
        {
            int byteCol = (pos.x() - ascX) / CharW;
            col = byteCol / DataSize;
        }
    }
    if (col < 0 || col >= cpr) return false;

    uint32_t topAddr = (uint32_t)(ScrollBar->value() * kBytesPerRow);
    outAddr = topAddr + (uint32_t)(row * kBytesPerRow + col * DataSize);
    return true;
}

void MemoryViewerWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setFont(Font);
    QFontMetrics fm(Font);
    CharW = fm.horizontalAdvance('0');
    CharH = fm.height() + 2;

    int sbW   = ScrollBar->width();
    int drawW = width() - sbW;

    p.fillRect(0, 0, drawW, height(), palette().color(QPalette::Base));

    int topRow  = ScrollBar->value();
    uint32_t topAddr = (uint32_t)(topRow) * (uint32_t)kBytesPerRow;

    int addrW  = CharW * 8 + 6;
    int hexX   = 2 + addrW;
    int cellW  = CellPxWidth();
    int cpr    = CellsPerRow();
    int hexW   = cellW * cpr;
    int ascX   = hexX + hexW + 4;
    int rows   = RowsVisible();

    // Header row
    p.fillRect(0, 0, drawW, CharH, palette().color(QPalette::AlternateBase));
    p.setPen(QColor(0x80, 0x80, 0x80));
    for (int col = 0; col < cpr; col++)
    {
        char hdr[4];
        snprintf(hdr, sizeof(hdr), "%X", col * DataSize);
        p.drawText(hexX + col * cellW, 2 + fm.ascent(), hdr);
    }

    // Compute selection range (byte addresses of selected cells)
    uint32_t selLo = 0, selHi = 0;
    if (HasSel)
    {
        selLo = std::min(SelAddr, AnchorAddr);
        selHi = std::max(SelAddr, AnchorAddr);
    }

    int y = 2 + CharH + fm.ascent();

    for (int row = 0; row < rows; row++, y += CharH)
    {
        uint32_t rowAddr = topAddr + (uint32_t)(row * kBytesPerRow);

        // Address label
        p.setPen(QColor(0x60, 0x60, 0xC0));
        char abuf[12];
        snprintf(abuf, sizeof(abuf), "%08X", rowAddr);
        p.drawText(2, y, abuf);

        // Hex cells
        for (int col = 0; col < cpr; col++)
        {
            uint32_t caddr = rowAddr + (uint32_t)(col * DataSize);
            uint32_t val   = ReadCell(caddr);
            bool frozen    = IsFrozen(caddr);
            bool inSel     = HasSel && caddr >= selLo && caddr <= selHi;
            bool isCursor  = HasSel && caddr == SelAddr;

            int hx = hexX + col * cellW;
            int cellPxW = CharW * DataSize * 2;

            if (frozen)
                p.fillRect(hx, y - fm.ascent(), cellPxW + 1, CharH, QColor(0xCC, 0xAA, 0x00, 80));
            if (inSel)
                p.fillRect(hx, y - fm.ascent(), cellPxW + 1, CharH, QColor(0x00, 0x80, 0xFF, inSel && isCursor ? 120 : 60));

            char hbuf[10];
            if (DataSize == 1)      snprintf(hbuf, sizeof(hbuf), "%02X", val);
            else if (DataSize == 2) snprintf(hbuf, sizeof(hbuf), "%04X", val);
            else                    snprintf(hbuf, sizeof(hbuf), "%08X", val);

            p.setPen(frozen ? QColor(0xDD, 0x88, 0x00) : palette().color(QPalette::Text));
            p.drawText(hx, y, hbuf);
        }

        // ASCII panel (always shows individual bytes)
        for (int b = 0; b < kBytesPerRow; b++)
        {
            uint32_t baddr = rowAddr + (uint32_t)b;
            uint8_t  bval  = ReadByte(baddr);
            bool frozen    = IsFrozen(baddr);
            // Highlight ASCII col for any selected cell covering this byte
            uint32_t cellBase = baddr & ~(uint32_t)(DataSize - 1);
            bool inSel = HasSel && cellBase >= selLo && cellBase <= selHi;

            int ax = ascX + b * CharW;
            if (inSel)
                p.fillRect(ax, y - fm.ascent(), CharW, CharH, QColor(0x00, 0x80, 0xFF, 40));

            char ch = (bval >= 0x20 && bval < 0x7F) ? (char)bval : '.';
            p.setPen(frozen ? QColor(0xDD, 0x88, 0x00) : QColor(0x40, 0xA0, 0x40));
            p.drawText(ax, y, QString(ch));
        }
    }
}

void MemoryViewerWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
    {
        uint32_t addr;
        if (HitTestCell(e->pos(), addr))
        {
            SelAddr    = addr;
            AnchorAddr = addr;
            HasSel     = true;
            Dragging   = true;
            EditNibble = 0;
            setFocus();
            update();
        }
    }
}

void MemoryViewerWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (!Dragging || !(e->buttons() & Qt::LeftButton)) return;
    uint32_t addr;
    if (HitTestCell(e->pos(), addr) && addr != SelAddr)
    {
        SelAddr = addr;
        update();
    }
}

void MemoryViewerWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        Dragging = false;
}

void MemoryViewerWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    uint32_t addr;
    if (!HitTestCell(e->pos(), addr)) return;

    uint32_t cur = ReadCell(addr);
    int hexChars = DataSize * 2;
    QString defText = QString("%1").arg(cur, hexChars, 16, QLatin1Char('0')).toUpper();

    bool ok;
    constexpr std::string_view titles[] = { "Edit byte", "Edit 16-bit word", "", "Edit 32-bit dword" };
    QString text = QInputDialog::getText(this, QLatin1String(titles[DataSize - 1].data()),
        QString("0x%1:").arg(addr, 8, 16, QLatin1Char('0')),
        QLineEdit::Normal, defText, &ok);
    if (ok && !text.isEmpty())
    {
        uint32_t v = text.toUInt(&ok, 16);
        if (ok) WriteCell(addr, v);
    }
    update();
}

void MemoryViewerWidget::wheelEvent(QWheelEvent* e)
{
    int rows = e->angleDelta().y() > 0 ? -3 : 3;
    ScrollBar->setValue(ScrollBar->value() + rows);
}

void MemoryViewerWidget::keyPressEvent(QKeyEvent* e)
{
    // Copy / paste
    if (e->key() == Qt::Key_C && (e->modifiers() & Qt::ControlModifier))
    {
        CopySelection();
        return;
    }
    if (e->key() == Qt::Key_V && (e->modifiers() & Qt::ControlModifier))
    {
        PasteHexStream();
        return;
    }

    if (!HasSel) { QWidget::keyPressEvent(e); return; }

    auto moveCursor = [&](uint32_t newAddr) {
        newAddr &= ~(uint32_t)(DataSize - 1);
        SelAddr    = newAddr;
        AnchorAddr = newAddr;
        EditNibble = 0;
        EnsureAddrVisible(newAddr);
        update();
    };

    QString k = e->text().toUpper();
    if (!k.isEmpty() && (k[0].isDigit() || (k[0] >= 'A' && k[0] <= 'F')))
    {
        int nibble    = k[0].isDigit() ? (k[0].toLatin1() - '0') : (k[0].toLatin1() - 'A' + 10);
        int totalNibs = DataSize * 2;
        int shift     = (totalNibs - 1 - EditNibble) * 4;
        uint32_t cur  = ReadCell(SelAddr);
        cur = (cur & ~(0xFu << shift)) | ((uint32_t)nibble << shift);
        WriteCell(SelAddr, cur);
        EditNibble++;
        if (EditNibble >= totalNibs)
        {
            EditNibble = 0;
            uint32_t next = SelAddr + DataSize;
            SelAddr    = next;
            AnchorAddr = next;
            EnsureAddrVisible(next);
        }
        update();
    }
    else if (e->key() == Qt::Key_Left)
    {
        moveCursor(SelAddr >= (uint32_t)DataSize ? SelAddr - DataSize : SelAddr);
    }
    else if (e->key() == Qt::Key_Right)
    {
        moveCursor(SelAddr + DataSize);
    }
    else if (e->key() == Qt::Key_Up)
    {
        moveCursor(SelAddr >= (uint32_t)kBytesPerRow ? SelAddr - kBytesPerRow : SelAddr);
    }
    else if (e->key() == Qt::Key_Down)
    {
        moveCursor(SelAddr + kBytesPerRow);
    }
    else if (e->key() == Qt::Key_PageUp)
    {
        ScrollBar->setValue(std::max(ScrollBar->minimum(), ScrollBar->value() - RowsVisible()));
        update();
    }
    else if (e->key() == Qt::Key_PageDown)
    {
        ScrollBar->setValue(std::min(ScrollBar->maximum(), ScrollBar->value() + RowsVisible()));
        update();
    }
    else QWidget::keyPressEvent(e);
}

void MemoryViewerWidget::contextMenuEvent(QContextMenuEvent* e)
{
    uint32_t addr = 0;
    bool onCell = HitTestCell(e->pos(), addr);

    QMenu menu(this);

    // Copy / paste
    QAction* copyAct  = nullptr;
    QAction* pasteAct = nullptr;
    if (HasSel)
        copyAct = menu.addAction("Copy selection  (Ctrl+C)");
    pasteAct = menu.addAction("Paste hex stream  (Ctrl+V)");
    if (HasSel || onCell)
        menu.addSeparator();

    // Edit
    QAction* editAct = nullptr;
    if (onCell)
    {
        constexpr std::string_view editLabels[] = { "Edit byte...", "Edit 16-bit word...", "", "Edit 32-bit dword..." };
        editAct = menu.addAction(QLatin1String(editLabels[DataSize - 1].data()));
        menu.addSeparator();
    }

    // Freeze
    QAction* freezeAct  = nullptr;
    QAction* freeze2Act = nullptr;
    QAction* freeze4Act = nullptr;
    QAction* unfreezeAct= nullptr;
    if (onCell)
    {
        if (!IsFrozen(addr))
        {
            freezeAct  = menu.addAction("Freeze 1 byte (current value)");
            freeze2Act = menu.addAction("Freeze 2 bytes (current value)");
            freeze4Act = menu.addAction("Freeze 4 bytes (current value)");
        }
        else
        {
            unfreezeAct = menu.addAction("Unfreeze");
        }
        menu.addSeparator();
    }

    // Dump
    QAction* dumpAct = menu.addAction("Dump region to file...");

    QAction* chosen = menu.exec(e->globalPos());
    if (!chosen) return;

    if (chosen == copyAct)
    {
        CopySelection();
    }
    else if (chosen == pasteAct)
    {
        if (!HasSel && onCell)
        {
            SelAddr    = addr;
            AnchorAddr = addr;
            HasSel     = true;
        }
        PasteHexStream();
    }
    else if (chosen == editAct)
    {
        uint32_t cur = ReadCell(addr);
        int hexChars = DataSize * 2;
        QString defText = QString("%1").arg(cur, hexChars, 16, QLatin1Char('0')).toUpper();
        bool ok;
        constexpr std::string_view editLabels[] = { "Edit byte", "Edit 16-bit word", "", "Edit 32-bit dword" };
        QString text = QInputDialog::getText(this, QLatin1String(editLabels[DataSize - 1].data()),
            QString("0x%1:").arg(addr, 8, 16, QLatin1Char('0')),
            QLineEdit::Normal, defText, &ok);
        if (ok && !text.isEmpty())
        {
            uint32_t v = text.toUInt(&ok, 16);
            if (ok) WriteCell(addr, v);
        }
    }
    else if (chosen == freezeAct)
    {
        Freeze(addr, ReadByte(addr), 1, Domain == 0);
    }
    else if (chosen == freeze2Act)
    {
        uint32_t v = (uint32_t)ReadByte(addr) | ((uint32_t)ReadByte(addr + 1) << 8);
        Freeze(addr, v, 2, Domain == 0);
    }
    else if (chosen == freeze4Act)
    {
        uint32_t v = (uint32_t)ReadByte(addr)
                   | ((uint32_t)ReadByte(addr + 1) << 8)
                   | ((uint32_t)ReadByte(addr + 2) << 16)
                   | ((uint32_t)ReadByte(addr + 3) << 24);
        Freeze(addr, v, 4, Domain == 0);
    }
    else if (chosen == unfreezeAct)
    {
        Unfreeze(addr);
    }
    else if (chosen == dumpAct)
    {
        DumpRegionToFile();
    }

    update();
}

void MemoryViewerWidget::CopySelection()
{
    if (!HasSel) return;
    uint32_t lo = std::min(SelAddr, AnchorAddr);
    uint32_t hi = std::max(SelAddr, AnchorAddr) + (uint32_t)DataSize - 1;

    QString text;
    for (uint32_t a = lo; a <= hi; a++)
    {
        if (!text.isEmpty()) text += ' ';
        char hbuf[3];
        snprintf(hbuf, sizeof(hbuf), "%02X", ReadByte(a));
        text += hbuf;
    }
    QApplication::clipboard()->setText(text);
}

void MemoryViewerWidget::PasteHexStream()
{
    if (!HasSel) return;
    QString raw = QApplication::clipboard()->text();

    // Strip all non-hex characters and parse pairs of nibbles
    QString hex;
    for (QChar c : raw)
    {
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
            hex += c.toUpper();
    }
    if (hex.isEmpty() || hex.size() % 2 != 0) return;

    uint32_t addr = std::min(SelAddr, AnchorAddr);
    for (int i = 0; i + 1 < hex.size(); i += 2)
    {
        bool ok;
        uint8_t val = (uint8_t)hex.mid(i, 2).toUInt(&ok, 16);
        if (!ok) return;
        WriteByte(addr, val);
        addr++;
    }
    update();
}

void MemoryViewerWidget::DumpRegionToFile()
{
    uint32_t dumpStart = RegionStart;
    uint32_t dumpSize  = RegionSize;

    if (dumpSize == 0)
    {
        // Full address space — ask the user for an explicit range
        bool ok;
        QString startStr = QInputDialog::getText(this, "Dump Memory",
            "Start address (hex):", QLineEdit::Normal, "00000000", &ok);
        if (!ok) return;
        dumpStart = startStr.toUInt(&ok, 16);
        if (!ok) return;

        QString sizeStr = QInputDialog::getText(this, "Dump Memory",
            "Size in bytes (hex):", QLineEdit::Normal, "01000000", &ok);
        if (!ok) return;
        dumpSize = sizeStr.toUInt(&ok, 16);
        if (!ok || dumpSize == 0) return;
    }

    QString defaultName = QString("dump_%1_%2b.bin")
        .arg(dumpStart, 8, 16, QLatin1Char('0'))
        .arg(dumpSize, 0, 16);
    QString path = QFileDialog::getSaveFileName(this, "Save Memory Dump", defaultName,
        "Binary files (*.bin);;All files (*)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;

    QByteArray buf;
    buf.reserve((int)dumpSize);
    for (uint32_t i = 0; i < dumpSize; i++)
        buf.append((char)ReadByte(dumpStart + i));
    f.write(buf);
}

HexEditorDialog::HexEditorDialog(EmuInstance* inst, QWidget* parent)
    : QMainWindow(parent), Inst(inst), Thread(inst->getEmuThread())
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("melonDS Hex Editor");
    resize(900, 400);
    BuildUI();

    connect(Thread, &EmuThread::debugFrameComplete,
            this, [this]{ MemView->Refresh(); }, Qt::QueuedConnection);
    connect(Thread, &EmuThread::debugBreakHit,
            this, [this](uint32_t, int){ MemView->Refresh(); }, Qt::QueuedConnection);

    RefreshTimer = new QTimer(this);
    RefreshTimer->setInterval(100);
    connect(RefreshTimer, &QTimer::timeout, this, &HexEditorDialog::OnRefreshTimer);
    RefreshTimer->start();
}

HexEditorDialog::~HexEditorDialog()
{
    closeDlg();
}

void HexEditorDialog::BuildUI()
{
    QWidget* w = new QWidget;
    QVBoxLayout* vl = new QVBoxLayout(w);
    vl->setContentsMargins(4, 4, 4, 4);

    QHBoxLayout* hl = new QHBoxLayout;

    MemRegionCombo = new QComboBox;
    struct R { std::string_view name; uint32_t start, size; int dom; };
    constexpr R kRegions[] = {
        { "Main RAM (ARM9)",    MainRAMBase,    MainRAMMaxSize,   0 },
        { "Shared WRAM",        SharedWRAMBase, SharedWRAMSize,   0 },
        { "IO Registers ARM9",  IORegsBase,     IORegsViewSize,   0 },
        { "Palette RAM",        PaletteRAMBase, PaletteRAMSize,   0 },
        { "VRAM",               VRAMBase,       VRAMSize,         0 },
        { "OAM",                OAMBase,        OAMSize,          0 },
        { "ITCM",               0x00000000,     ITCMPhysicalSize, 0 },
        { "ARM7 BIOS",          0x00000000,     ARM7BIOSSize,     1 },
        { "ARM7 WRAM",          ARM7WRAMBase,   ARM7WRAMSize,     1 },
        { "IO Registers ARM7",  IORegsBase,     IORegsViewSize,   1 },
        { "Full ARM9 space",    0x00000000,     0x00000000,       0 },
        { "Full ARM7 space",    0x00000000,     0x00000000,       1 },
    };
    for (const auto& r : kRegions)
    {
        QVariantList d; d << (uint)(r.start) << (uint)(r.size) << r.dom;
        MemRegionCombo->addItem(QLatin1String(r.name.data(), r.name.size()), d);
    }

    DtcmComboIdx = MemRegionCombo->count();
    {
        QVariantList d; d << (uint)0 << (uint)DTCMPhysicalSize << 0;
        MemRegionCombo->addItem("DTCM", d);
    }

    DataSizeCombo = new QComboBox;
    DataSizeCombo->addItem("1 byte");
    DataSizeCombo->addItem("2 byte (16-bit)");
    DataSizeCombo->addItem("4 byte (32-bit)");
    DataSizeCombo->setMaximumWidth(130);

    MemAddrEdit = new QLineEdit;
    MemAddrEdit->setPlaceholderText("Address (hex)");
    MemAddrEdit->setMaximumWidth(110);
    MemJumpBtn = new QPushButton("Jump");
    DumpBtn    = new QPushButton("Dump...");

    connect(MemRegionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &HexEditorDialog::OnMemRegionChanged);
    connect(DataSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &HexEditorDialog::OnDataSizeChanged);
    connect(MemJumpBtn,  &QPushButton::clicked,     this, &HexEditorDialog::OnMemJump);
    connect(MemAddrEdit, &QLineEdit::returnPressed, this, &HexEditorDialog::OnMemJump);
    connect(DumpBtn, &QPushButton::clicked, this, [this]{ MemView->DumpRegionToFile(); });

    QPushButton* clearFreezeBtn = new QPushButton("Clear Frozen");
    connect(clearFreezeBtn, &QPushButton::clicked, this, [this]{ MemView->ClearAllFrozen(); });

    hl->addWidget(MemRegionCombo, 3);
    hl->addWidget(DataSizeCombo);
    hl->addWidget(new QLabel("  Go to:"));
    hl->addWidget(MemAddrEdit, 2);
    hl->addWidget(MemJumpBtn);
    hl->addStretch();
    hl->addWidget(DumpBtn);
    hl->addWidget(clearFreezeBtn);
    vl->addLayout(hl);

    MemView = new MemoryViewerWidget(Inst, w);
    vl->addWidget(MemView, 1);

    setCentralWidget(w);
    OnMemRegionChanged(0);
}

void HexEditorDialog::OnMemJump()
{
    bool ok;
    uint32_t addr = MemAddrEdit->text().toUInt(&ok, 16);
    if (!ok) { statusBar()->showMessage("Invalid address"); return; }
    MemView->JumpTo(addr);
}

void HexEditorDialog::OnMemRegionChanged(int idx)
{
    if (idx < 0 || idx >= MemRegionCombo->count()) return;
    QVariantList d = MemRegionCombo->itemData(idx).toList();
    if (d.size() < 3) return;

    uint32_t start = d[0].toUInt();
    uint32_t size  = d[1].toUInt();
    int      dom   = d[2].toInt();

    MemView->SetRegion(start, size, dom);
    MemAddrEdit->setText(QString("%1").arg(start, 8, 16, QLatin1Char('0')).toUpper());
}

void HexEditorDialog::OnDataSizeChanged(int idx)
{
    constexpr int kSizes[] = { 1, 2, 4 };
    if (idx >= 0 && idx < 3)
        MemView->SetDataSize(kSizes[idx]);
}

void HexEditorDialog::OnRefreshTimer()
{
    NDS* nds = Inst->getNDS();
    if (!nds) return;

    uint32_t dtcmBase = nds->ARM9.DTCMBase;
    if (dtcmBase != LastDtcmBase || Nds != nds)
    {
        Nds = nds;
        LastDtcmBase = dtcmBase;
        QVariantList d; d << (uint)dtcmBase << (uint)DTCMPhysicalSize << 0;
        MemRegionCombo->setItemData(DtcmComboIdx, d);
        if (MemRegionCombo->currentIndex() == DtcmComboIdx)
            OnMemRegionChanged(DtcmComboIdx);
    }
}
