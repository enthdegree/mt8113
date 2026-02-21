# Peripherals

## eFuse

There is an efuse assigned registers around 0x11c5000. 
A brom routine kicks the peripheral alive by pulling up bit2 then waiting for bit0 to go up.
Bytes above that address are read by various callers and determine policy/config.

## The MT8113 BROM's eMMC access

The MT8113 MSDC (Memory stick and Secure Digital card Controller) has registers mapped to 0x11230000.
Early during a normal boot, the BROM sets up the eMMC in boot-up mode and reads boot0's contents in 512-byte blocks.
By the time the BROM jumps to either preloader (Little Kernel) or the BROM Download agent, the content of boot0 appears starting at some place past around 0x00100040. 
Under normal boot circumstances, it is Little Kernel that brings the eMMC out of Download mode and reads the next bootloader from it. 

Little Kernel's eMMC setup routine is successfully mimicked by [`mt8113_emmc.c`](../stage2_static/mt8113_emmc.c).

The eMMC is unresponsive to mtkclient stage2's MSDC/eMMC driver, "`mt_sd`".
I don't know what exactly is going wrong.
It is likely because the eMMC has been left in boot-up mode by the time mtkclient's stage2 executes.
Boot-up mode and other eMMC configs are configured by the eMMC's EXT_CSD registers and those registers persist with power-cycle.
