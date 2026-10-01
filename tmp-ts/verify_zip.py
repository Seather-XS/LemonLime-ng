"""验证导出的 .zip：结构、套层目录、内层 down.zip、密码加密。

用 Python 的 zipfile 读（它支持传统 ZipCrypto，并且 read() 会校验 CRC，
所以能真正判断加密包能不能用密码解开、内容是否正确）。
"""

import pathlib
import subprocess
import zipfile

DAY = pathlib.Path(r"d:\gengen-tuack\build\export-test\TestDay")
LEMON = pathlib.Path(r"d:\gengen-tuack\build\lemon.exe")
ZIP = DAY / "export" / "TestDay.zip"
LOGFILE = DAY / "export-log.txt"

PASSWORD = "pwd123"


def run(flags=(), password=None, zip_name="TestDay.zip"):
    global ZIP
    ZIP = DAY / "export" / zip_name

    if ZIP.exists():
        ZIP.unlink()

    if LOGFILE.exists():
        LOGFILE.unlink()

    cmd = [str(LEMON), "--export-package", "TestDay.cdf", *flags]

    if password:
        cmd += ["--password", password]

    code = subprocess.run(cmd, cwd=DAY).returncode
    log = LOGFILE.read_text(encoding="utf-8", errors="replace") if LOGFILE.exists() else ""

    if code not in (0, None) or not ZIP.exists():
        raise SystemExit(f"导出失败: exit={code}\n{log}")

    return log


def open_zip(flags, password=None):
    log = run(flags, password)
    return zipfile.ZipFile(ZIP), log


def show(title, names):
    print(f"  {title}: {names}")


def check(title, flags, password=None, expect_prefix=False, expect_nested=False):
    print(f"\n=== {title} ===")
    log = run(flags, password)
    outer = zipfile.ZipFile(ZIP)
    show("外层", outer.namelist())

    if expect_nested:
        assert outer.namelist() == ["down.zip"], f"外层应该只有 down.zip: {outer.namelist()}"

        # 没密码时必须打不开
        if password:
            try:
                outer.read("down.zip")
                raise SystemExit("!! 加密包竟然不用密码就能读")
            except RuntimeError as error:
                print(f"  不带密码读取被拒绝（符合预期）: {error}")

        data = outer.read("down.zip", pwd=password.encode() if password else None)
        inner = zipfile.ZipFile(__import__("io").BytesIO(data))
        show("内层 down.zip", inner.namelist())
        base = "TestDay/" if expect_prefix else ""
        assert f"{base}statement.pdf" in inner.namelist()
        assert f"{base}plus/plus1.in" in inner.namelist()
        assert f"{base}plus/samples/ignored.txt" not in inner.namelist(), "down 子目录里的文件不该被打包"
        content = inner.read(f"{base}plus/plus1.in", pwd=password.encode() if password else None)
        assert content.replace(b"\r\n", b"\n") == b"1 2\n", content
        print(f"  内层内容 OK: plus/plus1.in = {content!r}")
    else:
        names = outer.namelist()
        base = "TestDay/" if expect_prefix else ""
        assert f"{base}statement.pdf" in names, names
        assert f"{base}plus/plus1.in" in names, names
        assert f"{base}plus/samples/ignored.txt" not in names, "down 子目录里的文件不该被打包"
        assert f"{base}C. Graph/" in names, names

        if password:
            try:
                outer.read(f"{base}plus/plus1.in")
                raise SystemExit("!! 加密包竟然不用密码就能读")
            except RuntimeError as error:
                print(f"  不带密码读取被拒绝（符合预期）: {error}")

        content = outer.read(f"{base}plus/plus1.in", pwd=password.encode() if password else None)

        if content != b"1 2\n":
            # zipfile 读出来会做 CRC 校验，能读到就说明密码和 CRC 都对
            content = content.replace(b"\r\n", b"\n")

        assert content == b"1 2\n", content
        print(f"  内容 OK: plus/plus1.in = {content!r}")
        print("  down 子目录未被递归 ✓")

    print("  日志:")
    for line in log.strip().splitlines():
        print(f"    {line}")


