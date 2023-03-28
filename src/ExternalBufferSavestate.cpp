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

        // Skip past the length (which we'll fill in at the end)
        // and the reserved data.
        this->Var32(&zero); // Skip past the length, which we'll populate at the end
        this->Var32(&zero); // Skip past the reserved section, which will stay 0

        if (_reached_buffer_end)
        {
            Log(LogLevel::Error, "savestate: not enough memory in provided buffer, consider reallocating\n");
        }
    }
    else
    {
        VersionMajor = 0;
        VersionMinor = 0;

        u32 magic_offset = _buffer_offset;
        u32 loaded_magic = 0;
        Var32(&loaded_magic);
        if (loaded_magic != ((u32 *) SAVESTATE_MAGIC)[0])
        {
            Log(LogLevel::Error, "savestate: invalid magic %#08X at position %#08X\n", loaded_magic, magic_offset);
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

        // Skip past the reserved field
        _buffer_offset += 4;
    }
}

ExternalBufferSavestate::~ExternalBufferSavestate()
{
    if (Error) return;

    if (Saving)
    {
        if (CurSection != 0xFFFFFFFF)
        { // If we're in the middle of writing a section...
            // ...then we should finish up by writing its length in its header.

            // Go back to the current section's header and write its length
            // The section length is in the 4 bytes that follow its magic
            u32 section_length = _buffer_offset - CurSection;
            memcpy(_buffer + _buffer_offset + 4, &section_length, sizeof(section_length));
        }

        // Write the savestate's length in the header
        memcpy(_buffer + 8, &_buffer_offset, sizeof(_buffer_offset));
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
        { // If we're in the middle of writing another section...
            // ...then we should finish up by writing its length in its header.

            // Go back to the current section's header and write its length
            // The section length is in the 4 bytes that follow its magic
            u32 section_length = _buffer_offset - CurSection;
            memcpy(_buffer + _buffer_offset + 4, &section_length, sizeof(section_length));
        }

        // Now that we've finished up the previous section, let's start a new one
        CurSection = _buffer_offset;

        // Copy the magic...
        memcpy(_buffer + _buffer_offset, magic, 4);

        // Zero out the rest of the header (we'll populate it later)
        memset(_buffer + _buffer_offset + 4, 0, 12);

        // Move past the header so we can start writing more data.
        // Section headers are 16 bytes;
        // we'll come back and write the length later.
        // The last 8 bytes of section headers are reserved.
        _buffer_offset += 16;
    }
    else
    {
        // Move to the first byte after the savestate's header
        // (Remember, headers are 16 bytes)
        _buffer_offset = 0x10;

        for (;;)
        { // Until we finish reading this savestate...

            // Load this section's magic number
            u32 loaded_magic = 0;
            memcpy(&loaded_magic, _buffer + _buffer_offset, sizeof(loaded_magic));
            _buffer_offset += 4;

            if (loaded_magic != *((u32 *) magic))
            { // If this isn't the section we're looking for...
                if (loaded_magic == 0)
                {
                    Log(LogLevel::Error, "savestate: section %s not found. blarg\n", magic);
                    return;
                }

                // Determine how big this section is so we can skip it
                size_t loaded_length = 0;
                memcpy(&loaded_length, _buffer + _buffer_offset, sizeof(loaded_magic));

                // Skip past this section and the rest of its header
                _buffer_offset += loaded_length + 12;

                continue;
            }

            // By now we've found the right section.
            // Skip past its header so we can continue loading from it
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
        memcpy(_buffer + _buffer_offset, data, len);
    }
    else
    {
        memcpy(data, _buffer + _buffer_offset, len);
    }

    _buffer_offset += len;
}
