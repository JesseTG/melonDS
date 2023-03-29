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

#ifndef SAVESTATE_H
#define SAVESTATE_H

#include <string>
#include <stdio.h>
#include "types.h"

#define SAVESTATE_MAJOR 10
#define SAVESTATE_MINOR 0

class Savestate
{
public:
    Savestate(std::string filename, bool save);
    ~Savestate();

    [[nodiscard]] bool Error() const { return error; }

    [[nodiscard]] bool Saving() const { return saving; }

    void Section(const char* magic);

    void Var8(u8* var)
    {
        VarArray(var, sizeof(*var));
    }

    void Var16(u16* var)
    {
        VarArray(var, sizeof(*var));
    }

    void Var32(u32* var)
    {
        VarArray(var, sizeof(*var));
    }

    void Var64(u64* var)
    {
        VarArray(var, sizeof(*var));
    }

    void Bool32(bool* var)
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

    /// Writes or reads the given data into or from the given buffer.
    /// @param[in,out] data The buffer to operate on.
    /// Writes into this buffer if ::saving is true,
    /// reads from it if not.
    /// @param len The size of the buffer given by \c data.
    void VarArray(void* data, u32 len);

    [[nodiscard]] bool IsAtLeastVersion(u32 major, u32 minor) const
    {
        if (version_major > major) return true;
        if (version_major == major && version_minor >= minor) return true;
        return false;
    }

private:
    FILE* file;
    u32 current_section;
    u32 version_major;
    u32 version_minor;
    bool error;
    bool saving;
};

#endif // SAVESTATE_H
