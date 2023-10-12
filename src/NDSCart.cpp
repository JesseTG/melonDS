/*
    Copyright 2016-2022 melonDS team

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

#include <string.h>
#include "NDS.h"
#include "DSi.h"
#include "NDSCart.h"
#include "ARM.h"
#include "CRC32.h"
#include "DSi_AES.h"
#include "Platform.h"
#include "ROMList.h"
#include "melonDLDI.h"
#include "xxhash/xxhash.h"

using Platform::Log;
using Platform::LogLevel;

namespace NDSCart
{

// SRAM TODO: emulate write delays???

u16 SPICnt;
u32 ROMCnt;

u8 SPIData;
u32 SPIDataPos;
bool SPIHold;

u8 ROMCommand[8];
u32 ROMData;

u8 TransferData[0x4000];
u32 TransferPos;
u32 TransferLen;
u32 TransferDir;
u8 TransferCmd[8];

std::unique_ptr<CartCommon> Cart;

u32 Key1_KeyBuf[0x412];

u64 Key2_X;
u64 Key2_Y;


u32 ByteSwap(u32 val)
{
    return (val >> 24) | ((val >> 8) & 0xFF00) | ((val << 8) & 0xFF0000) | (val << 24);
}

void Key1_Encrypt(u32* data)
{
    u32 y = data[0];
    u32 x = data[1];
    u32 z;

    for (u32 i = 0x0; i <= 0xF; i++)
    {
        z = Key1_KeyBuf[i] ^ x;
        x =  Key1_KeyBuf[0x012 +  (z >> 24)        ];
        x += Key1_KeyBuf[0x112 + ((z >> 16) & 0xFF)];
        x ^= Key1_KeyBuf[0x212 + ((z >>  8) & 0xFF)];
        x += Key1_KeyBuf[0x312 +  (z        & 0xFF)];
        x ^= y;
        y = z;
    }

    data[0] = x ^ Key1_KeyBuf[0x10];
    data[1] = y ^ Key1_KeyBuf[0x11];
}

void Key1_Decrypt(u32* data)
{
    u32 y = data[0];
    u32 x = data[1];
    u32 z;

    for (u32 i = 0x11; i >= 0x2; i--)
    {
        z = Key1_KeyBuf[i] ^ x;
        x =  Key1_KeyBuf[0x012 +  (z >> 24)        ];
        x += Key1_KeyBuf[0x112 + ((z >> 16) & 0xFF)];
        x ^= Key1_KeyBuf[0x212 + ((z >>  8) & 0xFF)];
        x += Key1_KeyBuf[0x312 +  (z        & 0xFF)];
        x ^= y;
        y = z;
    }

    data[0] = x ^ Key1_KeyBuf[0x1];
    data[1] = y ^ Key1_KeyBuf[0x0];
}

void Key1_ApplyKeycode(u32* keycode, u32 mod)
{
    Key1_Encrypt(&keycode[1]);
    Key1_Encrypt(&keycode[0]);

    u32 temp[2] = {0,0};

    for (u32 i = 0; i <= 0x11; i++)
    {
        Key1_KeyBuf[i] ^= ByteSwap(keycode[i % mod]);
    }
    for (u32 i = 0; i <= 0x410; i+=2)
    {
        Key1_Encrypt(temp);
        Key1_KeyBuf[i  ] = temp[1];
        Key1_KeyBuf[i+1] = temp[0];
    }
}

void Key1_LoadKeyBuf(bool dsi, bool externalBios, u8 *bios, u32 biosLength)
{
    if (externalBios)
    {
        u32 expected_bios_length = dsi ? 0x10000 : 0x4000;
        if (biosLength != expected_bios_length)
        {
            Platform::Log(LogLevel::Error, "NDSCart: Expected an ARM7 BIOS of %u bytes, got %u bytes\n", expected_bios_length, biosLength);
        }
        else if (bios == nullptr)
        {
            Platform::Log(LogLevel::Error, "NDSCart: Expected an ARM7 BIOS of %u bytes, got nullptr\n", expected_bios_length);
        }
        else
        {
            memcpy(Key1_KeyBuf, bios + (dsi ? 0xC6D0 : 0x0030), 0x1048);
            Platform::Log(LogLevel::Debug, "NDSCart: Initialized Key1_KeyBuf from memory\n");
        }
    }
    else
    {
        // well
        memset(Key1_KeyBuf, 0, 0x1048);
        Platform::Log(LogLevel::Debug, "NDSCart: Initialized Key1_KeyBuf to zero\n");
    }
}

void Key1_InitKeycode(bool dsi, u32 idcode, u32 level, u32 mod, u8 *bios, u32 biosLength)
{
    Key1_LoadKeyBuf(dsi, Platform::GetConfigBool(Platform::ExternalBIOSEnable), bios, biosLength);

    u32 keycode[3] = {idcode, idcode>>1, idcode<<1};
    if (level >= 1) Key1_ApplyKeycode(keycode, mod);
    if (level >= 2) Key1_ApplyKeycode(keycode, mod);
    if (level >= 3)
    {
        keycode[1] <<= 1;
        keycode[2] >>= 1;
        Key1_ApplyKeycode(keycode, mod);
    }
}


void Key2_Encrypt(u8* data, u32 len)
{
    for (u32 i = 0; i < len; i++)
    {
        Key2_X = (((Key2_X >> 5) ^
                   (Key2_X >> 17) ^
                   (Key2_X >> 18) ^
                   (Key2_X >> 31)) & 0xFF)
                 + (Key2_X << 8);
        Key2_Y = (((Key2_Y >> 5) ^
                   (Key2_Y >> 23) ^
                   (Key2_Y >> 18) ^
                   (Key2_Y >> 31)) & 0xFF)
                 + (Key2_Y << 8);

        Key2_X &= 0x0000007FFFFFFFFFULL;
        Key2_Y &= 0x0000007FFFFFFFFFULL;
    }
}


CartCommon::CartCommon(u8* rom, u32 len, u32 chipid, bool badDSiDump, ROMListEntry romparams)
{
    ROM = rom;
    ROMLength = len;
    ChipID = chipid;
    ROMParams = romparams;

    memcpy(&Header, rom, sizeof(Header));
    IsDSi = Header.IsDSi() && !badDSiDump;
    DSiBase = Header.DSiRegionStart << 19;
}

CartCommon::~CartCommon()
{
    delete[] ROM;
}

u32 CartCommon::Checksum() const
{
    const NDSHeader& header = GetHeader();
    u32 crc = CRC32(ROM, 0x40);

    crc = CRC32(&ROM[header.ARM9ROMOffset], header.ARM9Size, crc);
    crc = CRC32(&ROM[header.ARM7ROMOffset], header.ARM7Size, crc);

    if (IsDSi)
    {
        crc = CRC32(&ROM[header.DSiARM9iROMOffset], header.DSiARM9iSize, crc);
        crc = CRC32(&ROM[header.DSiARM7iROMOffset], header.DSiARM7iSize, crc);
    }

    return crc;
}

void CartCommon::Reset()
{
    CmdEncMode = 0;
    DataEncMode = 0;
    DSiMode = false;
}

void CartCommon::SetupDirectBoot(const std::string& romname)
{
    CmdEncMode = 2;
    DataEncMode = 2;
    DSiMode = IsDSi && (NDS::ConsoleType==1);
}

void CartCommon::DoSavestate(Savestate* file)
{
    file->Section("NDCS");

    file->Var32(&CmdEncMode);
    file->Var32(&DataEncMode);
    file->Bool32(&DSiMode);
}

void CartCommon::SetupSave(u32 type)
{
}

void CartCommon::LoadSave(const u8* savedata, u32 savelen)
{
}

int CartCommon::ROMCommandStart(u8* cmd, u8* data, u32 len)
{
    if (CmdEncMode == 0)
    {
        switch (cmd[0])
        {
        case 0x9F:
            memset(data, 0xFF, len);
            return 0;

        case 0x00:
            memset(data, 0, len);
            if (len > 0x1000)
            {
                ReadROM(0, 0x1000, data, 0);
                for (u32 pos = 0x1000; pos < len; pos += 0x1000)
                    memcpy(data+pos, data, 0x1000);
            }
            else
                ReadROM(0, len, data, 0);
            return 0;

        case 0x90:
            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = ChipID;
            return 0;

        case 0x3C:
            CmdEncMode = 1;
            Key1_InitKeycode(false, *(u32*)&ROM[0xC], 2, 2, NDS::ARM7BIOS, sizeof(NDS::ARM7BIOS));
            DSiMode = false;
            return 0;

        case 0x3D:
            if (IsDSi)
            {
                CmdEncMode = 1;
                Key1_InitKeycode(true, *(u32*)&ROM[0xC], 1, 2, DSi::ARM7iBIOS, sizeof(DSi::ARM7iBIOS));
                DSiMode = true;
            }
            return 0;

        default:
            return 0;
        }
    }
    else if (CmdEncMode == 1)
    {
        // decrypt the KEY1 command as needed
        // (KEY2 commands do not need decrypted because KEY2 is handled entirely by hardware,
        // but KEY1 is not, so DS software is responsible for encrypting KEY1 commands)
        u8 cmddec[8];
        *(u32*)&cmddec[0] = ByteSwap(*(u32*)&cmd[4]);
        *(u32*)&cmddec[4] = ByteSwap(*(u32*)&cmd[0]);
        Key1_Decrypt((u32*)cmddec);
        u32 tmp = ByteSwap(*(u32*)&cmddec[4]);
        *(u32*)&cmddec[4] = ByteSwap(*(u32*)&cmddec[0]);
        *(u32*)&cmddec[0] = tmp;

        // TODO eventually: verify all the command parameters and shit

        switch (cmddec[0] & 0xF0)
        {
        case 0x40:
            DataEncMode = 2;
            return 0;

        case 0x10:
            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = ChipID;
            return 0;

        case 0x20:
            {
                u32 addr = (cmddec[2] & 0xF0) << 8;
                if (DSiMode)
                {
                    // the DSi region starts with 0x3000 unreadable bytes
                    // similarly to how the DS region starts at 0x1000 with 0x3000 unreadable bytes
                    // these contain data for KEY1 crypto
                    addr -= 0x1000;
                    addr += DSiBase;
                }
                ReadROM(addr, 0x1000, data, 0);
            }
            return 0;

        case 0xA0:
            CmdEncMode = 2;
            return 0;

        default:
            return 0;
        }
    }
    else if (CmdEncMode == 2)
    {
        switch (cmd[0])
        {
        case 0xB8:
            for (u32 pos = 0; pos < len; pos += 4)
                *(u32*)&data[pos] = ChipID;
            return 0;

        default:
            return 0;
        }
    }

    return 0;
}

void CartCommon::ROMCommandFinish(u8* cmd, u8* data, u32 len)
{
}

u8 CartCommon::SPIWrite(u8 val, u32 pos, bool last)
{
    return 0xFF;
}

void CartCommon::SetIRQ()
{
    NDS::SetIRQ(0, NDS::IRQ_CartIREQMC);
    NDS::SetIRQ(1, NDS::IRQ_CartIREQMC);
}

u8 *CartCommon::GetSaveMemory() const
{
    return nullptr;
}

u32 CartCommon::GetSaveMemoryLength() const
{
    return 0;
}

void CartCommon::ReadROM(u32 addr, u32 len, u8* data, u32 offset)
{
    if (addr >= ROMLength) return;
    if ((addr+len) > ROMLength)
        len = ROMLength - addr;

    memcpy(data+offset, ROM+addr, len);
}

const NDSBanner* CartCommon::Banner() const
{
    const NDSHeader& header = GetHeader();
    size_t bannersize = header.IsDSi() ? 0x23C0 : 0xA40;
    if (header.BannerOffset >= 0x200 && header.BannerOffset < (ROMLength - bannersize))
    {
        return reinterpret_cast<const NDSBanner*>(ROM + header.BannerOffset);
    }

    return nullptr;
}

CartRetail::CartRetail(u8* rom, u32 len, u32 chipid, bool badDSiDump, ROMListEntry romparams) : CartCommon(rom, len, chipid, badDSiDump, romparams)
{
    SRAM = nullptr;
}

CartRetail::~CartRetail()
{
    if (SRAM) delete[] SRAM;
}

void CartRetail::Reset()
{
    CartCommon::Reset();

    SRAMCmd = 0;
    SRAMAddr = 0;
    SRAMStatus = 0;
}

void CartRetail::DoSavestate(Savestate* file)
{
    CartCommon::DoSavestate(file);

    // we reload the SRAM contents.
    // it should be the same file, but the contents may change

    u32 oldlen = SRAMLength;

    file->Var32(&SRAMLength);
    if (SRAMLength != oldlen)
    {
        Log(LogLevel::Warn, "savestate: VERY BAD!!!! SRAM LENGTH DIFFERENT. %d -> %d\n", oldlen, SRAMLength);
        Log(LogLevel::Warn, "oh well. loading it anyway. adsfgdsf\n");

        if (oldlen) delete[] SRAM;
        SRAM = nullptr;
        if (SRAMLength) SRAM = new u8[SRAMLength];
    }
    if (SRAMLength)
    {
        file->VarArray(SRAM, SRAMLength);
    }

    // SPI status shito

    file->Var8(&SRAMCmd);
    file->Var32(&SRAMAddr);
    file->Var8(&SRAMStatus);

    if ((!file->Saving) && SRAM)
        Platform::WriteNDSSave(SRAM, SRAMLength, 0, SRAMLength);
}

void CartRetail::SetupSave(u32 type)
{
    if (SRAM) delete[] SRAM;
    SRAM = nullptr;

    if (type > 10) type = 0;
    int sramlen[] =
    {
        0,
        512,
        8192, 65536, 128*1024,
        256*1024, 512*1024, 1024*1024,
        8192*1024, 16384*1024, 65536*1024
    };
    SRAMLength = sramlen[type];

    if (SRAMLength)
    {
        SRAM = new u8[SRAMLength];
        memset(SRAM, 0xFF, SRAMLength);
    }

    switch (type)
    {
    case 1: SRAMType = 1; break; // EEPROM, small
    case 2:
    case 3:
    case 4: SRAMType = 2; break; // EEPROM, regular
    case 5:
    case 6:
    case 7: SRAMType = 3; break; // FLASH
    case 8:
    case 9:
    case 10: SRAMType = 4; break; // NAND
    default: SRAMType = 0; break; // ...whatever else
    }
}

void CartRetail::LoadSave(const u8* savedata, u32 savelen)
{
    if (!SRAM) return;

    u32 len = std::min(savelen, SRAMLength);
    memcpy(SRAM, savedata, len);
    Platform::WriteNDSSave(savedata, len, 0, len);
}

int CartRetail::ROMCommandStart(u8* cmd, u8* data, u32 len)
{
    if (CmdEncMode != 2) return CartCommon::ROMCommandStart(cmd, data, len);

    switch (cmd[0])
    {
    case 0xB7:
        {
            u32 addr = (cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4];
            memset(data, 0, len);

            if (((addr + len - 1) >> 12) != (addr >> 12))
            {
                u32 len1 = 0x1000 - (addr & 0xFFF);
                ReadROM_B7(addr, len1, data, 0);
                ReadROM_B7(addr+len1, len-len1, data, len1);
            }
            else
                ReadROM_B7(addr, len, data, 0);
        }
        return 0;

    default:
        return CartCommon::ROMCommandStart(cmd, data, len);
    }
}

u8 CartRetail::SPIWrite(u8 val, u32 pos, bool last)
{
    if (SRAMType == 0) return 0;

    if (pos == 0)
    {
        // handle generic commands with no parameters
        switch (val)
        {
        case 0x04: // write disable
            SRAMStatus &= ~(1<<1);
            return 0;
        case 0x06: // write enable
            SRAMStatus |= (1<<1);
            return 0;

        default:
            SRAMCmd = val;
            SRAMAddr = 0;
        }

        return 0xFF;
    }

    switch (SRAMType)
    {
    case 1: return SRAMWrite_EEPROMTiny(val, pos, last);
    case 2: return SRAMWrite_EEPROM(val, pos, last);
    case 3: return SRAMWrite_FLASH(val, pos, last);
    default: return 0xFF;
    }
}

u8 *CartRetail::GetSaveMemory() const
{
    return SRAM;
}

u32 CartRetail::GetSaveMemoryLength() const
{
    return SRAMLength;
}

void CartRetail::ReadROM_B7(u32 addr, u32 len, u8* data, u32 offset)
{
    addr &= (ROMLength-1);

    if (addr < 0x8000)
        addr = 0x8000 + (addr & 0x1FF);

    if (IsDSi && (addr >= DSiBase))
    {
        // for DSi carts:
        // * in DSi mode: block the first 0x3000 bytes of the DSi area
        // * in DS mode: block the entire DSi area

        if ((!DSiMode) || (addr < (DSiBase+0x3000)))
            addr = 0x8000 + (addr & 0x1FF);
    }

    memcpy(data+offset, ROM+addr, len);
}

u8 CartRetail::SRAMWrite_EEPROMTiny(u8 val, u32 pos, bool last)
{
    switch (SRAMCmd)
    {
    case 0x01: // write status register
        // TODO: WP bits should be nonvolatile!
        if (pos == 1)
            SRAMStatus = (SRAMStatus & 0x01) | (val & 0x0C);
        return 0;

    case 0x05: // read status register
        return SRAMStatus | 0xF0;

    case 0x02: // write low
    case 0x0A: // write high
        if (pos < 2)
        {
            SRAMAddr = val;
            SRAMFirstAddr = SRAMAddr;
        }
        else
        {
            // TODO: implement WP bits!
            if (SRAMStatus & (1<<1))
            {
                SRAM[(SRAMAddr + ((SRAMCmd==0x0A)?0x100:0)) & 0x1FF] = val;
            }
            SRAMAddr++;
        }
        if (last)
        {
            SRAMStatus &= ~(1<<1);
            Platform::WriteNDSSave(SRAM, SRAMLength,
                                   (SRAMFirstAddr + ((SRAMCmd==0x0A)?0x100:0)) & 0x1FF, SRAMAddr-SRAMFirstAddr);
        }
        return 0;

    case 0x03: // read low
    case 0x0B: // read high
        if (pos < 2)
        {
            SRAMAddr = val;
            return 0;
        }
        else
        {
            u8 ret = SRAM[(SRAMAddr + ((SRAMCmd==0x0B)?0x100:0)) & 0x1FF];
            SRAMAddr++;
            return ret;
        }

    case 0x9F: // read JEDEC ID
        return 0xFF;

    default:
        if (pos == 1)
            Log(LogLevel::Warn, "unknown tiny EEPROM save command %02X\n", SRAMCmd);
        return 0xFF;
    }
}

u8 CartRetail::SRAMWrite_EEPROM(u8 val, u32 pos, bool last)
{
    u32 addrsize = 2;
    if (SRAMLength > 65536) addrsize++;

    switch (SRAMCmd)
    {
    case 0x01: // write status register
        // TODO: WP bits should be nonvolatile!
        if (pos == 1)
            SRAMStatus = (SRAMStatus & 0x01) | (val & 0x0C);
        return 0;

    case 0x05: // read status register
        return SRAMStatus;

    case 0x02: // write
        if (pos <= addrsize)
        {
            SRAMAddr <<= 8;
            SRAMAddr |= val;
            SRAMFirstAddr = SRAMAddr;
        }
        else
        {
            // TODO: implement WP bits
            if (SRAMStatus & (1<<1))
            {
                SRAM[SRAMAddr & (SRAMLength-1)] = val;
            }
            SRAMAddr++;
        }
        if (last)
        {
            SRAMStatus &= ~(1<<1);
            Platform::WriteNDSSave(SRAM, SRAMLength,
                                   SRAMFirstAddr & (SRAMLength-1), SRAMAddr-SRAMFirstAddr);
        }
        return 0;

    case 0x03: // read
        if (pos <= addrsize)
        {
            SRAMAddr <<= 8;
            SRAMAddr |= val;
            return 0;
        }
        else
        {
            // TODO: size limit!!
            u8 ret = SRAM[SRAMAddr & (SRAMLength-1)];
            SRAMAddr++;
            return ret;
        }

    case 0x9F: // read JEDEC ID
        // TODO: GBAtek implies it's not always all FF (FRAM)
        return 0xFF;

    default:
        if (pos == 1)
            Log(LogLevel::Warn, "unknown EEPROM save command %02X\n", SRAMCmd);
        return 0xFF;
    }
}

u8 CartRetail::SRAMWrite_FLASH(u8 val, u32 pos, bool last)
{
    switch (SRAMCmd)
    {
    case 0x05: // read status register
        return SRAMStatus;

    case 0x02: // page program
        if (pos <= 3)
        {
            SRAMAddr <<= 8;
            SRAMAddr |= val;
            SRAMFirstAddr = SRAMAddr;
        }
        else
        {
            if (SRAMStatus & (1<<1))
            {
                // CHECKME: should it be &=~val ??
                SRAM[SRAMAddr & (SRAMLength-1)] = 0;
            }
            SRAMAddr++;
        }
        if (last)
        {
            SRAMStatus &= ~(1<<1);
            Platform::WriteNDSSave(SRAM, SRAMLength,
                                   SRAMFirstAddr & (SRAMLength-1), SRAMAddr-SRAMFirstAddr);
        }
        return 0;

    case 0x03: // read
        if (pos <= 3)
        {
            SRAMAddr <<= 8;
            SRAMAddr |= val;
            return 0;
        }
        else
        {
            u8 ret = SRAM[SRAMAddr & (SRAMLength-1)];
            SRAMAddr++;
            return ret;
        }

    case 0x0A: // page write
        if (pos <= 3)
        {
            SRAMAddr <<= 8;
            SRAMAddr |= val;
            SRAMFirstAddr = SRAMAddr;
        }
        else
        {
            if (SRAMStatus & (1<<1))
            {
                SRAM[SRAMAddr & (SRAMLength-1)] = val;
            }
            SRAMAddr++;
        }
        if (last)
        {
            SRAMStatus &= ~(1<<1);
            Platform::WriteNDSSave(SRAM, SRAMLength,
                                   SRAMFirstAddr & (SRAMLength-1), SRAMAddr-SRAMFirstAddr);
        }
        return 0;

    case 0x0B: // fast read
        if (pos <= 3)
        {
            SRAMAddr <<= 8;
            SRAMAddr |= val;
            return 0;
        }
        else if (pos == 4)
        {
            // dummy byte
            return 0;
        }
        else
        {
            u8 ret = SRAM[SRAMAddr & (SRAMLength-1)];
            SRAMAddr++;
            return ret;
        }

    case 0x9F: // read JEDEC IC
        // GBAtek says it should be 0xFF. verify?
        return 0xFF;

    case 0xD8: // sector erase
        if (pos <= 3)
        {
            SRAMAddr <<= 8;
            SRAMAddr |= val;
            SRAMFirstAddr = SRAMAddr;
        }
        if ((pos == 3) && (SRAMStatus & (1<<1)))
        {
            for (u32 i = 0; i < 0x10000; i++)
            {
                SRAM[SRAMAddr & (SRAMLength-1)] = 0;
                SRAMAddr++;
            }
        }
        if (last)
        {
            SRAMStatus &= ~(1<<1);
            Platform::WriteNDSSave(SRAM, SRAMLength,
                                   SRAMFirstAddr & (SRAMLength-1), SRAMAddr-SRAMFirstAddr);
        }
        return 0;

    case 0xDB: // page erase
        if (pos <= 3)
        {
            SRAMAddr <<= 8;
            SRAMAddr |= val;
            SRAMFirstAddr = SRAMAddr;
        }
        if ((pos == 3) && (SRAMStatus & (1<<1)))
        {
            for (u32 i = 0; i < 0x100; i++)
            {
                SRAM[SRAMAddr & (SRAMLength-1)] = 0;
                SRAMAddr++;
            }
        }
        if (last)
        {
            SRAMStatus &= ~(1<<1);
            Platform::WriteNDSSave(SRAM, SRAMLength,
                                   SRAMFirstAddr & (SRAMLength-1), SRAMAddr-SRAMFirstAddr);
        }
        return 0;

    default:
        if (pos == 1)
            Log(LogLevel::Warn, "unknown FLASH save command %02X\n", SRAMCmd);
        return 0xFF;
    }
}


CartRetailNAND::CartRetailNAND(u8* rom, u32 len, u32 chipid, ROMListEntry romparams) : CartRetail(rom, len, chipid, false, romparams)
{
}

CartRetailNAND::~CartRetailNAND()
{
}

void CartRetailNAND::Reset()
{
    CartRetail::Reset();

    SRAMAddr = 0;
    SRAMStatus = 0x20;
    SRAMWindow = 0;

    // ROM header 94/96 = SRAM addr start / 0x20000
    SRAMBase = *(u16*)&ROM[0x96] << 17;

    memset(SRAMWriteBuffer, 0, 0x800);
}

void CartRetailNAND::DoSavestate(Savestate* file)
{
    CartRetail::DoSavestate(file);

    file->Var32(&SRAMBase);
    file->Var32(&SRAMWindow);

    file->VarArray(SRAMWriteBuffer, 0x800);
    file->Var32(&SRAMWritePos);

    if (!file->Saving)
        BuildSRAMID();
}

void CartRetailNAND::LoadSave(const u8* savedata, u32 savelen)
{
    CartRetail::LoadSave(savedata, savelen);
    BuildSRAMID();
}

int CartRetailNAND::ROMCommandStart(u8* cmd, u8* data, u32 len)
{
    if (CmdEncMode != 2) return CartCommon::ROMCommandStart(cmd, data, len);

    switch (cmd[0])
    {
    case 0x81: // write data
        if ((SRAMStatus & (1<<4)) && SRAMWindow >= SRAMBase && SRAMWindow < (SRAMBase+SRAMLength))
        {
            u32 addr = (cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4];

            if (addr >= SRAMWindow && addr < (SRAMWindow+0x20000))
            {
                // the command is issued 4 times, each with the same address
                // seems they use the one from the first command (CHECKME)
                if (!SRAMAddr)
                    SRAMAddr = addr;
            }
        }
        else
            SRAMAddr = 0;
        return 1;

    case 0x82: // commit write
        if (SRAMAddr && SRAMWritePos)
        {
            if (SRAMLength && SRAMAddr < (SRAMBase+SRAMLength-0x20000))
            {
                memcpy(&SRAM[SRAMAddr - SRAMBase], SRAMWriteBuffer, 0x800);
                Platform::WriteNDSSave(SRAM, SRAMLength, SRAMAddr - SRAMBase, 0x800);
            }

            SRAMAddr = 0;
            SRAMWritePos = 0;
        }
        SRAMStatus &= ~(1<<4);
        return 0;

    case 0x84: // discard write buffer
        SRAMAddr = 0;
        SRAMWritePos = 0;
        return 0;

    case 0x85: // write enable
        if (SRAMWindow)
        {
            SRAMStatus |= (1<<4);
            SRAMWritePos = 0;
        }
        return 0;

    case 0x8B: // revert to ROM read mode
        SRAMWindow = 0;
        return 0;

    case 0x94: // return ID data
        {
            // TODO: check what the data really is. probably the NAND chip's ID.
            // also, might be different between different games or even between different carts.
            // this was taken from a Jam with the Band cart.
            u8 iddata[0x30] =
            {
                0xEC, 0xF1, 0x00, 0x95, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
            };

            if (SRAMLength) memcpy(&iddata[0x18], &SRAM[SRAMLength - 0x800], 16);

            memset(data, 0, len);
            memcpy(data, iddata, std::min(len, 0x30u));
        }
        return 0;

    case 0xB2: // set window for accessing SRAM
        {
            u32 addr = (cmd[1]<<24) | ((cmd[2]&0xFE)<<16);

            // window is 0x20000 bytes, address is aligned to that boundary
            // NAND remains stuck 'busy' forever if this is less than the starting SRAM address
            // TODO.
            if (addr < SRAMBase) Log(LogLevel::Warn,"NAND: !! BAD ADDR %08X < %08X\n", addr, SRAMBase);
            if (addr >= (SRAMBase+SRAMLength)) Log(LogLevel::Warn,"NAND: !! BAD ADDR %08X > %08X\n", addr, SRAMBase+SRAMLength);

            SRAMWindow = addr;
        }
        return 0;

    case 0xB7:
        {
            u32 addr = (cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4];

            if (SRAMWindow == 0)
            {
                // regular ROM mode
                memset(data, 0, len);

                if (((addr + len - 1) >> 12) != (addr >> 12))
                {
                    u32 len1 = 0x1000 - (addr & 0xFFF);
                    ReadROM_B7(addr, len1, data, 0);
                    ReadROM_B7(addr+len1, len-len1, data, len1);
                }
                else
                    ReadROM_B7(addr, len, data, 0);
            }
            else
            {
                // SRAM mode
                memset(data, 0xFF, len);

                if (SRAMWindow >= SRAMBase && SRAMWindow < (SRAMBase+SRAMLength) &&
                    addr >= SRAMWindow && addr < (SRAMWindow+0x20000))
                {
                    memcpy(data, &SRAM[addr - SRAMBase], len);
                }
            }
        }
        return 0;

    case 0xD6: // read NAND status
        {
            // status bits
            // bit5: ready
            // bit4: write enable

            for (u32 i = 0; i < len; i+=4)
                *(u32*)&data[i] = SRAMStatus * 0x01010101;
        }
        return 0;

    default:
        return CartRetail::ROMCommandStart(cmd, data, len);
    }
}

void CartRetailNAND::ROMCommandFinish(u8* cmd, u8* data, u32 len)
{
    if (CmdEncMode != 2) return CartCommon::ROMCommandFinish(cmd, data, len);

    switch (cmd[0])
    {
    case 0x81: // write data
        if (SRAMAddr)
        {
            if ((SRAMWritePos + len) > 0x800)
                len = 0x800 - SRAMWritePos;

            memcpy(&SRAMWriteBuffer[SRAMWritePos], data, len);
            SRAMWritePos += len;
        }
        return;

    default:
        return CartCommon::ROMCommandFinish(cmd, data, len);
    }
}

u8 CartRetailNAND::SPIWrite(u8 val, u32 pos, bool last)
{
    return 0xFF;
}

void CartRetailNAND::BuildSRAMID()
{
    // the last 128K of the SRAM are read-only.
    // most of it is FF, except for the NAND ID at the beginning
    // of the last 0x800 bytes.

    if (SRAMLength > 0x20000)
    {
        memset(&SRAM[SRAMLength - 0x20000], 0xFF, 0x20000);

        // TODO: check what the data is all about!
        // this was pulled from a Jam with the Band cart. may be different on other carts.
        // WarioWare DIY may have different data or not have this at all.
        // the ID data is also found in the response to command 94, and JwtB checks it.
        // WarioWare doesn't seem to care.
        // there is also more data here, but JwtB doesn't seem to care.
        u8 iddata[0x10] = {0xEC, 0x00, 0x9E, 0xA1, 0x51, 0x65, 0x34, 0x35, 0x30, 0x35, 0x30, 0x31, 0x19, 0x19, 0x02, 0x0A};
        memcpy(&SRAM[SRAMLength - 0x800], iddata, 16);
    }
}


CartRetailIR::CartRetailIR(u8* rom, u32 len, u32 chipid, u32 irversion, bool badDSiDump, ROMListEntry romparams) : CartRetail(rom, len, chipid, badDSiDump, romparams)
{
    IRVersion = irversion;
}

CartRetailIR::~CartRetailIR()
{
}

void CartRetailIR::Reset()
{
    CartRetail::Reset();

    IRCmd = 0;
}

void CartRetailIR::DoSavestate(Savestate* file)
{
    CartRetail::DoSavestate(file);

    file->Var8(&IRCmd);
}

u8 CartRetailIR::SPIWrite(u8 val, u32 pos, bool last)
{
    if (pos == 0)
    {
        IRCmd = val;
        return 0;
    }

    // TODO: emulate actual IR comm

    switch (IRCmd)
    {
    case 0x00: // pass-through
        return CartRetail::SPIWrite(val, pos-1, last);

    case 0x08: // ID
        return 0xAA;
    }

    return 0;
}


CartRetailBT::CartRetailBT(u8* rom, u32 len, u32 chipid, ROMListEntry romparams) : CartRetail(rom, len, chipid, false, romparams)
{
    Log(LogLevel::Info,"POKETYPE CART\n");
}

CartRetailBT::~CartRetailBT()
{
}

void CartRetailBT::Reset()
{
    CartRetail::Reset();
}

void CartRetailBT::DoSavestate(Savestate* file)
{
    CartRetail::DoSavestate(file);
}

u8 CartRetailBT::SPIWrite(u8 val, u32 pos, bool last)
{
    Log(LogLevel::Debug,"POKETYPE SPI: %02X %d %d - %08X\n", val, pos, last, NDS::GetPC(0));

    /*if (pos == 0)
    {
        // TODO do something with it??
        if(val==0xFF)SetIRQ();
    }
    if(pos==7)SetIRQ();*/

    return 0;
}


