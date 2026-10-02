"""跑一遍打包验证脚本，把输出写进 verify_out.txt（UTF-8）。"""

import os
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
env = dict(os.environ)
env["PYTHONIOENCODING"] = "utf-8"
env["PYTHONUTF8"] = "1"

result = subprocess.run(
    [sys.executable, str(REPO / "tmp-ts" / "verify_zip.py")],
    cwd=str(REPO),
    capture_output=True,
    env=env,
)
(REPO / "tmp-ts" / "verify_out.txt").write_bytes(result.stdout + result.stderr)
text = (result.stdout + result.stderr).decode("utf-8", "replace")
print("returncode", result.returncode)
print("\n".join(text.strip().splitlines()[-30:]))
