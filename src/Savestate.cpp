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
#include <cassert>
#include "Savestate.h"
#include "Platform.h"

using Platform::Log;
using Platform::LogLevel;

constexpr u32 NO_SECTION = 0xffffffff;
static const char* SAVESTATE_MAGIC = "MELN";

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

Savestate::Savestate(size_t initial_buffer_length) :
    buffer_length(initial_buffer_length),
    buffer_offset(0),
    version_major(SAVESTATE_MAJOR),
    version_minor(SAVESTATE_MINOR),
    mode(SavestateMode::Save),
    owned_buffer(true),
    current_section(NO_SECTION),
    closed(false),
    error(false)
{
    buffer = (u8*)malloc(initial_buffer_length);

    if (!buffer)
    {
        Platform::Log(LogLevel::Error, "Failed to allocate %u-byte savestate buffer\n", initial_buffer_length);
        error = true;
        return;
    }

    WriteHeader();
}

Savestate::Savestate(SavestateMode mode, u8 *buffer, size_t buffer_length) :
    buffer(buffer),
    buffer_length(buffer_length),
    buffer_offset(0),
    version_major(0),
    version_minor(0),
    mode(mode),
    owned_buffer(false),
    current_section(NO_SECTION),
    error(false),
    closed(false)
{
    if (Saving())
    {
        WriteHeader();
    }
    else
    {
        u32 read_magic = 0;

        // Ensure that the file starts with "MELN"
        Var32(&read_magic);
        if (read_magic != *((u32*)SAVESTATE_MAGIC))
        {
            Log(LogLevel::Error, "savestate: expected magic number %#08x (%s), got %#08x\n",
                *((u32*)SAVESTATE_MAGIC),
                SAVESTATE_MAGIC,
                read_magic
            );
            error = true;
            return;
        }

        u16 read_major;
        Var16(&read_major);
        version_major = read_major;
        if (version_major != SAVESTATE_MAJOR)
        {
            Log(LogLevel::Error, "savestate: bad version major %d, expecting %d\n", version_major, SAVESTATE_MAJOR);
            error = true;
            return;
        }

        u16 read_minor;
        Var16(&read_minor);
        version_minor = read_minor;
        if (version_minor > SAVESTATE_MINOR)
        {
            Log(LogLevel::Error, "savestate: state from the future, %d > %d\n", version_minor, SAVESTATE_MINOR);
            error = true;
            return;
        }

        u32 read_length = 0;
        Var32(&read_length);
        if (read_length != buffer_length)
        {
            Log(LogLevel::Error, "savestate: expected a length of %d, got %d\n", buffer_length, read_length);
            error = true;
            return;
        }

        // The next 4 bytes are reserved
        buffer_offset += 4;
    }
}

Savestate::~Savestate()
{
    if (owned_buffer)
    { // If this buffer is managed internally...
        free(buffer);
        // Don't free the buffer if it was externally allocated
    }
}

void Savestate::Section(const char* magic)
{
    if (error || closed) return;

    if (Saving())
    {
        // Go back to the current section's header and write the length
        CloseCurrentSection();

        current_section = buffer_offset;

        // Write the new section's magic number
        VarArray((void*)magic, 4);

        // The next 4 bytes are the length, which we'll come back to later.
        // The 8 bytes afterward are reserved, so we skip them.
        buffer_offset += 12;
    }
    else
    {
        // Start looking at the savestate's beginning, right after its header

        for (u32 offset = 0x10;;)
        { // Until we've found the desired section...
            // Get this section's magic number
            u32 read_magic = 0;
            memcpy(&read_magic, buffer + offset, sizeof(read_magic));
            offset += sizeof(read_magic);

            if (read_magic != ((u32*)magic)[0])
            { // If this isn't the right section...
                if (offset >= buffer_length)
                { // If we've reached the end of the file without finding this section...
                    Log(LogLevel::Error, "savestate: section %s not found. blarg\n", magic);
                    return;
                }

                // ...otherwise, let's keep looking
                u32 read_length = 0;
                memcpy(&read_length, buffer + offset, sizeof(read_length));

                // Skip past the remainder of this section
                offset += sizeof(read_length) + read_length - sizeof(read_length);
                continue;
            }

            offset += 12;
            break;
        }
    }
}