CartHomebrew::CartHomebrew(u8* rom, u32 len, u32 chipid, ROMListEntry romparams) : CartCommon(rom, len, chipid, false, romparams)
{
    SD = nullptr;
}

CartHomebrew::~CartHomebrew()
{
    if (SD)
    {
        delete SD;
    }
}

void CartHomebrew::Reset()
{
    CartCommon::Reset();

    ReadOnly = Platform::GetConfigBool(Platform::DLDI_ReadOnly);

    if (SD)
    {
        delete SD;
    }

    if (Platform::GetConfigBool(Platform::DLDI_Enable))
    {
        std::string folderpath;
        if (Platform::GetConfigBool(Platform::DLDI_FolderSync))
            folderpath = Platform::GetConfigString(Platform::DLDI_FolderPath);
        else
            folderpath = "";

        ApplyDLDIPatch(melonDLDI, sizeof(melonDLDI), ReadOnly);
        SD = new FATStorage(Platform::GetConfigString(Platform::DLDI_ImagePath),
                            (u64)Platform::GetConfigInt(Platform::DLDI_ImageSize) * 1024 * 1024,
                            ReadOnly,
                            folderpath);
    }
    else
        SD = nullptr;
}

void CartHomebrew::SetupDirectBoot(const std::string& romname)
{
    CartCommon::SetupDirectBoot(romname);

    if (SD)
    {
        // add the ROM to the SD volume

        if (!SD->InjectFile(romname, ROM, ROMLength))
            return;

        // setup argv command line

        char argv[512] = {0};
        int argvlen;

        strncpy(argv, "fat:/", 511);
        strncat(argv, romname.c_str(), 511);
        argvlen = strlen(argv);

        const NDSHeader& header = GetHeader();
        void (*writefn)(u32,u32) = (NDS::ConsoleType==1) ? DSi::ARM9Write32 : NDS::ARM9Write32;

        u32 argvbase = header.ARM9RAMAddress + header.ARM9Size;
        argvbase = (argvbase + 0xF) & ~0xF;

        for (u32 i = 0; i <= argvlen; i+=4)
            writefn(argvbase+i, *(u32*)&argv[i]);

        writefn(0x02FFFE70, 0x5F617267);
        writefn(0x02FFFE74, argvbase);
        writefn(0x02FFFE78, argvlen+1);
    }
}

