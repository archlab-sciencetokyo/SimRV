#!/usr/bin/env python3
"""
Create a UEFI GPT disk image with an EFI System Partition (FAT32) containing BOOTRISCV64.EFI.
Self-contained generator: no external parted, mkfs.fat, or root privileges required.
"""

import argparse
import os
import struct
import uuid
import zlib


def create_fat32_esp(kernel_path: str, esp_size_mb: int = 64) -> bytes:
    """Generate a raw FAT32 volume containing \\EFI\\BOOT\\BOOTRISCV64.EFI."""
    with open(kernel_path, "rb") as f:
        kernel_data = f.read()

    bytes_per_sector = 512
    sectors_per_cluster = 8  # 4 KiB clusters
    cluster_size = bytes_per_sector * sectors_per_cluster
    reserved_sectors = 32
    num_fats = 2
    total_sectors = (esp_size_mb * 1024 * 1024) // bytes_per_sector

    # Calculate FAT size
    # Total clusters approximately = total_sectors / sectors_per_cluster
    total_clusters = total_sectors // sectors_per_cluster
    fat_size_bytes = total_clusters * 4
    sectors_per_fat = (fat_size_bytes + bytes_per_sector - 1) // bytes_per_sector

    # Volume buffer
    volume = bytearray(total_sectors * bytes_per_sector)

    # Boot Sector (BPB)
    struct.pack_into(
        "<3s8sHBHBHHBHHIIIIHHIHH12sBBBI11s8s",
        volume,
        0,
        b"\xeb\x58\x90",  # jmpBoot
        b"MSWIN4.1",  # OEMName
        bytes_per_sector,
        sectors_per_cluster,
        reserved_sectors,
        num_fats,
        0,  # root entry count (0 for FAT32)
        0,  # total sectors 16
        0xF8,  # media descriptor (fixed disk)
        0,  # sectors per FAT 16
        63,  # sectors per track
        255,  # heads
        0,  # hidden sectors
        total_sectors,  # total sectors 32
        sectors_per_fat,
        0,  # ext flags
        0,  # FS version
        2,  # root cluster (Cluster 2)
        1,  # FS info sector
        6,  # backup boot sector
        b"\x00" * 12,
        0x80,  # drive number
        0,  # reserved1
        0x29,  # boot sig
        0x12345678,  # volume ID
        b"NO NAME    ",  # volume label
        b"FAT32   ",  # FS type
    )
    # Boot signature at 510
    volume[510] = 0x55
    volume[511] = 0xAA

    # FSInfo Sector (Sector 1)
    struct.pack_into("<II480sII", volume, bytes_per_sector, 0x41615252, 0x61417272, b"\x00" * 480, 0xAA550000, 0)
    volume[bytes_per_sector + 510] = 0x55
    volume[bytes_per_sector + 511] = 0xAA

    # Backup boot sector (Sector 6)
    volume[6 * bytes_per_sector : 7 * bytes_per_sector] = volume[0:bytes_per_sector]

    # Helper to calculate byte offset for a cluster
    cluster_start_sector = reserved_sectors + (num_fats * sectors_per_fat)

    def cluster_offset(cluster_num: int) -> int:
        return (cluster_start_sector + (cluster_num - 2) * sectors_per_cluster) * bytes_per_sector

    # Helper to set FAT entry
    def set_fat(cluster_num: int, val: int) -> None:
        for fat_idx in range(num_fats):
            fat_base = (reserved_sectors + fat_idx * sectors_per_fat) * bytes_per_sector
            struct.pack_into("<I", volume, fat_base + cluster_num * 4, val & 0x0FFFFFFF)

    # Initialize FAT table
    set_fat(0, 0x0FFFFFF8)  # Media type
    set_fat(1, 0x0FFFFFFF)  # EOF
    set_fat(2, 0x0FFFFFFF)  # Root dir (Cluster 2) EOF
    set_fat(3, 0x0FFFFFFF)  # EFI dir (Cluster 3) EOF
    set_fat(4, 0x0FFFFFFF)  # BOOT dir (Cluster 4) EOF

    # Clusters for kernel file start at 5
    kernel_len = len(kernel_data)
    needed_clusters = (kernel_len + cluster_size - 1) // cluster_size
    for i in range(needed_clusters):
        curr_cluster = 5 + i
        if i + 1 < needed_clusters:
            set_fat(curr_cluster, curr_cluster + 1)
        else:
            set_fat(curr_cluster, 0x0FFFFFFF)  # EOF

    # Format: 11s (name), B (attr), B (nt), B (crt_time_tenth), H (crt_time), H (crt_date), H (lst_acc_date),
    #         H (cluster_hi), H (wrt_time), H (wrt_date), H (cluster_lo), I (size) -> 32 bytes
    def pack_dirent(name: bytes, attr: int, cluster: int, size: int) -> bytes:
        return struct.pack(
            "<11sBBBHHHHHHHI",
            name,
            attr,
            0,
            0,  # nt, crt_time_tenth
            0,
            0,
            0,  # crt_time, crt_date, lst_acc_date
            (cluster >> 16) & 0xFFFF,
            0,
            0,  # wrt_time, wrt_date
            cluster & 0xFFFF,
            size,
        )

    # Write Root Directory Entry: "EFI        " -> Cluster 3
    root_dir_offset = cluster_offset(2)
    volume[root_dir_offset : root_dir_offset + 32] = pack_dirent(b"EFI        ", 0x10, 3, 0)

    # Write EFI Directory (Cluster 3): "." (Cluster 3), ".." (Cluster 0/2), "BOOT       " (Cluster 4)
    efi_dir_offset = cluster_offset(3)
    volume[efi_dir_offset : efi_dir_offset + 32] = pack_dirent(b".          ", 0x10, 3, 0)
    volume[efi_dir_offset + 32 : efi_dir_offset + 64] = pack_dirent(b"..         ", 0x10, 0, 0)
    volume[efi_dir_offset + 64 : efi_dir_offset + 96] = pack_dirent(b"BOOT       ", 0x10, 4, 0)

    # Write BOOT Directory (Cluster 4): ".", "..", "BOOTRISCEFI" (Cluster 5)
    boot_dir_offset = cluster_offset(4)
    volume[boot_dir_offset : boot_dir_offset + 32] = pack_dirent(b".          ", 0x10, 4, 0)
    volume[boot_dir_offset + 32 : boot_dir_offset + 64] = pack_dirent(b"..         ", 0x10, 3, 0)

    # 8.3 short name for BOOTRISCV64.EFI: "BOOTRI~1EFI"
    # To support direct discovery, write "BOOTRISCV64.EFI" using VFAT long filename (LFN) + short name
    # LFN Entry 1: "BOOTRISCV64.EFI"
    lfn1 = bytearray(32)
    lfn1[0] = 0x41  # Sequence 1 | 0x40 (Last LFN)
    # Name chars 1-5: B O O T R
    name1 = "BOOTR".encode("utf-16le")
    lfn1[1:11] = name1
    lfn1[11] = 0x0F  # LFN attribute
    lfn1[12] = 0x00  # type
    # Checksum of short name b"BOOTRI~1EFI"
    short_name = b"BOOTRI~1EFI"
    chk = 0
    for b in short_name:
        chk = (((chk & 1) << 7) + (chk >> 1) + b) & 0xFF
    lfn1[13] = chk
    # Name chars 6-11: I S C V 6 4
    name2 = "ISCV64".encode("utf-16le")
    lfn1[14:26] = name2
    lfn1[26:28] = b"\x00\x00"
    # Name chars 12-13: . E (and F I in next)
    name3 = ".efi".encode("utf-16le") + b"\x00\x00\xff\xff"
    lfn1[28:32] = name3[0:4]

    volume[boot_dir_offset + 64 : boot_dir_offset + 96] = lfn1

    # Short name entry
    volume[boot_dir_offset + 96 : boot_dir_offset + 128] = pack_dirent(short_name, 0x20, 5, kernel_len)

    # Copy Kernel data into clusters 5..N
    kernel_start = cluster_offset(5)
    volume[kernel_start : kernel_start + kernel_len] = kernel_data

    return bytes(volume)


