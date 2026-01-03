# mt8113 stage2

Here we endeavor to extend bkerler/mtkclient's stage2_static to do emmc read/write on a Kobo Clara BW. 
Currently they are broken (See [mtkclient issue 1332](https://github.com/bkerler/mtkclient/issues/1332)).
So far this is just reverse-engineering info about the Kobo Clara BW:

- boot process: [`boot.md`](./documentation/boot.md)
- boot0 partition: [`boot0.md`](./documentation/boot0.md)
- Peripherals [`perilpherals.md`](./documentation/peripherals.md)
