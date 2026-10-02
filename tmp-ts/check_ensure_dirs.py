"""编译并运行 ensure_dirs_test：验证 ensureTaskDirs 不再建 tests/、并清掉残留的 tests/。"""

import os
import pathlib
import shutil
import subprocess

QT = pathlib.Path(r"D:\Qt\6.8.3\mingw_64")
GCC = pathlib.Path(r"D:\Qt\Tools\mingw1310_64\bin")
REPO = pathlib.Path(r"d:\gengen-tuack")
HERE = REPO / "tmp-ts"
BUILD = REPO / "build"

env = dict(os.environ)
env["PATH"] = f"{GCC};{QT / 'bin'};" + env.get("PATH", "")

compile_cmd = [
    str(GCC / "c++.exe"), "-O2", "-std=c++17", "-DUNICODE", "-D_UNICODE",
    "-DSPDLOG_COMPILED_LIB",
    "-o", str(HERE / "ensure_dirs_test.exe"),
    str(HERE / "ensure_dirs_test.cpp"),
    f"-I{REPO / 'src'}", f"-I{REPO / 'src' / 'base'}", f"-I{BUILD}", f"-I{REPO}",
    f"-I{REPO / '3rdparty' / 'spdlog' / 'include'}", f"-I{REPO / '3rdparty' / 'SingleApplication'}",
    f"-I{QT / 'include'}", f"-I{QT / 'include' / 'QtCore'}", f"-I{QT / 'include' / 'QtGui'}",
    f"-I{QT / 'include' / 'QtWidgets'}", f"-I{QT / 'include' / 'QtNetwork'}",
    str(BUILD / "liblemon-base.a"),
    str(BUILD / "3rdparty" / "spdlog" / "libspdlog.a"),
    str(BUILD / "3rdparty" / "SingleApplication" / "libSingleApplication.a"),
    f"-L{BUILD}", f"-L{QT / 'lib'}", "-lQt6Widgets", "-lQt6Gui", "-lQt6Core", "-lQt6Network",
]
built = subprocess.run(compile_cmd, capture_output=True, text=True, env=env)
print("compile rc", built.returncode)
if built.returncode != 0:
    print((built.stdout + built.stderr)[-3000:])
    raise SystemExit(1)

# 造一个「老工程」：题目下有 tests/（里面有东西），根目录下还有老的 reports/、export/
day = BUILD / "export-test" / "EnsureDirs"
shutil.rmtree(day, ignore_errors=True)
problem = day / "problem" / "kapok"
(problem / "tests").mkdir(parents=True)
(problem / "tests" / "old.tmp").write_text("leftover\n", encoding="utf-8")
(day / "problem" / "other" / "tests").mkdir(parents=True)
(day / "reports").mkdir(parents=True)
(day / "reports" / "result.html").write_text("old report\n", encoding="utf-8")
(day / "export").mkdir(parents=True)
(day / "export" / "evaldata.zip").write_text("old zip\n", encoding="utf-8")

print("before:", sorted(p.name for p in problem.iterdir()),
      "|", sorted(p.name for p in day.iterdir()))

# 只对 kapok 调一次
run = subprocess.run([str(HERE / "ensure_dirs_test.exe"), str(day), "kapok"],
                     capture_output=True, text=True, env=env)
print("run rc", run.returncode, (run.stdout + run.stderr).strip()[-300:])

after = sorted(p.name for p in problem.iterdir())
print("after :", after, "|", sorted(p.name for p in day.iterdir()))
assert "tests" not in after, "tests/ 应该被删掉"
for needed in ["data", "down", "graders", "gen"]:
    assert needed in after, f"缺少 {needed}"

# 老的 reports/、export/ 清掉，新的 dist/ 建好
assert not (day / "reports").exists(), "老的 reports/ 应该被清掉"
assert not (day / "export").exists(), "老的 export/ 应该被清掉"
assert (day / "dist" / "reports").is_dir(), "dist/reports 应该建好"
assert (day / "dist" / "export").is_dir(), "dist/export 应该建好"

# 别人的题目（other）没被碰
assert (day / "problem" / "other" / "tests").exists(), "不该动别的题目"
print("OK：不再创建 tests/、删掉了残留的 tests/，dist/ 建好且老的 reports/、export/ 已清空；"
      "其它题目不受影响")
