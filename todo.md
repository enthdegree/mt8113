# Major milestone 1: develop a DA that is capable of unbricking MT8113 from brom 
1. In Ghidra, identify and understand eMMC init + read + write routines.
2. Find out how `mtk.py stage` and `stage2.py` work, try and get more obvious USB logging.
3. Extend stage2 to read/write MT8113 eMMC
4. Test DA on userdata logical block addresses; confirm it matches dd from Linux.

# Major milestone 2: Patch the LK binary to load an unauthorized U-boot. This isn't as hopeless as it sounds, [DavidBuchanan314 succeeded in doing it on a Rabbit R1](https://github.com/DavidBuchanan314/rabbit_r1_boot_notes/tree/main).
# Major milestone 3: Fix U-boot to run FIT images that aren't signed
# Major milestone 4: Load an unauthorized kernel...
