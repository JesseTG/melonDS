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

#include "DSi_MMCStorage.h"
#include "Platform.h"

using namespace Platform;

DSi_MMCStorage::DSi_MMCStorage(DSi_SDHost* host, bool internal, const std::string& filename)
    : DSi_SDDevice(host)
{
    Internal = internal;
    File = Platform::OpenLocalFile(filename, FileMode::ReadWriteExisting);

    SD = nullptr;

    ReadOnly = false;
}

DSi_MMCStorage::DSi_MMCStorage(DSi_SDHost* host, bool internal, const std::string& filename, u64 size, bool readonly, const std::string& sourcedir)
    : DSi_SDDevice(host)
{
    Internal = internal;
    File = nullptr;

    SD = new FATStorage(filename, size, readonly, sourcedir);
    SD->Open();

    ReadOnly = readonly;
}

DSi_MMCStorage::~DSi_MMCStorage() noexcept
{
    if (SD)
    {
        SD->Close();
        delete SD;
    }
    if (File)
    {
        CloseFile(File);
    }
}

void DSi_MMCStorage::Reset() noexcept
{
    // TODO: reset file access????

    CSR = 0x00000100; // checkme

    // TODO: busy bit
    // TODO: SDHC/SDXC bit
    OCR = 0x80FF8000;

    // TODO: customize based on card size etc
    u8 csd_template[16] = {0x40, 0x40, 0x96, 0xE9, 0x7F, 0xDB, 0xF6, 0xDF, 0x01, 0x59, 0x0F, 0x2A, 0x01, 0x26, 0x90, 0x00};
    memcpy(CSD, csd_template, 16);

    // checkme
    memset(SCR, 0, 8);
    *(u32*)&SCR[0] = 0x012A0000;

    memset(SSR, 0, 64);

    BlockSize = 0;
    RWAddress = 0;
    RWCommand = 0;
}

void DSi_MMCStorage::DoSavestate(Savestate* file) noexcept
{
    file->Section(Internal ? "NAND" : "SDCR");

    file->VarArray(CID, 16);
    file->VarArray(CSD, 16);

    file->Var32(&CSR);
    file->Var32(&OCR);
    file->Var32(&RCA);
    file->VarArray(SCR, 8);
    file->VarArray(SSR, 64);

    file->Var32(&BlockSize);
    file->Var64(&RWAddress);
    file->Var32(&RWCommand);

    // TODO: what about the file contents?
}

