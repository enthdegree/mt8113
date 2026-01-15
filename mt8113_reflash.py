#!/usr/bin/env python3
"""
MT8113 eMMC Reflash Tool - Standalone barebones reflashing utility

A minimal Python tool for reading and writing eMMC sectors on MT8113 devices.
This tool communicates directly with the MT8113 stage2 bootloader via USB
and supports both low-level sector operations and high-level partition operations.

PREREQUISITES:
    - Device must be in stage2 mode with eMMC initialized
    - PyUSB library installed: pip install pyusb
    - USB permissions configured for device access

COMMANDS:
    dump-extcsd
        Dump and parse the 512-byte EXT_CSD register from the eMMC device.
        Displays device information including region sizes and saves raw data.

    read-gpt
        Read and parse the GPT (GUID Partition Table) from the userdata region.
        Displays partition names, types, sizes, and LBA ranges.

    read-partition
        Read an entire partition by its label/name from the userdata region.
        Validates partition exists and reads all sectors with progress display.

    write-partition
        Write data to an entire partition by its label/name.
        Validates partition exists and file size matches partition size.
        Prompts for confirmation if file is smaller than partition.

    read
        Low-level sector read from eMMC regions (boot0, boot1, userdata).
        Supports arbitrary start sector and length with bounds checking.

    write
        Low-level sector write to eMMC regions.
        Validates bounds before writing. Use with caution on boot partitions.

USAGE EXAMPLES:
    # Dump and display EXT_CSD information
    python3 mt8113_reflash.py dump-extcsd

    # Read GPT partition table from userdata
    python3 mt8113_reflash.py read-gpt

    # Read entire partition by label
    python3 mt8113_reflash.py read-partition --label rootfs --output rootfs.img

    # Write partition by label (with validation)
    python3 mt8113_reflash.py write-partition --label recovery --input recovery.img

    # Read 1024 sectors from boot0 starting at sector 0
    python3 mt8113_reflash.py read --region boot0 --start 0 --length 1024 --output boot0_backup.bin

    # Read entire userdata region (length=0 means whole region)
    python3 mt8113_reflash.py read --region userdata --start 0 --length 0 --output userdata_full.img

    # Write data to userdata starting at sector 1000
    python3 mt8113_reflash.py write --region userdata --start 1000 --input data.bin

"""

import argparse
import os
import sys
import time
from struct import pack, unpack
import usb.core
import usb.util

import ext_csd_parser
import gpt_parser


# Region mapping (from mt8113_emmc.h)
REGIONS = {
    'boot0': 1,    # EMMC_PART_BOOT0
    'boot1': 2,    # EMMC_PART_BOOT1
    'userdata': 0  # EMMC_PART_USER
}


