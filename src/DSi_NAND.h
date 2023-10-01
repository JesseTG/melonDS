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
#include <memory>
#include <optional>
#include <vector>
#include <string>

struct AES_ctx;

namespace DSi_NAND
{

constexpr u32 MainPartitionOffset = 0x10EE00;
constexpr u32 MainPartitionType = FS_FAT16;

constexpr u32 PhotoPartitionOffset = 0x0CF09A00;
constexpr u32 PhotoPartitionType = FS_FAT12;

constexpr u32 Stage2BootInfoBlock1Offset = 0x200;

enum
{
    TitleData_PublicSav,
    TitleData_PrivateSav,
    TitleData_BannerSav,
};

typedef std::array<u8, 16> AESKey;
// TODO: Turn this into a proper struct with named fields
using DsiHardwareInfoN = std::array<u8, 0x9C>;
union DSiFirmwareSystemSettings;
class NANDMount;

/// @see https://problemkaputt.de/gbatek.htm#dsisdmmcinternalnandlayout
union Stage2BootInfo
{
    struct
    {
        u8 Zero0[0x20];
        u32 ARM9BootcodeOffset;
        u32 ARM9Size;
        u32 ARM9RAMAddress;
        u32 ARM9SizeAligned;
        u32 ARM7BootcodeOffset;
        u32 ARM7Size;
        u32 ARM7RAMAddress;
        u32 ARM7SizeAligned;
        u8 Zero1[0xBF];
        u8 ARMLoadmodeFlags;
        u8 RSABlock[0x80];
        u8 GlobalMBKSlotSettings[0x14];
        u8 LocalMBKWRAMARM9[0xC];
        u8 LocalMBKWRAMARM7[0xC];
        u8 GlobalMBK9[4];
        u8 Zero2[0x50];
    };
    u8 Bytes[0x200];
};

static_assert(sizeof(Stage2BootInfo) == 0x200, "Stage2BootInfo must be exactly 512 bytes");

/// Represents a raw DSi NAND image before it's mounted by fatfs.
/// Since fatfs can only mount a limited number of file systems at once,
/// file system operations can only be done on a mounted NAND image.
class NANDImage
{
public:
    static std::unique_ptr<NANDImage> New(const std::string& nandpath, const AESKey& es_keyY) noexcept;
    static std::unique_ptr<NANDImage> New(Platform::FileHandle* nandfile, const AESKey& es_keyY) noexcept;
    NANDImage(const NANDImage&) = delete;
    NANDImage& operator=(const NANDImage&) = delete;
    ~NANDImage() noexcept;

    [[nodiscard]] u64 GetConsoleID() const noexcept { return ConsoleID; }
    [[nodiscard]] const std::array<u8, 16>& GetEMMCCID() const noexcept { return eMMC_CID; }
    [[nodiscard]] Platform::FileHandle* GetFile() noexcept { return CurFile; }
    [[nodiscard]] const Stage2BootInfo& GetBootInfo() const noexcept { return BootInfo; }
private:
    friend class NANDMount;
    NANDImage(Platform::FileHandle* nandfile) noexcept;
    u32 ReadFATBlock(u64 addr, u32 len, u8* buf) noexcept;
    u32 WriteFATBlock(u64 addr, u32 len, const u8* buf) noexcept;
    void SetupFATCrypto(AES_ctx* ctx, u32 ctr) noexcept;
    bool ESEncrypt(u8* data, u32 len) noexcept;
    bool ESDecrypt(u8* data, u32 len) noexcept;
    Platform::FileHandle* CurFile;
    std::array<u8, 16> eMMC_CID {};
    u64 ConsoleID {};
    u8 FATIV[16] {};
    u8 FATKey[16] {};
    u8 ESKey[16] {};
    Stage2BootInfo BootInfo {};
};

union DSiSerialData;

class NANDMount
{
public:
    NANDMount() noexcept;
    explicit NANDMount(NANDImage& image) noexcept;
    NANDMount(const NANDMount&) = delete;
    NANDMount(NANDMount&& other) = delete;
    NANDMount& operator=(const NANDMount&) = delete;
    NANDMount& operator=(NANDMount&&) = delete;
    ~NANDMount() noexcept;

