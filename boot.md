# Typical boot paths for the MT8113

Most of the information in this document can be gleaned directly from [normalboot_921600.txt](./normalboot_921600.txt), which is what is printed to UART during a normal boot.

1. Under normal conditions, BROM loads the preloader (BL2) to SRAM from the emmc BOOT0 partition. BROM jumps to BL2 in SRAM after verifying its integrity (magic number header, checksum). If an efuse is burned then that check involves a knowing a cryptographic secret. (Is this mtkclient's SBC? "Signed BL2 Check"?) 
2. BL2 for the MT8113 is apparently Little Kernel. Little Kernel does device init like setting up DRAM.
3. Little Kernel uses its fitboot app to proceed. 

    a. fitboot selects the eMMC's tee_a GPT partition as the one containing a tee (TEE = Trusted Execution Environment). From tee_a it loads a FIT bundle (FIT= Flattened Image Tree) called "tz_ctl" into DRAM. tz_ctl is a secure world control image; TZ=TrustZone. tz_ctl describes both ATF (ARM Trusted Firmware) Secure Monitor which will be labeled BL31 by ATF, and TEE (same TEE as above) which will be labeled BL32 by ATF. the FITs either contain images of BL31 and BL32 or only descriptions of their size/location, I am not sure which.
   
    b. fitboot selects the eMMC's UBOOT GPT partition as a boot partition, one that will contain the "normal world control image", labeled BL33 by ATF. From UBOOT it loads a FIT bundle "bootimg_ctl" to DRAM. bootimg_ctl is a FIT bundle that describes the U-boot bootloader. Similar to above this FIT bundle either contains the BL33  image or only has a description of it, I am not sure.
   
    c. fitboot checks the integrity of both FIT bundles (tz_ctl and bootimg_ctl), including a check against a rsa2048 public key in Little Kernel called "dev".
   
    d. fitboot loads BL31 (ATF Secure Monitor), BL32 (TEE) and BL33 (U-Boot) into their respective memory regions then jumps to the BL31 entry point. 

5. ATF just seems to trust whatever Little Kernel loaded into DRAM and it jumps to the pointer to BL33/U-boot without any extra checks.
6. U-Boot runs. It loads one FIT image from the eMMC's boot_a GPT partition. This FIT image contains kernels, FDT(s) (Flattened Device Trees), metadata and signatures.

    a. The FIT image for this kernel contains a signature and U-Boot reports `Verifying Hash Integrity ... sha256,rsa2048:dev+ OK`. Would U-Boot have booted it if it wasn't there? Can we re-configure it to not do that without binary patching? I don't know.

7. U-Boot executes the kernel and then we're in Linux world.

# Download mode on the MT8113 

In step 1 above I mentioned "under normal conditions."
An abnormal condition during boot (there might be others): before executing the preloader, the BROM checks if "Download Mode" conditions are met (like shorted Download pins). 
In that case the BROM starts "Download Agent 1"/DA1 (which is really just some BROM routines) which waits a second or so to do a handshake over USB or UART. 
If the "Serial Link Authentication"/SLA efuse is burned on the device then the handshake includes a cryptographic challenge.
If the handshake succeeds DA1 exposes an interface to USB or UART with a short list of commands. 
Documentation on this interface is scattered and device-specific. 

At this point it is common to use this BROM interface to send a "Download Agent 2"/DA2 from a host machine into the device's memory, and have the device jump to execute DA2. 
Sometimes there is an efuse that makes the brom auth the download agent before jumping to execute it ("Download Agent Authentication"/DAA). 

## mtkclient and Download Mode
On devices with SLA and DAA enabled, bkerler published an exploit called kamakiri that disables SLA and DAA checks: while DA1 is waiting, if you upload a Kamakiri payload (possibly in a special way, I am not sure), then the payload will disable those checks and jump back to the state of DA1 waiting for commands from the host machine.

"stage2" which is part of mtkclient takes the place of DA2: when you run `mtk.py stage` on a host PC, that script waits for DA1 to appear as a USB device, then it handshakes with the device, uploads stage2 to memory and jumps to execute stage2. 
On the host machine `stage2.py [...]` addresses a running stage2.

On the mt8113, stage2 executes successfully and can be addressed over USB, but its included eMMC read/write routines don't work. 