void CartHomebrew::DoSavestate(Savestate* file)
{
    CartCommon::DoSavestate(file);
}

int CartHomebrew::ROMCommandStart(u8* cmd, u8* data, u32 len)
{
    if (CmdEncMode != 2) return CartCommon::ROMCommandStart(cmd, data, len);

    switch (cmd[0])
    {
    case 0xB7:
        {
            u32 addr = (cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4];
            memset(data, 0, len);

            if (((addr + len - 1) >> 12) != (addr >> 12))
            {
                u32 len1 = 0x1000 - (addr & 0xFFF);
                ReadROM_B7(addr, len1, data, 0);
                ReadROM_B7(addr+len1, len-len1, data, len1);
            }
            else
                ReadROM_B7(addr, len, data, 0);
        }
        return 0;

    case 0xC0: // SD read
        {
            u32 sector = (cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4];
            if (SD) SD->ReadSectors(sector, len>>9, data);
        }
        return 0;

    case 0xC1: // SD write
        return 1;

    default:
        return CartCommon::ROMCommandStart(cmd, data, len);
    }
}

void CartHomebrew::ROMCommandFinish(u8* cmd, u8* data, u32 len)
{
    if (CmdEncMode != 2) return CartCommon::ROMCommandFinish(cmd, data, len);

    // TODO: delayed SD writing? like we have for SRAM

    switch (cmd[0])
    {
    case 0xC1:
        {
            u32 sector = (cmd[1]<<24) | (cmd[2]<<16) | (cmd[3]<<8) | cmd[4];
            if (SD && (!ReadOnly)) SD->WriteSectors(sector, len>>9, data);
        }
        break;

    default:
        return CartCommon::ROMCommandFinish(cmd, data, len);
    }
}