check("1) 默认（不套层、不内层、不加密）", ())
check("2) --wrap 套一层比赛日目录", ("--wrap",), expect_prefix=True)
check("3) --nested 内层 down.zip", ("--nested",), expect_nested=True)
check("4) --nested --wrap --password", ("--nested", "--wrap"), password=PASSWORD,
      expect_prefix=True, expect_nested=True)
check("5) --password 直接加密外层（不套内层）", (), password=PASSWORD)


# ------------------------------------------------------------------
# 「测试数据」型
# ------------------------------------------------------------------
def check_testdata(title, flags, password=None, expect_nested=False, expect=(), not_expect=()):
    print(f"\n=== {title} ===")
    log = run(["--kind", "testdata", *flags], password, zip_name="evaldata.zip")
    outer = zipfile.ZipFile(ZIP)
    names = outer.namelist()

    if expect_nested:
        assert names == ["TestDay.zip"], names

        if password:
            try:
                outer.read("TestDay.zip")
                raise SystemExit("!! 加密包竟然不用密码就能读")
            except RuntimeError as error:
                print(f"  不带密码读取被拒绝（符合预期）: {error}")

        inner = zipfile.ZipFile(
            __import__("io").BytesIO(outer.read("TestDay.zip", pwd=password.encode() if password else None))
        )
        names = inner.namelist()
    else:
        show("外层", names)

    for name in expect:
        assert name in names, f"缺少 {name}\n实际: {names}"

    for name in not_expect:
        assert name not in names, f"不该出现 {name}\n实际: {names}"

    print("  条目结构符合预期 ✓")
    for line in log.strip().splitlines():
        print(f"    {line}")
    return names


# 默认：每道题一个目录，保留结构（data/、graders/）
check_testdata("6) 测试数据 · 默认（分题目录 + 保留结构）", (),
               expect=["plus/", "plus/data/plus1.in", "plus/data/plus1.ans",
                       "plus/graders/plus_grader.cpp", "tree/data/sub/nested.txt", "C. Graph/"])

# 不保留结构：文件铺到题目录下
check_testdata("7) 测试数据 · 不保留结构", ("--no-structure",),
               expect=["plus/plus1.in", "plus/plus1.ans", "plus/plus_grader.cpp",
                       "tree/tree1.in", "tree/nested.txt", "tree/tree_grader.cpp"],
               not_expect=["plus/data/plus1.in", "plus/graders/plus_grader.cpp"])

# 样例数据：一定保留结构，多出 down/
check_testdata("8) 测试数据 · 带样例数据（down/）", ("--samples",),
               expect=["plus/data/plus1.in", "plus/down/plus1.in", "plus/down/plus1.ans",
                       "plus/graders/plus_grader.cpp"],
               not_expect=["plus/plus1.in"])

# 不分题目录 + 平铺：重名文件被跳过并记日志
names = check_testdata("9) 测试数据 · 不分题目录 + 平铺", ("--no-per-task", "--no-structure"),
                       expect=["plus1.in", "plus1.ans", "plus_grader.cpp", "tree1.in",
                               "tree1.ans", "tree_grader.cpp", "nested.txt"],
                       not_expect=["plus/", "tree/", "C. Graph/"])
assert "Duplicate file name skipped" in open(LOGFILE, encoding="utf-8").read()
assert "duplicate task files: yes" in open(LOGFILE, encoding="utf-8").read()
print("  重名文件被跳过并记了日志 ✓")

# 全套：套层 + 内层同名压缩包 + 密码
check_testdata("10) 测试数据 · 套层 + 内层同名 zip + 密码", ("--wrap", "--nested"),
               password=PASSWORD, expect_nested=True,
               expect=["TestDay/", "TestDay/plus/data/plus1.in"])


