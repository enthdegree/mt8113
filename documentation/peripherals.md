# Peripherals

## eFuse

There is an efuse assigned registers around 0x11c5000. 
A brom routine kicks the peripheral alive by writing bit2 to 1 and waiting for bit0 to go to 1.
Bytes above that address (e.g. 0x11c5020) are read by various callers and determine policy/config.

## MSDC, eMMC access in the Kobo Clara BW's BROM download mode context

The MT8113 MSDC (Mediatek Storage Device Controller) has registers mapped to 0x11230000.
There is a known good MSDC-read-the-eMMC path in the BROM; that's how it loads BL2. 
By the time mtkclient's stage2 executes from BROM download mode, the BROM has already set up the MSDC to put the eMMC in PIO (Programmed Input/Output) mode and read the whole eMMC's boot0 partition that it wants in 512-byte blocks as a stream (the stream-advance routine's text at BROM 0x38ba). 
Some unknown incompatibility in stage2's `mt_sd` driver is unable to talk to it in this state.  

At stage2 runtime:
- a stream cursor at register 0x00102aec holds the u32 value 0x0003e400, the length of the boot0 partition's data contents. 
- content of boot0 appears around 0x00100040.
- if you call the stream-advance routine it returns 0x35 (a timeout).

