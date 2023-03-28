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


#include "SavestateCounter.h"

SavestateCounter::SavestateCounter() : _required_memory(0)
{
    Saving = true;
    Error = false;
    CurSection = -1;

    VersionMajor = SAVESTATE_MAJOR;
    VersionMinor = SAVESTATE_MINOR;

    u16 major = SAVESTATE_MAJOR;
    u16 minor = SAVESTATE_MINOR;
    u32 zero = 0;

    this->VarArray((void *) SAVESTATE_MAGIC, 4);
    this->Var16(&major);
    this->Var16(&minor);
    this->Var32(&zero); // Length to be fixed at the end
}

void SavestateCounter::Section(const char *magic)
{
    CurSection = _required_memory;
    _required_memory += 16; // Section headers are 16 bytes
}

void SavestateCounter::Var8(u8 *)
{
    _required_memory += sizeof(u8);
}

void SavestateCounter::Var16(u16 *)
{
    _required_memory += sizeof(u16);
}

void SavestateCounter::Var32(u32 *)
{
    _required_memory += sizeof(u32);
}

void SavestateCounter::Var64(u64 *)
{
    _required_memory += sizeof(u64);
}

void SavestateCounter::VarArray(void *, u32 len)
{
    _required_memory += len;
}