void CartHomebrew::ApplyDLDIPatchAt(u8* binary, u32 dldioffset, const u8* patch, u32 patchlen, bool readonly)
{
    if (patch[0x0D] > binary[dldioffset+0x0F])
    {
        Log(LogLevel::Error, "DLDI driver ain't gonna fit, sorry\n");
        return;
    }

    Log(LogLevel::Info, "existing driver is: %s\n", &binary[dldioffset+0x10]);
    Log(LogLevel::Info, "new driver is: %s\n", &patch[0x10]);

    u32 memaddr = *(u32*)&binary[dldioffset+0x40];
    if (memaddr == 0)
        memaddr = *(u32*)&binary[dldioffset+0x68] - 0x80;

    u32 patchbase = *(u32*)&patch[0x40];
    u32 delta = memaddr - patchbase;

    u32 patchsize = 1 << patch[0x0D];
    u32 patchend = patchbase + patchsize;

    memcpy(&binary[dldioffset], patch, patchlen);

    *(u32*)&binary[dldioffset+0x40] += delta;
    *(u32*)&binary[dldioffset+0x44] += delta;
    *(u32*)&binary[dldioffset+0x48] += delta;
    *(u32*)&binary[dldioffset+0x4C] += delta;
    *(u32*)&binary[dldioffset+0x50] += delta;
    *(u32*)&binary[dldioffset+0x54] += delta;
    *(u32*)&binary[dldioffset+0x58] += delta;
    *(u32*)&binary[dldioffset+0x5C] += delta;

    *(u32*)&binary[dldioffset+0x68] += delta;
    *(u32*)&binary[dldioffset+0x6C] += delta;
    *(u32*)&binary[dldioffset+0x70] += delta;
    *(u32*)&binary[dldioffset+0x74] += delta;
    *(u32*)&binary[dldioffset+0x78] += delta;
    *(u32*)&binary[dldioffset+0x7C] += delta;

    u8 fixmask = patch[0x0E];

    if (fixmask & 0x01)
    {
        u32 fixstart = *(u32*)&patch[0x40] - patchbase;
        u32 fixend = *(u32*)&patch[0x44] - patchbase;

        for (u32 addr = fixstart; addr < fixend; addr+=4)
        {
            u32 val = *(u32*)&binary[dldioffset+addr];
            if (val >= patchbase && val < patchend)
                *(u32*)&binary[dldioffset+addr] += delta;
        }
    }
    if (fixmask & 0x02)
    {
        u32 fixstart = *(u32*)&patch[0x48] - patchbase;
        u32 fixend = *(u32*)&patch[0x4C] - patchbase;

        for (u32 addr = fixstart; addr < fixend; addr+=4)
        {
            u32 val = *(u32*)&binary[dldioffset+addr];
            if (val >= patchbase && val < patchend)
                *(u32*)&binary[dldioffset+addr] += delta;
        }
    }
    if (fixmask & 0x04)
    {
        u32 fixstart = *(u32*)&patch[0x50] - patchbase;
        u32 fixend = *(u32*)&patch[0x54] - patchbase;

        for (u32 addr = fixstart; addr < fixend; addr+=4)
        {
            u32 val = *(u32*)&binary[dldioffset+addr];
            if (val >= patchbase && val < patchend)
                *(u32*)&binary[dldioffset+addr] += delta;
        }
    }
    if (fixmask & 0x08)
    {
        u32 fixstart = *(u32*)&patch[0x58] - patchbase;
        u32 fixend = *(u32*)&patch[0x5C] - patchbase;

        memset(&binary[dldioffset+fixstart], 0, fixend-fixstart);
    }

    if (readonly)
    {
        // clear the can-write feature flag
        binary[dldioffset+0x64] &= ~0x02;

        // make writeSectors() return failure
        u32 writesec_addr = *(u32*)&binary[dldioffset+0x74];
        writesec_addr -= memaddr;
        writesec_addr += dldioffset;
        *(u32*)&binary[writesec_addr+0x00] = 0xE3A00000; // mov r0, #0
        *(u32*)&binary[writesec_addr+0x04] = 0xE12FFF1E; // bx lr
    }

    Log(LogLevel::Debug, "applied DLDI patch at %08X\n", dldioffset);
}

