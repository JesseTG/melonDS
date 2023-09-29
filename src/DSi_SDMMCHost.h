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

#ifndef DSI_SDMMCHOST_H
#define DSI_SDMMCHOST_H

#include <optional>

#include "DSi_SD.h"
#include "DSi_MMCNANDStorage.h"
#include "DSi_MMCSDCardStorage.h"

class DSi_SDMMCHost final : public DSi_SDHost
{
public:
    DSi_SDMMCHost();
    void Reset() noexcept override;
    void DoSavestate(Savestate* file) noexcept override;

    [[nodiscard]] const std::unique_ptr<DSi_NAND::NANDImage>& GetNAND() const noexcept { return NAND.GetNAND(); }
    [[nodiscard]] std::unique_ptr<DSi_NAND::NANDImage>& GetNAND() noexcept { return NAND.GetNAND(); }


    [[nodiscard]] DSi_NAND::NANDMount MountNAND() noexcept;
protected:
    u16 ReadMMIO() noexcept override;
private:
    std::optional<DSi_MMCSDCardStorage> SDCard;
    DSi_MMCNANDStorage NAND;
};

#endif // DSI_SDMMCHOST_H
