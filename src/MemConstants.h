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

#ifndef MELONDS_MEMCONSTANTS_H
#define MELONDS_MEMCONSTANTS_H

#include "types.h"

namespace melonDS
{
// NDS memory map base addresses
constexpr u32 MainRAMBase    = 0x02000000;
constexpr u32 SharedWRAMBase = 0x03000000;
constexpr u32 ARM7WRAMBase   = 0x03800000;
constexpr u32 IORegsBase     = 0x04000000;
constexpr u32 PaletteRAMBase = 0x05000000;
constexpr u32 VRAMBase       = 0x06000000;
constexpr u32 OAMBase        = 0x07000000;
constexpr u32 ARM9BIOSBase   = 0xFFFF0000;

// NDS memory map sizes
constexpr u32 MainRAMMaxSize = 0x1000000;
constexpr u32 SharedWRAMSize = 0x8000;
constexpr u32 ARM7WRAMSize = 0x10000;
constexpr u32 IORegsViewSize = 0x00001000;
constexpr u32 PaletteRAMSize = 0x00000800;
constexpr u32 VRAMSize       = 0x00100000;
constexpr u32 OAMSize        = 0x00000800;
constexpr u32 NWRAMSize = 0x40000;
constexpr u32 ARM9BIOSSize = 0x1000;
constexpr u32 ARM7BIOSSize = 0x4000;
constexpr u32 DSiBIOSSize = 0x10000;
constexpr u32 ITCMPhysicalSize = 0x8000;
constexpr u32 DTCMPhysicalSize = 0x4000;

constexpr u32 ARM7BIOSCRC32 = 0x1280f0d5;
constexpr u32 ARM9BIOSCRC32 = 0x2ab23573;

// CRC's for the low 32K BIOS regions
constexpr u32 ARM7iBIOSLowCRC32 = 0x5434691D;
constexpr u32 ARM9iBIOSLowCRC32 = 0x11E7C1EA;

// CRC's for the full BIOS
constexpr u32 ARM7iBIOSCRC32 = 0x4316CC42;
constexpr u32 ARM9iBIOSCRC32 = 0xBAE84F6C;
}

#endif // MELONDS_MEMCONSTANTS_H