void CartHomebrew::ApplyDLDIPatch(const u8* patch, u32 patchlen, bool readonly)
{
    if (*(u32*)&patch[0] != 0xBF8DA5ED ||
        *(u32*)&patch[4] != 0x69684320 ||
        *(u32*)&patch[8] != 0x006D6873)
    {
        Log(LogLevel::Error, "bad DLDI patch\n");
        return;
    }

    u32 offset = *(u32*)&ROM[0x20];
    u32 size = *(u32*)&ROM[0x2C];

    u8* binary = &ROM[offset];

    for (u32 i = 0; i < size; )
    {
        if (*(u32*)&binary[i  ] == 0xBF8DA5ED &&
            *(u32*)&binary[i+4] == 0x69684320 &&
            *(u32*)&binary[i+8] == 0x006D6873)
        {
            Log(LogLevel::Debug, "DLDI structure found at %08X (%08X)\n", i, offset+i);
            ApplyDLDIPatchAt(binary, i, patch, patchlen, readonly);
            i += patchlen;
        }
        else
            i++;
    }
}

void CartHomebrew::ReadROM_B7(u32 addr, u32 len, u8* data, u32 offset)
{
    // TODO: how strict should this be for homebrew?

    addr &= (ROMLength-1);

    memcpy(data+offset, ROM+addr, len);
}



