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

enum class [[deprecated]] SavestateMode {
    Load,
    Save,
};

class SavestateWriter;

/// Contains a buffer that's used to serialize the state of the emulated Nintendo DS.
/// May be created with its own buffer, or with an externally-managed one.
// TODO rename to SavestateBuffer
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
    Savestate(u8* buffer, size_t buffer_length);

    Savestate(const Savestate&) = delete;

    /// Cleans up all state.
    /// If this \c Savestate manages its own buffer,
    /// then it will be deallocated.
    /// An externally-owned buffer will \em not be freed.
    ~Savestate();

    [[nodiscard, deprecated]] bool Error() const { return error; }

    [[nodiscard, deprecated]] bool Saving() const { return mode == SavestateMode::Save; }

    /// @returns The length of the allocated memory in bytes,
    /// \em not the number of bytes written to it.
    /// TODO save vs load
    [[nodiscard]] size_t BufferLength() const { return buffer_length; }

    /// @returns The number of bytes written
    /// TODO save vs load
    [[nodiscard, deprecated]] size_t Length() const { return buffer_offset; }

    /// @returns The address of the underlying buffer.
    /// @warning Do \em not save this pointer,
    /// it can change when the referred buffer is resized.
    [[nodiscard]] u8* GetBuffer() { return buffer; }

    /// @returns The address of the underlying buffer.
    /// @warning Do \em not save this pointer if writing to the buffer,
    /// it can change when the referred buffer is resized.
    [[nodiscard]] const u8* GetBuffer() const { return buffer; }

    // TODO rename to IsMemoryOwned
    [[nodiscard]] bool IsBufferOwned() const { return owned_buffer; }

    /// TODO writes a section or advances to this section
    [[deprecated]] void Section(const char* magic);

    [[deprecated]] void Var8(u8* var)
    {
        VarArray(var, sizeof(*var));
    }

    [[deprecated]] void Var16(u16* var)
    {
        VarArray(var, sizeof(*var));
    }

    [[deprecated]] void Var32(u32* var)
    {
        VarArray(var, sizeof(*var));
    }

    [[deprecated]] void Var64(u64* var)
    {
        VarArray(var, sizeof(*var));
    }

    [[deprecated]] void Bool32(bool* var)
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
    [[deprecated]] void VarArray(void* data, u32 len);

    /// TODO
    /// intended for writing to disk or copying to another buffer
    /// @note A pointer to the buffer isn't directly exposed because it may vary over time,
    /// e.g. when reallocating the underlying memory.
    /// @warning do not save the pointer, it can change
    [[deprecated]] bool ProcessData(const std::function<bool(const u8*, u32)>& function) const
    {
        return function(buffer, buffer_offset);
    }

    /// Finishes writing the savestate.
    /// Specifically, this function writes the length in the state's header
    /// and in the current section (if any).
    /// @post Calls to any function do nothing until res
    /// TODO
    [[deprecated]] void Finish();

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
    [[deprecated]] static std::function<bool(const u8*, u32)> SaveFunction(std::string filename);

private:
    u8* buffer;
    u32 buffer_length;
    [[deprecated]] u32 buffer_offset;
    // Index of the first byte in the current section's header
    [[deprecated]] u32 current_section;
    [[deprecated]] u32 version_major;
    [[deprecated]] u32 version_minor;
    [[deprecated]] bool error;
    [[deprecated]] SavestateMode mode;
    bool owned_buffer;
    [[deprecated]] bool closed;

    [[deprecated]] void WriteHeader();

    [[deprecated]] void CloseCurrentSection();

    /// Resizes the buffer to the given length.
    /// @param new_length The new length of the buffer in bytes.
    /// @returns \c true if the buffer was resized successfully.
    bool Resize(u32 new_length);

    friend class SavestateWriter;
};

/// Use this class to serialize the emulated console state.
/// Multiple \c SavestateWriters can operate on the same \c Savestate
/// at different points in time, should you wish to reduce memory allocations.
class SavestateWriter final
{
public:
    /// Constructs a \c SavestateWriter that writes to the given \c Savestate,
    /// starting with the savestate's header.
    /// @param state The \c Savestate to write to.
    /// It \em must live longer than this \c SavestateWriter,
    /// or else you will get undefined behavior.
    /// @note \c state might be resized by this \c SavestateWriter if it's full,
    /// unless it was constructed with an externally-owned buffer.
    explicit SavestateWriter(Savestate& state);

    /// \c SavestateWriter cannot be copied.
    SavestateWriter& operator= (const SavestateWriter&) = delete;

