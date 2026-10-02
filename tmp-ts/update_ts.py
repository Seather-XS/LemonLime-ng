"""跑 lupdate + merge_ts（临时脚本）：把新词条并进 translations/*.ts。"""

import os
import subprocess
import sys

REPO = r"d:\gengen-tuack"
LUPDATE = r"D:\Qt\6.8.3\mingw_64\bin\lupdate.exe"

env = dict(os.environ)
env["PYTHONIOENCODING"] = "utf-8"
env["PYTHONUTF8"] = "1"

step = subprocess.run(
    [LUPDATE, f"{REPO}\\src", "-ts",
     f"{REPO}\\tmp-ts\\en_US.ts", f"{REPO}\\tmp-ts\\zh_CN.ts", f"{REPO}\\tmp-ts\\zh_TW.ts"],
    capture_output=True, text=True, env=env,
)
print(step.stdout)
if step.returncode != 0:
    print(step.stderr)
    raise SystemExit(step.returncode)

merge = subprocess.run([sys.executable, f"{REPO}\\tmp-ts\\merge_ts.py"],
                       capture_output=True, text=True, env=env)
print(merge.stdout)
if merge.returncode != 0:
    print(merge.stderr)
    raise SystemExit(merge.returncode)
