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

#include "ExternalBufferSavestate.h"

#include <cassert>
#include <cstring>

#include "Platform.h"

using Platform::Log;
using Platform::LogLevel;

ExternalBufferSavestate::ExternalBufferSavestate(u8 *buffer, size_t buffer_length, bool saving) :
        _buffer(buffer),
        _buffer_length(buffer_length),
        _buffer_offset(0),
        _reached_buffer_end(false)
{
    Error = false;
    Saving = saving;
    CurSection = -1;

    if (buffer == nullptr)
    {
        Log(LogLevel::Error, "savestate: provided buffer was NULL\n");
        Error = true;
        return;
    }
    else if (buffer_length < 8)
    {
        Log(LogLevel::Error, "savestate: provided buffer was too short\n");
        Error = true;
        return;
    }

    if (Saving)
    {
        VersionMajor = SAVESTATE_MAJOR;
        VersionMinor = SAVESTATE_MINOR;

        u16 major = SAVESTATE_MAJOR;
        u16 minor = SAVESTATE_MINOR;
        u32 zero = 0;

        this->VarArray((void *) SAVESTATE_MAGIC, 4);
        this->Var16(&major);
        this->Var16(&minor);
        this->Var32(&zero); // Length to be fixed at the end

        if (_reached_buffer_end)
        {
            Log(LogLevel::Error, "savestate: not enough memory in provided buffer, consider reallocating\n");
        }
    }
    else
    {
        VersionMajor = 0;
        VersionMinor = 0;

        u32 loaded_magic = 0;
        Var32(&loaded_magic);
        if (loaded_magic != ((u32 *) SAVESTATE_MAGIC)[0])
        {
            Log(LogLevel::Error, "savestate: invalid magic %08X\n", loaded_magic);
            Error = true;
            return;
        }

        u16 loaded_major_version, loaded_minor_version;

        Var16(&loaded_major_version);
        Var16(&loaded_minor_version);
        VersionMajor = loaded_major_version;
        VersionMinor = loaded_minor_version;

        if (VersionMajor != SAVESTATE_MAJOR)
        {
            Log(LogLevel::Error, "savestate: bad version major %d, expecting %d\n", VersionMajor, SAVESTATE_MAJOR);
            Error = true;
            return;
        }

        if (VersionMinor > SAVESTATE_MINOR)
        {
            Log(LogLevel::Error, "savestate: state from the future, %d > %d\n", VersionMinor, SAVESTATE_MINOR);
            Error = true;
            return;
        }

        u32 loaded_length = 0;
        Var32(&loaded_length);

        if (loaded_length > _buffer_length)
        {
            Log(LogLevel::Error, "savestate: length of provided buffer (%zu) is smaller than savestate size (%d)\n", _buffer_length,
                   loaded_length);
            Error = true;
            return;
        }

        _buffer_offset += 4;
    }
}


void ExternalBufferSavestate::SetBuffer(u8 *new_buffer, size_t buffer_length)
{
    if (buffer_length < this->_buffer_length)
    {
        Log(LogLevel::Error, "savestate: setting new buffer that's smaller (%zu) than the original (%zu)\n", buffer_length,
               this->_buffer_length);
    }

    if (new_buffer != nullptr && buffer_length > 0)
    {
        if (buffer_length > _buffer_length)
        {
            _reached_buffer_end = false;
        }
        _buffer = new_buffer;
        _buffer_length = buffer_length;
    }
}

void ExternalBufferSavestate::Section(const char *magic)
{
    if (Error) return;

    if (Saving)
    {
        if (CurSection != 0xFFFFFFFF)
        { // If we haven't yet started saving the emulator's state...
            u32 pos = _buffer_offset;
            _buffer_offset += 4;

            u32 len = pos - CurSection;
            memcpy(_buffer + _buffer_offset, &len, sizeof(len));

            _buffer_offset = pos;
            // Write the current section's length first, *then* the magic
        }

        CurSection = _buffer_offset;

        memcpy(_buffer + _buffer_offset, magic, 4);
        _buffer_offset += 16;
    }
    else
    {
        _buffer_offset = 0x10;
        // Move to the savestate's header...

        for (;;)
        {
            u32 loaded_magic = 0;

            memcpy(&loaded_magic, _buffer + _buffer_offset, sizeof(loaded_magic));
            _buffer_offset += 4;
            if (loaded_magic != ((u32 *) magic)[0])
            {
                if (loaded_magic == 0)
                {
                    Log(LogLevel::Error, "savestate: section %s not found. blarg\n", magic);
                    return;
                }

                loaded_magic = 0;
                memcpy(&loaded_magic, _buffer + _buffer_offset, sizeof(loaded_magic));
                _buffer_offset += 4;
                _buffer_offset -= 8;
                continue;
            }

            _buffer_offset += 12;
            break;
        }
    }
}

void ExternalBufferSavestate::Var8(u8 *var)
{
    VarArray(var, sizeof(*var));
}

void ExternalBufferSavestate::Var16(u16 *var)
{
    VarArray(var, sizeof(*var));
}

void ExternalBufferSavestate::Var32(u32 *var)
{
    VarArray(var, sizeof(*var));
}

void ExternalBufferSavestate::Var64(u64 *var)
{
    VarArray(var, sizeof(*var));
}

void ExternalBufferSavestate::VarArray(void *data, u32 len)
{
    if (Error) return;

    assert(_buffer_offset <= _buffer_length);

    if (data == nullptr)
    {
        Log(LogLevel::Error, "savestate: null pointer into VarArray\n");
        Error = true;
        return;
    }

    if (len == 0)
    {
        return;
    }

    if (_buffer_length - _buffer_offset < len)
    { // If this operation would take us past the buffer's end...
        Log(LogLevel::Error, "savestate: exhausted savestate buffer (%u)\n", len);
        _reached_buffer_end = true;
        Error = true;
        return;
    }

    if (Saving)
    {
        memcpy(_buffer, data, len);
    }
    else
    {
        memcpy(data, _buffer, len);
    }

    _buffer_offset += len;
}