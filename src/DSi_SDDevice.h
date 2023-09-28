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
    DSi_SDDevice(DSi_SDHost* host) { Host = host; IRQ = false; ReadOnly = false; }
    virtual ~DSi_SDDevice() = default;

    virtual void Reset() = 0;

    virtual void DoSavestate(Savestate* file) = 0;

    virtual void SendCMD(u8 cmd, u32 param) = 0;
    virtual void ContinueTransfer() = 0;

    bool IRQ;
    [[deprecated("Move to DSi_MMCSDCardStorage, it's the only SDDevice that can be read-only")]] bool ReadOnly;

protected:
    DSi_SDHost* Host;
};
#endif // DSI_SDDEVICE_H
