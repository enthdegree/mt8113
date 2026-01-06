# Peripherals

## eFuse

There is an efuse assigned registers around 0x11c5000. 
A brom routine kicks the peripheral alive by pulling up bit2 then waiting for bit0 to go up.
Bytes above that address (e.g. 0x11c5020) are read by various callers and determine policy/config.

## MSDC, eMMC access in the Kobo Clara BW's BROM download mode context

The MT8113 MSDC (Memory stick and Secure Digital card Controller) has registers mapped to 0x11230000.
By the time mtkclient's stage2 executes from BROM download mode, the BROM has already had MSDC start the eMMC in boot-up mode and read boot0's contents in 512-byte blocks.
The eMMC is unresponsive at this stage and stage2's MSDC driver doesn't talk to it properly.

At stage2 runtime:

- A software-level stream cursor at register 0x00102aec holds the u32 value 0x0003e400, the length of the boot0 partition's data contents. That likely came from the header.
- Content of boot0 appear starting at some places past around 0x00100040 but stage2 (our code entry point) is overwriting it... No matter, we can just dump boot0 from linux.
- Calling the BROM's eMMC stream-advance routine returns 0x35 / timeout.
- The EMMC_STS register is all 0s. Calling some BROM routines to start the eMMC in boot-up mode brings the "eMMC bootup state" bit in EMMC_STS up to 1, but it's likely MSDC is just asserting that without any eMMC participation. 

The next actor that brings the eMMC out of this state is apparently the Little Kernel preloader which is AArch64 v8A LE instructions stored in boot0. 
Details about how it inits the eMMC are unknown right now. 