void DSi_MMCStorage::SendCMD(u8 cmd, u32 param) noexcept
{
    if (CSR & (1<<5))
    {
        CSR &= ~(1<<5);
        return SendACMD(cmd, param);
    }

    switch (cmd)
    {
    case 0: // reset/etc
        Host->SendResponse(CSR, true);
        return;

    case 1: // SEND_OP_COND
        // CHECKME!!
        // also TODO: it's different for the SD card
        if (Internal)
        {
            param &= ~(1<<30);
            OCR &= 0xBF000000;
            OCR |= (param & 0x40FFFFFF);
            Host->SendResponse(OCR, true);
            SetState(0x01);
        }
        else
        {
            Log(LogLevel::Debug, "CMD1 on SD card!!\n");
        }
        return;

    case 2:
    case 10: // get CID
        Host->SendResponse(*(u32*)&CID[12], false);
        Host->SendResponse(*(u32*)&CID[8], false);
        Host->SendResponse(*(u32*)&CID[4], false);
        Host->SendResponse(*(u32*)&CID[0], true);
        if (cmd == 2) SetState(0x02);
        return;

    case 3: // get/set RCA
        if (Internal)
        {
            RCA = param >> 16;
            Host->SendResponse(CSR|0x10000, true); // huh??
        }
        else
        {
            // TODO
            Log(LogLevel::Debug, "CMD3 on SD card: TODO\n");
            Host->SendResponse((CSR & 0x1FFF) | ((CSR >> 6) & 0x2000) | ((CSR >> 8) & 0xC000) | (1 << 16), true);
        }
        return;

    case 6: // MMC: 'SWITCH'
        // TODO!
        Host->SendResponse(CSR, true);
        return;

    case 7: // select card (by RCA)
        Host->SendResponse(CSR, true);
        return;

    case 8: // set voltage
        Host->SendResponse(param, true);
        return;

    case 9: // get CSD
        Host->SendResponse(*(u32*)&CSD[12], false);
        Host->SendResponse(*(u32*)&CSD[8], false);
        Host->SendResponse(*(u32*)&CSD[4], false);
        Host->SendResponse(*(u32*)&CSD[0], true);
        return;

    case 12: // stop operation
        SetState(0x04);
        if (File) FileFlush(File);
        RWCommand = 0;
        Host->SendResponse(CSR, true);
        return;

    case 13: // get status
        Host->SendResponse(CSR, true);
        return;

    case 16: // set block size
        BlockSize = param;
        if (BlockSize > 0x200)
        {
            // TODO! raise error
            Log(LogLevel::Warn, "!! SD/MMC: BAD BLOCK LEN %d\n", BlockSize);
            BlockSize = 0x200;
        }
        SetState(0x04); // CHECKME
        Host->SendResponse(CSR, true);
        return;

    case 18: // read multiple blocks
        //printf("READ_MULTIPLE_BLOCKS addr=%08X size=%08X\n", param, BlockSize);
        RWAddress = param;
        if (OCR & (1<<30))
        {
            RWAddress <<= 9;
            BlockSize = 512;
        }
        RWCommand = 18;
        Host->SendResponse(CSR, true);
        RWAddress += ReadBlock(RWAddress);
        SetState(0x05);
        return;

    case 25: // write multiple blocks
        //printf("WRITE_MULTIPLE_BLOCKS addr=%08X size=%08X\n", param, BlockSize);
        RWAddress = param;
        if (OCR & (1<<30))
        {
            RWAddress <<= 9;
            BlockSize = 512;
        }
        RWCommand = 25;
        Host->SendResponse(CSR, true);
        RWAddress += WriteBlock(RWAddress);
        SetState(0x04);
        return;

    case 55: // appcmd prefix
        CSR |= (1<<5);
        Host->SendResponse(CSR, true);
        return;
    }

    Log(LogLevel::Warn, "MMC: unknown CMD %d %08X\n", cmd, param);
}

void DSi_MMCStorage::SendACMD(u8 cmd, u32 param)
{
    switch (cmd)
    {
    case 6: // set bus width (TODO?)
        //printf("SET BUS WIDTH %08X\n", param);
        Host->SendResponse(CSR, true);
        return;

    case 13: // get SSR
        Host->SendResponse(CSR, true);
        Host->DataRX(SSR, 64);
        return;

    case 41: // set operating conditions
        // CHECKME:
        // DSi boot2 sets this to 0x40100000 (hardcoded)
        // then has two codepaths depending on whether bit30 did get set
        // is it settable at all on the MMC? probably not.
        if (Internal) param &= ~(1<<30);
        OCR &= 0xBF000000;
        OCR |= (param & 0x40FFFFFF);
        Host->SendResponse(OCR, true);
        SetState(0x01);
        return;

    case 42: // ???
        Host->SendResponse(CSR, true);
        return;

    case 51: // get SCR
        Host->SendResponse(CSR, true);
        Host->DataRX(SCR, 8);
        return;
    }

    Log(LogLevel::Warn, "MMC: unknown ACMD %d %08X\n", cmd, param);
}

void DSi_MMCStorage::ContinueTransfer() noexcept
{
    if (RWCommand == 0) return;

    u32 len = 0;

    switch (RWCommand)
    {
    case 18:
        len = ReadBlock(RWAddress);
        break;

    case 25:
        len = WriteBlock(RWAddress);
        break;
    }

    RWAddress += len;
}
