"""重新构建 lemon（临时脚本）。"""

import os
import subprocess

env = dict(os.environ)
env["PATH"] = r"D:\Qt\Tools\mingw1310_64\bin;C:\mingw64\bin;" + env.get("PATH", "")

done = subprocess.run(
    ["cmake", "--build", r"d:\gengen-tuack\build", "--target", "lemon"],
    capture_output=True,
    text=True,
    env=env,
)
print("rc", done.returncode)
print(done.stdout[-1500:])
print(done.stderr[-1500:])
