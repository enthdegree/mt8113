# Major milestone 1: develop a DA that is capable of unbricking MT8113 from brom 
1. In Ghidra, identify and understand eMMC init + read + write routines.
2. Find out how `mtk.py stage` and `stage2.py` work, try and get more obvious USB logging.
3. Extend stage2 to read/write MT8113 eMMC
4. Test DA on userdata logical block addresses; confirm it matches dd from Linux.

# Major milestone 2: Get LK and U-boot to load a FIT image that isn't signed. 

    - Take an existing FIT image and change it superficially + strip the signature and checksum nodes. We don't know if LK and U-boot will actually boot this. 
    - If they aren't there is still hope we can patch LK and U-boot to always pass auth. This isn't as hopeless as it sounds, [DavidBuchanan314 succeeded in doing it on the Rabbit R1's LK.](https://github.com/DavidBuchanan314/rabbit_r1_boot_notes/tree/main)

# Major milestone 3: Fix U-boot to run FIT images that aren't signed

# Major milestone 4: Load an unauthorized kernel...
