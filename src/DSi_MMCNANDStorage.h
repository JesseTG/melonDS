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

#ifndef DSI_MMCNANDSTORAGE_H
#define DSI_MMCNANDSTORAGE_H

#include "DSi_MMCStorage.h"

namespace DSi_NAND
{
    class NANDImage;
    class NANDMount;
}

class DSi_MMCNANDStorage final : public DSi_MMCStorage
{
public:
    DSi_MMCNANDStorage(DSi_SDMMCHost* host, std::unique_ptr<DSi_NAND::NANDImage>&& nand) noexcept;
    explicit DSi_MMCNANDStorage(DSi_SDMMCHost* host) noexcept;

    [[nodiscard]] const std::unique_ptr<DSi_NAND::NANDImage>& GetNAND() const noexcept { return NAND; }
    [[nodiscard]] std::unique_ptr<DSi_NAND::NANDImage>& GetNAND() noexcept { return NAND; }

    [[nodiscard]] DSi_NAND::NANDMount MountNAND() noexcept;
protected:
    u32 ReadBlock(u64 addr) noexcept override;
    u32 WriteBlock(u64 addr) noexcept override;
private:
    std::unique_ptr<DSi_NAND::NANDImage> NAND;
};

#endif // DSI_MMCNANDSTORAGE_H
