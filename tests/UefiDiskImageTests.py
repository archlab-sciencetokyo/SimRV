#!/usr/bin/env python3
"""Checks the generated GPT ESP and RISC-V removable-media loader path."""

from __future__ import annotations

import struct
import sys
import tempfile
import unittest
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import create_uefi_disk  # noqa: E402


class UefiDiskImageTests(unittest.TestCase):
    def test_gpt_esp_contains_bootriscv64_efi(self) -> None:
        payload = bytes(range(256)) * 7
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            kernel = root / "kernel.efi"
            disk = root / "uefi.img"
            kernel.write_bytes(payload)
            create_uefi_disk.create_uefi_disk(str(disk), str(kernel))

            with disk.open("rb") as image:
                image.seek(512)
                self.assertEqual(image.read(8), b"EFI PART")
                image.seek(1024)
                esp_entry = image.read(128)
                self.assertEqual(
                    esp_entry[:16],
                    uuid.UUID("C12A7328-F81F-11D2-BA4B-00A0C93EC93B").bytes_le,
                )
                esp_lba = struct.unpack_from("<Q", esp_entry, 32)[0]
                esp_base = esp_lba * 512

                image.seek(esp_base)
                boot_sector = image.read(512)
                self.assertEqual(boot_sector[510:512], b"\x55\xaa")
                sectors_per_cluster = boot_sector[13]
                reserved_sectors = struct.unpack_from("<H", boot_sector, 14)[0]
                fat_count = boot_sector[16]
                sectors_per_fat = struct.unpack_from("<I", boot_sector, 36)[0]
                total_sectors = struct.unpack_from("<I", boot_sector, 32)[0]
                cluster_count = (
                    total_sectors - reserved_sectors - fat_count * sectors_per_fat
                ) // sectors_per_cluster
                self.assertGreaterEqual(cluster_count, 65_525)
                self.assertEqual(boot_sector[82:90], b"FAT32   ")

                def cluster_offset(cluster: int) -> int:
                    first_data_sector = reserved_sectors + fat_count * sectors_per_fat
                    sector = first_data_sector + (cluster - 2) * sectors_per_cluster
                    return esp_base + sector * 512

                # EFI/BOOT is cluster 4 in the image generator's fixed directory tree.
                image.seek(cluster_offset(4))
                boot_directory = image.read(160)
                first_lfn = boot_directory[96:128]
                second_lfn = boot_directory[64:96]
                self.assertEqual((first_lfn[11], second_lfn[11]), (0x0F, 0x0F))

                def lfn_text(entry: bytes) -> str:
                    raw = entry[1:11] + entry[14:26] + entry[28:32]
                    return raw.decode("utf-16le").split("\0", 1)[0].replace("\uffff", "")

                filename = lfn_text(first_lfn) + lfn_text(second_lfn)
                self.assertEqual(filename, "BOOTRISCV64.EFI")
                short_entry = boot_directory[128:160]
                self.assertEqual(struct.unpack_from("<I", short_entry, 28)[0], len(payload))


if __name__ == "__main__":
    unittest.main()
