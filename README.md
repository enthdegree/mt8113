# mt8113 stage2

Here we endeavor to extend bkerler/mtkclient's stage2_static to do emmc read/write on a Kobo Clara BW. 
Currently they are broken (See [mtkclient issue 1332](https://github.com/bkerler/mtkclient/issues/1332)).
We have a fair bit of reverse engineering info on the Kobo Clara BW:

- boot process: [`boot.md`](./documentation/boot.md)
- boot0 partition: [`boot0.md`](./documentation/boot0.md)
- Peripherals [`perilpherals.md`](./documentation/peripherals.md)

We also have an eMMC read of boot0 and userdata 512-byte sectors with PIO CMD17 via a pad config stolen from LK in boot0! 
See the `emmc_read_test` routine in [`stage2.c`](./stage2_static/stage2.c).
