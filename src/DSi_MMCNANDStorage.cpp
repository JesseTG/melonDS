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

DSi_MMCNANDStorage::DSi_MMCNANDStorage(DSi_SDMMCHost* host, std::unique_ptr<DSi_NAND::NANDImage>&& nand) noexcept :
    DSi_MMCStorage(host, true),
    NAND(std::move(nand))
{
}

DSi_MMCNANDStorage::DSi_MMCNANDStorage(DSi_SDMMCHost* host) noexcept :
    DSi_MMCStorage(host, true)
{
}