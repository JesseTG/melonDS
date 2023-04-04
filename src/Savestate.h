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

#include <functional>
#include <string>
#include <vector>
#include "types.h"

#define SAVESTATE_MAJOR 10
#define SAVESTATE_MINOR 0

enum class SavestateMode {
    Load,
    Save,
};

class Savestate final
{
public:
    static constexpr size_t DEFAULT_BUFFER_LENGTH = 8 * 1000 * 1000; // 8 MB

    [[deprecated]] Savestate(std::string filename, bool save);

    /// Initializes a \c Savestate in save mode with an internally-managed buffer.
    /// @param initial_buffer_length The initial size of the buffer.
    /// If it's full, a new one that's twice as large will be allocated.
    explicit Savestate(size_t initial_buffer_length = DEFAULT_BUFFER_LENGTH);

    /// Initializes a \c Savestate with a pre-existing buffer.
    /// If this constructor is used
    /// then \c Savestate will not allocate any memory,
    /// not even to expand a full buffer.
    /// This constructor is intended for use with memory that's managed elsewhere.
    ///
    /// @param save true if this \c Savestate is used to save a state,
    /// \c false if used to load it.
    /// @param buffer The byte array that will be used to save or load the state.
    /// It is an error for this to be \c nullptr.
    /// @param buffer_length The size of \c buffer in bytes.
    ///
    Savestate(SavestateMode mode, u8* buffer, size_t buffer_length);

    Savestate(const Savestate&) = delete;
    Savestate(Savestate&& other);

    /// Cleans up all state.
    /// If this \c Savestate manages its own buffer,
    /// then it will be deallocated.
    /// An externally-owned buffer will \em not be freed.
    ~Savestate();

    [[nodiscard]] bool Error() const { return error; }

    [[nodiscard]] bool Saving() const { return mode == SavestateMode::Save; }

    /// @returns The length of the allocated memory in bytes,
    /// \em not the number of bytes written to it.
    /// TODO save vs load
    [[nodiscard]] size_t BufferLength() const { return buffer_length; }

    /// @returns The number of bytes written
    /// TODO save vs load
    [[nodiscard]] size_t Length() const { return buffer_offset; }

    /// TODO writes a section or advances to this section
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
        // for compatibility
        if (Saving())
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
    /// If there is an error, then the state of this buffer is unchanged
    /// except for the ::Error flag.
    /// @param len The size of the buffer given by \c data.
    void VarArray(void* data, u32 len);

    /// TODO
    /// intended for writing to disk or copying to another buffer
    /// @note A pointer to the buffer isn't directly exposed because it may vary over time,
    /// e.g. when reallocating the underlying memory.
    /// @warning do not save the pointer, it can change
    bool ProcessData(const std::function<bool(const u8*, u32)>& function) const
    {
        return function(buffer, buffer_offset);
    }

    /// Finishes writing the savestate.
    /// Specifically, this function writes the length in the state's header
    /// and in the current section (if any).
    /// @post Calls to any function do nothing until res
    /// TODO
    void Finish();

    [[nodiscard]] bool IsAtLeastVersion(u32 major, u32 minor) const
    {
        if (version_major > major) return true;
        if (version_major == major && version_minor >= minor) return true;
        return false;
    }

    /// TODO
    /// Returns a function that saves data to the file.
    /// Intended for use by ::ProcessData.
    /// \param filename
    /// \return
    static std::function<bool(const u8*, u32)> SaveFunction(std::string filename);

private:
    u8* buffer;
    u32 buffer_length;
    u32 buffer_offset;
    // Index of the first byte in the current section's header
    u32 current_section;
    u32 version_major;
    u32 version_minor;
    bool error;
    SavestateMode mode;
    bool owned_buffer;
    bool closed;

    void WriteHeader();

    void CloseCurrentSection();
};

#endif // SAVESTATE_H