class MT8113USB:
    """USB communication layer for MT8113 device"""

    def __init__(self):
        # USB device IDs for MT8113 in stage2 mode
        self.vendor_id = 0x0e8d
        self.product_id = 0x0003
        self.dev = None
        self.interface = 1  # Interface 1 is CDC Data with bulk endpoints
        self.ep_out = None
        self.ep_in = None
        self.current_region = None  # Track current region to avoid unnecessary switches

    def connect(self):
        """Find and configure USB device"""
        print("Connecting to MT8113 device...")

        # Find the device
        self.dev = usb.core.find(idVendor=self.vendor_id, idProduct=self.product_id)

        if self.dev is None:
            raise RuntimeError(f"MT8113 device not found (VID:PID = {self.vendor_id:04x}:{self.product_id:04x})")

        print(f"Found device: {self.dev}")

        # Set configuration
        try:
            self.dev.set_configuration()
        except usb.core.USBError as e:
            print(f"Warning: Could not set configuration: {e}")

        # Get the active configuration
        cfg = self.dev.get_active_configuration()
        intf = cfg[(self.interface, 0)]

        # Find the bulk endpoints
        self.ep_out = usb.util.find_descriptor(
            intf,
            custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT
        )

        self.ep_in = usb.util.find_descriptor(
            intf,
            custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN
        )

        if self.ep_out is None or self.ep_in is None:
            raise RuntimeError("Could not find USB endpoints")

        #print(f"Endpoints: OUT={self.ep_out.bEndpointAddress:02x}, IN={self.ep_in.bEndpointAddress:02x}")
        print("Connected successfully")

    def usbwrite(self, data):
        """Write data to USB device"""
        if isinstance(data, int):
            data = pack(">I", data)  # Big-endian for protocol
        self.ep_out.write(data)

    def usbread(self, size):
        """Read data from USB device, accumulating chunks until size is reached"""
        try:
            data = bytearray()
            while len(data) < size:
                chunk = self.ep_in.read(size - len(data), timeout=5000)
                data.extend(chunk)
            return bytes(data)
        except usb.core.USBError as e:
            raise RuntimeError(f"USB read error: {e}")

    def send_command(self, cmd, *args):
        """Send command with magic header and arguments"""
        self.usbwrite(pack(">I", 0xf00dd00d))  # Magic
        self.usbwrite(pack(">I", cmd))  # Command
        for arg in args:
            self.usbwrite(pack(">I", arg))  # Arguments

    def kick_watchdog(self):
        self.send_command(0x3001)

    def read_sector(self, region_id, sector_num):
        """Read single 512-byte sector from specified region"""
        # Send read command
        self.send_command(0x1001, region_id, sector_num)

        # Receive 512 bytes
        data = self.usbread(512)
        if len(data) != 512:
            raise RuntimeError(f"Expected 512 bytes, got {len(data)}")
        return data

    def write_sector(self, region_id, sector_num, data):
        """Write single 512-byte sector to specified region"""
        if len(data) != 512:
            raise ValueError(f"Data must be exactly 512 bytes, got {len(data)}")

        # Send write command
        self.send_command(0x1002, region_id, sector_num)

        # Send 512 bytes of data
        self.usbwrite(data)

        # Wait for success response (0xD0D0D0D0)
        response = self.usbread(4)
        response_val = unpack("<I", response)[0]
        return response_val == 0xD0D0D0D0

    def get_ext_csd(self):
        """Retrieve 512-byte EXT_CSD register from device"""
        self.send_command(0x1003)
        ext_csd = self.usbread(512)
        if len(ext_csd) != 512:
            raise RuntimeError(f"Expected 512 bytes of EXT_CSD, got {len(ext_csd)}")
        return ext_csd


def get_and_save_ext_csd(usb, output_file='ext_csd.bin'):
    """Retrieve EXT_CSD from device, save to file, and parse"""
    print(f"\nRetrieving EXT_CSD from device...")
    ext_csd = usb.get_ext_csd()

    # Save raw EXT_CSD to file
    with open(output_file, 'wb') as f:
        f.write(ext_csd)
    print(f"Saved raw EXT_CSD to {output_file}")

    # Parse and display using external parser module
    info = ext_csd_parser.parse(ext_csd)
    ext_csd_parser.print_summary(info)

    return info


def read_flash(usb, region, start_sector, num_sectors, output_file, region_sizes):
    """Read sectors from eMMC region to file"""
    region_id = REGIONS[region]
    max_sectors = region_sizes[region]

    # Handle length=0 meaning entire region
    if num_sectors == 0:
        num_sectors = max_sectors - start_sector
        print(f"Reading entire region from sector {start_sector} ({num_sectors} sectors)")

    # Bounds checking
    if start_sector >= max_sectors:
        raise ValueError(f"Start sector {start_sector} exceeds region size {max_sectors} sectors")
    if start_sector + num_sectors > max_sectors:
        raise ValueError(f"Read range {start_sector}+{num_sectors} exceeds region size {max_sectors} sectors")

    print(f"\nReading {num_sectors} sectors from {region} starting at sector {start_sector}")
    print(f"Output file: {output_file}")

    # Progress tracking
    start_time = time.time()
    bytes_per_sector = 512

    with open(output_file, 'wb') as f:
        for i in range(num_sectors):
            sector_num = start_sector + i
            data = usb.read_sector(region_id, sector_num)
            f.write(data)

            # Progress display with speed and ETA
            elapsed = time.time() - start_time
            if elapsed > 0 and (i % 10 == 0 or i == num_sectors - 1):  # Update every 10 sectors
                sectors_done = i + 1
                speed_sectors_per_sec = sectors_done / elapsed
                speed_mbps = (speed_sectors_per_sec * bytes_per_sector) / (1024 * 1024)
                remaining_sectors = num_sectors - sectors_done
                eta_seconds = remaining_sectors / speed_sectors_per_sec if speed_sectors_per_sec > 0 else 0

                print(f"Read: {sectors_done}/{num_sectors} sectors "
                      f"({speed_mbps:.2f} MB/s, ETA: {eta_seconds:.0f}s)    ", end='\r', flush=True)

    print()  # Newline after progress
    elapsed_total = time.time() - start_time
    avg_speed = (num_sectors * bytes_per_sector) / (1024 * 1024) / elapsed_total
    print(f"Read complete: {num_sectors} sectors in {elapsed_total:.1f}s (avg {avg_speed:.2f} MB/s)")


