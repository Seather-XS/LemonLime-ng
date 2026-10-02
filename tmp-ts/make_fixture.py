"""给导出验证准备 fixture。

按题型把题目配好，并在每道题下放上 gen/、tests/ 这两个「不该被导出」的目录，
用来验证测试数据包只带该带的东西：

  A. Plus    传统题          → 只有 data/
  B. Tree    使用 SPJ        → data/ + graders/ 里的校验器源码
  C. Graph   交互题          → data/ + graders/ 里的交互库与主交互程序
  D. Answer  提交答案题      → 只有 data/

可以反复运行（幂等）：已有文件不覆盖，缺的补上。
"""

import json
import pathlib

DAY = pathlib.Path(r"d:\gengen-tuack\build\export-test\TestDay")
CDF = DAY / "TestDay.cdf"

GRADER_TEMPLATE = "// {name}: {note}\n#include \"testlib.h\"\nint main() {{ return 0; }}\n"


def write(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def ensure(path: pathlib.Path, text: str) -> None:
    if not path.exists():
        write(path, text)


def prepare_files() -> None:
    for task in ["plus", "tree", "C. Graph", "D. Answer"]:
        base = DAY / "problem" / task
        for sub in ["data", "graders", "gen", "tests"]:
            (base / sub).mkdir(parents=True, exist_ok=True)

        # 生成器与中间产物：导出时一定不能出现
        ensure(base / "gen" / "g.cpp", "// generator of %s, must not be exported\n" % task)
        ensure(base / "tests" / "leftover.tmp", "leftover of %s, must not be exported\n" % task)

        # 样例数据：down/ 本层的文件要导出，down/ 里子目录下的内容一律不许被读
        ensure(base / "down" / "sample1.in", "1 2\n")
        ensure(base / "down" / "sample1.ans", "3\n")
        ensure(base / "down" / "samples" / "ignored.txt", "in a subfolder, must not be exported\n")
        ensure(base / "down" / "nested" / "ignored.txt", "in a subfolder, must not be exported\n")

    # 传统题：graders/ 里就算有文件，也不该被导出
    ensure(DAY / "problem/plus/graders/plus_grader.cpp", GRADER_TEMPLATE.format(name="plus_grader", note="不使用，不该导出"))

    # SPJ：# 被引用的校验器要导出，别的 graders 文件不要
    ensure(DAY / "problem/tree/graders/tree_spj.cpp", GRADER_TEMPLATE.format(name="tree_spj", note="SPJ 校验器，要导出"))
    ensure(DAY / "problem/tree/graders/tree_grader.cpp", GRADER_TEMPLATE.format(name="tree_grader", note="不该导出"))

    # 交互题：交互库 + 主交互程序都要导出，未引用的不要
    ensure(DAY / "problem/C. Graph/graders/graph.h", "// graph.h: 交互库，要导出\n")
    ensure(DAY / "problem/C. Graph/graders/graph_grader.cpp",
           GRADER_TEMPLATE.format(name="graph_grader", note="主交互程序，要导出"))
    ensure(DAY / "problem/C. Graph/graders/unused.cpp", GRADER_TEMPLATE.format(name="unused", note="不该导出"))

    # 提交答案题
    ensure(DAY / "problem/D. Answer/data/ans1.in", "3 4\n")
    ensure(DAY / "problem/D. Answer/data/ans1.ans", "7\n")
    ensure(DAY / "problem/D. Answer/graders/d_grader.cpp", GRADER_TEMPLATE.format(name="d_grader", note="不该导出"))


def patch_contest() -> None:
    data = json.loads(CDF.read_text(encoding="utf-8"))
    tasks = data["tasks"]
    by_title = {task["problemTitle"]: task for task in tasks}

    # A. Plus：传统题（LineByLine），不需要 graders/
    plus = by_title["A. Plus"]
    plus.update(taskType=0, comparisonMode=0)

    # B. Tree：传统题 + Lemon 风格 SPJ 校验器
    tree = by_title["B. Tree"]
    tree.update(taskType=0, comparisonMode=4, specialJudge="tree/graders/tree_spj.cpp")

    # C. Graph：交互题（交互库 + 主交互程序）
    graph = by_title["C. Graph"]
    graph.update(taskType=2, comparisonMode=0,
                 interactor="C. Graph/graders/graph.h", interactorName="graph.h",
                 grader="C. Graph/graders/graph_grader.cpp")

    # D. Answer：提交答案题
    if "D. Answer" not in by_title:
        tasks.append({
            "problemTitle": "D. Answer",
            "sourceFileName": "D. Answer",
            "inputFileName": "ans.in",
            "outputFileName": "ans.out",
            "taskType": 1,
            "comparisonMode": 0,
            "answerFileExtension": "out",
        })

    CDF.write_text(json.dumps(data, ensure_ascii=False, indent=4), encoding="utf-8")


prepare_files()
patch_contest()
print("fixture ready:", DAY)