    /// \c SavestateWriter cannot be copied.
    SavestateWriter(const SavestateWriter&) = delete;

    /// Writes a section header to the \c Savestate.
    /// @note May expand the \c Savestate's buffer if it's internally-owned.
    /// @param magic A string that uniquely identifies this section.
    /// It must be exactly four characters long, not counting the null terminator.
    void Section(const char* magic);

    /// Writes a value to the \c Savestate,
    /// expanding the underlying buffer if necessary and possible.
    /// @tparam T The type of the value to write.
    /// Must be an integer (including char and bool) or an enum;
    /// anything else is a compiler error.
    /// This can usually be deduced automatically,
    /// but you can specify it explicitly.
    /// @param var The value to write.
    template<class T>
    void Var(T var)
    {
        static_assert(std::is_integral_v<T> || std::is_enum_v<T>, "Only integers and enums can be written to save states");
        static_assert(!std::is_pointer_v<T>, "Pointers cannot be written to save states (double-check your casts)");
        VarArray(&var, sizeof(var));
    }

    [[deprecated("Use Var<u8> instead")]] void Var8(u8 var)
    {
        VarArray(&var, sizeof(var));
    }

    [[deprecated("Use Var<u16> instead")]] void Var16(u16 var)
    {
        VarArray(&var, sizeof(var));
    }

    [[deprecated("Use Var<u32> instead")]] void Var32(u32 var)
    {
        VarArray(&var, sizeof(var));
    }

    [[deprecated("Use Var<u64> instead")]] void Var64(u64 var)
    {
        VarArray(&var, sizeof(var));
    }

    /// Older savestates used 32-bit values to represent some booleans.
    /// This function is provided for compatibility with those.
    void Bool32(bool var)
    {
        // for compatibility
        Var<u32>(var);
    }

    /// Writes the given data to the \c Savestate.
    /// @param[in] data The data to write.
    /// @param len The size of the data given by \c data, in bytes.
    /// @note Does nothing if \c data is \c nullptr or \c len is zero.
    void VarArray(const void* data, u32 len);

    [[nodiscard]] bool IsAtLeastVersion(u32 major, u32 minor) const
    {
        if (version_major > major) return true;
        if (version_major == major && version_minor >= minor) return true;
        return false;
    }

    /// Finishes writing the savestate.
    /// Specifically, this function closes the current section
    /// and writes the state's overall length in its header.
    void Finish();

private:
    Savestate& state;
    u32 buffer_offset;
    // Index of the first byte in the current section's header
    u32 current_section;
    u16 version_major;
    u16 version_minor;
    bool error;
    bool closed;

    void CloseCurrentSection();
};

/// Use this class to deserialize the emulated console state.
/// This class does not modify the \c Savestate it's given.
/// TODO SFINAE
class SavestateReader final
{
public:
    explicit SavestateReader(const Savestate &state);
    /// \c SavestateReader cannot be copied.
    SavestateReader& operator= (const SavestateReader&) = delete;

    /// \c SavestateReader cannot be copied.
    SavestateReader(const SavestateReader&) = delete;

    void Section(const char* magic);

    /// TODO write docs
    /// TODO talk about sfinae
    template<class T>
    void Var(T& var)
    {
        static_assert(std::is_integral_v<T> || std::is_enum_v<T>, "Only integers and enums can be written to save states");
        static_assert(!std::is_pointer_v<T>, "Pointers cannot be written to save states (double-check your casts)");
        VarArray(&var, sizeof(var));
    }

    [[deprecated("Use Var<u8> instead")]] void Var8(u8& var)
    {
        VarArray(&var, sizeof(var));
    }

    [[deprecated("Use Var<u16> instead")]] void Var16(u16& var)
    {
        VarArray(&var, sizeof(var));
    }

    [[deprecated("Use Var<u32> instead")]] void Var32(u32& var)
    {
        VarArray(&var, sizeof(var));
    }

    [[deprecated("Use Var<u64> instead")]] void Var64(u64& var)
    {
        VarArray(&var, sizeof(var));
    }

    void Bool32(bool& var)
    {
        // for compatibility
        u32 val;
        Var<u32>(val);
        var = val != 0;
    }

    void VarArray(void* data, u32 len);

    [[nodiscard]] bool IsAtLeastVersion(u32 major, u32 minor) const
    {
        if (version_major > major) return true;
        if (version_major == major && version_minor >= minor) return true;
        return false;
    }

    void Finish();

private:
    const Savestate& state;
    u32 buffer_offset;
    // Index of the first byte in the current section's header
    u16 version_major;
    u16 version_minor;
    bool error;
    bool closed;
};

#endif // SAVESTATE_H
