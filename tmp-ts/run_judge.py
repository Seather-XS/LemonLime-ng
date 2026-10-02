"""命令行跑一遍评测（隐藏入口 --judge-day），再把日志拷回工作区，方便事后分析。

用法：python tmp-ts\\run_judge.py [比赛日.cdf] [选手名] [题目标题]
默认用 day1 例子里的 std / rainbow。
"""

import os
import pathlib
import shutil
import subprocess
import sys

QT = r"D:\Qt\6.8.3\mingw_64\bin"
TOOLS = r"D:\Qt\Tools\mingw1310_64\bin"
EXE = r"d:\gengen-tuack\build\lemon.exe"
DEFAULT_DAY = r"C:\Users\16702\Desktop\test\project\lemon-ng\example\day1\day1.cdf"
HERE = pathlib.Path(__file__).resolve().parent
LOG_DIR = pathlib.Path(os.environ["LOCALAPPDATA"]) / "Lemonlime" / "logs"
TARGET = HERE / "judge-log.txt"

day = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DAY
contestant = sys.argv[2] if len(sys.argv) > 2 else "std"
task = sys.argv[3] if len(sys.argv) > 3 else "rainbow"

# 日志按日期分文件，取最新的那份。
LOG = sorted(LOG_DIR.glob("lemonlime-log_*.txt"))[-1]

env = dict(os.environ)
env["PATH"] = f"{TOOLS};{QT};C:\\mingw64\\bin;" + env.get("PATH", "")

before = LOG.stat().st_size

done = subprocess.run(
    [EXE, "--judge-day", day, "--contestant", contestant, "--task", task],
    capture_output=True,
    text=True,
    env=env,
    timeout=900,
)
print("rc", done.returncode)
if done.stderr:
    print("stderr tail:", done.stderr[-500:])

shutil.copy2(LOG, TARGET)
print(f"{LOG.name}: {before} -> {LOG.stat().st_size} bytes，已拷到 {TARGET}")
