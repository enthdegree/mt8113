# Typical boot paths for the MT8113 in a Kobo Clara BW

Most of the information in this document can be gleaned directly from [normalboot_921600.txt](./normalboot_921600.txt), which is printed to UART during a normal boot.

1. Under normal conditions, BROM loads the preloader (BL2) to SRAM from the emmc BOOT0 partition. BROM jumps to BL2 in SRAM after verifying its integrity (magic number header, checksum). If an efuse is burned then that check involves a knowing a cryptographic secret. (This might be mtkclient's SBC/Secure Boot Control) 
2. BL2 for the MT8113 is Little Kernel. Little Kernel does device init like setting up DRAM.
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
An abnormal condition during boot (there might be others): before executing the preloader, the BROM checks if "Download Mode" conditions are met (like shorted Download pins on the PCB).
In that case the BROM runs some routines which wait a second or so for a handshake over USB or UART. I haven't isolated these routines in the MT8113 brom yet. 
If the "Serial Link Authentication"/SLA efuse is enabled on the device (it isn't on the Kobo) then the handshake includes a cryptographic challenge.
If the handshake succeeds then an interface with a short list of commands is exposed to USB or UART. 
Documentation on this interface is scattered and device-specific. 

At this point it is common to use this interface to send a "Download Agent"/DA from a host machine into the device's memory, and have the device jump to execute it. 
Not enabled on the Kobo, but there is an efuse that makes the BROM authenticate the DA before jumping to execute it ("Download Agent Authentication"/DAA). 

"DA" and "Download mode" can mean a million different things depending on platform and context. 

 - On some devices the BROM's download mode is distinct from a download mode exposed by the preloader, or some other part of boot. 
 - On some devices the usual process is that two DA are uploaded. The first one (DA1) sets some things up before asking for a second DA (DA2). Either of those can be authenticated by whoever asked for them. 
 - Which of these are enabled and/or accessible, how to access them, what's loaded to ram before/after the DA executes, what jumps to which DA, is extremely context and platform-specific.
 - On the Kobo MT8113 we apparently only have the one Download Mode: the one that BROM runs, unsecured, before the preloader/LK is loaded into SRAM. 

## mtkclient and Download Mode
bkerler published an exploit called kamakiri that disables SLA and DAA checks for devices with those enabled: while BROM is waiting for a DA, if you upload a Kamakiri payload in a specific context and specific way, then the Kamakiri payload will execute, disable those SLA & DAA checks and jump back to some type of download mode. 
The details here don't apply to the Kobo's MT8113 as those protections aren't enabled here.

"stage2", shipped as part of mtkclient, takes the place of a DA: when you run `mtk.py stage` on a host PC, that script waits for the MediaTek BROM in Download mode to appear as a USB device. It handshakes with the device, uploads stage2 to the device's memory and addresses the device to jump to execute stage2. 
On the host machine `stage2.py [...]` addresses a running stage2.

On the mt8113, stage2 executes successfully after uploading it from BROM download mode. 
It can be addressed over USB, but its included eMMC read/write routines don't work. 
