# mt8113 stage2 emmc r/w

Here we endeavor to make a download agent for some 2024-era Kobo ebook readers.
The goal is to allow recovery from bricks such as the one from [mtkclient issue 1332](https://github.com/bkerler/mtkclient/issues/1332) and maybe eventually allow for safe kernel development.
The approach is to modify bkerler/mtkclient's stage2_static as a platform. 
Needless to say, at this stage everything in this repo is dangerous.

## Status

We have succeeded in getting observable PIO eMMC read/write of 512-byte sectors with CMD17/CMD24 from a BROM Download Mode context. 
Routines are in [`mt8113_emmc.c`](./stage2_static/mt8113_emmc.c). 
Some small tests that print status to UART succeed:

 - `emmc_boot0_verify_test`: read the first two sectors of `boot0` and look for magic strings.
 - `emmc_roundtrip_test`: read, overwrite and then revert some sector in userdata.

See below for how to trigger those routines.
I'm only testing on a Kobo Clara BW right now but in principle most MT8113-based Kobos should work too.
This requires more dangerous testing.

## Tests

Connect the Kobo to a computer via USB and start mtkclient to try to upload the custom stage2:  

```
python3 mtk.py stage --stage2 path/to/custom/stage2.bin
```

Then put the Kobo in download mode: hold its PCB's download pin shorted, then tap & release a short on the reset pin. 
This is easy to do with two bits of aluminum foil. 
- Factory-fresh boards have a thin clear protective coating you have to scrape off 
- Newer board revisions only need download shorted 

Eventually mtkclient should see the Kobo in BROM download mode and stage2 will upload + execute.
To exec the test routines you need to add a path to `stage2.py` that sends the cmd `0x7000`.  
In my copy of stage2.py I run it like this:
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