    bool ReadHardwareInfoS(DSiSerialData& data) noexcept;
    bool ReadHardwareInfoN(DsiHardwareInfoN& data) noexcept;

    bool ReadUserData(DSiFirmwareSystemSettings& data) noexcept;
    bool PatchUserData(const DSiFirmwareSystemSettings& data) noexcept;
    bool ListTitles(u32 category, std::vector<u32>& titlelist) noexcept;
    bool TitleExists(u32 category, u32 titleid);
    bool GetTitleInfo(u32 category, u32 titleid, u32& version, NDSHeader* header, NDSBanner* banner) noexcept;
    bool ImportTitle(const char* appfile, const DSi_TMD::TitleMetadata& tmd, bool readonly);
    bool ImportTitle(const u8* app, size_t appLength, const DSi_TMD::TitleMetadata& tmd, bool readonly);
    bool DeleteTitle(u32 category, u32 titleid) noexcept;
    bool RemoveFile(const char* path) noexcept;
    bool RemoveDir(const char* path) noexcept;
    u32 GetTitleDataMask(u32 category, u32 titleid);
    bool ImportTitleData(u32 category, u32 titleid, int type, const char* file);
    bool ExportTitleData(u32 category, u32 titleid, int type, const char* file);

    explicit operator bool() const noexcept { return Image != nullptr; }

    // Temporarily public
private:
    friend class NANDImage;
    bool ImportFile(const char* path, const u8* data, size_t len) noexcept;
    bool ImportFile(const char* path, const char* in) noexcept;
    bool ExportFile(const char* path, const char* out) noexcept;
    u32 GetTitleVersion(u32 category, u32 titleid) noexcept;
    bool CreateTicket(const char* path, u32 titleid0, u32 titleid1, u8 version) noexcept;
    bool CreateSaveFile(const char* path, u32 len) noexcept;
    bool InitTitleFileStructure(const NDSHeader& header, const DSi_TMD::TitleMetadata& tmd, bool readonly) noexcept;
    FATFS CurFS {};
    NANDImage* Image;
};

typedef std::array<u8, 20> SHA1Hash;
typedef std::array<u8, 8> TitleID;

/// Firmware settings for the DSi, saved to the NAND as TWLCFG0.dat or TWLCFG1.dat.
/// @note The file is normally 16KiB, but only the first 432 bytes are used;
/// the rest is FF-padded.
/// This struct excludes the padding.
/// @see https://problemkaputt.de/gbatek.htm#dsisdmmcfirmwaresystemsettingsdatafiles
union DSiFirmwareSystemSettings
{
    struct
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
    u8 Bytes[432];

    void UpdateHash();
};

static_assert(sizeof(DSiFirmwareSystemSettings) == 432, "DSiFirmwareSystemSettings must be exactly 432 bytes");

enum class ConsoleRegion : u8
{
    Japan,
    USA,
    Europe,
    Australia,
    China,
    Korea,
};

/// Data file saved to 0:/sys/HWINFO_S.dat.
/// @note The file is normally 16KiB, but only the first 164 bytes are used;
/// the rest is FF-padded.
/// This struct excludes the padding.
/// @see https://problemkaputt.de/gbatek.htm#dsisdmmcfirmwaremiscfiles
union DSiSerialData
{
    u8 Bytes[164];
    struct
    {
        u8 RsaSha1HMAC[0x80];
        u32 Version;
        u32 EntrySize;
        u32 SupportedLanguages;
        u8 Unknown0[4];
        ConsoleRegion Region;
        char Serial[12];
        u8 Unknown1[3];
        u8 TitleIDLSBs[4];
    };
};

static_assert(sizeof(DSiSerialData) == 164, "DSiSerialData must be exactly 164 bytes");


}

#endif // DSI_NAND_H