def write_flash(usb, region, start_sector, input_file, region_sizes):
    """Write sectors from file to eMMC region"""
    region_id = REGIONS[region]
    max_sectors = region_sizes[region]

    # Get input file size and calculate sector count
    file_size = os.path.getsize(input_file)
    file_sectors = (file_size + 511) // 512  # Round up

    # Bounds checking
    if start_sector >= max_sectors:
        raise ValueError(f"Start sector {start_sector} exceeds region size {max_sectors} sectors")
    if start_sector + file_sectors > max_sectors:
        raise ValueError(f"Write range {start_sector}+{file_sectors} exceeds region size {max_sectors} sectors")

    print(f"\nWriting {file_sectors} sectors to {region} starting at sector {start_sector}")
    print(f"Input file: {input_file} ({file_size} bytes)")

    # Progress tracking
    start_time = time.time()
    bytes_per_sector = 512
    sectors_written = 0

    with open(input_file, 'rb') as f:
        sector_num = start_sector
        while True:
            data = f.read(512)
            if not data:
                break

            # Pad last sector to 512 bytes if needed
            if len(data) < 512:
                data += b'\x00' * (512 - len(data))

            success = usb.write_sector(region_id, sector_num, data)
            if not success:
                raise RuntimeError(f"Write failed at sector {sector_num}")

            sectors_written += 1
            sector_num += 1

            # Progress display with speed and ETA
            elapsed = time.time() - start_time
            if elapsed > 0 and (sectors_written % 10 == 0 or sectors_written == file_sectors):  # Update every 10 sectors
                speed_sectors_per_sec = sectors_written / elapsed
                speed_mbps = (speed_sectors_per_sec * bytes_per_sector) / (1024 * 1024)
                remaining_sectors = file_sectors - sectors_written
                eta_seconds = remaining_sectors / speed_sectors_per_sec if speed_sectors_per_sec > 0 else 0

                print(f"Write: {sectors_written}/{file_sectors} sectors "
                      f"({speed_mbps:.2f} MB/s, ETA: {eta_seconds:.0f}s)    ", end='\r', flush=True)

    print()  # Newline after progress
    elapsed_total = time.time() - start_time
    avg_speed = (sectors_written * bytes_per_sector) / (1024 * 1024) / elapsed_total
    print(f"Write complete: {sectors_written} sectors in {elapsed_total:.1f}s (avg {avg_speed:.2f} MB/s)")


