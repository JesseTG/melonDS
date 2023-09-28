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

#ifndef DSI_MMCSDCARDSTORAGE_H
#define DSI_MMCSDCARDSTORAGE_H

#include "DSi_MMCStorage.h"

class FATStorage;

class DSi_MMCSDCardStorage final : public DSi_MMCStorage
{
public:
    DSi_MMCSDCardStorage(DSi_SDMMCHost* host, std::unique_ptr<FATStorage>&& sd) noexcept;
    explicit DSi_MMCSDCardStorage(DSi_SDMMCHost* host) noexcept;

    [[nodiscard]] const std::unique_ptr<FATStorage>& GetSDCard() const noexcept { return SD; }
    [[nodiscard]] std::unique_ptr<FATStorage>& GetSDCard() noexcept { return SD; }
private:
    std::unique_ptr<FATStorage> SD;
};

#endif // DSI_MMCSDCARDSTORAGE_H
