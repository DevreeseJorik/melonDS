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

#ifndef ARMDISASSEMBLER_H
#define ARMDISASSEMBLER_H

#include <string>
#include <vector>
#include <stdint.h>
#include <functional>

namespace ARMDisassembler
{

struct DisasmResult
{
    uint32_t Address;
    uint32_t Encoding;    // raw instruction bytes (16-bit for Thumb, 32-bit for ARM / BL pair)
    bool     IsThumb;
    int      Size;
    std::string Instruction;
};

DisasmResult DisassembleARM(uint32_t addr, uint32_t instr);
DisasmResult DisassembleThumb(uint32_t addr, uint16_t instr, uint16_t next, bool& consumed32);

std::vector<DisasmResult> DisassembleRange(
    uint32_t startAddr, int count, bool isThumb,
    std::function<uint32_t(uint32_t)> readWord,
    std::function<uint16_t(uint32_t)> readHalf);

} // namespace ARMDisassembler

#endif // ARMDISASSEMBLER_H
