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

#ifndef SAVESTATECOUNTER_H
#define SAVESTATECOUNTER_H

#include "Savestate.h"

/// A Savestate "implementation" that counts the required memory for a savestate.
/// Intended to be used when allocating a buffer for use by ExternalBufferSavestate.
class SavestateCounter final : public Savestate
{
public:
    SavestateCounter();
    ~SavestateCounter() final = default;

    /// Ignores the value and adds 16 to the savestate size.
    void Section(const char* magic) final;

    /// Ignores the value and adds 1 to the savestate size.
    void Var8(u8* var) final;

    /// Ignores the value and adds 2 to the savestate size.
    void Var16(u16* var) final;

    /// Ignores the value and adds 4 to the savestate size.
    void Var32(u32* var) final;

    /// Ignores the value and adds 8 to the savestate size.
    void Var64(u64* var) final;

    /// Ignores the value and adds \c len to the savestate size.
    void VarArray(void* data, u32 len) final;

    [[nodiscard]] size_t RequiredMemory() const { return _required_memory; }


private:
    size_t _required_memory;
};

#endif // SAVESTATECOUNTER_H
