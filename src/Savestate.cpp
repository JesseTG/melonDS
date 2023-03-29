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

#include <stdio.h>
#include "Savestate.h"
#include "Platform.h"

using Platform::Log;
using Platform::LogLevel;

/*
    Savestate format

    header:
    00 - magic MELN
    04 - version major
    06 - version minor
    08 - length
    0C - reserved (should be game serial later!)

    section header:
    00 - section magic
    04 - section length
    08 - reserved
    0C - reserved

    Implementation details

    version difference:
    * different major means savestate file is incompatible
    * different minor means adjustments may have to be made
*/

// TODO: buffering system! or something of that sort
// repeated fread/fwrite is slow on Switch

Savestate::Savestate(std::string filename, bool save)
{
    const char* magic = "MELN";

    error = false;

    if (save)
    {
        saving = true;
        file = Platform::OpenLocalFile(filename, "wb");
        if (!file)
        {
            Log(LogLevel::Error, "savestate: file %s doesn't exist\n", filename.c_str());
            error = true;
            return;
        }

        VersionMajor = SAVESTATE_MAJOR;
        VersionMinor = SAVESTATE_MINOR;

        fwrite(magic, 4, 1, file);
        fwrite(&VersionMajor, 2, 1, file);
        fwrite(&VersionMinor, 2, 1, file);
        fseek(file, 8, SEEK_CUR); // length to be fixed later
    }
    else
    {
        saving = false;
        file = Platform::OpenFile(filename, "rb");
        if (!file)
        {
            Log(LogLevel::Error, "savestate: file %s doesn't exist\n", filename.c_str());
            error = true;
            return;
        }

        u32 len;
        fseek(file, 0, SEEK_END);
        len = (u32)ftell(file);
        fseek(file, 0, SEEK_SET);

        u32 buf = 0;

        fread(&buf, 4, 1, file);
        if (buf != ((u32*)magic)[0])
        {
            Log(LogLevel::Error, "savestate: invalid magic %08X\n", buf);
            error = true;
            return;
        }

        VersionMajor = 0;
        VersionMinor = 0;

        fread(&VersionMajor, 2, 1, file);
        if (VersionMajor != SAVESTATE_MAJOR)
        {
            Log(LogLevel::Error, "savestate: bad version major %d, expecting %d\n", VersionMajor, SAVESTATE_MAJOR);
            error = true;
            return;
        }

        fread(&VersionMinor, 2, 1, file);
        if (VersionMinor > SAVESTATE_MINOR)
        {
            Log(LogLevel::Error, "savestate: state from the future, %d > %d\n", VersionMinor, SAVESTATE_MINOR);
            error = true;
            return;
        }

        buf = 0;
        fread(&buf, 4, 1, file);
        if (buf != len)
        {
            Log(LogLevel::Error, "savestate: bad length %d\n", buf);
            error = true;
            return;
        }

        fseek(file, 4, SEEK_CUR);
    }

    current_section = -1;
}

Savestate::~Savestate()
{
    if (error) return;

    if (saving)
    {
        if (current_section != 0xFFFFFFFF)
        {
            u32 pos = (u32)ftell(file);
            fseek(file, current_section + 4, SEEK_SET);

            u32 len = pos - current_section;
            fwrite(&len, 4, 1, file);

            fseek(file, pos, SEEK_SET);
        }

        fseek(file, 0, SEEK_END);
        u32 len = (u32)ftell(file);
        fseek(file, 8, SEEK_SET);
        fwrite(&len, 4, 1, file);
    }

    if (file) fclose(file);
}

void Savestate::Section(const char* magic)
{
    if (error) return;

    if (saving)
    {
        if (current_section != 0xFFFFFFFF)
        {
            u32 pos = (u32)ftell(file);
            fseek(file, current_section + 4, SEEK_SET);

            u32 len = pos - current_section;
            fwrite(&len, 4, 1, file);

            fseek(file, pos, SEEK_SET);
        }

        current_section = (u32)ftell(file);

        fwrite(magic, 4, 1, file);
        fseek(file, 12, SEEK_CUR);
    }
    else
    {
        fseek(file, 0x10, SEEK_SET);

        for (;;)
        {
            u32 buf = 0;

            fread(&buf, 4, 1, file);
            if (buf != ((u32*)magic)[0])
            {
                if (buf == 0)
                {
                    Log(LogLevel::Error, "savestate: section %s not found. blarg\n", magic);
                    return;
                }

                buf = 0;
                fread(&buf, 4, 1, file);
                fseek(file, buf-8, SEEK_CUR);
                continue;
            }

            fseek(file, 12, SEEK_CUR);
            break;
        }
    }
}

void Savestate::Var8(u8* var)
{
    if (error) return;

    if (saving)
    {
        fwrite(var, 1, 1, file);
    }
    else
    {
        fread(var, 1, 1, file);
    }
}

void Savestate::Var16(u16* var)
{
    if (error) return;

    if (saving)
    {
        fwrite(var, 2, 1, file);
    }
    else
    {
        fread(var, 2, 1, file);
    }
}

void Savestate::Var32(u32* var)
{
    if (error) return;

    if (saving)
    {
        fwrite(var, 4, 1, file);
    }
    else
    {
        fread(var, 4, 1, file);
    }
}

void Savestate::Var64(u64* var)
{
    if (error) return;

    if (saving)
    {
        fwrite(var, 8, 1, file);
    }
    else
    {
        fread(var, 8, 1, file);
    }
}

void Savestate::Bool32(bool* var)
{
    // for compability
    if (saving)
    {
        u32 val = *var;
        Var32(&val);
    }
    else
    {
        u32 val;
        Var32(&val);
        *var = val != 0;
    }
}

void Savestate::VarArray(void* data, u32 len)
{
    if (error) return;

    if (saving)
    {
        fwrite(data, len, 1, file);
    }
    else
    {
        fread(data, len, 1, file);
    }
}
