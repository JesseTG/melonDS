/*
    Copyright 2016-2023 melonDS team

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

#pragma once

#include "types.h"

namespace melonDS
{
constexpr u32 MainRAMMaxSize = 0x1000000;
constexpr u32 SharedWRAMSize = 0x8000;
constexpr u32 ARM7WRAMSize = 0x10000;
constexpr u32 DSiNWRAMSize = 0x40000;
constexpr u32 ARM9BIOSLength = 0x1000;
constexpr u32 ARM7BIOSLength = 0x4000;
}
