# Typical boot paths for the MT8113 in a Kobo Clara BW

Most of the information in this document can be gleaned directly from [normalboot_921600.txt](../logs/normalboot_921600.txt), which is printed to UART during a normal boot.

1. BROM runs in ARM v7 LE (as far as I can tell). Under normal conditions, BROM loads the preloader (BL2) to SRAM from the emmc BOOT0 partition. BROM jumps to BL2 in SRAM after verifying its integrity (magic number header, checksum). If an efuse is burned then that check involves a knowing a cryptographic secret. (This might be mtkclient's SBC/Secure Boot Control) 
2. BL2 for the MT8113 is Little Kernel. Little Kernel does device init like setting up DRAM. As far as I know Little Kernel runs in AArch64 v8A LE.
3. Little Kernel uses its fitboot app to proceed. 

    a. fitboot selects the eMMC's tee_a GPT partition as the one containing a tee (TEE = Trusted Execution Environment). From tee_a it loads a FIT bundle (FIT= Flattened Image Tree) called "tz_ctl" into DRAM. tz_ctl is a secure world control image; TZ=TrustZone. tz_ctl describes both ATF (ARM Trusted Firmware) Secure Monitor which will be labeled BL31 by ATF, and TEE (same TEE as above) which will be labeled BL32 by ATF. the FITs either contain images of BL31 and BL32 or only descriptions of their size/location, I am not sure which.
   
    b. fitboot selects the eMMC's UBOOT GPT partition as a boot partition, one that will contain the "normal world control image", labeled BL33 by ATF. From UBOOT it loads a FIT bundle "bootimg_ctl" to DRAM. bootimg_ctl is a FIT bundle that describes the U-boot bootloader. Similar to above this FIT bundle either contains the BL33  image or only has a description of it, I am not sure.
   
    c. fitboot checks the integrity of both FIT bundles (tz_ctl and bootimg_ctl), including a check against a rsa2048 public key in Little Kernel called "dev".
   
    d. fitboot loads BL31 (ATF Secure Monitor), BL32 (TEE) and BL33 (U-Boot) into their respective memory regions then jumps to the BL31 entry point. 

5. ATF just seems to trust whatever Little Kernel loaded into DRAM and it jumps to the pointer to BL33/U-boot without any extra checks.
6. U-Boot runs. It loads one FIT image from the eMMC's boot_a GPT partition. This FIT image contains kernels, FDT(s) (Flattened Device Trees), metadata and signatures.

    a. The FIT image for this kernel contains a signature and U-Boot reports `Verifying Hash Integrity ... sha256,rsa2048:dev+ OK`. Would U-Boot have booted it if it wasn't there? Can we re-configure it to not do that without binary patching? I don't know.

7. U-Boot executes the kernel and then we're in Linux world.

# Download mode on the Kobo Clara BW's MT8113 

In step 1 above I mentioned "under normal conditions."
An abnormal condition during boot (there might be others): before executing the preloader, the BROM checks if "Download Mode" conditions are met.
Details are unclear since I haven't decompiled those gates in the BROM yet, but one such condition is shorted Download pins on the PCB. Another is if other available boot methods fail in the BROM (e.g. a corrupted boot0 region).

In that case the BROM runs some routines which wait a second or so for a handshake over USB or UART. 
If the "Serial Link Authentication"/SLA efuse is enabled on the device (it isn't on the Kobo) then the handshake includes a cryptographic challenge.
If the handshake succeeds then an interface with a short list of commands is exposed to USB or UART. 
Documentation on this interface is scattered and device-specific. 

At this point it is common to use this interface to send a "Download Agent"/DA from a host machine into the device's memory, and have the device jump to execute it. 
Not enabled on the Kobo, but there is an efuse that makes the BROM authenticate the DA before jumping to execute it ("Download Agent Authentication"/DAA). 

"DA" and "Download mode" can mean a million different things depending on platform and context. 

 - On some devices the BROM's download mode is distinct from a download mode exposed by the preloader, or some other part of boot. 
 - On some devices the usual process is that two DA are uploaded. The first one (DA1) sets some things up before asking for a second DA (DA2). Either of those can be authenticated by whoever asked for them. 
 - Which of these are enabled and/or accessible, how to access them, what's loaded to ram before/after the DA executes, what jumps to which DA, is extremely context and platform-specific.
 - On the Kobo MT8113 we apparently only have the one Download Mode: the one that BROM runs. It is unsecured and runs before the preloader/LK is loaded into SRAM. 

## mtkclient and Download Mode
bkerler published an exploit called kamakiri that disables SLA and DAA checks for devices with those enabled.[1] 
While BROM is waiting for a DA, if you upload a Kamakiri payload in a specific context and specific way, then the Kamakiri payload will execute, disable those SLA & DAA checks and jump back to some type of download mode. 
The details aren't very important here since the Kobo's MT8113 doesn't have those protections enabled. 

Even though we don't need kamakiri, mtkclient is still useful to build over.
"stage2", shipped as part of mtkclient, takes the place of a DA. 
When you run `mtk.py stage` on a host PC, that script waits for the MediaTek BROM in Download mode to appear as a USB device. 
`mtk.py` then handshakes with the BROM, uploads & runs the kamakiri payload even though we didn't need it, then uploads & executes stage2 on the device. 
On the host machine `stage2.py [...]` addresses a running stage2.

On the mt8113, stage2 executes successfully after uploading it from BROM download mode. 
It can be addressed over USB, but its included eMMC read/write routines don't work. 

--

[1]: An explanation of Kamakiriv2 from R0rt1z2: Roger: "Another note regarding the kamakiri2 exploit, it abuses a buffer overflow in the USB SET_LINECODING request. This request expects you to send 7 bytes, but the implementation does not check how many bytes are actually sent (they just check that the size isn't 0), making it possible to overflow the USB ring buffer (or so I believe). In combination with this, a BROM-specific command (0xDA, used to peek and poke BROM registers) can be abused to gain arbitrary memory read and write access. Using these primitives, a payload is uploaded to 0x100A00, and a pointer used by USB-related functions is overwritten to point to that address. The next time a USB-related routine is executed, it immediately jumps to the payload, which patches BROM memory (not BROM itself) to "spoof" the device as insecure (for example, with SLA / DAA / SBC disabled)"
