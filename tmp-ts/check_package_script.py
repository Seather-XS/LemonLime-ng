"""验证 package.ps1 的 -PruneBuildJunk / -OutDir 两种模式（在临时目录里跑，不动真正的 build）。"""

import pathlib
import shutil
import subprocess

ROOT = pathlib.Path(r"d:\gengen-tuack")
BUILD = ROOT / "build"
JUNK = BUILD / "junk-test"
FRESH = BUILD / "package-fresh-test"

env = dict(__import__("os").environ)


def run(args):
    return subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                           str(ROOT / "package.ps1"), *args],
                          cwd=ROOT, capture_output=True, text=True, env=env, timeout=1800)


print("=== A) 就地在「假 build 目录」里部署 + -PruneBuildJunk ===")
shutil.rmtree(JUNK, ignore_errors=True)
JUNK.mkdir(parents=True)
shutil.copy2(BUILD / "lemon.exe", JUNK / "lemon.exe")

fake = {
    "build.ninja": "junk",
    "CMakeCache.txt": "junk",
    "CMakeFiles/lemon.dir/main.obj": "junk",
    "lemon_autogen/mocs_compilation.cpp.obj": "junk",
    "3rdparty/spdlog/build.ninja": "junk",
    "liblemon-core.a": "junk",
    "dead.obj": "junk",
    "zh_CN.qm": "junk",
    ".ninja_log": "junk",
}

for name, text in fake.items():
    path = JUNK / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")

done = run(["-NoBuild", "-BuildDir", str(JUNK), "-PruneBuildJunk"])
print(done.stdout[-3000:])
assert done.returncode == 0, done.stderr[-2000:]

left = sorted(str(path.relative_to(JUNK)) for path in JUNK.rglob("*") if path.is_file())
print("  剩下的文件：")
for name in left:
    print("   ", name)

for name in fake:
    assert not (JUNK / name).exists(), f"{name} 应该被清掉"
assert (JUNK / "lemon.exe").exists()
assert (JUNK / "Qt6Widgets.dll").exists()
assert (JUNK / "statement-templates/noi new/main.tex").exists()
assert (JUNK / "LICENSE").exists()
print("  就地部署 + 清构建中间产物 ✓")

print("\n=== B) -OutDir 指向新目录（非就地）===")
shutil.rmtree(FRESH, ignore_errors=True)
done = run(["-NoBuild", "-OutDir", str(FRESH)])
print(done.stdout[-1500:])
assert done.returncode == 0, done.stderr[-2000:]
assert (FRESH / "lemon.exe").exists()
assert (FRESH / "platforms/qwindows.dll").exists()
assert not (FRESH / "CMakeFiles").exists()
assert not list(FRESH.glob("*.a")), "新目录里不该有构建产物"
print("  干净的发布目录 ✓")

print("\n=== C) -OutDir 指向「别人的目录」：默认拒绝，加 -Force 才清空 ===")
shutil.rmtree(FRESH, ignore_errors=True)
FRESH.mkdir(parents=True)
(FRESH / "precious.txt").write_text("用户自己的东西", encoding="utf-8")

done = run(["-NoBuild", "-OutDir", str(FRESH)])
combined = done.stdout + done.stderr
assert done.returncode != 0 and "已经有" in combined, combined[-800:]
assert (FRESH / "precious.txt").exists(), "拒绝的时候不该动里面的文件"
print("  默认拒绝 OK（precious.txt 还在）")

done = run(["-NoBuild", "-OutDir", str(FRESH), "-Force"])
assert done.returncode == 0, done.stderr[-1000:]
assert not (FRESH / "precious.txt").exists()
assert (FRESH / "lemon.exe").exists()
print("  -Force 后正常 OK")

# 已经部署过的目录（里面有 lemon.exe）直接刷新，不用 -Force
done = run(["-NoBuild", "-OutDir", str(FRESH)])
assert done.returncode == 0, done.stderr[-1000:]
print("  部署过的目录可以直接刷新 OK")

shutil.rmtree(JUNK, ignore_errors=True)
shutil.rmtree(FRESH, ignore_errors=True)

print("\n=== D) 用部署好的 build\\lemon.exe 干点真活（PATH 里没有 Qt / MinGW）===")
REAL = pathlib.Path(r"C:\Users\16702\Desktop\test\project\lemon-ng\example\day1\day1.cdf")
WORKDIR = BUILD / "deployed-check"
shutil.rmtree(WORKDIR, ignore_errors=True)
WORKDIR.mkdir(parents=True)
shutil.copy2(REAL, WORKDIR / "day1.cdf")

cleanEnv = dict(env)
cleanEnv["PATH"] = r"C:\Windows\system32;C:\Windows"

done = subprocess.run([str(BUILD / "lemon.exe"), "--check-statement-ui", "day1.cdf"],
                      cwd=WORKDIR, capture_output=True, text=True, env=cleanEnv, timeout=300)
report = WORKDIR / "statement-ui-check.txt"
assert done.returncode == 0, f"rc={done.returncode}\n{done.stdout}\n{done.stderr}"
assert report.exists(), "没有生成报告"
text = report.read_text(encoding="utf-8")
print("  " + text.strip().replace("\n", "\n  "))
assert text.startswith("problems=0"), text
print("  部署后的程序能独立跑完整流程 ✓")

shutil.rmtree(WORKDIR, ignore_errors=True)

print("\nOK：package.ps1 的就地 / 独立目录 / 保护逻辑都对")
