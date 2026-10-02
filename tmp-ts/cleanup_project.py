"""清掉项目里明确无用的文件：

- 根目录：build_log.txt（未跟踪的构建日志）、cfg_log.txt（早期误提交的 CMake 配置日志）
- tmp-ts：verify_out.txt（脚本输出）、一次性的 survey*.py
- build/export-test：测试用的临时比赛日（make_fixture.py 随时能重建）

只删这些；build/ 里的构建中间产物交给 package.ps1 -PruneBuildJunk（保留部署好的程序）。
"""

import pathlib
import shutil

ROOT = pathlib.Path(r"d:\gengen-tuack")

files = [
    ROOT / "build_log.txt",
    ROOT / "cfg_log.txt",
    ROOT / "tmp-ts" / "verify_out.txt",
    ROOT / "tmp-ts" / "survey.py",
    ROOT / "tmp-ts" / "survey_junk.py",
    ROOT / "tmp-ts" / "survey_junk2.py",
]

dirs = [
    ROOT / "build" / "export-test",
    ROOT / "build" / "junk-test",
    ROOT / "build" / "package-fresh-test",
]

freed = 0

for path in files:
    if not path.exists():
        print(f"   跳过（不存在）{path.relative_to(ROOT)}")
        continue

    freed += path.stat().st_size
    path.unlink()
    print(f"   删除文件 {path.relative_to(ROOT)}  ({path.stat().st_size if path.exists() else 0} B)")


for path in dirs:
    if not path.exists():
        print(f"   跳过（不存在）{path.relative_to(ROOT)}")
        continue

    size = sum(item.stat().st_size for item in path.rglob("*") if item.is_file())
    shutil.rmtree(path, ignore_errors=True)
    freed += size
    print(f"   删除目录 {path.relative_to(ROOT)}  ({size / 1024:.0f} KB)")

print(f"\n共释放 {freed / 1024:.0f} KB（build/ 的中间产物见下）")