bool Init()
{
    Cart = nullptr;

    return true;
}

void DeInit()
{
    Cart = nullptr;
}

void Reset()
{
    ResetCart();
}

void DoSavestate(Savestate* file)
{
    file->Section("NDSC");

    file->Var16(&SPICnt);
    file->Var32(&ROMCnt);

    file->Var8(&SPIData);
    file->Var32(&SPIDataPos);
    file->Bool32(&SPIHold);

    file->VarArray(ROMCommand, 8);
    file->Var32(&ROMData);

    file->VarArray(TransferData, 0x4000);
    file->Var32(&TransferPos);
    file->Var32(&TransferLen);
    file->Var32(&TransferDir);
    file->VarArray(TransferCmd, 8);

    // cart inserted/len/ROM/etc should be already populated
    // savestate should be loaded after the right game is loaded
    // (TODO: system to verify that indeed the right ROM is loaded)
    // (what to CRC? whole ROM? code binaries? latter would be more convenient for ie. romhaxing)

    u32 carttype = 0;
    u32 cartchk = 0;
    if (Cart)
    {
        carttype = Cart->Type();
        cartchk = Cart->Checksum();
    }

    if (file->Saving)
    {
        file->Var32(&carttype);
        file->Var32(&cartchk);
    }
    else
    {
        u32 savetype;
        file->Var32(&savetype);
        if (savetype != carttype) return;

        u32 savechk;
        file->Var32(&savechk);
        if (savechk != cartchk) return;
    }

    if (Cart) Cart->DoSavestate(file);
}


bool ReadROMParams(u32 gamecode, ROMListEntry* params)
{
    u32 offset = 0;
    u32 chk_size = ROMListEntryCount >> 1;
    for (;;)
    {
        u32 key = 0;
        const ROMListEntry* curentry = &ROMList[offset + chk_size];
        key = curentry->GameCode;

        if (key == gamecode)
        {
            memcpy(params, curentry, sizeof(ROMListEntry));
            return true;
        }
        else
        {
            if (key < gamecode)
            {
                if (chk_size == 0)
                    offset++;
                else
                    offset += chk_size;
            }
            else if (chk_size == 0)
            {
                return false;
            }

            chk_size >>= 1;
        }

        if (offset >= ROMListEntryCount)
        {
            return false;
        }
    }
}


void DecryptSecureArea(u8* out)
{
    const NDSHeader& header = Cart->GetHeader();
    const u8* cartrom = Cart->GetROM();

    u32 gamecode = header.GameCodeAsU32();
    u32 arm9base = header.ARM9ROMOffset;

    memcpy(out, &cartrom[arm9base], 0x800);

    Key1_InitKeycode(false, gamecode, 2, 2, NDS::ARM7BIOS, sizeof(NDS::ARM7BIOS));
    Key1_Decrypt((u32*)&out[0]);

    Key1_InitKeycode(false, gamecode, 3, 2, NDS::ARM7BIOS, sizeof(NDS::ARM7BIOS));
    for (u32 i = 0; i < 0x800; i += 8)
        Key1_Decrypt((u32*)&out[i]);

    XXH64_hash_t hash = XXH64(out, 0x800, 0);
    Log(LogLevel::Debug, "Secure area post-decryption xxh64 hash: %zx\n", hash);

    if (!strncmp((const char*)out, "encryObj", 8))
    {
        Log(LogLevel::Info, "Secure area decryption OK\n");
        *(u32*)&out[0] = 0xE7FFDEFF;
        *(u32*)&out[4] = 0xE7FFDEFF;
    }
    else
    {
        Log(LogLevel::Warn, "Secure area decryption failed\n");
        for (u32 i = 0; i < 0x800; i += 4)
            *(u32*)&out[i] = 0xE7FFDEFF;
    }
}