def read_gpt(usb, output_file=None):
    """Read and parse GPT partition table from userdata region

    GPT structure:
    - LBA 0: Protective MBR (sector 0)
    - LBA 1: GPT Header (sector 1)
    - LBA 2+: Partition entries (typically 128 entries * 128 bytes = 16KB = 32 sectors)
    """
    print("\nReading GPT partition table from userdata region...")

    region_id = REGIONS['userdata']

    # Read protective MBR (LBA 0)
    print("Reading protective MBR (LBA 0)...")
    protective_mbr = usb.read_sector(region_id, 0)

    # Read GPT header (LBA 1)
    print("Reading GPT header (LBA 1)...")
    gpt_header_sector = usb.read_sector(region_id, 1)

    # Parse header to get partition entry information
    try:
        header = gpt_parser.parse_gpt_header(gpt_header_sector)
    except ValueError as e:
        print(f"Error: {e}")
        print("No valid GPT found on userdata region")
        return None

    # Calculate how many sectors we need to read for partition entries
    entries_per_sector = 512 // header['partition_entry_size']
    sectors_needed = (header['num_partition_entries'] + entries_per_sector - 1) // entries_per_sector

    print(f"Reading {sectors_needed} sectors of partition entries (starting at LBA {header['partition_entries_lba']})...")

    # Read partition entry sectors
    partition_entries_data = bytearray()
    for i in range(sectors_needed):
        sector = usb.read_sector(region_id, header['partition_entries_lba'] + i)
        partition_entries_data.extend(sector)

    # Parse complete GPT
    gpt_info = gpt_parser.parse_gpt(
        protective_mbr,
        gpt_header_sector,
        partition_entries_data,
        header['num_partition_entries'],
        header['partition_entry_size']
    )

    # Display parsed information
    gpt_parser.print_gpt_summary(gpt_info)

    # Optionally save raw GPT data to file
    if output_file:
        print(f"\nSaving raw GPT data to {output_file}...")
        with open(output_file, 'wb') as f:
            f.write(protective_mbr)
            f.write(gpt_header_sector)
            f.write(partition_entries_data)
        total_bytes = len(protective_mbr) + len(gpt_header_sector) + len(partition_entries_data)
        print(f"Saved {total_bytes} bytes ({total_bytes // 512} sectors)")

    return gpt_info


def find_partition_by_label(gpt_info, label):
    """Find a partition by its label/name

    Args:
        gpt_info: Parsed GPT information from read_gpt()
        label: Partition name to search for (case-insensitive)

    Returns:
        Partition dict if found, None otherwise
    """
    if gpt_info is None:
        return None

    label_lower = label.lower()
    for part in gpt_info['partitions']:
        if part['name'].lower() == label_lower:
            return part

    return None


def read_partition(usb, label, output_file):
    """Read entire partition by label to file

    Args:
        usb: MT8113USB instance
        label: Partition label/name
        output_file: Output filename
    """
    print(f"\nReading partition '{label}'...")

    # First read GPT to find the partition
    gpt_info = read_gpt(usb, output_file=None)
    if gpt_info is None:
        raise RuntimeError("Failed to read GPT partition table")

    # Find partition by label
    partition = find_partition_by_label(gpt_info, label)
    if partition is None:
        available = [p['name'] for p in gpt_info['partitions']]
        raise ValueError(f"Partition '{label}' not found. Available partitions: {', '.join(available)}")

    print(f"\nFound partition: {partition['name']}")
    print(f"  Type: {partition['type_name']}")
    print(f"  Range: LBA {partition['first_lba']} - {partition['last_lba']}")
    print(f"  Size: {partition['size_sectors']} sectors ({partition['size_mb']:.2f} MiB)")

    # Read the partition
    region_id = REGIONS['userdata']
    start_lba = partition['first_lba']
    num_sectors = partition['size_sectors']

    print(f"\nReading {num_sectors} sectors starting at LBA {start_lba}...")

    # Progress tracking
    start_time = time.time()
    bytes_per_sector = 512

    with open(output_file, 'wb') as f:
        for i in range(num_sectors):
            sector_num = start_lba + i
            data = usb.read_sector(region_id, sector_num)
            f.write(data)

            # Progress display with speed and ETA
            elapsed = time.time() - start_time
            if elapsed > 0 and (i % 10 == 0 or i == num_sectors - 1):
                sectors_done = i + 1
                speed_sectors_per_sec = sectors_done / elapsed
                speed_mbps = (speed_sectors_per_sec * bytes_per_sector) / (1024 * 1024)
                remaining_sectors = num_sectors - sectors_done
                eta_seconds = remaining_sectors / speed_sectors_per_sec if speed_sectors_per_sec > 0 else 0

                print(f"Read: {sectors_done}/{num_sectors} sectors "
                      f"({speed_mbps:.2f} MB/s, ETA: {eta_seconds:.0f}s)    ", end='\r', flush=True)

    print()  # Newline after progress
    elapsed_total = time.time() - start_time
    avg_speed = (num_sectors * bytes_per_sector) / (1024 * 1024) / elapsed_total
    print(f"Read complete: {num_sectors} sectors in {elapsed_total:.1f}s (avg {avg_speed:.2f} MB/s)")


