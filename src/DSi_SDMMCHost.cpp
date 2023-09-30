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

#include "DSi_SDMMCHost.h"
#include "DSi_MMCStorage.h"
#include "DSi.h"

DSi_SDMMCHost::DSi_SDMMCHost() : DSi_SDHost(0), SDCard(this), NAND(this)
{
    Ports[0] = &SDCard;
    Ports[1] = &NAND;
    Reset();
}

void DSi_SDMMCHost::Reset() noexcept
{
    DSi_SDHost::Reset();
}

void DSi_SDMMCHost::DoSavestate(Savestate* file) noexcept
{
    DSi_SDHost::DoSavestate(file);

    NAND.DoSavestate(file);
    if (SDCard.GetSDCard())
        SDCard.DoSavestate(file);
}

DSi_NAND::NANDMount DSi_SDMMCHost::MountNAND() noexcept
{
    return NAND.MountNAND();
}

u16 DSi_SDMMCHost::ReadMMIO() noexcept
{
    u16 ret = (IRQStatus & 0x031D);
    if (SDCard.GetSDCard()) // basic check of whether the SD card is inserted
    {
        ret |= 0x0020;
        if (!SDCard.GetSDCard()->IsReadOnly())
            ret |= 0x0080;
    }

    return ret;
}