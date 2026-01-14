"""
GPT (GUID Partition Table) parser for eMMC userdata region
Reads and parses GPT header and partition entries
"""
from struct import unpack
import uuid


def parse_gpt_header(sector_data):
    """Parse GPT header from LBA 1 (second sector)

    GPT Header structure (512 bytes, only first 92 bytes used):
    - Offset 0-7: Signature "EFI PART" (0x5452415020494645)
    - Offset 8-11: Revision (0x00010000 for GPT 1.0)
    - Offset 12-15: Header size (usually 92 bytes)
    - Offset 16-19: CRC32 of header
    - Offset 20-23: Reserved (must be zero)
    - Offset 24-31: Current LBA (should be 1)
    - Offset 32-39: Backup LBA
    - Offset 40-47: First usable LBA
    - Offset 48-55: Last usable LBA
    - Offset 56-71: Disk GUID
    - Offset 72-79: Partition entries starting LBA (usually 2)
    - Offset 80-83: Number of partition entries
    - Offset 84-87: Size of partition entry (usually 128 bytes)
    - Offset 88-91: CRC32 of partition entries array
    """
    if len(sector_data) < 512:
        raise ValueError(f"GPT header must be at least 512 bytes, got {len(sector_data)}")

    # Check signature
    signature = sector_data[0:8]
    if signature != b'EFI PART':
        raise ValueError(f"Invalid GPT signature: {signature!r} (expected b'EFI PART')")

    revision = unpack("<I", sector_data[8:12])[0]
    header_size = unpack("<I", sector_data[12:16])[0]
    header_crc32 = unpack("<I", sector_data[16:20])[0]
    current_lba = unpack("<Q", sector_data[24:32])[0]
    backup_lba = unpack("<Q", sector_data[32:40])[0]
    first_usable_lba = unpack("<Q", sector_data[40:48])[0]
    last_usable_lba = unpack("<Q", sector_data[48:56])[0]
    disk_guid = uuid.UUID(bytes_le=bytes(sector_data[56:72]))
    partition_entries_lba = unpack("<Q", sector_data[72:80])[0]
    num_partition_entries = unpack("<I", sector_data[80:84])[0]
    partition_entry_size = unpack("<I", sector_data[84:88])[0]
    partition_array_crc32 = unpack("<I", sector_data[88:92])[0]

    return {
        'signature': signature.decode('ascii'),
        'revision': f"{revision >> 16}.{revision & 0xFFFF}",
        'header_size': header_size,
        'header_crc32': header_crc32,
        'current_lba': current_lba,
        'backup_lba': backup_lba,
        'first_usable_lba': first_usable_lba,
        'last_usable_lba': last_usable_lba,
        'disk_guid': str(disk_guid),
        'partition_entries_lba': partition_entries_lba,
        'num_partition_entries': num_partition_entries,
        'partition_entry_size': partition_entry_size,
        'partition_array_crc32': partition_array_crc32
    }


def parse_partition_entry(entry_data):
    """Parse a single GPT partition entry (128 bytes)

    Partition Entry structure:
    - Offset 0-15: Partition type GUID
    - Offset 16-31: Unique partition GUID
    - Offset 32-39: First LBA
    - Offset 40-47: Last LBA (inclusive)
    - Offset 48-55: Attribute flags
    - Offset 56-127: Partition name (UTF-16LE, 36 characters max)
    """
    if len(entry_data) < 128:
        raise ValueError(f"Partition entry must be 128 bytes, got {len(entry_data)}")

    # Check if entry is unused (all zeros for type GUID)
    type_guid_bytes = entry_data[0:16]
    if type_guid_bytes == b'\x00' * 16:
        return None  # Unused entry

    type_guid = uuid.UUID(bytes_le=bytes(type_guid_bytes))
    unique_guid = uuid.UUID(bytes_le=bytes(entry_data[16:32]))
    first_lba = unpack("<Q", entry_data[32:40])[0]
    last_lba = unpack("<Q", entry_data[40:48])[0]
    attributes = unpack("<Q", entry_data[48:56])[0]

    # Decode partition name (UTF-16LE, null-terminated)
    name_bytes = entry_data[56:128]
    try:
        name = name_bytes.decode('utf-16-le').rstrip('\x00')
    except Exception:
        name = "<decode error>"

    size_sectors = last_lba - first_lba + 1
    size_mb = (size_sectors * 512) / (1024 * 1024)

    return {
        'type_guid': str(type_guid),
        'type_name': get_partition_type_name(type_guid),
        'unique_guid': str(unique_guid),
        'first_lba': first_lba,
        'last_lba': last_lba,
        'size_sectors': size_sectors,
        'size_mb': size_mb,
        'attributes': attributes,
        'name': name
    }


