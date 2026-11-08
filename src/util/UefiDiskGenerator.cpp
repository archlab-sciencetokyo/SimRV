/**
 * @file UefiDiskGenerator.cpp
 * @brief Self-contained unprivileged UEFI GPT disk generator (FAT32 ESP + optional rootfs).
 */
#include "simrv/util/UefiDiskGenerator.hpp"

#include <array>
#include <bit>
#include <boost/endian/conversion.hpp>
#include <cstring>
#include <format>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>

namespace simrv::util {

namespace {

constexpr uint32_t kSectorSize = 512;

auto crc32(const uint8_t* data, size_t length) noexcept -> uint32_t {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320u & (-(crc & 1)));
        }
    }
    return ~crc;
}

inline void write_le16(uint8_t* p, uint16_t val) {
    boost::endian::endian_store<uint16_t, 2, boost::endian::order::little>(p, val);
}

inline void write_le32(uint8_t* p, uint32_t val) {
    boost::endian::endian_store<uint32_t, 4, boost::endian::order::little>(p, val);
}

inline void write_le64(uint8_t* p, uint64_t val) {
    boost::endian::endian_store<uint64_t, 8, boost::endian::order::little>(p, val);
}

void pack_dirent(uint8_t* out, std::string_view name83, uint8_t attr, uint32_t cluster,
                 uint32_t size) {
    std::memset(out, 0, 32);
    std::memcpy(out, name83.data(), std::min(name83.size(), size_t{11}));
    out[11] = attr;
    write_le16(out + 20, static_cast<uint16_t>(cluster >> 16));
    write_le16(out + 26, static_cast<uint16_t>(cluster & 0xFFFF));
    write_le32(out + 28, size);
}

void pack_lfn_entry(uint8_t* out, uint8_t seq, std::u16string_view chars, bool last, uint8_t chk) {
    std::memset(out, 0xFF, 32);
    out[0] = seq | (last ? 0x40 : 0x00);
    out[11] = 0x0F;  // Attribute LFN
    out[12] = 0;     // Type
    out[13] = chk;
    out[26] = 0;  // Cluster_lo
    out[27] = 0;

    auto copy_u16 = [&](int out_offset, size_t str_idx) {
        if (str_idx < chars.size()) {
            write_le16(out + out_offset, chars[str_idx]);
        } else if (str_idx == chars.size()) {
            write_le16(out + out_offset, 0x0000);
        } else {
            write_le16(out + out_offset, 0xFFFF);
        }
    };

    // Chars 1..5 at bytes 1..10
    for (size_t i = 0; i < 5; ++i) copy_u16(1 + i * 2, i);
    // Chars 6..11 at bytes 14..25
    for (size_t i = 0; i < 6; ++i) copy_u16(14 + i * 2, 5 + i);
    // Chars 12..13 at bytes 28..31
    for (size_t i = 0; i < 2; ++i) copy_u16(28 + i * 2, 11 + i);
}

