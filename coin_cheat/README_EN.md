# Gun Bros coin editing example

This tool edits **your own save file** to set ordinary Coins and golden Glu Coins. It defaults to **99,999 of each** and updates the save checksum automatically. No VPK rebuild or reinstall is needed.

It supports the Classic port's **1,224-byte `GBVP`, version 1** offline profile. You need Python 3 on your computer; the tool uses only the Python standard library.

Download the [example package](https://github.com/vctorwei/Gun-Bros-Psvita/raw/refs/heads/main/coin_cheat/gunbros-coin-cheat-example.zip), or get [set_coins.py](https://github.com/vctorwei/Gun-Bros-Psvita/blob/main/coin_cheat/set_coins.py) separately. [Chinese instructions](https://github.com/vctorwei/Gun-Bros-Psvita/blob/main/coin_cheat/README.md).

## Quick start

1. Fully close Gun Bros, then open VitaShell. A paused game can still overwrite the file with the balance held in memory.
2. Use USB or VitaShell FTP to download your latest file from:

   ```text
   ux0:/data/gunbros/gunbros_free/vita_profile_v1.dat
   ```

   Keep a separate, unchanged backup. If the file does not exist yet, play normally, save and exit first so the port can create it.
3. Extract the example ZIP. Put your downloaded `vita_profile_v1.dat` in the same folder as `set_coins.py`.
4. Open a terminal in that folder and run:

   ```sh
   python3 set_coins.py
   ```

   On Windows you can use `py -3 set_coins.py`.
5. The tool creates `vita_profile_v1.modified.dat` and prints both balances. It preserves the input and refuses to overwrite an existing output. Use another output name or move the previous output before running it again.
6. With the game still closed, upload the generated file to the original directory, naming it **`vita_profile_v1.dat`**. Start Gun Bros and check both balances.

To restore your save, fully close the game and put your original backup back at the same path and filename. Use a current profile from your own game: an old profile may be superseded by newer native progress during loading.

The package supplies the tool and instructions. Each player supplies their own profile file to preserve their progress, equipment and purchases.

## Custom amounts

Set both balances:

```sh
python3 set_coins.py --coins 50000 --glu-coins 1000
```

Change only ordinary Coins:

```sh
python3 set_coins.py --coins 99999 --glu-coins keep
```

Change only golden Glu Coins:

```sh
python3 set_coins.py --coins keep --glu-coins 99999
```

Choose input and output paths (quote paths containing spaces):

```sh
python3 set_coins.py "my save/vita_profile_v1.dat" --output "my save/edited.dat"
```

Coins must be a whole number between `0` and `18446744073709551615`; Glu Coins must be between `0` and `4294967295`. These are storage limits, not a guarantee that every amount will display sensibly in the game. Use the default or a modest custom amount.

## File format

All offsets are zero-based and all numbers use little-endian byte order. The two balances are independent.

| Field | Offset | Type |
| --- | --- | --- |
| Ordinary Coins | `0xA0` | Unsigned 64-bit integer, 8 bytes |
| Golden Glu Coins | `0xA8` | Unsigned 32-bit integer, 4 bytes |
| Checksum | `0x14` | Unsigned 32-bit integer, 4 bytes |

Before writing anything, the tool validates the size, `GBVP` magic, version, payload sizes, item count and original checksum. It changes only the selected balance fields and the checksum.

The checksum is 32-bit FNV-1a over all 1,224 bytes, with bytes `0x14` through `0x17` cleared first. Start at `2166136261`, XOR each byte and multiply by `16777619` modulo `2^32`. Store `1` if the result is zero, otherwise store the result, at `0x14` in little-endian order.

The format was checked against the port's [save implementation](https://github.com/vctorwei/Gun-Bros-Psvita/blob/main/source/patch/objects.inc.c). Editing the balance without updating this checksum causes the port to reject the profile.
