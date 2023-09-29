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

#ifndef DSI_SDDEVICE_H
#define DSI_SDDEVICE_H

#include "Savestate.h"
#include "FATStorage.h"
#include "FIFO.h"
#include "DSi_SD.h"

#include <cstring>

class DSi_SDHost;
class DSi_SDDevice
{
public:
    DSi_SDDevice() noexcept : IRQ(false) {}
    virtual ~DSi_SDDevice() noexcept = 0;

    virtual void Reset() noexcept = 0;

    virtual void DoSavestate(Savestate* file) noexcept = 0;

    virtual void SendCMD(u8 cmd, u32 param) noexcept = 0;
    virtual void ContinueTransfer() noexcept = 0;

    bool IRQ;
};
#endif // DSI_SDDEVICE_H
