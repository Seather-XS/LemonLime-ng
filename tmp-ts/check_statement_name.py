"""验证题面相关的三件事：

1. 题面选项卡的 PDF 文件名模板：`<day>` / `<title-day>` / `<title>` 占位符解析（隐藏入口 --pdf-name）。
2. 「导出 PDF」与「打开 PDF」已经并成一个按钮（隐藏入口 --check-statement-ui）。
3. 选手目录的导出可以选 statement/ 下的题面文件（--export-package --statement）。
"""

import json
import os
import pathlib
import shutil
import subprocess
import zipfile

SRC = pathlib.Path(r"C:\Users\16702\Desktop\test\project\lemon-ng\example\day1\day1.cdf")
ROOT = pathlib.Path(r"d:\gengen-tuack\build\export-test\StatementName")
LEMON = r"d:\gengen-tuack\build\lemon.exe"

PROJECT_TITLE = "NOI 2026 模拟赛"
DAY_TITLE = "第一试"
PATTERN = "<title>-<title-day>-<day>"
PDF_NAME = f"{PROJECT_TITLE}-{DAY_TITLE}-day1.pdf"

env = dict(os.environ)
env["PATH"] = r"D:\Qt\6.8.3\mingw_64\bin;" + env.get("PATH", "")


def run(*args, cwd=ROOT):
    return subprocess.run([LEMON, *args], cwd=cwd, capture_output=True, text=True, env=env, timeout=600)


def make_fixture():
    shutil.rmtree(ROOT, ignore_errors=True)
    (ROOT / "day1").mkdir(parents=True)

    data = json.loads(SRC.read_text(encoding="utf-8"))
    data["statementPdfName"] = PATTERN
    (ROOT / "day1" / "day1.cdf").write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")

    project = {"title": PROJECT_TITLE, "days": [{"title": DAY_TITLE, "file": "day1/day1.cdf"}]}
    (ROOT / "contest.conf").write_text(json.dumps(project, ensure_ascii=False), encoding="utf-8")

    # 题面目录里放两个文件：模板算出来的那个 + 一个老名字。
    statement = ROOT / "day1" / "statement"
    shutil.copytree(pathlib.Path(SRC).parent / "statement", statement)
    (statement / PDF_NAME).write_bytes(b"%PDF-1.4 fake")
    (statement / "statement.pdf").write_bytes(b"%PDF-1.4 fake old")
    return data


def check_pdf_name():
    print("\n=== 1) PDF 文件名模板 ===")
    assert run("--pdf-name", "day1/day1.cdf").returncode == 0
    text = (ROOT / "day1" / "statement-name.txt").read_text(encoding="utf-8")
    print("  " + text.replace("\n", "\n  ").strip())
    fields = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)

    assert fields["day"] == "day1", fields
    assert fields["title-day"] == DAY_TITLE, fields
    assert fields["title"] == PROJECT_TITLE, fields
    assert fields["pattern"] == PATTERN, fields
    assert fields["file"] == PDF_NAME, fields

    # 没有工程文件时：<title> 退回比赛日自己的标题，<title-day> 退回比赛日文件名。
    # 注意放在没有 contest.conf 的目录下（上一级也不能有）。
    plain = pathlib.Path(r"d:\gengen-tuack\build\export-test\StatementNamePlain")
    shutil.rmtree(plain, ignore_errors=True)
    plain.mkdir(parents=True)
    data = json.loads((ROOT / "day1" / "day1.cdf").read_text(encoding="utf-8"))
    data["statementPdfName"] = "<title>-<day>"
    assert data["contestTitle"], "真实 day1.cdf 里应该有比赛标题"
    (plain / "day9.cdf").write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")

    assert run("--pdf-name", "day9.cdf", cwd=plain).returncode == 0
    text = (plain / "statement-name.txt").read_text(encoding="utf-8")
    print("  「没有工程文件」的退路：" + text.splitlines()[-1].strip())
    fields = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)

    assert fields["title-day"] == "", fields
    assert fields["title"] == data["contestTitle"], fields
    assert fields["file"] == f"{data['contestTitle']}-day9.pdf", fields
    print("  占位符（含没有工程文件时的退路）都正确 ✓")


def check_statement_ui():
    print("\n=== 2) 题面选项卡只剩一个导出按钮 ===")
    assert run("--check-statement-ui", "day1/day1.cdf").returncode == 0
    text = (ROOT / "day1" / "statement-ui-check.txt").read_text(encoding="utf-8")
    print("  " + text.replace("\n", "\n  ").strip())
    fields = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)

    assert fields["problems"] == "0", fields
    assert fields["pdf buttons"] == "1", fields
    assert fields["resolved"] == PDF_NAME, fields
    assert fields["pattern"] == PATTERN, fields
    combo = next(line for line in text.splitlines() if line.startswith("statement combo:"))
    assert PDF_NAME in combo, combo

    # 默认题面还没导出时，下拉框里要有个标着「(missing)」的条目，别让用户以为没得选。
    (ROOT / "day1" / "statement" / PDF_NAME).unlink()
    assert run("--check-statement-ui", "day1/day1.cdf").returncode == 0
    text = (ROOT / "day1" / "statement-ui-check.txt").read_text(encoding="utf-8")
    combo = [line for line in text.splitlines() if line.startswith("statement combo")][0]
    print("  " + combo.strip())
    assert f"{PDF_NAME} (missing)" in text, text
    (ROOT / "day1" / "statement" / PDF_NAME).write_bytes(b"%PDF-1.4 fake")
    print("  按钮合并 + 文件名框写回比赛日 + 题面对下拉框都正确 ✓")


def contestant_zip(flags):
    target = ROOT / "day1" / "dist" / "export" / "day1.zip"

    if target.exists():
        target.unlink()

    done = run("--export-package", "day1/day1.cdf", "--kind", "contestant", *flags)
    assert done.returncode == 0, done.stdout + done.stderr
    assert target.exists(), "没有生成 day1.zip"

    with zipfile.ZipFile(target) as archive:
        return [name for name in archive.namelist() if not name.endswith("/")]


def check_package_statement():
    print("\n=== 3) 选手目录里自选题面文件 ===")
    names = contestant_zip([])
    print(f"  默认（模板算出的名字）: {names}")
    assert PDF_NAME in names, names
    assert "statement.pdf" not in names, names

    names = contestant_zip(["--statement", "statement.pdf"])
    print(f"  指定 statement.pdf: {names}")
    assert "statement.pdf" in names, names
    assert PDF_NAME not in names, names

    # 题面放在包根目录，不带 statement/ 这层目录
    assert not any(name.startswith("statement/") for name in names), names
    print("  题面按所选文件进入压缩包根目录 ✓")


make_fixture()
check_pdf_name()
check_statement_ui()
check_package_statement()

print("\nOK：题面文件名模板 / 单个导出按钮 / 自选题面文件都符合预期")