def write_partition(usb, label, input_file):
    """Write entire partition by label from file

    Args:
        usb: MT8113USB instance
        label: Partition label/name
        input_file: Input filename

    Validates that file size matches partition size exactly.
    """
    print(f"\nWriting partition '{label}'...")

    # Get input file size
    file_size = os.path.getsize(input_file)
    file_sectors = (file_size + 511) // 512  # Round up

    # First read GPT to find the partition
    gpt_info = read_gpt(usb, output_file=None)
    if gpt_info is None:
        raise RuntimeError("Failed to read GPT partition table")

    # Find partition by label
    partition = find_partition_by_label(gpt_info, label)
    if partition is None:
        available = [p['name'] for p in gpt_info['partitions']]
        raise ValueError(f"Partition '{label}' not found. Available partitions: {', '.join(available)}")

    print(f"\nFound partition: {partition['name']}")
    print(f"  Type: {partition['type_name']}")
    print(f"  Range: LBA {partition['first_lba']} - {partition['last_lba']}")
    print(f"  Size: {partition['size_sectors']} sectors ({partition['size_mb']:.2f} MiB)")

    # Validate file size matches partition size
    partition_sectors = partition['size_sectors']
    if file_sectors > partition_sectors:
        raise ValueError(
            f"File is too large for partition '{label}'\n"
            f"  File size: {file_sectors} sectors ({file_size} bytes)\n"
            f"  Partition size: {partition_sectors} sectors ({partition_sectors * 512} bytes)\n"
            f"  Exceeds by: {file_sectors - partition_sectors} sectors"
        )

    if file_sectors < partition_sectors:
        print(f"\nWarning: File is smaller than partition")
        print(f"  File size: {file_sectors} sectors ({file_size} bytes)")
        print(f"  Partition size: {partition_sectors} sectors ({partition_sectors * 512} bytes)")
        print(f"  {partition_sectors - file_sectors} sectors will remain unchanged at the end")

        # Ask for confirmation
        response = input("Continue with write? [y/N]: ")
        if response.lower() not in ['y', 'yes']:
            print("Write cancelled")
            return

    # Write the partition
    region_id = REGIONS['userdata']
    start_lba = partition['first_lba']

    print(f"\nWriting {file_sectors} sectors starting at LBA {start_lba}...")

    # Progress tracking
    start_time = time.time()
    bytes_per_sector = 512
    sectors_written = 0

    with open(input_file, 'rb') as f:
        sector_num = start_lba
        while True:
            data = f.read(512)
            if not data:
                break

            # Pad last sector to 512 bytes if needed
            if len(data) < 512:
                data += b'\x00' * (512 - len(data))

            success = usb.write_sector(region_id, sector_num, data)
            if not success:
                raise RuntimeError(f"Write failed at sector {sector_num}")

            sectors_written += 1
            sector_num += 1

            # Progress display with speed and ETA
            elapsed = time.time() - start_time
            if elapsed > 0 and (sectors_written % 10 == 0 or sectors_written == file_sectors):
                speed_sectors_per_sec = sectors_written / elapsed
                speed_mbps = (speed_sectors_per_sec * bytes_per_sector) / (1024 * 1024)
                remaining_sectors = file_sectors - sectors_written
                eta_seconds = remaining_sectors / speed_sectors_per_sec if speed_sectors_per_sec > 0 else 0

                print(f"Write: {sectors_written}/{file_sectors} sectors "
                      f"({speed_mbps:.2f} MB/s, ETA: {eta_seconds:.0f}s)    ", end='\r', flush=True)

    print()  # Newline after progress
    elapsed_total = time.time() - start_time
    avg_speed = (sectors_written * bytes_per_sector) / (1024 * 1024) / elapsed_total
    print(f"Write complete: {sectors_written} sectors in {elapsed_total:.1f}s (avg {avg_speed:.2f} MB/s)")


def generate_test_sector(sector_index):
    """Generate a test sector with systematic pattern: 0x00C0FFEE ^ word_index"""
    data = bytearray(512)
    for i in range(128):  # 128 32-bit words per sector
        word_index = sector_index * 128 + i
        value = 0x00C0FFEE ^ word_index
        # Little-endian 32-bit word
        data[i*4:i*4+4] = pack("<I", value)
    return bytes(data)


