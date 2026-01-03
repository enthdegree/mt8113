# Peripherals

## eFuse

There is an efuse assigned registers around 0x11c5000. 
A brom routine kicks the peripheral alive by writing bit2 to 1 and waiting for bit0 to go to 1.
Bytes above that address (e.g. 0x11c5020) are read by various callers and determine policy/config.

## MSDC, eMMC access in the Kobo Clara BW's BROM download mode context

The MT8113 MSDC (Mediatek Storage Device Controller) has registers mapped to 0x11230000.
There is a known good MSDC-read-the-eMMC path in the BROM; that's how it loads BL2. 
Unfortunately, some unknown incompatibility in stage2's `mt_sd` driver is unable to talk to it in this state.  

By the time mtkclient's stage2 executes from BROM download mode, the BROM has already set up the MSDC to talk to the eMMC in PIO (Programmed Input/Output) mode and read the whole piece of the eMMC's boot0 partition that it wants in 512-byte blocks. 
A read cursor at 0x00102aec during stage2's runtime holds the u32 0x0003e400, the length of the boot0 partition's data contents. Also at stage2 runtime, content of boot0 appears around 0x00100040.