void Savestate::VarArray(void* data, u32 len)
{
    if (error || closed) return;

    assert(buffer_offset <= buffer_length);

    if (Saving())
    {
        if (buffer_offset + len > buffer_length)
        { // If writing the given data would take us past the buffer's end...
            Log(LogLevel::Warn, "savestate: %u-byte write would exceed %u-byte savestate buffer\n", len, buffer_length);

            if (owned_buffer) {
                // If we're allowed to resize this buffer...
                void* resized = realloc(buffer, buffer_length * 2);
                if (!resized)
                { // If the buffer couldn't be expanded...
                    Log(LogLevel::Error, "savestate: Failed to resize owned savestate buffer\n");
                    error = true;
                    return;
                }
                else
                {
                    buffer = static_cast<u8 *>(resized);
                    buffer_length *= 2;
                }
            }
            else {
                Log(LogLevel::Error, "savestate: Buffer is externally-owned, cannot resize it\n");
                error = true;
                return;
            }
        }

        memcpy(buffer + buffer_offset, data, len);
    }
    else
    {
        if (buffer_offset + len > buffer_length)
        { // If reading the requested amount of data would take us past the state's edge...
            Log(LogLevel::Error, "savestate: %u-byte read would exceed %u-byte savestate buffer\n", len, buffer_length);
            error = true;
            return;

            // Can't realloc here.
            // Not only do we not own the buffer pointer (when loading a state),
            // but we can't magically make the desired data appear.
        }

        memcpy(data, buffer + buffer_offset, len);
    }

    buffer_offset += len;
}

void Savestate::Finish()
{
    if (error || closed) return;

    if (Saving())
    {
        // Go back to the current section's header and write the length
        CloseCurrentSection();

        // Write the length of the entire savestate in its header
        memcpy(buffer + 8, &buffer_offset, sizeof(buffer_offset));
    }

    closed = true;
}


std::function<bool(const u8*, u32)> Savestate::SaveFunction(std::string filename)
{
    return [&filename](const u8* data, u32 length) {
        FILE* output = Platform::OpenLocalFile(filename, "wb");
        if (!output) {
            Platform::Log(Platform::Error, "Failed to open \"%s\" for reading\n", filename.c_str());
            return false;
        }

        if (fwrite(data, length, 1, output) < length) {
            Platform::Log(Platform::Error, "Failed to write full savestate to \"%s\"\n", filename.c_str());
            return false;
        }

        fclose(output);
        return true;
    };
}



void Savestate::CloseCurrentSection()
{
    if (current_section != NO_SECTION)
    { // If we're in the middle of writing a section...

        // Go back to the section's header
        // Get the length of the section we've written thus far
        u32 section_length = buffer_offset - current_section;

        // Write the length in the section's header
        // (specifically the first 4 bytes after the magic number)
        memcpy(buffer + current_section + 4, &section_length, sizeof(section_length));

        current_section = NO_SECTION;
    }
}

void Savestate::WriteHeader()
{
    u16 major = SAVESTATE_MAJOR;
    u16 minor = SAVESTATE_MINOR;
    u32 zero = 0;

    VarArray((void *) SAVESTATE_MAGIC, 4);
    Var16(&major);
    Var16(&minor);

    // The next 4 bytes are the file's length, which will be filled in at the end
    Var32(&zero);

    // The following 4 bytes are reserved
    Var32(&zero);
}

// TODO: Clean this up
bool Savestate::Resize(u32 new_length)
{
    if (owned_buffer) {
        // If we're allowed to resize this buffer...
        void* resized = realloc(buffer, new_length);
        if (!resized)
        { // If the buffer couldn't be expanded...
            Log(LogLevel::Error, "savestate: Failed to resize owned savestate buffer\n");
            error = true;
            return false;
        }
        else
        {
            buffer = static_cast<u8 *>(resized);
            buffer_length = new_length;
            return true;
        }
    }
    else {
        Log(LogLevel::Error, "savestate: Buffer is externally-owned, cannot resize it\n");
        error = true;
        return false;
    }
}

SavestateWriter::SavestateWriter(Savestate &state) :
    state(state),
    buffer_offset(0),
    current_section(NO_SECTION),
    error(false),
    version_major(SAVESTATE_MAJOR),
    version_minor(SAVESTATE_MINOR),
    closed(false)
{
    // The magic number
    VarArray((void *) SAVESTATE_MAGIC, 4);

    // The major and minor versions
    Var<u16>(version_major);
    Var<u16>(version_minor);

    // The next 4 bytes are the file's length, which will be filled in at the end
    Var<u32>(0);

    // The following 4 bytes are reserved
    Var<u32>(0);
}

void SavestateWriter::Section(const char *magic)
{
    if (error || closed) return;

    // Go back to the current section's header and write the length
    CloseCurrentSection();

    current_section = buffer_offset;

    // Write the new section's magic number
    VarArray((void*)magic, 4);

    // The next 4 bytes are the length, which we'll come back to later.
    // The 8 bytes afterward are reserved, so we skip them.
    buffer_offset += 12;
}

