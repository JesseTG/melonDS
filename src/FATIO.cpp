/*
    Copyright 2023 melonDS team

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

#include "fatfs/ff.h"
#include "fatfs/diskio.h"

/// The physical drive number of the mounted DSi NAND.
constexpr BYTE DSI_NAND_PDRV = 0;

/// The physical drive number of the mounted DSi SD card.
constexpr BYTE DSI_SD_CARD_DRIVE = 1;

/// The physical drive number of the DLDI homebrew SD card.
constexpr BYTE DLDI_SD_CARD_DRIVE = 2;