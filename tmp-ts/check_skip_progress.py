"""验证：选手被取消测试时，上报给进度条的进度 = 这位选手本轮所有试题的时限之和。

做法：拿真实 day1.cdf，加一条「怎么都匹配不上」的命名规则，让选手被判为命名不合规，
再用隐藏入口 --judge-day --rules 跑（会走「跳过」分支），对比日志里的 progress。
"""

import json
import os
import pathlib
import shutil
import subprocess

SRC = pathlib.Path(r"C:\Users\16702\Desktop\test\project\lemon-ng\example\day1\day1.cdf")
WORK = pathlib.Path(r"d:\gengen-tuack\build\export-test\SkipProgress")
LEMON = r"d:\gengen-tuack\build\lemon.exe"
LOG_DIR = pathlib.Path(os.environ["LOCALAPPDATA"]) / "Lemonlime" / "logs"
CONTESTANT = "std"

env = dict(os.environ)
env["PATH"] = r"D:\Qt\6.8.3\mingw_64\bin;" + env.get("PATH", "")

shutil.rmtree(WORK, ignore_errors=True)
WORK.mkdir(parents=True)

data = json.loads(SRC.read_text(encoding="utf-8"))
data["namingCheck"] = True
data["namingPattern"] = "ZZZ-*"   # 谁都匹配不上 → 全部选手被判命名不合规
data["violationCheck"] = False
(WORK / "day.cdf").write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")

# 期望值：与 Task::getTotalTimeLimit() 一致 —— Σ 每个测试点的 timeLimit × 输入文件数
expected = 0
for task in data["tasks"]:
    for case in task.get("testCases", []):
        expected += int(case.get("timeLimit", 0)) * len(case.get("inputFiles", []))

log = sorted(LOG_DIR.glob("lemonlime-log_*.txt"))[-1]
before = len(log.read_text(encoding="utf-8", errors="replace").splitlines())

done = subprocess.run([LEMON, "--judge-day", "day.cdf", "--contestant", CONTESTANT, "--rules"],
                      cwd=WORK, capture_output=True, text=True, env=env, timeout=300)
print("rc", done.returncode)

lines = log.read_text(encoding="utf-8", errors="replace").splitlines()[before:]
skipped = [line.strip() for line in lines if "judge-day: skipped" in line]
for line in skipped:
    print("  ", line.split("] ")[-1])

actual = None
for line in skipped:
    if f"skipped {CONTESTANT} " in line:
        actual = int(line.rsplit(" ", 1)[-1])

print(f"选手 {CONTESTANT}：期望进度 {expected} ms，实际上报 {actual} ms")
assert actual is not None, "没有被取消测试的选手上报进度（signal 没发出来？）"
assert actual == expected, "上报的进度和该选手所有试题的时限之和不一致"
print("OK：被取消测试的选手会把整份工时补给进度条")