def get_partition_type_name(type_guid):
    """Map common partition type GUIDs to human-readable names"""
    type_guid_str = str(type_guid).lower()

    # Common partition types
    KNOWN_TYPES = {
        'c12a7328-f81f-11d2-ba4b-00a0c93ec93b': 'EFI System',
        '21686148-6449-6e6f-744e-656564454649': 'BIOS Boot',
        '024dee41-33e7-11d3-9d69-0008c781f39f': 'MBR partition scheme',
        'e3c9e316-0b5c-4db8-817d-f92df00215ae': 'Microsoft Reserved',
        'ebd0a0a2-b9e5-4433-87c0-68b6b72699c7': 'Microsoft Basic Data',
        '0fc63daf-8483-4772-8e79-3d69d8477de4': 'Linux Filesystem',
        '0657fd6d-a4ab-43c4-84e5-0933c84b4f4f': 'Linux Swap',
        'e6d6d379-f507-44c2-a23c-238f2a3df928': 'Linux LVM',
        'a19d880f-05fc-4d3b-a006-743f0f84911e': 'Linux RAID',
        '48465300-0000-11aa-aa11-00306543ecac': 'Apple HFS+',
        '7c3457ef-0000-11aa-aa11-00306543ecac': 'Apple APFS',
        '69646961-6700-11aa-aa11-00306543ecac': 'Apple RAID',
    }

    return KNOWN_TYPES.get(type_guid_str, 'Unknown')


def parse_gpt(protective_mbr, gpt_header_sector, partition_entries_data, num_entries, entry_size):
    """Parse complete GPT structure

    Args:
        protective_mbr: Sector 0 (LBA 0) - 512 bytes
        gpt_header_sector: Sector 1 (LBA 1) - 512 bytes
        partition_entries_data: Raw partition entries starting at LBA 2
        num_entries: Number of partition entries from header
        entry_size: Size of each entry from header (usually 128)
    """
    # Parse header
    header = parse_gpt_header(gpt_header_sector)
    # Parse partition entries
    partitions = []
    for i in range(num_entries):
        offset = i * entry_size
        if offset + entry_size > len(partition_entries_data):
            break

        entry_data = partition_entries_data[offset:offset + entry_size]
        entry = parse_partition_entry(entry_data)

        if entry is not None:  # Skip unused entries
            entry['index'] = i
            partitions.append(entry)

    return {
        'header': header,
        'partitions': partitions
    }


def print_gpt_summary(gpt_info):
    """Print formatted GPT information"""
    header = gpt_info['header']
    partitions = gpt_info['partitions']

    print("\n=== GPT Header ===")
    print(f"Signature: {header['signature']}")
    print(f"Revision: {header['revision']}")
    print(f"Header Size: {header['header_size']} bytes")
    print(f"Current LBA: {header['current_lba']}")
    print(f"Backup LBA: {header['backup_lba']}")
    print(f"First Usable LBA: {header['first_usable_lba']}")
    print(f"Last Usable LBA: {header['last_usable_lba']}")
    print(f"Disk GUID: {header['disk_guid']}")
    print(f"Partition Entries: {header['num_partition_entries']} entries starting at LBA {header['partition_entries_lba']}")
    print(f"Partition Entry Size: {header['partition_entry_size']} bytes")

    print(f"\n=== Partitions ({len(partitions)} found) ===")
    for part in partitions:
        print(f"\nPartition {part['index']}:")
        print(f"  Name: {part['name']}")
        print(f"  Type: {part['type_name']}")
        print(f"  Type GUID: {part['type_guid']}")
        print(f"  Unique GUID: {part['unique_guid']}")
        print(f"  Range: LBA {part['first_lba']} - {part['last_lba']}")
        print(f"  Size: {part['size_sectors']} sectors ({part['size_mb']:.2f} MiB)")
        print(f"  Attributes: 0x{part['attributes']:016x}")


def export_gpt_to_dict(gpt_info):
    """Export GPT info as a dictionary suitable for JSON serialization"""
    return {
        'header': gpt_info['header'],
        'partitions': gpt_info['partitions']
    }
