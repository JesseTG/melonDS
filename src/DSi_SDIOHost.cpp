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

#include "DSi_SDIOHost.h"

DSi_SDIOHost::DSi_SDIOHost() : DSi_SDHost(1)
{

}

void DSi_SDIOHost::Reset() noexcept
{
    DSi_SDHost::Reset();
}

u16 DSi_SDIOHost::ReadMMIO() noexcept
{
    u16 ret = (IRQStatus & 0x031D);
    // SDIO wifi is always inserted, I guess
    ret |= 0x00A0;
    return ret;
}