def roundtrip_test(usb, region, start_sector, num_sectors, region_sizes):
    """Perform roundtrip write/read test on specified sectors

    1. Read original data (for restoration)
    2. Write systematic test pattern
    3. Read back and verify
    4. Restore original data

    Args:
        usb: MT8113USB instance
        region: Region name (boot0, boot1, userdata)
        start_sector: Starting sector number
        num_sectors: Number of sectors to test
        region_sizes: Dict of region sizes from EXT_CSD
    """
    region_id = REGIONS[region]
    max_sectors = region_sizes[region]

    # Bounds checking
    if start_sector >= max_sectors:
        raise ValueError(f"Start sector {start_sector} exceeds region size {max_sectors} sectors")
    if start_sector + num_sectors > max_sectors:
        raise ValueError(f"Test range {start_sector}+{num_sectors} exceeds region size {max_sectors} sectors")

    print(f"\n=== Roundtrip Test ===")
    print(f"Region: {region} (id={region_id})")
    print(f"Sectors: {start_sector} to {start_sector + num_sectors - 1} ({num_sectors} sectors)")
    print(f"Test pattern: 0x00C0FFEE ^ word_index")

    # Step 1: Read original data
    print(f"\n[1/4] Reading original data...")
    original_data = []
    for i in range(num_sectors):
        sector_num = start_sector + i
        data = usb.read_sector(region_id, sector_num)
        original_data.append(data)
        print(f"  Read sector {sector_num} ({i+1}/{num_sectors})", end='\r')
    print()

    # Step 2: Write test pattern
    print(f"\n[2/4] Writing test pattern...")
    for i in range(num_sectors):
        sector_num = start_sector + i
        test_data = generate_test_sector(i)
        success = usb.write_sector(region_id, sector_num, test_data)
        if not success:
            raise RuntimeError(f"Write failed at sector {sector_num}")
        print(f"  Wrote sector {sector_num} ({i+1}/{num_sectors})", end='\r')
    print()

    # Step 3: Read back and verify
    print(f"\n[3/4] Reading back and verifying...")
    errors = []
    for i in range(num_sectors):
        sector_num = start_sector + i
        readback = usb.read_sector(region_id, sector_num)
        expected = generate_test_sector(i)

        if readback != expected:
            # Find first mismatch
            for j in range(512):
                if readback[j] != expected[j]:
                    errors.append({
                        'sector': sector_num,
                        'offset': j,
                        'expected': expected[j],
                        'got': readback[j]
                    })
                    break
        print(f"  Verified sector {sector_num} ({i+1}/{num_sectors})", end='\r')
    print()

    # Step 4: Restore original data
    print(f"\n[4/4] Restoring original data...")
    for i in range(num_sectors):
        sector_num = start_sector + i
        success = usb.write_sector(region_id, sector_num, original_data[i])
        if not success:
            raise RuntimeError(f"Restore failed at sector {sector_num}")
        print(f"  Restored sector {sector_num} ({i+1}/{num_sectors})", end='\r')
    print()

    # Report results
    print(f"\n=== Results ===")
    if errors:
        print(f"FAILED: {len(errors)} sector(s) had verification errors")
        for err in errors[:5]:  # Show first 5 errors
            print(f"  Sector {err['sector']} offset {err['offset']}: "
                  f"expected 0x{err['expected']:02x}, got 0x{err['got']:02x}")
        if len(errors) > 5:
            print(f"  ... and {len(errors) - 5} more errors")
        return False
    else:
        print(f"PASSED: All {num_sectors} sectors verified successfully")
        return True


