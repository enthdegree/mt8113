"""
Simple EXT_CSD parser for extracting region sizes.
Can be replaced with a more comprehensive parser later.
"""
from struct import unpack


def parse(ext_csd_bytes):
    """Parse raw EXT_CSD bytes and return dictionary of relevant fields.

    Args:
        ext_csd_bytes: 512 bytes of raw EXT_CSD data

    Returns:
        Dictionary with parsed fields
    """
    if len(ext_csd_bytes) != 512:
        raise ValueError(f"EXT_CSD must be 512 bytes, got {len(ext_csd_bytes)}")

    # EXT_CSD fields (all little-endian unless noted)
    boot_mult = ext_csd_bytes[226]  # BOOT_SIZE_MULT
    sec_count = unpack("<I", ext_csd_bytes[212:216])[0]  # SEC_COUNT
    ext_csd_rev = ext_csd_bytes[192]  # EXT_CSD_REV
    card_type = ext_csd_bytes[196]  # CARD_TYPE

    # Calculate sizes
    boot_size_kb = boot_mult * 128  # Boot partition size in KiB
    boot_sectors = (boot_size_kb * 1024) // 512

    return {
        'ext_csd_rev': ext_csd_rev,
        'card_type': card_type,
        'boot_size_mult': boot_mult,
        'boot_sectors': boot_sectors,
        'sec_count': sec_count,
        'regions': {
            'boot0': boot_sectors,
            'boot1': boot_sectors,
            'userdata': sec_count
        }
    }


def print_summary(info):
    """Print formatted summary of EXT_CSD info."""
    print("\nEXT_CSD Summary:")
    print(f"  EXT_CSD Revision: {info['ext_csd_rev']}")
    print(f"  Card Type: 0x{info['card_type']:02x}")
    print(f"  BOOT_SIZE_MULT: {info['boot_size_mult']} (each boot partition: {info['boot_sectors']} sectors)")
    print(f"  SEC_COUNT: {info['sec_count']} sectors")
    print(f"\nRegion Sizes:")
    for region, sectors in info['regions'].items():
        size_mb = (sectors * 512) / (1024 * 1024)
        print(f"  {region:8s}: {sectors:10d} sectors ({size_mb:.2f} MiB)")