void SavestateWriter::CloseCurrentSection()
{
    if (current_section != NO_SECTION)
    { // If we're in the middle of writing a section...

        // Go back to the section's header
        // Get the length of the section we've written thus far
        u32 section_length = buffer_offset - current_section;

        // Write the length in the section's header
        // (specifically the first 4 bytes after the magic number)
        memcpy(state.GetBuffer() + current_section + 4, &section_length, sizeof(section_length));

        current_section = NO_SECTION;
    }
}

void SavestateWriter::VarArray(const void* data, u32 len)
{
    if (error || closed) return;

    assert(buffer_offset <= state.BufferLength());

    if (buffer_offset + len > state.BufferLength())
    { // If writing the given data would take us past the buffer's end...
        Log(LogLevel::Warn, "savestate: %u-byte write would exceed %u-byte savestate buffer\n", len, state.BufferLength());

        if (!(state.IsBufferOwned() && state.Resize(state.BufferLength() * 2)))
        { // If we're not allowed to resize this buffer, or if we are but failed...
            Log(LogLevel::Error, "savestate: Failed to write %d bytes to savestate\n", len);
            error = true;
            return;
        }
    }

    memcpy(state.GetBuffer() + buffer_offset, data, len);

    buffer_offset += len;
}

void SavestateWriter::Finish()
{
    if (error || closed) return;

    // Go back to the current section's header and write the length
    CloseCurrentSection();

    // Write the length of the entire savestate in its header
    memcpy(state.GetBuffer() + 8, &buffer_offset, sizeof(buffer_offset));

    closed = true;
}


SavestateReader::SavestateReader(const Savestate &state) :
    state(state),
    buffer_offset(0),
    error(false),
    version_major(SAVESTATE_MAJOR),
    version_minor(SAVESTATE_MINOR),
    closed(false)
{
    u32 read_magic = 0;

    // Ensure that the file starts with "MELN"
    Var<u32>(read_magic);
    if (read_magic != *((u32*)SAVESTATE_MAGIC))
    {
        Log(LogLevel::Error, "savestate: expected magic number %#08x (%s), got %#08x\n",
            *((u32*)SAVESTATE_MAGIC),
            SAVESTATE_MAGIC,
            read_magic
        );
        error = true;
        return;
    }

    Var<u16>(version_major);
    if (version_major != SAVESTATE_MAJOR)
    {
        Log(LogLevel::Error, "savestate: bad version major %d, expecting %d\n", version_major, SAVESTATE_MAJOR);
        error = true;
        return;
    }

    Var<u16>(version_minor);
    if (version_minor > SAVESTATE_MINOR)
    {
        Log(LogLevel::Error, "savestate: state from the future, %d > %d\n", version_minor, SAVESTATE_MINOR);
        error = true;
        return;
    }

    u32 read_length = 0;
    Var<u32>(read_length);
    if (read_length != state.Length())
    {
        Log(LogLevel::Error, "savestate: expected a length of %d, got %d\n", state.Length(), read_length);
        error = true;
        return;
    }

    // The next 4 bytes are reserved
    buffer_offset += 4;
}

void SavestateReader::Section(const char* magic)
{
    if (error || closed) return;

    // Start looking at the savestate's beginning, right after its header

    const u8* buffer = state.GetBuffer();
    u32 buffer_length = state.BufferLength();
    // We're reading from the savestate, so these values won't change

    for (u32 offset = 0x10;;)
    { // Until we've found the desired section...
        // Get this section's magic number
        u32 read_magic = 0;
        memcpy(&read_magic, buffer + offset, sizeof(read_magic));
        offset += sizeof(read_magic);

        if (read_magic != ((u32*)magic)[0])
        { // If this isn't the right section...
            if (offset >= buffer_length)
            { // If we've reached the end of the file without finding this section...
                Log(LogLevel::Error, "savestate: section %s not found. blarg\n", magic);
                return;
            }

            // ...otherwise, let's keep looking
            u32 read_length = 0;
            memcpy(&read_length, buffer + offset, sizeof(read_length));

            // Skip past the remainder of this section
            offset += sizeof(read_length) + read_length - sizeof(read_length);
            continue;
        }

        offset += 12;
        break;
    }
}

void SavestateReader::VarArray(void *data, u32 len)
{
    if (error || closed) return;

    assert(buffer_offset <= state.BufferLength());

    if (buffer_offset + len > state.BufferLength())
    { // If reading the requested amount of data would take us past the state's edge...
        Log(LogLevel::Error, "savestate: %u-byte read would exceed %u-byte savestate buffer\n", len, state.BufferLength());
        error = true;
        return;

        // Can't realloc here.
        // Not only do we not own the buffer pointer (when loading a state),
        // but we can't magically make the desired data appear.
    }

    memcpy(data, state.GetBuffer() + buffer_offset, len);

    buffer_offset += len;
}

void SavestateReader::Finish()
{
    if (error || closed) return;

    closed = true;
}
