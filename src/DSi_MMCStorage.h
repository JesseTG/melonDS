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

#ifndef DSI_MMCSTORAGE_H
#define DSI_MMCSTORAGE_H

#include <string>

#include "types.h"
#include "DSi_SDDevice.h"

class DSi_SDMMCHost;

class DSi_MMCStorage : public DSi_SDDevice
{
public:
    ~DSi_MMCStorage() noexcept override = default;

    void Reset() noexcept final;

    void DoSavestate(Savestate* file) noexcept override;

    void SetCID(const u8* cid) noexcept { memcpy(CID, cid, 16); }

    void SendCMD(u8 cmd, u32 param) noexcept override;
    void SendACMD(u8 cmd, u32 param);

    void ContinueTransfer() noexcept override;

protected:
    DSi_MMCStorage(DSi_SDMMCHost* host, bool internal);
    virtual u32 ReadBlock(u64 addr) noexcept = 0;
    virtual u32 WriteBlock(u64 addr) noexcept = 0;
    virtual void StopOperation() noexcept {}

    DSi_SDMMCHost* Host;
    [[deprecated("Make this implicit in the subclass instead")]] bool Internal;

    u8 CID[16];
    u8 CSD[16];

    u32 CSR;
    u32 OCR;
    u32 RCA;
    u8 SCR[8];
    u8 SSR[64];

    u32 BlockSize;
    u32 RWCommand;
    u64 RWAddress;

    void SetState(u32 state) { CSR &= ~(0xF << 9); CSR |= (state << 9); }

};

#endif // DSI_MMCSTORAGE_H
