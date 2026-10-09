# Gun Bros 金币修改方法

可以只改存档文件，**不需要重新编译或安装 VPK**。普通金币和金色 Glu 金币是两个独立字段，修改其中一个不会自动修改另一个。

可以直接下载 [示例工具包](https://github.com/vctorwei/Gun-Bros-Psvita/raw/refs/heads/main/coin_cheat/gunbros-coin-cheat-example.zip)，解压后放入自己的最新存档，运行 `python3 set_coins.py`。包内附有独立脚本和中英文说明；每位玩家使用自己的存档，保留现有进度。

**[English instructions](https://github.com/vctorwei/Gun-Bros-Psvita/blob/main/coin_cheat/README_EN.md)** · [独立脚本](https://github.com/vctorwei/Gun-Bros-Psvita/blob/main/coin_cheat/set_coins.py)

本方法适用于这个移植版 Classic 的 `GBVP`、版本 `1` 离线存档。格式根据 [存档实现](https://github.com/vctorwei/Gun-Bros-Psvita/blob/main/source/patch/objects.inc.c) 核对；如果文件格式不符，下面的脚本会拒绝修改。

## 要修改的文件

```text
ux0:/data/gunbros/gunbros_free/vita_profile_v1.dat
```

不是 `prefs.dat`、`savegame.dat` 或 `vita_purchases_v1.dat`。后者记录购买相关状态，本方法修改的是玩家钱包余额。

文件大小应为 **1,224 字节**。所有数字使用小端序，偏移从文件开头的 `0` 开始计算。

| 内容 | 偏移 | 长度 / 类型 |
| --- | --- | --- |
| 普通金币 Coins | `0xA0` | 8 字节，无符号 64 位整数 |
| 金色 Glu Coins | `0xA8` | 4 字节，无符号 32 位整数 |
| 校验值 | `0x14` | 4 字节，无符号 32 位整数 |

**改完余额必须重新计算校验值。** 否则游戏会拒绝加载这个存档，修改不会正常生效。

## 操作步骤

1. 完全关闭 Gun Bros，回到 VitaShell。不要只停留在游戏暂停菜单；游戏仍在运行时可能用内存中的旧余额覆盖文件。
2. 用 USB 或 VitaShell FTP 下载上面的最新文件，并在电脑上另存一份原始备份。FTP 地址使用 VitaShell 当前显示的地址。
3. 解压示例工具包，将下载的 `vita_profile_v1.dat` 放到包内 `set_coins.py` 的同一个文件夹。也可以手动保存下面的示例代码。
4. 在该文件夹运行 `python3 set_coins.py`。Windows 也可以使用 `py -3 set_coins.py`。
5. 脚本会生成 `vita_profile_v1.modified.dat`，保留原文件。确认输出的两种余额后，将生成文件上传到原目录，命名为 **`vita_profile_v1.dat`**，替换 PSV 上对应文件。
6. 重新启动游戏，检查普通金币和金色 Glu 金币。需要恢复时，完全关闭游戏，再把原始备份以原文件名放回原目录。

如果还没有这个文件，先正常进入游戏并保存、退出，让移植版生成存档，再下载修改。

工具包里的独立脚本还支持命令行参数：`python3 set_coins.py --coins 99999 --glu-coins keep` 只改普通金币；`python3 set_coins.py --coins keep --glu-coins 99999` 只改金色 Glu 金币。自定义金额与文件路径的用法见英文说明。以下手动保存的简短示例通过代码顶部的两项变量设置金额。

## Python 3 示例

将下面代码保存为 `set_coins.py`。默认把两种金币都设置为 **99,999**；只想改一种时，将另一项设置为 `None`，保留其现有余额。

```python
from pathlib import Path
import struct

COINS = 99999       # 普通金币；None 表示保留
GLU_COINS = 99999   # 金色 Glu 金币；None 表示保留

source = Path("vita_profile_v1.dat")
output = Path("vita_profile_v1.modified.dat")


def checksum(data):
    payload = bytearray(data)
    struct.pack_into("<I", payload, 0x14, 0)
    value = 2166136261
    for byte in payload:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value or 1


def validate(data):
    if len(data) != 1224:
        raise ValueError("文件大小不符：只支持 1224 字节的 GBVP/v1 存档")
    magic, version, config_size, progress_size, count, saved = (
        struct.unpack_from("<6I", data, 0)
    )
    if (magic, version, config_size, progress_size) != (0x50564247, 1, 0x78, 0x38):
        raise ValueError("存档格式不符：请确认文件和移植版版本")
    if count > 256 or saved != checksum(data):
        raise ValueError("原存档校验失败：请重新下载，不要覆盖 PSV 文件")


original = source.read_bytes()
validate(original)
modified = bytearray(original)
for value, offset, fmt, bits in (
    (COINS, 0xA0, "<Q", 64),
    (GLU_COINS, 0xA8, "<I", 32),
):
    if value is not None:
        if not isinstance(value, int) or not 0 <= value < (1 << bits):
            raise ValueError("金币数量超出该字段的范围")
        struct.pack_into(fmt, modified, offset, value)

struct.pack_into("<I", modified, 0x14, checksum(modified))
validate(modified)
with output.open("xb") as handle:
    handle.write(modified)

print("已生成：", output)
print("普通金币：", struct.unpack_from("<Q", modified, 0xA0)[0])
print("Glu 金币：", struct.unpack_from("<I", modified, 0xA8)[0])
print("原文件保持原样；输出文件已存在时会拒绝覆盖。")
```

校验算法是 **32 位 FNV-1a**：计算前将 `0x14–0x17` 四个字节清零，对完整的 1,224 字节计算，结果为零时保存为 `1`，最后把结果以小端序写回 `0x14`。脚本只修改选中的金币字段和这个校验值，其余存档内容保持原样。
