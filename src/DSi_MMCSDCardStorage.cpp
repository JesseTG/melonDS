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

#include "DSi_MMCSDCardStorage.h"
#include "DSi_SDMMCHost.h"

DSi_MMCSDCardStorage::DSi_MMCSDCardStorage(DSi_SDMMCHost* host, std::unique_ptr<FATStorage>&& sd) noexcept
    : DSi_MMCStorage(host, false), SD(std::move(sd))
{}

DSi_MMCSDCardStorage::DSi_MMCSDCardStorage(DSi_SDMMCHost* host) noexcept
    : DSi_MMCStorage(host, false)
{}

u32 DSi_MMCSDCardStorage::ReadBlock(u64 addr) noexcept
{
    u32 len = Host->GetTransferrableLen(BlockSize);

    u8 data[0x200];
    SD->ReadSectors((u32)(addr >> 9), 1, data);

    return Host->DataRX(&data[addr & 0x1FF], len);
}

u32 DSi_MMCSDCardStorage::WriteBlock(u64 addr) noexcept
{
    u32 len = Host->GetTransferrableLen(BlockSize);

    u8 data[0x200];
    if (len < 0x200 && SD != nullptr)
    {
        SD->ReadSectors((u32)(addr >> 9), 1, data);
    }

    if ((len = Host->DataTX(&data[addr & 0x1FF], len)) && SD != nullptr && !SD->IsReadOnly())
    {
        SD->WriteSectors((u32)(addr >> 9), 1, data);
    }

    return len;
}