std::unique_ptr<CartCommon> ParseROM(const u8* romdata, u32 romlen)
{
    if (romdata == nullptr)
    {
        Log(LogLevel::Error, "NDSCart: romdata is null\n");
        return nullptr;
    }

    if (romlen == 0)
    {
        Log(LogLevel::Error, "NDSCart: romlen is zero\n");
        return nullptr;
    }

    u32 cartromsize = 0x200;
    while (cartromsize < romlen)
        cartromsize <<= 1; // ROM size must be a power of 2

    u8* cartrom = nullptr;
    try
    {
        cartrom = new u8[cartromsize];
    }
    catch (const std::bad_alloc& e)
    {
        Log(LogLevel::Error, "NDSCart: failed to allocate memory for ROM (%d bytes)\n", cartromsize);

        return nullptr;
    }

    // copy romdata into cartrom then zero out the remaining space
    memcpy(cartrom, romdata, romlen);
    memset(cartrom + romlen, 0, cartromsize - romlen);

    NDSHeader header {};
    memcpy(&header, cartrom, sizeof(header));

    bool dsi = header.IsDSi();
    bool badDSiDump = false;

    u32 dsiRegion = header.DSiRegionMask;
    if (dsi && dsiRegion == 0)
    {
        Log(LogLevel::Info, "DS header indicates DSi, but region is zero. Going in bad dump mode.\n");
        badDSiDump = true;
        dsi = false;
    }

    u32 gamecode = header.GameCodeAsU32();

    u32 arm9base = header.ARM9ROMOffset;
    bool homebrew = header.IsHomebrew();

    ROMListEntry romparams {};
    if (!ReadROMParams(gamecode, &romparams))
    {
        // set defaults
        Log(LogLevel::Warn, "ROM entry not found for gamecode %d\n", gamecode);

        romparams.GameCode = gamecode;
        romparams.ROMSize = cartromsize;
        if (homebrew)
            romparams.SaveMemType = 0; // no saveRAM for homebrew
        else
            romparams.SaveMemType = 2; // assume EEPROM 64k (TODO FIXME)
    }

    if (romparams.ROMSize != romlen)
        Log(LogLevel::Warn, "!! bad ROM size %d (expected %d) rounded to %d\n", romlen, romparams.ROMSize, cartromsize);

    // generate a ROM ID
    // note: most games don't check the actual value
    // it just has to stay the same throughout gameplay
    u32 cartid = 0x000000C2;

    if (cartromsize >= 1024 * 1024 && cartromsize <= 128 * 1024 * 1024)
        cartid |= ((cartromsize >> 20) - 1) << 8;
    else
        cartid |= (0x100 - (cartromsize >> 28)) << 8;

    if (romparams.SaveMemType >= 8 && romparams.SaveMemType <= 10)
        cartid |= 0x08000000; // NAND flag

    if (dsi)
        cartid |= 0x40000000;

    // cart ID for Jam with the Band
    // TODO: this kind of ID triggers different KEY1 phase
    // (repeats commands a bunch of times)
    //cartid = 0x88017FEC;
    //cartid = 0x80007FC2; // pokémon typing adventure

    u32 irversion = 0;
    if ((gamecode & 0xFF) == 'I')
    {
        if (((gamecode >> 8) & 0xFF) < 'P')
            irversion = 1; // Active Health / Walk with Me
        else
            irversion = 2; // Pokémon HG/SS, B/W, B2/W2
    }

    std::unique_ptr<CartCommon> cart;
    if (homebrew)
        cart = std::make_unique<CartHomebrew>(cartrom, cartromsize, cartid, romparams);
    else if (cartid & 0x08000000)
        cart = std::make_unique<CartRetailNAND>(cartrom, cartromsize, cartid, romparams);
    else if (irversion != 0)
        cart = std::make_unique<CartRetailIR>(cartrom, cartromsize, cartid, irversion, badDSiDump, romparams);
    else if ((gamecode & 0xFFFFFF) == 0x505A55) // UZPx
        cart = std::make_unique<CartRetailBT>(cartrom, cartromsize, cartid, romparams);
    else
        cart = std::make_unique<CartRetail>(cartrom, cartromsize, cartid, badDSiDump, romparams);

    if (romparams.SaveMemType > 0)
        cart->SetupSave(romparams.SaveMemType);

    return cart;
}

// Why a move function? Because the Cart object is polymorphic,
// and cloning polymorphic objects without knowing the underlying type is annoying.
bool InsertROM(std::unique_ptr<CartCommon>&& cart)
{
    if (!cart) {
        Log(LogLevel::Error, "Failed to insert invalid cart; existing cart (if any) was not ejected.\n");
        return false;
    }

    if (Cart)
        EjectCart();

    Cart = std::move(cart);

    Cart->Reset();

    const NDSHeader& header = Cart->GetHeader();
    const ROMListEntry romparams = Cart->GetROMParams();
    const u8* cartrom = Cart->GetROM();
    if (header.ARM9ROMOffset >= 0x4000 && header.ARM9ROMOffset < 0x8000)
    {
        // reencrypt secure area if needed
        if (*(u32*)&cartrom[header.ARM9ROMOffset] == 0xE7FFDEFF && *(u32*)&cartrom[header.ARM9ROMOffset + 0x10] != 0xE7FFDEFF)
        {
            Log(LogLevel::Debug, "Re-encrypting cart secure area\n");

            strncpy((char*)&cartrom[header.ARM9ROMOffset], "encryObj", 8);

            Key1_InitKeycode(false, romparams.GameCode, 3, 2, NDS::ARM7BIOS, sizeof(NDS::ARM7BIOS));
            for (u32 i = 0; i < 0x800; i += 8)
                Key1_Encrypt((u32*)&cartrom[header.ARM9ROMOffset + i]);

            Key1_InitKeycode(false, romparams.GameCode, 2, 2, NDS::ARM7BIOS, sizeof(NDS::ARM7BIOS));
            Key1_Encrypt((u32*)&cartrom[header.ARM9ROMOffset]);

            Log(LogLevel::Debug, "Re-encrypted cart secure area\n");
        }
        else
        {
            Log(LogLevel::Debug, "No need to re-encrypt cart secure area\n");
        }
    }

    Log(LogLevel::Info, "Inserted cart with game code: %.4s\n", header.GameCode);
    Log(LogLevel::Info, "Inserted cart with ID: %08X\n", Cart->ID());
    Log(LogLevel::Info, "ROM entry: %08X %08X\n", romparams.ROMSize, romparams.SaveMemType);

    DSi::SetCartInserted(true);

    return true;
}

bool LoadROM(const u8* romdata, u32 romlen)
{
    std::unique_ptr<CartCommon> cart = ParseROM(romdata, romlen);

    return InsertROM(std::move(cart));
}

void LoadSave(const u8* savedata, u32 savelen)
{
    if (Cart)
        Cart->LoadSave(savedata, savelen);
}

void SetupDirectBoot(const std::string& romname)
{
    if (Cart)
        Cart->SetupDirectBoot(romname);
}

u8* GetSaveMemory()
{
    return Cart ? Cart->GetSaveMemory() : nullptr;
}

u32 GetSaveMemoryLength()
{
    return Cart ? Cart->GetSaveMemoryLength() : 0;
}

void EjectCart()
{
    if (!Cart) return;

    // ejecting the cart triggers the gamecard IRQ
    NDS::SetIRQ(0, NDS::IRQ_CartIREQMC);
    NDS::SetIRQ(1, NDS::IRQ_CartIREQMC);

    Cart = nullptr;

    DSi::SetCartInserted(false);

    // CHECKME: does an eject imply anything for the ROM/SPI transfer registers?
}

void ResetCart()
{
    // CHECKME: what if there is a transfer in progress?

    SPICnt = 0;
    ROMCnt = 0;

    SPIData = 0;
    SPIDataPos = 0;
    SPIHold = false;

    memset(ROMCommand, 0, 8);
    ROMData = 0;

    Key2_X = 0;
    Key2_Y = 0;

    memset(TransferData, 0, 0x4000);
    TransferPos = 0;
    TransferLen = 0;
    TransferDir = 0;
    memset(TransferCmd, 0, 8);
    TransferCmd[0] = 0xFF;

    if (Cart) Cart->Reset();
}


void ROMEndTransfer(u32 param)
{
    ROMCnt &= ~(1<<31);

    if (SPICnt & (1<<14))
        NDS::SetIRQ((NDS::ExMemCnt[0]>>11)&0x1, NDS::IRQ_CartXferDone);

    if (Cart)
        Cart->ROMCommandFinish(TransferCmd, TransferData, TransferLen);
}