auto create_fat32_esp(const std::vector<uint8_t>& kernel_data, uint32_t esp_size_mb)
    -> std::vector<uint8_t> {
    const uint32_t sectors_per_cluster = 1;
    const uint32_t cluster_size = kSectorSize * sectors_per_cluster;
    const uint32_t reserved_sectors = 32;
    const uint32_t num_fats = 2;
    const uint32_t total_sectors = (esp_size_mb * 1024 * 1024) / kSectorSize;

    const uint32_t total_clusters = total_sectors / sectors_per_cluster;
    const uint32_t fat_size_bytes = total_clusters * 4;
    const uint32_t sectors_per_fat = (fat_size_bytes + kSectorSize - 1) / kSectorSize;

    std::vector<uint8_t> volume(total_sectors * kSectorSize, 0);

    // Boot Sector (BPB)
    volume[0] = 0xEB;
    volume[1] = 0x58;
    volume[2] = 0x90;
    std::memcpy(&volume[3], "MSWIN4.1", 8);
    write_le16(&volume[11], kSectorSize);
    volume[13] = sectors_per_cluster;
    write_le16(&volume[14], reserved_sectors);
    volume[16] = num_fats;
    volume[21] = 0xF8;             // Fixed disk
    write_le16(&volume[24], 63);   // Sectors per track
    write_le16(&volume[26], 255);  // Heads
    write_le32(&volume[32], total_sectors);
    write_le32(&volume[36], sectors_per_fat);
    write_le32(&volume[44], 2);  // Root cluster
    write_le16(&volume[48], 1);  // FSInfo sector
    write_le16(&volume[50], 6);  // Backup boot sector
    volume[66] = 0x29;           // Extended boot signature
    write_le32(&volume[67], 0x12345678);
    std::memcpy(&volume[71], "NO NAME    ", 11);
    std::memcpy(&volume[82], "FAT32   ", 8);
    volume[510] = 0x55;
    volume[511] = 0xAA;

    // FSInfo Sector (Sector 1)
    write_le32(&volume[kSectorSize + 0], 0x41615252);
    write_le32(&volume[kSectorSize + 484], 0x61417272);
    write_le32(&volume[kSectorSize + 488], 0xFFFFFFFF);
    write_le32(&volume[kSectorSize + 492], 2);
    volume[kSectorSize + 510] = 0x55;
    volume[kSectorSize + 511] = 0xAA;

    // Backup boot sector (Sector 6)
    std::memcpy(&volume[6 * kSectorSize], &volume[0], kSectorSize);

    const uint32_t cluster_start_sector = reserved_sectors + (num_fats * sectors_per_fat);
    auto cluster_offset = [&](uint32_t cluster_num) {
        return (cluster_start_sector + (cluster_num - 2) * sectors_per_cluster) * kSectorSize;
    };

    auto set_fat = [&](uint32_t cluster_num, uint32_t val) {
        for (uint32_t f = 0; f < num_fats; ++f) {
            size_t base = (reserved_sectors + f * sectors_per_fat) * kSectorSize;
            write_le32(&volume[base + cluster_num * 4], val & 0x0FFFFFFFu);
        }
    };

    set_fat(0, 0x0FFFFFF8);
    set_fat(1, 0x0FFFFFFF);
    set_fat(2, 0x0FFFFFFF);  // Root dir
    set_fat(3, 0x0FFFFFFF);  // EFI dir
    set_fat(4, 0x0FFFFFFF);  // BOOT dir

    const uint32_t kernel_len = static_cast<uint32_t>(kernel_data.size());
    const uint32_t needed_clusters = (kernel_len + cluster_size - 1) / cluster_size;
    for (uint32_t i = 0; i < needed_clusters; ++i) {
        uint32_t curr = 5 + i;
        if (i + 1 < needed_clusters) {
            set_fat(curr, curr + 1);
        } else {
            set_fat(curr, 0x0FFFFFFF);
        }
    }

    // Root Directory (Cluster 2)
    pack_dirent(&volume[cluster_offset(2)], "EFI        ", 0x10, 3, 0);

    // EFI Directory (Cluster 3)
    pack_dirent(&volume[cluster_offset(3) + 0], ".          ", 0x10, 3, 0);
    pack_dirent(&volume[cluster_offset(3) + 32], "..         ", 0x10, 0, 0);
    pack_dirent(&volume[cluster_offset(3) + 64], "BOOT       ", 0x10, 4, 0);

    // BOOT Directory (Cluster 4)
    pack_dirent(&volume[cluster_offset(4) + 0], ".          ", 0x10, 4, 0);
    pack_dirent(&volume[cluster_offset(4) + 32], "..         ", 0x10, 3, 0);

    const std::string_view short_name = "BOOTRI~1EFI";
    uint8_t chk = 0;
    for (char c : short_name) {
        chk =
            static_cast<uint8_t>((((chk & 1) << 7) + (chk >> 1) + static_cast<uint8_t>(c)) & 0xFF);
    }

    pack_lfn_entry(&volume[cluster_offset(4) + 64], 2, u"FI", true, chk);
    pack_lfn_entry(&volume[cluster_offset(4) + 96], 1, u"BOOTRISCV64.E", false, chk);
    pack_dirent(&volume[cluster_offset(4) + 128], short_name, 0x20, 5, kernel_len);

    // Copy kernel data into Cluster 5
    std::memcpy(&volume[cluster_offset(5)], kernel_data.data(), kernel_data.size());

    return volume;
}

}  // namespace

