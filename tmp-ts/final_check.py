"""清理后的最终检查：目录大小、程序能不能单独跑、构建系统是否现成可用。"""

import os
import pathlib
import subprocess

ROOT = pathlib.Path(r"d:\gengen-tuack")


def size(path: pathlib.Path) -> int:
    if path.is_file():
        return path.stat().st_size

    return sum(item.stat().st_size for item in path.rglob("*") if item.is_file())


def mb(value: int) -> str:
    return f"{value / 1024 / 1024:8.1f} MB"


print("=== 顶层 ===")
total = 0

for path in sorted(ROOT.iterdir(), key=lambda p: p.name.lower()):
    value = size(path)
    total += value
    print(f"   {path.name:24} {mb(value)}")

print(f"\n   合计（含 .git）{mb(total)}")

print("\n=== build/ ===")
for path in sorted((ROOT / "build").iterdir(), key=lambda p: (p.is_file(), p.name.lower())):
    print(f"   {path.name:24} {mb(size(path))}")

print(f"   build/ 合计 {mb(size(ROOT / 'build'))}")

print("\n=== 程序自检（PATH 只有系统目录）===")
env = dict(os.environ)
env["PATH"] = r"C:\Windows\system32;C:\Windows"
report = pathlib.Path(os.environ["TEMP"]) / "lemon-final-check.txt"
done = subprocess.run([str(ROOT / "build" / "lemon.exe"), "--self-test", str(report)],
                      cwd=ROOT / "build", capture_output=True, text=True, env=env, timeout=300)
print(report.read_text(encoding="utf-8").rstrip() if report.exists() else "（没有报告）")
print("  自检 rc =", done.returncode)
assert done.returncode == 0, "自检失败"
report.unlink(missing_ok=True)

print("\n=== 构建系统（ninja 干跑，不真的编译）===")
env2 = dict(os.environ)
env2["PATH"] = r"D:\Qt\Tools\mingw1310_64\bin;C:\mingw64\bin;" + env2.get("PATH", "")
dry = subprocess.run(["ninja", "-n", "lemon"], cwd=ROOT / "build", capture_output=True, text=True,
                     env=env2)
steps = [line for line in dry.stdout.splitlines() if line.strip()]
print(f"   ninja -n lemon rc={dry.returncode}，待执行 {len(steps)} 步；最后一步：{steps[-1] if steps else '-'}")
assert dry.returncode == 0 and len(steps) > 100

print("\n=== git 状态（未跟踪的新文件 + 改动）===")
status = subprocess.run(["git", "status", "--short"], cwd=ROOT, capture_output=True, text=True,
                        encoding="utf-8", errors="replace").stdout.strip().splitlines()
changed = [line for line in status if not line.startswith("??")]
untracked = [line for line in status if line.startswith("??")]
print(f"   改动/删除 {len(changed)} 项，未跟踪 {len(untracked)} 项")

for line in untracked:
    print("     ", line)

print("\nOK：项目已清理干净")