void ROMPrepareData(u32 param)
{
    if (TransferDir == 0)
    {
        if (TransferPos >= TransferLen)
            ROMData = 0;
        else
            ROMData = *(u32*)&TransferData[TransferPos];

        TransferPos += 4;
    }

    ROMCnt |= (1<<23);

    if (NDS::ExMemCnt[0] & (1<<11))
        NDS::CheckDMAs(1, 0x12);
    else
        NDS::CheckDMAs(0, 0x05);
}

void WriteROMCnt(u32 val)
{
    u32 xferstart = (val & ~ROMCnt) & (1<<31);
    ROMCnt = (val & 0xFF7F7FFF) | (ROMCnt & 0x20800000);

    // all this junk would only really be useful if melonDS was interfaced to
    // a DS cart reader
    if (val & (1<<15))
    {
        u32 snum = (NDS::ExMemCnt[0]>>8)&0x8;
        u64 seed0 = *(u32*)&NDS::ROMSeed0[snum] | ((u64)NDS::ROMSeed0[snum+4] << 32);
        u64 seed1 = *(u32*)&NDS::ROMSeed1[snum] | ((u64)NDS::ROMSeed1[snum+4] << 32);

        Key2_X = 0;
        Key2_Y = 0;
        for (u32 i = 0; i < 39; i++)
        {
            if (seed0 & (1ULL << i)) Key2_X |= (1ULL << (38-i));
            if (seed1 & (1ULL << i)) Key2_Y |= (1ULL << (38-i));
        }

        Log(LogLevel::Debug, "seed0: %02X%08X\n", (u32)(seed0>>32), (u32)seed0);
        Log(LogLevel::Debug, "seed1: %02X%08X\n", (u32)(seed1>>32), (u32)seed1);
        Log(LogLevel::Debug, "key2 X: %02X%08X\n", (u32)(Key2_X>>32), (u32)Key2_X);
        Log(LogLevel::Debug, "key2 Y: %02X%08X\n", (u32)(Key2_Y>>32), (u32)Key2_Y);
    }

    // transfers will only start when bit31 changes from 0 to 1
    // and if AUXSPICNT is configured correctly
    if (!(SPICnt & (1<<15))) return;
    if (SPICnt & (1<<13)) return;
    if (!xferstart) return;

    u32 datasize = (ROMCnt >> 24) & 0x7;
    if (datasize == 7)
        datasize = 4;
    else if (datasize > 0)
        datasize = 0x100 << datasize;

    TransferPos = 0;
    TransferLen = datasize;

    *(u32*)&TransferCmd[0] = *(u32*)&ROMCommand[0];
    *(u32*)&TransferCmd[4] = *(u32*)&ROMCommand[4];

    memset(TransferData, 0xFF, TransferLen);

    /*printf("ROM COMMAND %04X %08X %02X%02X%02X%02X%02X%02X%02X%02X SIZE %04X\n",
           SPICnt, ROMCnt,
           TransferCmd[0], TransferCmd[1], TransferCmd[2], TransferCmd[3],
           TransferCmd[4], TransferCmd[5], TransferCmd[6], TransferCmd[7],
           datasize);*/

    // default is read
    // commands that do writes will change this
    TransferDir = 0;

    if (Cart)
        TransferDir = Cart->ROMCommandStart(TransferCmd, TransferData, TransferLen);

    if ((datasize > 0) && (((ROMCnt >> 30) & 0x1) != TransferDir))
        Log(LogLevel::Debug, "NDSCART: !! BAD TRANSFER DIRECTION FOR CMD %02X, DIR=%d, ROMCNT=%08X\n", ROMCommand[0], TransferDir, ROMCnt);

    ROMCnt &= ~(1<<23);

    // ROM transfer timings
    // the bus is parallel with 8 bits
    // thus a command would take 8 cycles to be transferred
    // and it would take 4 cycles to receive a word of data
    // TODO: advance read position if bit28 is set
    // TODO: during a write transfer, bit23 is set immediately when beginning the transfer(?)

    u32 xfercycle = (ROMCnt & (1<<27)) ? 8 : 5;
    u32 cmddelay = 8;

    // delays are only applied when the WR bit is cleared
    // CHECKME: do the delays apply at the end (instead of start) when WR is set?
    if (!(ROMCnt & (1<<30)))
    {
        cmddelay += (ROMCnt & 0x1FFF);
        if (datasize) cmddelay += ((ROMCnt >> 16) & 0x3F);
    }

    if (datasize == 0)
        NDS::ScheduleEvent(NDS::Event_ROMTransfer, false, xfercycle*cmddelay, ROMEndTransfer, 0);
    else
        NDS::ScheduleEvent(NDS::Event_ROMTransfer, false, xfercycle*(cmddelay+4), ROMPrepareData, 0);
}

void AdvanceROMTransfer()
{
    ROMCnt &= ~(1<<23);

    if (TransferPos < TransferLen)
    {
        u32 xfercycle = (ROMCnt & (1<<27)) ? 8 : 5;
        u32 delay = 4;
        if (!(ROMCnt & (1<<30)))
        {
            if (!(TransferPos & 0x1FF))
                delay += ((ROMCnt >> 16) & 0x3F);
        }

        NDS::ScheduleEvent(NDS::Event_ROMTransfer, false, xfercycle*delay, ROMPrepareData, 0);
    }
    else
        ROMEndTransfer(0);
}

u32 ReadROMData()
{
    if (ROMCnt & (1<<30)) return 0;

    if (ROMCnt & (1<<23))
    {
        AdvanceROMTransfer();
    }

    return ROMData;
}

void WriteROMData(u32 val)
{
    if (!(ROMCnt & (1<<30))) return;

    ROMData = val;

    if (ROMCnt & (1<<23))
    {
        if (TransferDir == 1)
        {
            if (TransferPos < TransferLen)
                *(u32*)&TransferData[TransferPos] = ROMData;

            TransferPos += 4;
        }

        AdvanceROMTransfer();
    }
}


void WriteSPICnt(u16 val)
{
    if ((SPICnt & 0x2040) == 0x2040 && (val & 0x2000) == 0x0000)
    {
        // forcefully reset SPI hold
        SPIHold = false;
    }

    SPICnt = (SPICnt & 0x0080) | (val & 0xE043);

    // AUXSPICNT can be changed during a transfer
    // in this case, the transfer continues until the end, even if bit13 or bit15 are cleared
    // if the transfer speed is changed, the transfer continues at the new speed (TODO)
    if (SPICnt & (1<<7))
        Log(LogLevel::Debug, "!! CHANGING AUXSPICNT DURING TRANSFER: %04X\n", val);
}

void SPITransferDone(u32 param)
{
    SPICnt &= ~(1<<7);
}

u8 ReadSPIData()
{
    if (!(SPICnt & (1<<15))) return 0;
    if (!(SPICnt & (1<<13))) return 0;
    if (SPICnt & (1<<7)) return 0; // checkme

    return SPIData;
}

void WriteSPIData(u8 val)
{
    if (!(SPICnt & (1<<15))) return;
    if (!(SPICnt & (1<<13))) return;
    if (SPICnt & (1<<7)) return;

    SPICnt |= (1<<7);

    bool hold = SPICnt&(1<<6);
    bool islast = false;
    if (!hold)
    {
        if (SPIHold) SPIDataPos++;
        else         SPIDataPos = 0;
        islast = true;
        SPIHold = false;
    }
    else if (hold && (!SPIHold))
    {
        SPIHold = true;
        SPIDataPos = 0;
    }
    else
    {
        SPIDataPos++;
    }

    if (Cart) SPIData = Cart->SPIWrite(val, SPIDataPos, islast);
    else      SPIData = 0;

    // SPI transfers one bit per cycle -> 8 cycles per byte
    u32 delay = 8 * (8 << (SPICnt & 0x3));
    NDS::ScheduleEvent(NDS::Event_ROMSPITransfer, false, delay, SPITransferDone, 0);
}

}
