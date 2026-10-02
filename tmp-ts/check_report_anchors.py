"""验证导出的成绩单里每个 #cXpY 链接都有对应锚点。

重点：被取消测试（违规 / 命名不合规）的选手，成绩表里每一道试题的格子都要能跳到那段
「测试被取消」的说明，而不是只有第一题能跳。正常选手也一并检查（防止改坏）。
"""

import json
import os
import pathlib
import re
import shutil
import subprocess

SRC = pathlib.Path(r"C:\Users\16702\Desktop\test\project\lemon-ng\example\day1\day1.cdf")
WORK = pathlib.Path(r"d:\gengen-tuack\build\export-test\ReportAnchors")
LEMON = r"d:\gengen-tuack\build\lemon.exe"

env = dict(os.environ)
env["PATH"] = r"D:\Qt\6.8.3\mingw_64\bin;" + env.get("PATH", "")


def run(title: str, disqualify_state: int) -> None:
    data = json.loads(SRC.read_text(encoding="utf-8"))
    tasks = len(data["tasks"])

    for contestant in data["contestants"]:
        contestant["disqualifyStateValue"] = disqualify_state
        contestant["disqualifyMessage"] = "命名不合规（验证用）"

    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    (WORK / "day.cdf").write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")

    done = subprocess.run([LEMON, "--export-reports", "day.cdf"], cwd=WORK,
                          capture_output=True, text=True, env=env, timeout=600)
    assert done.returncode == 0, f"导出失败 rc={done.returncode}"

    print(f"\n=== {title} ===")
    for path in sorted((WORK / "dist" / "reports").glob("*.html")):
        html = path.read_text(encoding="utf-8", errors="replace")
        links = set(re.findall(r'href="#([^"]+)"', html))
        anchors = set(re.findall(r'id="([^"]+)"', html)) | set(re.findall(r'name="([^"]+)"', html))
        missing = sorted(link for link in links if link not in anchors)
        print(f"  {path.name}: 链接 {len(links)}，锚点 {len(anchors)}，缺失 {len(missing)}")
        assert not missing, f"{path.name} 里这些链接点不到锚点：{missing}"

        # 赛区报告里只包含本赛区的选手，编号也是局部的：按文件里实际出现的选手来查。
        present = sorted(int(match) for match in
                         {re.fullmatch(r"c(\d+)", anchor).group(1) for anchor in anchors
                          if re.fullmatch(r"c(\d+)", anchor)})
        for contestant_index in present:
            have = [task for task in range(tasks) if f"c{contestant_index}p{task}" in anchors]
            assert len(have) == tasks, f"{path.name} 选手 {contestant_index} 只有 {len(have)}/{tasks} 个试题锚点"

        print(f"  {len(present)} 位选手，每位的 {tasks} 道试题锚点齐全 ✓")

        # 每位选手的最后都要有「返回顶部」（被取消测试的选手之前漏了）。
        tops = len(re.findall(r'href="#top"', html))
        assert tops == len(present), f"{path.name} 只有 {tops} 个「返回顶部」，应为 {len(present)} 个"
        print(f"  {tops} 个「返回顶部」链接 ✓")

        # 逐段看：每位选手那一段（从 <a name="cN"> 到下一个选手）都要有「返回顶部」。
        for part in re.split(r'<a name="c\d+">', html)[1:]:
            assert 'href="#top"' in part, f"{path.name} 有选手那一段没有「返回顶部」"


run("选手被取消测试（命名不合规）", 2)
run("选手正常参与评测", 0)
print("\nOK：成绩表里每个链接都能跳到对应锚点")
