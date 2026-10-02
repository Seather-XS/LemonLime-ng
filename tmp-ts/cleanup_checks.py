"""清掉验证用的临时目录（每次跑完检查脚本后使用）。"""

import pathlib
import shutil

BASE = pathlib.Path(r"d:\gengen-tuack\build\export-test")

for name in ("StatementName", "StatementNamePlain", "ReportAnchors", "LiveSkip", "SkipProgress"):
    target = BASE / name
    shutil.rmtree(target, ignore_errors=True)
    print(f"removed {target} -> {not target.exists()}")