auto generate_uefi_disk(const UefiDiskConfig& config) -> bool {
    std::ifstream kernel_file(config.kernel_payload, std::ios::binary);
    if (!kernel_file) {
        std::cerr << std::format("Error: Cannot open kernel payload: {}\n",
                                 config.kernel_payload.string());
        return false;
    }

    std::vector<uint8_t> kernel_data((std::istreambuf_iterator<char>(kernel_file)),
                                     std::istreambuf_iterator<char>());
    kernel_file.close();

    const auto esp_data = create_fat32_esp(kernel_data, config.esp_size_mb);
    const uint64_t esp_sectors = esp_data.size() / kSectorSize;
    const uint64_t start_lba_p1 = 2048;  // 1 MiB alignment
    const uint64_t end_lba_p1 = start_lba_p1 + esp_sectors - 1;

    uint64_t rootfs_size = 0;
    if (config.rootfs_payload && std::filesystem::is_regular_file(*config.rootfs_payload)) {
        rootfs_size = std::filesystem::file_size(*config.rootfs_payload);
    }
    const uint64_t rootfs_sectors = (rootfs_size + kSectorSize - 1) / kSectorSize;
    const uint64_t start_lba_p2 = end_lba_p1 + 2048;
    const uint64_t end_lba_p2 = (rootfs_sectors > 0) ? (start_lba_p2 + rootfs_sectors - 1) : 0;

    const uint64_t total_sectors = ((rootfs_sectors > 0) ? end_lba_p2 : end_lba_p1) + 2048;

    std::ofstream out(config.output_image, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << std::format("Error: Cannot create disk output: {}\n",
                                 config.output_image.string());
        return false;
    }

    // Protective MBR
    std::vector<uint8_t> mbr(kSectorSize, 0);
    mbr[446 + 4] = 0xEE;           // GPT partition
    write_le32(&mbr[446 + 8], 1);  // Starting LBA
    write_le32(&mbr[446 + 12],
               static_cast<uint32_t>(std::min<uint64_t>(0xFFFFFFFFu, total_sectors - 1)));
    mbr[510] = 0x55;
    mbr[511] = 0xAA;

    // GPT Partition Entries (128 entries * 128 bytes)
    std::vector<uint8_t> entries(128 * 128, 0);

    // ESP Type GUID: C12A7328-F81F-11D2-BA4B-00A0C93EC93B
    constexpr std::array<uint8_t, 16> kEspGuid = {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
                                                  0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};
    std::memcpy(&entries[0], kEspGuid.data(), 16);
    // Deterministic unique partition GUID
    write_le64(&entries[16], 0x1122334455667788ull);
    write_le64(&entries[24], 0x99AABBCCDDEEFF00ull);
    write_le64(&entries[32], start_lba_p1);
    write_le64(&entries[40], end_lba_p1);
    const std::u16string_view p1_name = u"EFI System Partition";
    for (size_t i = 0; i < p1_name.size(); ++i) {
        write_le16(&entries[56 + i * 2], p1_name[i]);
    }

    if (rootfs_sectors > 0) {
        // Linux RootFS GUID: 0FC63DAF-8483-4772-8E79-3D69D8477DE4
        constexpr std::array<uint8_t, 16> kLinuxGuid = {0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84,
                                                        0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69,
                                                        0xD8, 0x47, 0x7D, 0xE4};
        std::memcpy(&entries[128], kLinuxGuid.data(), 16);
        write_le64(&entries[128 + 16], 0x8877665544332211ull);
        write_le64(&entries[128 + 24], 0x00FFEEDDCCBBAA99ull);
        write_le64(&entries[128 + 32], start_lba_p2);
        write_le64(&entries[128 + 40], end_lba_p2);
        const std::u16string_view p2_name = u"Linux RootFS";
        for (size_t i = 0; i < p2_name.size(); ++i) {
            write_le16(&entries[128 + 56 + i * 2], p2_name[i]);
        }
    }

    const uint32_t entries_crc = crc32(entries.data(), entries.size());

    // Primary GPT Header
    std::vector<uint8_t> gpt_header(kSectorSize, 0);
    std::memcpy(&gpt_header[0], "EFI PART", 8);
    write_le32(&gpt_header[8], 0x00010000);           // Rev 1.0
    write_le32(&gpt_header[12], 92);                  // Header size
    write_le64(&gpt_header[24], 1);                   // MyLBA
    write_le64(&gpt_header[32], total_sectors - 1);   // AlternateLBA
    write_le64(&gpt_header[40], 34);                  // FirstUsableLBA
    write_le64(&gpt_header[48], total_sectors - 34);  // LastUsableLBA
    write_le64(&gpt_header[56], 0xCAFEBABEDEADBEEFull);
    write_le64(&gpt_header[64], 0x0123456789ABCDEFull);
    write_le64(&gpt_header[72], 2);    // PartitionEntryLBA
    write_le32(&gpt_header[80], 128);  // NumEntries
    write_le32(&gpt_header[84], 128);  // EntrySize
    write_le32(&gpt_header[88], entries_crc);
    write_le32(&gpt_header[16], crc32(gpt_header.data(), 92));

    // Write MBR at 0
    out.seekp(0);
    out.write(reinterpret_cast<const char*>(mbr.data()), mbr.size());

    // Write Primary GPT Header at LBA 1
    out.seekp(1 * kSectorSize);
    out.write(reinterpret_cast<const char*>(gpt_header.data()), gpt_header.size());

    // Write Primary Entries at LBA 2
    out.seekp(2 * kSectorSize);
    out.write(reinterpret_cast<const char*>(entries.data()), entries.size());

    // Write ESP Data at start_lba_p1
    out.seekp(start_lba_p1 * kSectorSize);
    out.write(reinterpret_cast<const char*>(esp_data.data()), esp_data.size());

    // Write RootFS if present
    if (rootfs_sectors > 0 && config.rootfs_payload) {
        std::ifstream rootfs_in(*config.rootfs_payload, std::ios::binary);
        out.seekp(start_lba_p2 * kSectorSize);
        std::vector<char> buffer(1024 * 1024);
        while (rootfs_in) {
            rootfs_in.read(buffer.data(), buffer.size());
            const auto count = rootfs_in.gcount();
            if (count > 0) {
                out.write(buffer.data(), count);
            }
        }
    }

    // Write Backup Entries at total_sectors - 33
    const uint64_t backup_entries_lba = total_sectors - 33;
    out.seekp(backup_entries_lba * kSectorSize);
    out.write(reinterpret_cast<const char*>(entries.data()), entries.size());

    // Backup GPT Header
    std::vector<uint8_t> backup_header = gpt_header;
    write_le64(&backup_header[24], total_sectors - 1);
    write_le64(&backup_header[32], 1);
    write_le64(&backup_header[72], backup_entries_lba);
    write_le32(&backup_header[16], 0);
    write_le32(&backup_header[16], crc32(backup_header.data(), 92));

    out.seekp((total_sectors - 1) * kSectorSize);
    out.write(reinterpret_cast<const char*>(backup_header.data()), backup_header.size());
    out.close();

    std::cout << std::format("Created UEFI GPT disk image: {} ({} MiB)\n",
                             config.output_image.string(),
                             (total_sectors * kSectorSize) / (1024 * 1024));
    std::cout << std::format("  ESP Partition: LBA {}..{} ({} MiB FAT32)\n", start_lba_p1,
                             end_lba_p1, config.esp_size_mb);
    if (rootfs_sectors > 0) {
        std::cout << std::format("  RootFS Partition: LBA {}..{} ({} MiB)\n", start_lba_p2,
                                 end_lba_p2, rootfs_size / (1024 * 1024));
    }

    return true;
}

}  // namespace simrv::util
