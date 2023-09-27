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

#ifndef DSI_NAND_H
#define DSI_NAND_H

#include "fatfs/ff.h"
#include "types.h"
#include "NDS_Header.h"
#include "DSi_TMD.h"
#include "SPI_Firmware.h"
#include <array>
#include <vector>
#include <string>

struct AES_ctx;

namespace DSi_NAND
{

constexpr u32 MainPartitionOffset = 0x10EE00;
constexpr u32 MainPartitionType = FS_FAT16;

constexpr u32 PhotoPartitionOffset = 0x0CF09A00;
constexpr u32 PhotoPartitionType = FS_FAT12;

enum
{
    TitleData_PublicSav,
    TitleData_PrivateSav,
    TitleData_BannerSav,
};

typedef std::array<u8, 16> AESKey;

/// Represents a raw DSi NAND image before it's mounted by fatfs.
/// Since fatfs can only mount a limited number of file systems at once,
/// file system operations can only be done on a mounted NAND image.
class NANDImage
{
public:
    static std::unique_ptr<NANDImage> New(const std::string& nandpath, const AESKey& es_keyY) noexcept;
    static std::unique_ptr<NANDImage> New(Platform::FileHandle& nandfile, const AESKey& es_keyY) noexcept;
};

class NANDMount
{
public:
    static std::unique_ptr<NANDMount> New(Platform::FileHandle* nandfile, const AESKey& es_keyY) noexcept;
    NANDMount(const NANDMount&) = delete;
    NANDMount(NANDMount&&) noexcept;
    NANDMount& operator=(const NANDMount&) = delete;
    NANDMount& operator=(NANDMount&&) noexcept;
    ~NANDMount() noexcept;

    [[nodiscard]] u64 GetConsoleID() const noexcept { return ConsoleID; }
    [[nodiscard]] const std::array<u8, 16>& GetEMMCCID() const noexcept { return eMMC_CID; }
    Platform::FileHandle* GetFile() noexcept { return CurFile; }
    // TODO: Split everything that doesn't need a mounted file system into a NANDImage
    // (I.e. data that's part of the image, but not part of the file system, like the console ID)
    void ReadHardwareInfo(u8* dataS, u8* dataN);

    void ReadUserData(u8* data);
    void PatchUserData();
    void ListTitles(u32 category, std::vector<u32>& titlelist);
    bool TitleExists(u32 category, u32 titleid);
    void GetTitleInfo(u32 category, u32 titleid, u32& version, NDSHeader* header, NDSBanner* banner);
    bool ImportTitle(const char* appfile, const DSi_TMD::TitleMetadata& tmd, bool readonly);
    bool ImportTitle(const u8* app, size_t appLength, const DSi_TMD::TitleMetadata& tmd, bool readonly);
    void DeleteTitle(u32 category, u32 titleid);
    void RemoveFile(const char* path);
    void RemoveDir(const char* path);
    u32 GetTitleDataMask(u32 category, u32 titleid);
    bool ImportTitleData(u32 category, u32 titleid, int type, const char* file);
    bool ExportTitleData(u32 category, u32 titleid, int type, const char* file);

    // Temporarily public
    u32 ReadFATBlock(u64 addr, u32 len, u8* buf) noexcept;
    u32 WriteFATBlock(u64 addr, u32 len, const u8* buf) noexcept;
private:
    NANDMount(Platform::FileHandle* nandfile) noexcept;
    void SetupFATCrypto(AES_ctx* ctx, u32 ctr) noexcept;
    bool ESEncrypt(u8* data, u32 len) noexcept;
    bool ESDecrypt(u8* data, u32 len) noexcept;
    bool ImportFile(const char* path, const u8* data, size_t len) noexcept;
    bool ImportFile(const char* path, const char* in) noexcept;
    bool ExportFile(const char* path, const char* out) noexcept;
    u32 GetTitleVersion(u32 category, u32 titleid) noexcept;
    bool CreateTicket(const char* path, u32 titleid0, u32 titleid1, u8 version) noexcept;
    bool CreateSaveFile(const char* path, u32 len) noexcept;
    bool InitTitleFileStructure(const NDSHeader& header, const DSi_TMD::TitleMetadata& tmd, bool readonly) noexcept;
    FATFS CurFS {};
    Platform::FileHandle* CurFile;
    std::array<u8, 16> eMMC_CID {};
    u64 ConsoleID {};
    u8 FATIV[16] {};
    u8 FATKey[16] {};
    u8 ESKey[16] {};
};

typedef std::array<u8, 20> SHA1Hash;
typedef std::array<u8, 8> TitleID;

/// Firmware settings for the DSi, saved to the NAND as TWLCFG0.dat or TWLCFG1.dat.
/// @note The file is normally 16KiB, but only the first 432 bytes are used;
/// the rest is FF-padded.
/// This struct excludes the padding.
/// @see https://problemkaputt.de/gbatek.htm#dsisdmmcfirmwaresystemsettingsdatafiles
struct DSiFirmwareSystemSettings
{
    SHA1Hash Hash;
    u8 Zero00[108];
    u8 Version;
    u8 UpdateCounter;
    u8 Zero01[2];
    u32 BelowRAMAreaSize;
    u32 ConfigFlags;
    u8 Zero02;
    u8 CountryCode;
    SPI_Firmware::Language Language;
    u8 RTCYear;
    u32 RTCOffset;
    u8 Zero3[4];
    u8 EULAVersion;
    u8 Zero04[9];
    u8 AlarmHour;
    u8 AlarmMinute;
    u8 Zero05[2];
    bool AlarmEnable;
    u8 Zero06[2];
    u8 SystemMenuUsedTitleSlots;
    u8 SystemMenuFreeTitleSlots;
    u8 Unknown0;
    u8 Unknown1;
    u8 Zero07[3];
    TitleID SystemMenuMostRecentTitleID;
    u16 TouchCalibrationADC1[2];
    u8 TouchCalibrationPixel1[2];
    u16 TouchCalibrationADC2[2];
    u8 TouchCalibrationPixel2[2];
    u8 Unknown2[4];
    u8 Zero08[4];
    u8 FavoriteColor;
    u8 Zero09;
    u8 BirthdayMonth;
    u8 BirthdayDay;
    char16_t Nickname[11];
    char16_t Message[27];
    u8 ParentalControlsFlags;
    u8 Zero10[6];
    u8 ParentalControlsRegion;
    u8 ParentalControlsYearsOfAgeRating;
    u8 ParentalControlsSecretQuestion;
    u8 Unknown3;
    u8 Zero11[2];
    char ParentalControlsPIN[5];
    char16_t ParentalControlsSecretAnswer[65];
};

static_assert(sizeof(DSiFirmwareSystemSettings) == 432, "DSiFirmwareSystemSettings must be exactly 432 bytes");

}

#endif // DSI_NAND_H
