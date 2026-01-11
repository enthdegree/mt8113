# mt8113 stage2 emmc r/w

Here we endeavor to make a download agent for some 2024-era Kobo ebook readers.
The goal is to allow recovery from bricks such as the one from [mtkclient issue 1332](https://github.com/bkerler/mtkclient/issues/1332) and maybe eventually allow for custom kernel development.
The approach is to extend bkerler/mtkclient's stage2_static to do emmc read/write on semi-recent Kobo ebook readers. 
Needless to say, at this stage everything in this repo is dangerous.

## Status

We have succeeded in getting observable PIO eMMC read/write of 512-byte sectors with CMD17/CMD24 from a BROM Download Mode context. 
Routines are in [`mt8113_emmc.c`](./stage2_static/mt8113_emmc.c).

 - `emmc_boot0_verify_test`: read the first two sectors of `boot0` and look for magic strings.
 - `emmc_roundtrip_test`: read, overwrite and then revert some sector in userdata.

These routines (by default only `emmc_boot0_verify_test`) can be triggered via [`stage2.c`](./stage2_static/stage2.c). 
See below.
They print status to UART.

I'm only testing on a Kobo Clara BW right now but in principle most MT8113-based Kobos should work too.
This requires more dangerous testing.

## Tests

To upload stage2, leave the Kobo plugged in with USB to a computer running mtkclient: 

```
python3 mtk.py stage --stage2 path/to/custom/stage2.bin
```

Then put the Kobo in download mode: hold its PCB's download pin shorted, then tap & release a short on the reset pin. 
This is easy to do with two bits of aluminum foil. 

To actually exec the test routines you need to add a `0x7000` trigger to mtkclient's `stage2.py`. 
I edited stage2 and run it like this:
```
python3 ./stage2.py custom
```
Outcomes are printed to UART.

## Documentation

We also have a growing amount of reverse engineering info on the Kobo Clara BW:

- boot process: [`boot.md`](./documentation/boot.md)
- boot0 partition: [`boot0.md`](./documentation/boot0.md)
- Peripherals [`perilpherals.md`](./documentation/peripherals.md)

Todo: add some obscure-to-me stuff I learned about eMMC and early boot.

