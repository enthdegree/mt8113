# Peripherals

## eFuse

There is an efuse assigned registers around 0x11c5000. 
A brom routine kicks the peripheral alive by pulling up bit2 then waiting for bit0 to go up.
Bytes above that address are read by various callers and determine policy/config.

## The MT8113 BROM's eMMC access

The MT8113 MSDC (Memory stick and Secure Digital card Controller) has registers mapped to 0x11230000.
By the time mtkclient's stage2 executes from BROM download mode, the BROM has already had MSDC start the eMMC in boot-up mode and read boot0's contents in 512-byte blocks.
The eMMC is unresponsive to mtkclient's `mt_sd` driver. 
The eMMC is probably ignoring all its init routines because it's in boot-up mode. I don't know what exactly is going wrong.

At stage2 runtime:

- A software-level stream cursor at register 0x00102aec holds the u32 value 0x0003e400, the length of the boot0 partition's data contents. That likely came from the header.
- Content of boot0 appear starting at some places past around 0x00100040 and stage2 partially overwrites it.
- Calling the BROM's eMMC stream-advance routine returns 0x35 / timeout.
- The EMMC_STS register is all 0s. Calling some BROM routines to start the eMMC in boot-up mode brings the "eMMC bootup state" bit in EMMC_STS up to 1, but it's likely MSDC is just asserting that without any eMMC participation. 

Under normal boot circumstances, the next actor that brings the eMMC out of this state is the Little Kernel preloader in boot0. 
Its setup is what the (working) routines in [`mt8113_emmc.c`](../stage2_static/mt8113_emmc.c) mimick.
