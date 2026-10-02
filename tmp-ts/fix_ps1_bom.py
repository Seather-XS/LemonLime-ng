"""把 .ps1 转成 UTF-8 with BOM（PowerShell 5.1 没有 BOM 会按 GBK 读，中文会读坏）。

用法：python fix_ps1_bom.py <文件>[...]
"""

import pathlib
import sys

for name in sys.argv[1:]:
    path = pathlib.Path(name)
    raw = path.read_bytes()

    if raw.startswith(b"\xef\xbb\xbf"):
        print(f"{path.name}: 已经有 BOM，跳过")
        continue

    text = raw.decode("utf-8")
    path.write_bytes(b"\xef\xbb\xbf" + text.encode("utf-8"))
    print(f"{path.name}: 已加上 UTF-8 BOM（{len(text)} 字符）")