def main():
    parser = argparse.ArgumentParser(
        description='MT8113 eMMC Reflash Tool - Read and write eMMC sectors',
        epilog='Example: python3 mt8113_reflash.py read --region boot0 --start 0 --length 1024 --output boot0.bin'
    )
    subparsers = parser.add_subparsers(dest='command', required=True, help='Command to execute')

    # Dump EXT_CSD command
    extcsd_parser = subparsers.add_parser('dump-extcsd',
                                          help='Dump and parse EXT_CSD register')
    extcsd_parser.add_argument('--output', default='ext_csd.bin',
                              help='Output filename for raw EXT_CSD (default: ext_csd.bin)')

    # Read GPT command
    gpt_parser_cmd = subparsers.add_parser('read-gpt',
                                           help='Read and parse GPT partition table from userdata')
    gpt_parser_cmd.add_argument('--output', default=None,
                               help='Optional: save raw GPT data to file')

    # Read partition by label command
    read_part_parser = subparsers.add_parser('read-partition',
                                            help='Read entire partition by label/name')
    read_part_parser.add_argument('--label', required=True,
                                 help='Partition label/name')
    read_part_parser.add_argument('--output', required=True,
                                 help='Output filename')

    # Write partition by label command
    write_part_parser = subparsers.add_parser('write-partition',
                                             help='Write entire partition by label/name')
    write_part_parser.add_argument('--label', required=True,
                                  help='Partition label/name')
    write_part_parser.add_argument('--input', required=True,
                                  help='Input filename')

    # Read command
    read_parser = subparsers.add_parser('read',
                                       help='Read sectors from eMMC region')
    read_parser.add_argument('--region', required=True,
                            choices=['boot0', 'boot1', 'userdata'],
                            help='eMMC region to read from')
    read_parser.add_argument('--start', type=int, required=True,
                            help='Starting sector number')
    read_parser.add_argument('--length', type=int, required=True,
                            help='Number of sectors to read (0 = entire region from start)')
    read_parser.add_argument('--output', required=True,
                            help='Output filename')

    # Write command
    write_parser = subparsers.add_parser('write',
                                        help='Write sectors to eMMC region')
    write_parser.add_argument('--region', required=True,
                             choices=['boot0', 'boot1', 'userdata'],
                             help='eMMC region to write to')
    write_parser.add_argument('--start', type=int, required=True,
                             help='Starting sector number')
    write_parser.add_argument('--input', required=True,
                             help='Input filename')

    # Roundtrip test command - tests end of boot1 (safe, boot1 is typically empty)
    # boot1 is 4MB = 8192 sectors, test 100 sectors starting at 8000
    subparsers.add_parser('roundtrip-test',
                          help='Write/read/verify roundtrip test (uses end of boot1)')

    args = parser.parse_args()

    try:
        # Connect to USB device
        usb = MT8113USB()
        usb.connect()

        if args.command == 'dump-extcsd':
            # Dump EXT_CSD command
            get_and_save_ext_csd(usb, args.output)
            print(f"\nEXT_CSD dumped successfully to {args.output}")

        elif args.command == 'read-gpt':
            # Read GPT partition table
            read_gpt(usb, args.output)
            print("\nGPT read successfully")

        elif args.command == 'read-partition':
            # Read partition by label
            read_partition(usb, args.label, args.output)
            print(f"\nPartition '{args.label}' read successfully to {args.output}")

        elif args.command == 'write-partition':
            # Write partition by label
            write_partition(usb, args.label, args.input)
            print(f"\nPartition '{args.label}' written successfully from {args.input}")

        elif args.command == 'read':
            # Get region sizes first
            info = get_and_save_ext_csd(usb, 'ext_csd.bin')
            region_sizes = info['regions']

            # Perform read operation
            read_flash(usb, args.region, args.start, args.length,
                      args.output, region_sizes)

        elif args.command == 'write':
            # Get region sizes first
            info = get_and_save_ext_csd(usb, 'ext_csd.bin')
            region_sizes = info['regions']

            # Perform write operation
            write_flash(usb, args.region, args.start, args.input,
                       region_sizes)

        elif args.command == 'roundtrip-test':
            # Get region sizes first
            info = get_and_save_ext_csd(usb, 'ext_csd.bin')
            region_sizes = info['regions']

            # Perform roundtrip test on end of boot1 (safe area)
            success = roundtrip_test(usb, 'boot1', 8000, 100, region_sizes)
            sys.exit(0 if success else 1)

    except KeyboardInterrupt:
        print("\n\nOperation cancelled by user")
        sys.exit(1)
    except Exception as e:
        print(f"\nError: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
