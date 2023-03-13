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

#ifndef EXTERNALBUFFERSAVESTATE_H
#define EXTERNALBUFFERSAVESTATE_H

#include <string>
#include <memory>
#include <vector>
#include <stdio.h>
#include "types.h"

#include "Savestate.h"

class ExternalBufferSavestate final : public Savestate
{
public:
    ExternalBufferSavestate(u8* buffer, size_t buffer_length, bool saving);
    ~ExternalBufferSavestate() final = default;

    void Section(const char* magic) final;

    void Var8(u8* var) final;
    void Var16(u16* var) final;
    void Var32(u32* var) final;
    void Var64(u64* var) final;

    void VarArray(void* data, u32 len) final;

    [[nodiscard]] bool ReachedBufferEnd() const { return _reached_buffer_end; }
    u8* Buffer() { return _buffer; }
    [[nodiscard]] const u8* Buffer() const { return _buffer; }
    void SetBuffer(u8* new_buffer, size_t buffer_length);
    [[nodiscard]] size_t BufferLength() const { return _buffer_length; }
    [[nodiscard]] size_t BufferOffset() const { return _buffer_offset; }


private:
    u8* _buffer;
    size_t _buffer_length;
    size_t _buffer_offset;
    bool _reached_buffer_end;
};

#endif // EXTERNALBUFFERSAVESTATE_H
