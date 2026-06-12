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

#include "ARMDisassembler.h"
#include <cstdio>
#include <string_view>

namespace ARMDisassembler
{

constexpr const char* kCond[16] = {
    "EQ","NE","CS","CC","MI","PL","VS","VC",
    "HI","LS","GE","LT","GT","LE","",  "NV"
};
constexpr const char* kReg[16] = {
    "R0","R1","R2","R3","R4","R5","R6","R7",
    "R8","R9","R10","R11","R12","SP","LR","PC"
};
constexpr const char* kDP[16] = {
    "AND","EOR","SUB","RSB","ADD","ADC","SBC","RSC",
    "TST","TEQ","CMP","CMN","ORR","MOV","BIC","MVN"
};
constexpr const char* kShift[4] = { "LSL","LSR","ASR","ROR" };

template<typename... Args>
static std::string fmt(const char* format, Args... args)
{
    int size = std::snprintf(nullptr, 0, format, args...) + 1;
    char buf[size];
    std::snprintf(buf, size, format, args...);
    return std::string(buf);
}

static uint32_t rotImm(uint32_t instr)
{
    uint32_t imm = instr & 0xFF;
    uint32_t rot = ((instr >> 8) & 0xF) << 1;
    return (imm >> rot) | (imm << ((32 - rot) & 31));
}

static std::string fmtImm(uint32_t v)
{
    return fmt(v < 10 ? "#%u" : "#0x%X", v);
}

static std::string armShift(uint32_t instr)
{
    int type = (instr >> 5) & 3;
    if ((instr >> 4) & 1)
    {
        return std::string(", ") + kShift[type] + " " + kReg[(instr>>8)&0xF];
    }
    int amt = (instr >> 7) & 0x1F;
    if (amt == 0)
    {
        if (type == 0) return "";
        if (type == 3) return ", RRX";
        amt = 32;
    }
    return fmt(", %s #%d", kShift[type], amt);
}

static std::string armOp2(uint32_t instr, bool isImm)
{
    if (isImm)
    {
        uint32_t v = rotImm(instr);
        return fmtImm(v);
    }
    return std::string(kReg[instr&0xF]) + armShift(instr);
}

static std::string armAddr(uint32_t instr, bool isImm)
{
    int rn    = (instr>>16)&0xF;
    bool pre  = (instr>>24)&1;
    bool up   = (instr>>23)&1;
    bool wb   = (instr>>21)&1;
    std::string_view sign = up ? "" : "-";

    std::string base = kReg[rn];
    std::string ofs;
    if (isImm)
    {
        uint32_t offset = instr & 0xFFF;
        if (offset)
            ofs = fmt(", #%s%u", sign.data(), offset);
    }
    else
    {
        int rm = instr & 0xF;
        std::string sh = armShift(instr);
        ofs = fmt(", %s%s%s", sign.data(), kReg[rm], sh.c_str());
    }

    if (pre)
        return fmt("[%s%s]%s", base.c_str(), ofs.c_str(), wb ? "!" : "");
    else
        return fmt("[%s]%s", base.c_str(), ofs.c_str());
}

static std::string armAddrH(uint32_t instr)
{
    int rn   = (instr>>16)&0xF;
    bool pre = (instr>>24)&1;
    bool up  = (instr>>23)&1;
    bool wb  = (instr>>21)&1;
    bool imm = (instr>>22)&1;
    std::string_view sign = up ? "" : "-";

    std::string ofs;
    if (imm)
    {
        uint32_t off = ((instr>>4)&0xF0)|(instr&0xF);
        if (off) ofs = fmt(", #%s%u", sign.data(), off);
    }
    else
    {
        ofs = fmt(", %s%s", sign.data(), kReg[instr&0xF]);
    }
    if (pre)
        return fmt("[%s%s]%s", kReg[rn], ofs.c_str(), wb ? "!" : "");
    else
        return fmt("[%s]%s", kReg[rn], ofs.c_str());
}

static std::string regList(uint32_t mask, bool forceUser)
{
    std::string s = "{";
    bool first = true;
    for (int i = 0; i < 16; i++)
    {
        if (!(mask & (1<<i))) continue;
        if (!first) s += ",";
        s += kReg[i];
        first = false;
    }
    s += "}";
    if (forceUser) s += "^";
    return s;
}

DisasmResult DisassembleARM(uint32_t addr, uint32_t instr)
{
    DisasmResult r;
    r.Address  = addr;
    r.Encoding = instr;
    r.IsThumb  = false;
    r.Size     = 4;

    const char* c = kCond[instr>>28];

    if ((instr & 0xFD70F000) == 0xF550F000)
    {
        bool isImm = !((instr >> 25) & 1);
        r.Instruction = fmt("PLD %s", armAddr(instr, isImm).c_str());
        return r;
    }

    if ((instr & 0xFE000000) == 0xFA000000)
    {
        int32_t off = ((instr & 0xFFFFFF) << 2) | (((instr>>24)&1)<<1);
        if (off & (1<<25)) off |= (int32_t)0xFC000000;
        r.Instruction = fmt("BLX 0x%08X", addr+8+(uint32_t)off);
        return r;
    }

    if ((instr & 0x0E000000) == 0x0A000000)
    {
        int32_t off = (instr & 0xFFFFFF);
        if (off & 0x800000) off |= (int32_t)0xFF000000;
        off <<= 2;
        bool bl = (instr>>24)&1;
        r.Instruction = fmt("B%s%s 0x%08X", bl?"L":"", c, addr+8+(uint32_t)off);
        return r;
    }

    if ((instr & 0x0E000000) == 0x08000000)
    {
        bool ld = (instr>>20)&1, wb = (instr>>21)&1, fu = (instr>>22)&1;
        constexpr const char* am[] = {"DA","IA","DB","IB"};
        r.Instruction = fmt("%s%s%s %s%s, %s",
            ld?"LDM":"STM", c, am[(instr>>23)&3],
            kReg[(instr>>16)&0xF], wb?"!":"",
            regList(instr&0xFFFF, fu).c_str());
        return r;
    }

    if ((instr & 0x0FC000F0) == 0x00000090)
    {
        bool acc = (instr>>21)&1, s = (instr>>20)&1;
        int rd=(instr>>16)&0xF, rn=(instr>>12)&0xF, rs=(instr>>8)&0xF, rm=instr&0xF;
        if (acc)
            r.Instruction = fmt("MLA%s%s %s,%s,%s,%s",c,s?"S":"",kReg[rd],kReg[rm],kReg[rs],kReg[rn]);
        else
            r.Instruction = fmt("MUL%s%s %s,%s,%s",c,s?"S":"",kReg[rd],kReg[rm],kReg[rs]);
        return r;
    }

    if ((instr & 0x0F8000F0) == 0x00800090)
    {
        bool sgn=(instr>>22)&1, acc=(instr>>21)&1, s=(instr>>20)&1;
        int hi=(instr>>16)&0xF, lo=(instr>>12)&0xF, rs=(instr>>8)&0xF, rm=instr&0xF;
        r.Instruction = fmt("%s%s%s%s %s,%s,%s,%s",
            sgn?"S":"U", acc?"MLAL":"MULL", c, s?"S":"",
            kReg[lo],kReg[hi],kReg[rm],kReg[rs]);
        return r;
    }

    if ((instr & 0x0FB00FF0) == 0x01000090)
    {
        bool b=(instr>>22)&1;
        r.Instruction = fmt("SWP%s%s %s,%s,[%s]",c,b?"B":"",
            kReg[(instr>>12)&0xF],kReg[instr&0xF],kReg[(instr>>16)&0xF]);
        return r;
    }

    if ((instr & 0x0FFFFFF0) == 0x012FFF10)
    {
        r.Instruction = fmt("BX%s %s",c,kReg[instr&0xF]);
        return r;
    }
    if ((instr & 0x0FFFFFF0) == 0x012FFF30)
    {
        r.Instruction = fmt("BLX%s %s",c,kReg[instr&0xF]);
        return r;
    }

    if ((instr & 0xFFF000F0) == 0xE1200070)
    {
        uint32_t imm = ((instr >> 8) & 0xFFF) << 4 | (instr & 0xF);
        r.Instruction = fmt("BKPT #0x%04X", imm);
        return r;
    }

    if ((instr & 0x0FFF0FF0) == 0x016F0F10)
    {
        r.Instruction = fmt("CLZ%s %s,%s",c,kReg[(instr>>12)&0xF],kReg[instr&0xF]);
        return r;
    }

    if ((instr & 0x0FBF0FFF) == 0x010F0000)
    {
        bool spsr=(instr>>22)&1;
        r.Instruction = fmt("MRS%s %s,%s",c,kReg[(instr>>12)&0xF],spsr?"SPSR":"CPSR");
        return r;
    }

    if ((instr & 0x0DB0F000) == 0x0120F000)
    {
        bool spsr=(instr>>22)&1, imm=(instr>>25)&1;
        uint32_t fm=(instr>>16)&0xF;
        std::string fs;
        if(fm&1) fs+="C";
        if(fm&2) fs+="X";
        if(fm&4) fs+="S";
        if(fm&8) fs+="F";
        if (imm)
        {
            r.Instruction = fmt("MSR%s %s_%s,#0x%X",c,spsr?"SPSR":"CPSR",fs.c_str(),rotImm(instr));
        }
        else
        {
            r.Instruction = fmt("MSR%s %s_%s,%s",c,spsr?"SPSR":"CPSR",fs.c_str(),kReg[instr&0xF]);
        }
        return r;
    }

    if ((instr & 0x0F9000F0) == 0x01000050)
    {
        constexpr const char* qops[] = {"QADD","QSUB","QDADD","QDSUB"};
        r.Instruction = fmt("%s%s %s,%s,%s",
            qops[(instr>>21)&3], c,
            kReg[(instr>>12)&0xF], kReg[instr&0xF], kReg[(instr>>16)&0xF]);
        return r;
    }

    if ((instr & 0x0FF00090) == 0x01000080)
    {
        int x=(instr>>5)&1, y=(instr>>6)&1;
        r.Instruction = fmt("SMLA%s%s%s %s,%s,%s,%s",
            x?"T":"B", y?"T":"B", c,
            kReg[(instr>>16)&0xF], kReg[instr&0xF],
            kReg[(instr>>8)&0xF], kReg[(instr>>12)&0xF]);
        return r;
    }

    if ((instr & 0x0FF00090) == 0x01200080)
    {
        int y=(instr>>6)&1;
        bool smulw=(instr>>5)&1;
        if (smulw)
            r.Instruction = fmt("SMULW%s%s %s,%s,%s",
                y?"T":"B", c,
                kReg[(instr>>16)&0xF], kReg[instr&0xF], kReg[(instr>>8)&0xF]);
        else
            r.Instruction = fmt("SMLAW%s%s %s,%s,%s,%s",
                y?"T":"B", c,
                kReg[(instr>>16)&0xF], kReg[instr&0xF],
                kReg[(instr>>8)&0xF], kReg[(instr>>12)&0xF]);
        return r;
    }

    if ((instr & 0x0FF00090) == 0x01400080)
    {
        int x=(instr>>5)&1, y=(instr>>6)&1;
        r.Instruction = fmt("SMLAL%s%s%s %s,%s,%s,%s",
            x?"T":"B", y?"T":"B", c,
            kReg[(instr>>12)&0xF], kReg[(instr>>16)&0xF],
            kReg[instr&0xF], kReg[(instr>>8)&0xF]);
        return r;
    }

    if ((instr & 0x0FF00090) == 0x01600080)
    {
        int x=(instr>>5)&1, y=(instr>>6)&1;
        r.Instruction = fmt("SMUL%s%s%s %s,%s,%s",
            x?"T":"B", y?"T":"B", c,
            kReg[(instr>>16)&0xF], kReg[instr&0xF], kReg[(instr>>8)&0xF]);
        return r;
    }

    if ((instr & 0x0E000090) == 0x00000090 && (instr & 0x60))
    {
        int op=(instr>>5)&3;
        if (op)
        {
            bool ld=(instr>>20)&1;
            if (op == 2 && !ld)
            {
                r.Instruction = fmt("LDR%sD %s,%s",c,
                    kReg[(instr>>12)&0xF], armAddrH(instr).c_str());
            }
            else if (op == 3 && !ld)
            {
                r.Instruction = fmt("STR%sD %s,%s",c,
                    kReg[(instr>>12)&0xF], armAddrH(instr).c_str());
            }
            else
            {
                constexpr const char* hn[] = {"","H","SB","SH"};
                r.Instruction = fmt("%s%s%s %s,%s",ld?"LDR":"STR",c,hn[op],
                    kReg[(instr>>12)&0xF], armAddrH(instr).c_str());
            }
            return r;
        }
    }

    if ((instr & 0x0F000000) == 0x0F000000)
    {
        r.Instruction = fmt("SVC%s #0x%X",c,instr&0xFFFFFF);
        return r;
    }

    if ((instr & 0x0FF00000) == 0x0C400000)
    {
        r.Instruction = fmt("MCRR%s p%u,%u,%s,%s,C%u", c,
            (instr>>8)&0xF, (instr>>4)&0xF,
            kReg[(instr>>12)&0xF], kReg[(instr>>16)&0xF], instr&0xF);
        return r;
    }

    if ((instr & 0x0FF00000) == 0x0C500000)
    {
        r.Instruction = fmt("MRRC%s p%u,%u,%s,%s,C%u", c,
            (instr>>8)&0xF, (instr>>4)&0xF,
            kReg[(instr>>12)&0xF], kReg[(instr>>16)&0xF], instr&0xF);
        return r;
    }

    if ((instr & 0x0E000000) == 0x0C000000)
    {
        bool ld=(instr>>20)&1, n=(instr>>22)&1;
        bool pre=(instr>>24)&1, up=(instr>>23)&1, wb=(instr>>21)&1;
        uint32_t off=(instr&0xFF)<<2;
        int cp=(instr>>8)&0xF, crd=(instr>>12)&0xF, rn=(instr>>16)&0xF;
        std::string adr;
        if (pre)
        {
            if (off) adr = fmt("[%s,#%s%u]%s",kReg[rn],up?"":"-",off,wb?"!":"");
            else     adr = fmt("[%s]%s",kReg[rn],wb?"!":"");
        }
        else
        {
            if (wb)  adr = fmt("[%s],#%s%u",kReg[rn],up?"":"-",off);
            else     adr = fmt("[%s],{%u}",kReg[rn],instr&0xFF);
        }
        r.Instruction = fmt("%s%s%s p%u,C%u,%s",
            ld?"LDC":"STC", c, n?"L":"", cp, crd, adr.c_str());
        return r;
    }

    if ((instr & 0x0F000010) == 0x0E000000)
    {
        r.Instruction = fmt("CDP%s p%u,%u,C%u,C%u,C%u,%u", c,
            (instr>>8)&0xF, (instr>>20)&0xF,
            (instr>>12)&0xF, (instr>>16)&0xF, instr&0xF, (instr>>5)&7);
        return r;
    }

    if ((instr & 0x0F000010) == 0x0E000010)
    {
        bool ld=(instr>>20)&1;
        r.Instruction = fmt("%s%s p%u,%u,%s,C%u,C%u,%u",
            ld?"MRC":"MCR",c, (instr>>8)&0xF, (instr>>21)&7,
            kReg[(instr>>12)&0xF], (instr>>16)&0xF, instr&0xF, (instr>>5)&7);
        return r;
    }

    if ((instr & 0x0C000000) == 0x00000000)
    {
        bool imm=(instr>>25)&1;
        int op=(instr>>21)&0xF, s=(instr>>20)&1;
        int rn=(instr>>16)&0xF, rd=(instr>>12)&0xF;
        bool noResult=(op>=8&&op<=11), noRn=(op==13||op==15);

        std::string o2 = armOp2(instr, imm);
        if (noResult)
            r.Instruction = fmt("%s%s %s,%s",kDP[op],c,kReg[rn],o2.c_str());
        else if (noRn)
            r.Instruction = fmt("%s%s%s %s,%s",kDP[op],c,s?"S":"",kReg[rd],o2.c_str());
        else
            r.Instruction = fmt("%s%s%s %s,%s,%s",kDP[op],c,s?"S":"",kReg[rd],kReg[rn],o2.c_str());
        return r;
    }

    if ((instr & 0x0C000000) == 0x04000000)
    {
        bool isImm=!((instr>>25)&1), b=(instr>>22)&1, ld=(instr>>20)&1;
        bool t=!(instr>>24&1) && (instr>>21&1);
        r.Instruction = fmt("%s%s%s%s %s,%s",ld?"LDR":"STR",c,b?"B":"",t?"T":"",
            kReg[(instr>>12)&0xF], armAddr(instr,isImm).c_str());
        return r;
    }

    r.Instruction = fmt("???%s 0x%08X",c,instr);
    return r;
}

DisasmResult DisassembleThumb(uint32_t addr, uint16_t instr, uint16_t next, bool& consumed32)
{
    DisasmResult r;
    r.Address  = addr;
    r.IsThumb  = true;
    r.Size     = 2;
    consumed32 = false;

    if ((instr & 0xF800) == 0xF000)
    {
        bool blx = (next & 0xF800) == 0xE800;
        bool bl  = (next & 0xF800) == 0xF800;
        if (bl || blx)
        {
            int32_t off = ((int32_t)(instr & 0x7FF) << 12);
            if (off & 0x400000) off |= (int32_t)0xFF800000;
            off += (next & 0x7FF) << 1;
            if (blx) off &= ~2;
            r.Encoding   = ((uint32_t)next << 16) | instr;
            r.Size       = 4;
            consumed32   = true;
            r.Instruction = fmt("%s 0x%08X", blx?"BLX":"BL", addr+4+(uint32_t)off);
            return r;
        }
    }

    r.Encoding = instr;

    if ((instr & 0xE000) == 0x0000 && (instr & 0x1800) != 0x1800)
    {
        int op=(instr>>11)&3;
        if (op < 3)
        {
            int amt=(instr>>6)&0x1F;
            r.Instruction = fmt("%s R%u,R%u,#%d",kShift[op],(instr)&7,(instr>>3)&7,amt?amt:32);
            return r;
        }
    }

    if ((instr & 0xF800) == 0x1800)
    {
        bool sub=(instr>>9)&1, imm=(instr>>10)&1;
        std::string operand = imm ? fmt("#%u",(instr>>6)&7) : fmt("R%u",(instr>>6)&7);
        r.Instruction = fmt("%s R%u,R%u,%s",sub?"SUB":"ADD",(instr)&7,(instr>>3)&7,operand.c_str());
        return r;
    }

    if ((instr & 0xE000) == 0x2000)
    {
        constexpr const char* op4[] = {"MOV","CMP","ADD","SUB"};
        r.Instruction = fmt("%s R%u,#%u",op4[(instr>>11)&3],(instr>>8)&7,instr&0xFF);
        return r;
    }

    if ((instr & 0xFC00) == 0x4000)
    {
        constexpr const char* alus[] = {
            "AND","EOR","LSL","LSR","ASR","ADC","SBC","ROR",
            "TST","NEG","CMP","CMN","ORR","MUL","BIC","MVN"
        };
        r.Instruction = fmt("%s R%u,R%u",alus[(instr>>6)&0xF],instr&7,(instr>>3)&7);
        return r;
    }

    if ((instr & 0xFC00) == 0x4400)
    {
        int op=(instr>>8)&3;
        int rs=((instr>>3)&7)|((instr>>6)&8);
        int rd=(instr&7)|((instr>>4)&8);
        constexpr const char* hops[] = {"ADD","CMP","MOV"};
        if (op < 3)
        {
            r.Instruction = fmt("%s %s,%s",hops[op],kReg[rd],kReg[rs]);
        }
        else
        {
            r.Instruction = fmt("%s %s",(rd&8)?"BLX":"BX",kReg[rs]);
        }
        return r;
    }

    if ((instr & 0xF800) == 0x4800)
    {
        uint32_t off = (instr&0xFF)<<2;
        r.Instruction = fmt("LDR R%u,[PC,#%u]",( instr>>8)&7, off);
        return r;
    }

    if ((instr & 0xF000) == 0x5000)
    {
        constexpr const char* ls8[] = {"STR","STRH","STRB","LDRSB","LDR","LDRH","LDRB","LDRSH"};
        r.Instruction = fmt("%s R%u,[R%u,R%u]",ls8[(instr>>9)&7],instr&7,(instr>>3)&7,(instr>>6)&7);
        return r;
    }

    if ((instr & 0xE000) == 0x6000)
    {
        bool b=(instr>>12)&1, ld=(instr>>11)&1;
        uint32_t off=(instr>>6)&0x1F; if(!b) off<<=2;
        r.Instruction = fmt("%s%s R%u,[R%u,#%u]",ld?"LDR":"STR",b?"B":"",instr&7,(instr>>3)&7,off);
        return r;
    }

    if ((instr & 0xF000) == 0x8000)
    {
        bool ld=(instr>>11)&1;
        uint32_t off=((instr>>6)&0x1F)<<1;
        r.Instruction = fmt("%s R%u,[R%u,#%u]",ld?"LDRH":"STRH",instr&7,(instr>>3)&7,off);
        return r;
    }

    if ((instr & 0xF000) == 0x9000)
    {
        bool ld=(instr>>11)&1;
        r.Instruction = fmt("%s R%u,[SP,#%u]",ld?"LDR":"STR",(instr>>8)&7,(instr&0xFF)<<2);
        return r;
    }

    if ((instr & 0xF000) == 0xA000)
    {
        bool sp=(instr>>11)&1;
        r.Instruction = fmt("ADD R%u,%s,#%u",(instr>>8)&7,sp?"SP":"PC",(instr&0xFF)<<2);
        return r;
    }

    if ((instr & 0xF600) == 0xB400)
    {
        bool pop=(instr>>11)&1, extra=(instr>>8)&1;
        uint32_t rl=instr&0xFF;
        std::string s="{";
        bool first=true;
        for(int i=0;i<8;i++){if(!(rl&(1<<i)))continue;if(!first)s+=",";s+=std::string("R")+char('0'+i);first=false;}
        if(extra){if(!first)s+=",";s+=pop?"PC":"LR";}
        s+="}";
        r.Instruction = fmt("%s %s",pop?"POP":"PUSH",s.c_str());
        return r;
    }

    if ((instr & 0xFF00) == 0xB000)
    {
        bool sub=(instr>>7)&1;
        r.Instruction = fmt("%s SP,#%u",sub?"SUB":"ADD",(instr&0x7F)<<2);
        return r;
    }

    if ((instr & 0xFF00) == 0xBE00)
    {
        r.Instruction = fmt("BKPT #0x%02X",instr&0xFF);
        return r;
    }

    if ((instr & 0xF000) == 0xC000)
    {
        bool ld=(instr>>11)&1;
        std::string rl="{";bool first=true;
        for(int i=0;i<8;i++){if(!(instr&(1<<i)))continue;if(!first)rl+=",";rl+=std::string("R")+char('0'+i);first=false;}
        rl+="}";
        r.Instruction = fmt("%sIA R%u!,%s",ld?"LDM":"STM",(instr>>8)&7,rl.c_str());
        return r;
    }

    if ((instr & 0xF000) == 0xD000)
    {
        int cond=(instr>>8)&0xF;
        if (cond==0xF)
        {
            r.Instruction = fmt("SVC #0x%02X",instr&0xFF);
        }
        else
        {
            int32_t off=(int8_t)(instr&0xFF);
            r.Instruction = fmt("B%s 0x%08X",kCond[cond],addr+4+(uint32_t)(off*2));
        }
        return r;
    }

    if ((instr & 0xF800) == 0xE000)
    {
        int32_t off=(instr&0x7FF); if(off&0x400)off|=(int32_t)0xFFFFF800;
        r.Instruction = fmt("B 0x%08X",addr+4+(uint32_t)(off*2));
        return r;
    }

    r.Instruction = fmt("??? 0x%04X",instr);
    return r;
}

std::vector<DisasmResult> DisassembleRange(
    uint32_t startAddr, int count, bool isThumb,
    std::function<uint32_t(uint32_t)> readWord,
    std::function<uint16_t(uint32_t)> readHalf)
{
    std::vector<DisasmResult> out;
    out.reserve(count);
    uint32_t addr = startAddr;

    for (int i = 0; i < count; i++)
    {
        if (isThumb)
        {
            uint16_t h0 = readHalf(addr);
            uint16_t h1 = readHalf(addr + 2);
            bool ate32 = false;
            DisasmResult dr = DisassembleThumb(addr, h0, h1, ate32);
            out.push_back(dr);
            addr += ate32 ? 4 : 2;
        }
        else
        {
            out.push_back(DisassembleARM(addr, readWord(addr)));
            addr += 4;
        }
    }
    return out;
}

} // namespace ARMDisassembler