def create_uefi_disk(output_img: str, kernel_path: str, rootfs_path: str | None = None) -> None:
    """Create a partitioned GPT image with an ESP partition (FAT32) and optional ext4 rootfs."""
    esp_data = create_fat32_esp(kernel_path, esp_size_mb=64)
    sector_size = 512

    esp_sectors = len(esp_data) // sector_size
    start_lba_p1 = 2048  # 1 MiB alignment
    end_lba_p1 = start_lba_p1 + esp_sectors - 1

    rootfs_data = b""
    if rootfs_path and os.path.exists(rootfs_path):
        with open(rootfs_path, "rb") as f:
            rootfs_data = f.read()

    rootfs_sectors = (len(rootfs_data) + sector_size - 1) // sector_size if rootfs_data else 0
    start_lba_p2 = end_lba_p1 + 2048
    end_lba_p2 = start_lba_p2 + rootfs_sectors - 1 if rootfs_sectors > 0 else 0

    total_sectors = (end_lba_p2 if rootfs_sectors > 0 else end_lba_p1) + 2048

    disk = bytearray(total_sectors * sector_size)

    # 1. Protective MBR at LBA 0
    disk[446 : 446 + 16] = struct.pack(
        "<BBBBBBBBII",
        0x00,  # Boot indicator
        0x00,
        0x02,
        0x00,  # Starting CHS
        0xEE,  # GPT protective partition
        0xFF,
        0xFF,
        0xFF,  # Ending CHS
        1,  # Starting LBA
        min(0xFFFFFFFF, total_sectors - 1),
    )
    disk[510] = 0x55
    disk[511] = 0xAA

    # 2. GPT Partition Entries at LBA 2..33
    entries = bytearray(128 * 128)
    # Partition 1: EFI System Partition (C12A7328-F81F-11D2-BA4B-00A0C93EC93B)
    esp_type_guid = uuid.UUID("C12A7328-F81F-11D2-BA4B-00A0C93EC93B").bytes_le
    part1_guid = uuid.uuid4().bytes_le
    part1_name = "EFI System Partition".encode("utf-16le")
    struct.pack_into(
        "<16s16sQQQ72s",
        entries,
        0,
        esp_type_guid,
        part1_guid,
        start_lba_p1,
        end_lba_p1,
        0,  # attributes
        part1_name,
    )

    # Partition 2: Linux rootfs if provided (0FC63DAF-8483-4772-8E79-3D69D8477DE4)
    if rootfs_sectors > 0:
        linux_type_guid = uuid.UUID("0FC63DAF-8483-4772-8E79-3D69D8477DE4").bytes_le
        part2_guid = uuid.uuid4().bytes_le
        part2_name = "Linux RootFS".encode("utf-16le")
        struct.pack_into(
            "<16s16sQQQ72s",
            entries,
            128,
            linux_type_guid,
            part2_guid,
            start_lba_p2,
            end_lba_p2,
            0,
            part2_name,
        )

    entries_crc = zlib.crc32(entries)

    # 3. Primary GPT Header at LBA 1
    disk_guid = uuid.uuid4().bytes_le
    gpt_header = bytearray(92)
    struct.pack_into(
        "<8sIIIIQQQQ16sQIII",
        gpt_header,
        0,
        b"EFI PART",  # Signature
        0x00010000,  # Revision 1.0
        92,  # Header size
        0,  # CRC32 placeholder
        0,  # Reserved
        1,  # MyLBA
        total_sectors - 1,  # AlternateLBA
        34,  # FirstUsableLBA
        total_sectors - 34,  # LastUsableLBA
        disk_guid,
        2,  # PartitionEntryLBA
        128,  # NumberOfPartitionEntries
        128,  # SizeOfPartitionEntry
        entries_crc,
    )
    header_crc = zlib.crc32(gpt_header)
    struct.pack_into("<I", gpt_header, 16, header_crc)

    disk[1 * sector_size : 1 * sector_size + 92] = gpt_header
    disk[2 * sector_size : 2 * sector_size + len(entries)] = entries

    # 4. Backup GPT Entries and Header at end of disk
    backup_entries_lba = total_sectors - 33
    disk[backup_entries_lba * sector_size : backup_entries_lba * sector_size + len(entries)] = entries

    backup_gpt_header = bytearray(gpt_header)
    struct.pack_into("<Q", backup_gpt_header, 24, total_sectors - 1)  # MyLBA
    struct.pack_into("<Q", backup_gpt_header, 32, 1)  # AlternateLBA
    struct.pack_into("<Q", backup_gpt_header, 72, backup_entries_lba)  # PartitionEntryLBA
    struct.pack_into("<I", backup_gpt_header, 16, 0)  # Reset CRC
    backup_header_crc = zlib.crc32(backup_gpt_header)
    struct.pack_into("<I", backup_gpt_header, 16, backup_header_crc)

    disk[(total_sectors - 1) * sector_size : (total_sectors - 1) * sector_size + 92] = backup_gpt_header
    p1_byte_offset = start_lba_p1 * sector_size
    disk[p1_byte_offset : p1_byte_offset + len(esp_data)] = esp_data

    # Copy RootFS payload if provided
    if rootfs_sectors > 0:
        p2_byte_offset = start_lba_p2 * sector_size
        disk[p2_byte_offset : p2_byte_offset + len(rootfs_data)] = rootfs_data

    # Write output file
    with open(output_img, "wb") as f:
        f.write(disk)

    print(f"Created UEFI GPT disk image: {output_img} ({len(disk) // (1024 * 1024)} MiB)")
    print(f"  ESP Partition: LBA {start_lba_p1}..{end_lba_p1} (64 MiB FAT32)")
    if rootfs_sectors > 0:
        print(f"  RootFS Partition: LBA {start_lba_p2}..{end_lba_p2} ({len(rootfs_data) // (1024 * 1024)} MiB)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Create a UEFI GPT disk image.")
    parser.add_argument("--kernel", required=True, help="Path to Linux Image (EFI stub executable)")
    parser.add_argument("--rootfs", help="Optional path to ext4 rootfs image")
    parser.add_argument("-o", "--output", default="uefi-disk.img", help="Output disk image path")
    args = parser.parse_args()

    create_uefi_disk(args.output, args.kernel, args.rootfs)
