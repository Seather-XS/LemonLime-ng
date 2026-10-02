"""验证：选手的测试被取消时，后台成绩表在那一刻就更新了。

做法：拿真实 day1.cdf，加一条谁都匹配不上的命名规则（所有选手都被判命名不合规），
再用隐藏入口 --check-live-skip 跑：它先把成绩表刷成「没有违规」的样子，然后在
contestantSkipped 信号发出的瞬间读一遍表格里该选手的总分格 —— 颜色变成「取消色」、
悬停提示是取消原因，就说明表格是当场刷新的，而不是等下一个正常选手测完。
"""

import json
import os
import pathlib
import shutil
import subprocess

SRC = pathlib.Path(r"C:\Users\16702\Desktop\test\project\lemon-ng\example\day1\day1.cdf")
WORK = pathlib.Path(r"d:\gengen-tuack\build\export-test\LiveSkip")
LEMON = r"d:\gengen-tuack\build\lemon.exe"

env = dict(os.environ)
env["PATH"] = r"D:\Qt\6.8.3\mingw_64\bin;" + env.get("PATH", "")

shutil.rmtree(WORK, ignore_errors=True)
WORK.mkdir(parents=True)

data = json.loads(SRC.read_text(encoding="utf-8"))
data["namingCheck"] = True
data["namingPattern"] = "ZZZ-*"  # 谁都匹配不上 → 全部选手被判命名不合规
data["violationCheck"] = False
(WORK / "day.cdf").write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")

done = subprocess.run([LEMON, "--check-live-skip", "day.cdf"], cwd=WORK,
                      capture_output=True, text=True, env=env, timeout=600)

report = WORK / "live-skip-check.txt"
assert report.exists(), f"没有生成报告（rc={done.returncode}）\n{done.stdout}\n{done.stderr}"
text = report.read_text(encoding="utf-8", errors="replace").rstrip()

print(text)

lines = text.splitlines()
assert lines and lines[0] == f"skipped={len(data['contestants'])} stale=0", \
    f"首行汇总不对：{lines[0] if lines else '<空>'}"

before = [line for line in lines if line.startswith("before:")]
at_signal = [line for line in lines if line.startswith("at signal:")]
assert len(before) == len(data["contestants"]), "before 快照数量不对"
assert all("已经是取消色" not in line for line in before), "测试前成绩表就已经是取消色，测试前提不成立"
assert len(at_signal) == len(data["contestants"]), "at signal 快照数量不对"
assert all(line.endswith("OK") for line in at_signal), "有选手的表格在信号发出时还是旧样子"
assert done.returncode == 0, f"隐藏入口返回 {done.returncode}"

print(f"\nOK：{len(at_signal)} 位选手被取消测试时，成绩表当场就变成了「测试被取消」的样子")
