# Major milestone 1: develop a DA that is capable of unbricking a Kobo from brom
# Major milestone 2: Get LK and U-boot to load a FIT image that isn't signed. 

- Take an existing FIT image and change it superficially + strip the signature and checksum nodes. We don't know if LK and U-boot will actually boot this. 
- If they aren't there is still hope we can patch LK and U-boot to always pass auth. This isn't as hopeless as it sounds, [DavidBuchanan314 succeeded in doing it on the Rabbit R1's LK.](https://github.com/DavidBuchanan314/rabbit_r1_boot_notes/tree/main)

# Major milestone 3: Load an unauthorized kernel...
