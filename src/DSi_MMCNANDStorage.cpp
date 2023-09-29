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

#include "DSi_MMCNANDStorage.h"
#include "DSi_NAND.h"
#include "DSi_SDMMCHost.h"

DSi_MMCNANDStorage::DSi_MMCNANDStorage(DSi_SDMMCHost* host, std::unique_ptr<DSi_NAND::NANDImage>&& nand) noexcept :
    DSi_MMCStorage(host, true),
    NAND(std::move(nand))
{
}

DSi_MMCNANDStorage::DSi_MMCNANDStorage(DSi_SDMMCHost* host) noexcept :
    DSi_MMCStorage(host, true)
{
}

DSi_NAND::NANDMount DSi_MMCNANDStorage::MountNAND() noexcept
{
    return NAND ? DSi_NAND::NANDMount(*NAND) : DSi_NAND::NANDMount();
}

u32 DSi_MMCNANDStorage::ReadBlock(u64 addr) noexcept
{
    u32 len = Host->GetTransferrableLen(BlockSize);

    u8 data[0x200];
    FileSeek(NAND->GetFile(), addr, Platform::FileSeekOrigin::Start);
    FileRead(&data[addr & 0x1FF], 1, len, NAND->GetFile());

    return Host->DataRX(&data[addr & 0x1FF], len);
}

u32 DSi_MMCNANDStorage::WriteBlock(u64 addr) noexcept
{
    u32 len = Host->GetTransferrableLen(BlockSize);

    u8 data[0x200];
    if ((len = Host->DataTX(&data[addr & 0x1FF], len)))
    {
        FileSeek(NAND->GetFile(), addr, Platform::FileSeekOrigin::Start);
        FileWrite(&data[addr & 0x1FF], 1, len, NAND->GetFile());
    }

    return len;
}