# ------------------------------------------------------------------
# 「选手代码」型（外层固定 answers.zip）
# ------------------------------------------------------------------
def check_answers(title, flags, password=None, whole=None, expect=(), not_expect=(), inspect=None):
    """whole = 期望的外层完整条目列表（按顺序）；inspect(outer) 关句柄前调。"""
    print(f"\n=== {title} ===")
    log = run(["--kind", "answers", *flags], password, zip_name="answers.zip")

    with zipfile.ZipFile(ZIP) as outer:
        names = outer.namelist()

        if whole is not None:
            assert names == whole, f"外层不一致\n实际: {names}"
        else:
            show("外层", names)

        for name in expect:
            assert name in names, f"缺少 {name}\n实际: {names}"

        for name in not_expect:
            assert name not in names, f"不该出现 {name}\n实际: {names}"

        if password:
            for entry in names:
                if entry.endswith("/"):
                    continue

                try:
                    outer.read(entry)
                    raise SystemExit(f"!! {entry} 竟然不用密码就能读")
                except RuntimeError:
                    pass

        if inspect:
            inspect(outer)

    print("  条目结构符合预期 ✓")
    for line in log.strip().splitlines():
        print(f"    {line}")
    return names


def read_inner(outer, name, password=None):
    inner = zipfile.ZipFile(__import__("io").BytesIO(outer.read(name, pwd=password.encode() if password else None)))
    return inner


# 默认：赛区同名文件夹是强制的
check_answers("11) 选手代码 · 默认（每赛区一层目录）", (),
              expect=["HN/", "HN/HN-S001/", "HN/HN-S001/plus.cpp", "HN/HN-S001/tree.cpp",
                      "HN/HN-S002/plus.cpp", "ZJ/", "ZJ/ZJ-S001/plus.cpp"],
              not_expect=["HN.zip", "ZJ.zip", "HN-S001/"])

# 只开压缩包嵌套：赛区目录照旧，另外每赛区一个内层 zip
check_answers("12) 选手代码 · 压缩包嵌套（每赛区一个 zip）", ("--nested",),
              expect=["HN/", "ZJ/", "HN.zip", "ZJ.zip"],
              inspect=lambda outer: (
                  print(f"    HN.zip 内容: {read_inner(outer, 'HN.zip').namelist()}"),
                  None,
              )[1])

# 只开同名文件夹：<比赛日>/ 下再按赛区分一层
check_answers("13) 选手代码 · 同名文件夹（比赛日 + 赛区两层）", ("--wrap",),
              expect=["TestDay/", "TestDay/HN/", "TestDay/HN/HN-S001/",
                      "TestDay/HN/HN-S001/plus.cpp", "TestDay/ZJ/ZJ-S001/plus.cpp"],
              not_expect=["TestDay/HN.zip", "HN/"])

# 两个都开：外层 = <比赛日>/ 目录 + 每个赛区的同名文件夹与同名内层 zip
def _inspect_both(outer):
    inner = read_inner(outer, "TestDay/HN.zip")
    print(f"    TestDay/HN.zip 内容: {inner.namelist()}")
    assert inner.namelist() == ["HN-S001/", "HN-S001/plus.cpp", "HN-S001/tree.cpp", "HN-S002/",
                                "HN-S002/plus.cpp"], inner.namelist()


check_answers("14) 选手代码 · 同名文件夹 + 内层 zip", ("--wrap", "--nested"),
              expect=["TestDay/", "TestDay/HN/", "TestDay/HN/HN-S001/plus.cpp",
                      "TestDay/ZJ/ZJ-S001/plus.cpp", "TestDay/HN.zip", "TestDay/ZJ.zip"],
              inspect=_inspect_both)

# 带密码：内外层都要密码
check_answers("15) 选手代码 · 套层 + 内层 zip + 密码", ("--wrap", "--nested"), password=PASSWORD,
              expect=["TestDay/", "TestDay/HN.zip", "TestDay/ZJ.zip"],
              inspect=lambda outer: (
                  print(f"    解密后的 TestDay/ZJ.zip: "
                        f"{read_inner(outer, 'TestDay/ZJ.zip', PASSWORD).namelist()}"),
                  None,
              )[1])

print("\n全部通过 ✓")
