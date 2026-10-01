"""把 lupdate 生成的新条目合并进现有 .ts（不改动其它内容），并填好新条目的译文。

只用一次：tmp-ts/*.ts 是 lupdate 的输出，translations/*.ts 是原文件。
"""

import html
import pathlib
import re

REPO = pathlib.Path(r"d:\gengen-tuack")
REAL_DIR = REPO / "translations"
TMP_DIR = REPO / "tmp-ts"

CONTEXT_RE = re.compile(r"([ \t]*)<context>(.*?)</context>", re.S)
NAME_RE = re.compile(r"<name>(.*?)</name>", re.S)
MESSAGE_RE = re.compile(r"([ \t]*)<message>(.*?)</message>", re.S)
SOURCE_RE = re.compile(r"<source>(.*?)</source>", re.S)
TRANS_RE = re.compile(r"<translation[^>]*>.*?</translation>", re.S)

ZH_CN = {
    "Export": "导出",
    "Package type:": "包类型：",
    "Refresh": "刷新",
    "Export .zip": "导出 .zip",
    "One subfolder per task (named after the task's folder), containing the files directly inside that task's <b>down/</b> folder (subfolders are skipped). The statement PDF is placed in the zip root.":
        "每道题一个子目录（用题目目录名），放入该题 <b>down/</b> 目录下的文件（不包含子目录里的文件）；题面 PDF 放在压缩包根目录。",
    "Output:": "输出：",
    "Package contents": "打包内容",
    "Export log": "导出日志",
    "Path in package": "包内路径",
    "Source on disk": "磁盘来源",
    "No contest": "尚未打开比赛",
    "%1 file(s), %2 folder(s)": "%1 个文件，%2 个目录",
    "The statement PDF is missing; export it in the Statement tab first if you need it.":
        "题面 PDF 不存在；如果需要，请先在「题面」选项卡里导出。",
    "The statement PDF (%1) does not exist yet, so the package will not contain the statement. Export anyway?":
        "题面 PDF（%1）还不存在，压缩包里将不含题面。\n仍要导出吗？",
    "Export failed: %1": "导出失败：%1",
    "Pack this contest day into .zip files for the target platforms...": "把当前比赛日打包成各平台要的 .zip...",
    "Contestant Directory": "选手目录",
    "Statement PDF not found: %1": "找不到题面 PDF：%1",
    "No down folder for task %1: %2": "题目 %1 没有 down 目录：%2",
    "%1: %2 file(s)": "%1：%2 个文件",
    "Nothing to export": "没有可导出的内容",
    "Cannot make folder %1": "无法创建目录 %1",
    "Cannot overwrite %1": "无法覆盖 %1",
    "Writing %1": "正在写入 %1",
    "Done: %1 folder(s), %2 file(s)": "完成：%1 个目录，%2 个文件",
    "Cannot write %1": "无法写入 %1",
    "Export is done": "导出完成",
    "Region": "赛区",
    "Cannot open file %1": "无法打开文件 %1",
    "Options": "选项",
    "Wrap everything in a folder named after the contest day file": "把所有内容套进与比赛日文件同名的目录",
    "Adds one more level: <day>.zip contains a <day>/ folder holding everything.":
        "多套一层：<day>.zip 里是一个 <day>/ 目录，所有内容都在里面。",
    "Pack everything into an inner zip named down.zip": "把全部内容打包成内层 down.zip（压缩包套压缩包）",
    "The outer .zip will hold nothing but down.zip; all the content lives inside it.":
        "外层 .zip 里只有 down.zip，其它内容都在里面。",
    "Encrypt with a password (ZipCrypto)": "使用密码加密（ZipCrypto）",
    "Password:": "密码：",
    "Show": "显示",
    "Please type a password first.": "请先输入密码。",
    "Cannot write the archive": "无法写入压缩包",
    "down.zip: %1 folder(s), %2 file(s)": "down.zip：%1 个目录，%2 个文件",
    "Test Data": "测试数据",
    "Nest an inner zip named %1": "内层压缩包命名为 %1",
    "Give each task its own folder": "每道题单独一个目录",
    "Keep the original folder structure (data/, graders/, ...)": "保留原来的目录结构（data/、graders/ 等）",
    "Also export the sample data (down/)": "一并导出样例数据（down/）",
    "The sample data is taken from each task's down/ folder, keeping the structure.":
        "样例数据取自每道题的 down/ 目录，并保留目录结构。",
    "Test data of every task: the whole <b>data/</b> and <b>graders/</b> folders (plus <b>down/</b> when the sample data is included). The statement PDF is not packed.":
        "每道题的测试数据：完整的 <b>data/</b> 与 <b>graders/</b> 目录（勾选样例数据时再加 <b>down/</b>）。不含题面 PDF。",
    "Each task gets its own folder inside the package.": "每道题在压缩包里单独一个目录。",
    "Some tasks use the same data file names, so every task must keep its own folder.":
        "有试题的数据文件重名，因此每道题必须单独一个目录。",
    "Some tasks use the same data file names, so they cannot share one folder.":
        "有试题的数据文件重名，不能放在同一个目录里。",
    "The sample data keeps the folder structure, so this cannot be turned off.":
        "样例数据会保留目录结构，因此不能关闭。",
    "Keeps data/, graders/ and down/ inside each task folder.":
        "每道题目录里保留 data/、graders/、down/ 这几层。",
    "Adds one more level: the .zip contains a <day>/ folder holding everything.":
        "多套一层：.zip 里是一个 <day>/ 目录，所有内容都在里面。",
    "The outer .zip will hold nothing but the inner zip; all the content lives inside it.":
        "外层 .zip 里只有内层压缩包，其它内容都在里面。",
    "No %1 folder for task %2": "题目 %2 没有 %1 目录",
    "Duplicate file name skipped: %1": "文件重名已跳过：%1",
    "%1: %2 folder(s), %3 file(s)": "%1：%2 个目录，%3 个文件",
    "Answers": "选手代码",
    "No contestant in current contest": "当前比赛还没有选手",
    "Give every region its own folder (named after the region)": "每个赛区单独一个文件夹（用赛区名）",
    "Contestants of one region go into a folder named after it.":
        "同一个赛区的选手放进以赛区命名的目录。",
    "Also wrap everything in a folder named after the contest day file": "再套一层与比赛日同名的目录",
    "Gives <day>/ and, when regions are enabled, <day>/<region>/ for every region.":
        "得到 <day>/；启用赛区时每个赛区再占一层：<day>/<region>/。",
    "Answers of every contestant: one folder per contestant with all of their source files. With regions enabled you can group them into region folders and/or one inner zip per region. The <day>/ wrapper is optional.":
        "每位选手一个目录，里面是他全部题目的源代码。启用赛区时可按赛区分组：赛区同名文件夹、或每个赛区一个内层压缩包（可同时开）；<day>/ 那层可选。",
    "Give every region its own inner zip": "每个赛区单独一个内层压缩包",
    "The outer answers.zip will also hold one <region>.zip per region.":
        "answers.zip 里还会为每个赛区放一个 <region>.zip。",
    "Answers of every contestant: one folder per contestant with all of their source files. With regions enabled you can group them into region folders and/or one inner zip per region.":
        "每位选手一个目录，里面是他全部题目的源代码。启用赛区时可按赛区分组：赛区同名文件夹、或每个赛区一个内层压缩包（可同时开）。",
}

ZH_TW = {
    "Export": "匯出",
    "Package type:": "套件類型：",
    "Refresh": "重新整理",
    "Export .zip": "匯出 .zip",
    "One subfolder per task (named after the task's folder), containing the files directly inside that task's <b>down/</b> folder (subfolders are skipped). The statement PDF is placed in the zip root.":
        "每題一個子目錄（使用題目目錄名），放入該題 <b>down/</b> 目錄下的檔案（不含子目錄裡的檔案）；題面 PDF 放在壓縮檔根目錄。",
    "Output:": "輸出：",
    "Package contents": "打包內容",
    "Export log": "匯出記錄",
    "Path in package": "壓縮檔內路徑",
    "Source on disk": "磁碟來源",
    "No contest": "尚未開啟比賽",
    "%1 file(s), %2 folder(s)": "%1 個檔案，%2 個目錄",
    "The statement PDF is missing; export it in the Statement tab first if you need it.":
        "題面 PDF 不存在；若需要，請先在「題面」分頁匯出。",
    "The statement PDF (%1) does not exist yet, so the package will not contain the statement. Export anyway?":
        "題面 PDF（%1）還不存在，壓縮檔將不含題面。\n仍要匯出嗎？",
    "Export failed: %1": "匯出失敗：%1",
    "Pack this contest day into .zip files for the target platforms...": "把目前比賽日打包成各平台要的 .zip...",
    "Contestant Directory": "選手目錄",
    "Statement PDF not found: %1": "找不到題面 PDF：%1",
    "No down folder for task %1: %2": "題目 %1 沒有 down 目錄：%2",
    "%1: %2 file(s)": "%1：%2 個檔案",
    "Nothing to export": "沒有可匯出的內容",
    "Cannot make folder %1": "無法建立目錄 %1",
    "Cannot overwrite %1": "無法覆蓋 %1",
    "Writing %1": "正在寫入 %1",
    "Done: %1 folder(s), %2 file(s)": "完成：%1 個目錄，%2 個檔案",
    "Cannot write %1": "無法寫入 %1",
    "Export is done": "匯出完成",
    "Region": "賽區",
    "Cannot open file %1": "無法開啟檔案 %1",
    "Options": "選項",
    "Wrap everything in a folder named after the contest day file": "把所有內容套進與比賽日文件同名的目錄",
    "Adds one more level: <day>.zip contains a <day>/ folder holding everything.":
        "多套一層：<day>.zip 裡是一個 <day>/ 目錄，所有內容都在裡面。",
    "Pack everything into an inner zip named down.zip": "把全部內容打包成內層 down.zip（壓縮檔套壓縮檔）",
    "The outer .zip will hold nothing but down.zip; all the content lives inside it.":
        "外層 .zip 裡只有 down.zip，其它內容都在裡面。",
    "Encrypt with a password (ZipCrypto)": "使用密碼加密（ZipCrypto）",
    "Password:": "密碼：",
    "Show": "顯示",
    "Please type a password first.": "請先輸入密碼。",
    "Cannot write the archive": "無法寫入壓縮包",
    "down.zip: %1 folder(s), %2 file(s)": "down.zip：%1 個目錄，%2 個檔案",
    "Test Data": "測試資料",
    "Nest an inner zip named %1": "內層壓縮檔命名為 %1",
    "Give each task its own folder": "每題單獨一個目錄",
    "Keep the original folder structure (data/, graders/, ...)": "保留原來的目錄結構（data/、graders/ 等）",
    "Also export the sample data (down/)": "一併匯出樣例資料（down/）",
    "The sample data is taken from each task's down/ folder, keeping the structure.":
        "樣例資料取自每題的 down/ 目錄，並保留目錄結構。",
    "Test data of every task: the whole <b>data/</b> and <b>graders/</b> folders (plus <b>down/</b> when the sample data is included). The statement PDF is not packed.":
        "每題的測試資料：完整的 <b>data/</b> 與 <b>graders/</b> 目錄（勾選樣例資料時再加上 <b>down/</b>）。不含題面 PDF。",
    "Each task gets its own folder inside the package.": "每題在壓縮檔裡單獨一個目錄。",
    "Some tasks use the same data file names, so every task must keep its own folder.":
        "有試題的資料檔案重名，因此每題必須單獨一個目錄。",
    "Some tasks use the same data file names, so they cannot share one folder.":
        "有試題的資料檔案重名，不能放在同一個目錄裡。",
    "The sample data keeps the folder structure, so this cannot be turned off.":
        "樣例資料會保留目錄結構，因此不能關閉。",
    "Keeps data/, graders/ and down/ inside each task folder.":
        "每題目錄裡保留 data/、graders/、down/ 這幾層。",
    "Adds one more level: the .zip contains a <day>/ folder holding everything.":
        "多套一層：.zip 裡是一個 <day>/ 目錄，所有內容都在裡面。",
    "The outer .zip will hold nothing but the inner zip; all the content lives inside it.":
        "外層 .zip 裡只有內層壓縮檔，其它內容都在裡面。",
    "No %1 folder for task %2": "題目 %2 沒有 %1 目錄",
    "Duplicate file name skipped: %1": "檔案重名已跳過：%1",
    "%1: %2 folder(s), %3 file(s)": "%1：%2 個目錄，%3 個檔案",
    "Answers": "選手代碼",
    "No contestant in current contest": "目前比賽還沒有選手",
    "Give every region its own folder (named after the region)": "每個賽區單獨一個目錄（用賽區名）",
    "Contestants of one region go into a folder named after it.":
        "同一個賽區的選手放進以賽區命名的目錄。",
    "Also wrap everything in a folder named after the contest day file": "再套一層與比賽日同名的目錄",
    "Gives <day>/ and, when regions are enabled, <day>/<region>/ for every region.":
        "得到 <day>/；啟用賽區時每個賽區再佔一層：<day>/<region>/。",
    "Answers of every contestant: one folder per contestant with all of their source files. With regions enabled you can group them into region folders and/or one inner zip per region. The <day>/ wrapper is optional.":
        "每位選手一個目錄，裡面是他全部題目的原始碼。啟用賽區時可按賽區分組：賽區同名目錄、或每個賽區一個內層壓縮檔（可同時開）；<day>/ 那層可選。",
    "Give every region its own inner zip": "每個賽區單獨一個內層壓縮檔",
    "The outer answers.zip will also hold one <region>.zip per region.":
        "answers.zip 裡還會為每個賽區放一個 <region>.zip。",
    "Answers of every contestant: one folder per contestant with all of their source files. With regions enabled you can group them into region folders and/or one inner zip per region.":
        "每位選手一個目錄，裡面是他全部題目的原始碼。啟用賽區時可按賽區分組：賽區同名目錄、或每個賽區一個內層壓縮檔（可同時開）。",
}

TRANSLATIONS = {"zh_CN": ZH_CN, "zh_TW": ZH_TW}

# 已经作废的字符串（语义变了），从 .ts 里直接删掉
OBSOLETE = {
    "Pack each task's down files into down.zip",
    "Inside each task folder the down files are packed into down.zip instead.",
    "%1 file(s) from the down folder",
    "Pack everything into an inner zip named down.zip",
    "The outer .zip will hold nothing but down.zip; all the content lives inside it.",
    "Adds one more level: <day>.zip contains a <day>/ folder holding everything.",
    "down.zip: %1 folder(s), %2 file(s)",
    "Give every region its own folder (named after the region)",
    "Contestants of one region go into a folder named after it.",
    "Answers of every contestant: one folder per contestant with all of their source files. With regions enabled you can group them into region folders and/or one inner zip per region.",
}


def norm(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def escape_xml(text: str) -> str:
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def parse(text):
    """-> list of dict(name, start, end, header, messages=[(source, block, start, end)])"""
    contexts = []
    for match in CONTEXT_RE.finditer(text):
        body = match.group(2)
        name = NAME_RE.search(body)
        messages = []
        for msg in MESSAGE_RE.finditer(body):
            source = SOURCE_RE.search(msg.group(2))
            if source:
                messages.append(
                    (
                        norm(source.group(1)),
                        msg.group(0),
                        match.start(2) + msg.start(),
                        match.start(2) + msg.end(),
                    )
                )
        contexts.append(
            {
                "name": name.group(1).strip() if name else "",
                "start": match.start(),
                "end": match.end(),
                "header": match.group(1) + "<context>",
                "messages": messages,
            }
        )
    return contexts


def merge(language: str) -> str:
    real_path = REAL_DIR / f"{language}.ts"
    tmp_path = TMP_DIR / f"{language}.ts"
    real_raw = real_path.read_text(encoding="utf-8")
    tmp_raw = tmp_path.read_text(encoding="utf-8")

    eol = "\r\n" if "\r\n" in real_raw else "\n"
    real = real_raw.replace("\r\n", "\n")
    tmp = tmp_raw.replace("\r\n", "\n")

    real_contexts = parse(real)
    known = {(c["name"], src) for c in real_contexts for src, _, _, _ in c["messages"]}

    replacements = []  # (start, end, text) 覆盖插入点
    appended = []  # 新 context 整块

    # 先记下要删掉的作废条目（含其后的换行）。
    # 注意：不能把它们从 known 里去掉 —— lupdate 的副本里还留着这些 obsolete 条目，
    # 一旦「不认识」了就会又被插回来，等于白删。
    for context in real_contexts:
        for src, _block, start, end in context["messages"]:
            if html.unescape(src) in OBSOLETE:
                trailing = end + 1 if real[end: end + 1] == "\n" else end
                replacements.append((start, trailing, ""))

    for context in parse(tmp):
        missing = [
            (src, block, start, end)
            for src, block, start, end in context["messages"]
            if (context["name"], src) not in known
        ]
        if not missing:
            continue

        table = TRANSLATIONS.get(language, {})
        blocks = []
        for src, block, _start, _end in missing:
            key = html.unescape(src)
            if key in table:
                block = TRANS_RE.sub("<translation>%s</translation>" % escape_xml(table[key]), block, count=1)
            else:
                # en_US 的现有条目都是空译文，去掉 lupdate 加的 unfinished 标记保持一致
                block = block.replace('<translation type="unfinished"></translation>', "<translation></translation>")
            blocks.append(block)

        text = "\n".join(blocks)
        target = next((c for c in real_contexts if c["name"] == context["name"]), None)

        if target is None:
            # 整个 context 都是新的：把 tmp 里那块原样搬过来，只补上译文。
            context_text = tmp[context["start"]: context["end"]]
            for src, block, _start, _end in missing:
                fixed = block
                key = html.unescape(src)
                if key in table:
                    fixed = TRANS_RE.sub(
                        "<translation>%s</translation>" % escape_xml(table[key]), fixed, count=1
                    )
                else:
                    fixed = block.replace(
                        '<translation type="unfinished"></translation>', "<translation></translation>"
                    )
                context_text = context_text.replace(block, fixed, 1)
            appended.append(context_text)
            continue

        # 插到这个 context 的 </context> 之前
        close = real.rindex("</context>", target["start"], target["end"])
        replacements.append((close, close, text + "\n"))

    for start, end, text in sorted(replacements, key=lambda item: item[0], reverse=True):
        real = real[:start] + text + real[end:]

    if appended:
        last_close = real.rindex("</TS>")
        real = real[:last_close] + "\n".join(appended) + "\n" + real[last_close:]

    out = real.replace("\n", eol)
    real_path.write_text(out, encoding="utf-8")
    return f"{language}: inserted {sum(1 for _ in replacements)} context(s) + {len(appended)} new context(s)"


for lang in ("en_US", "zh_CN", "zh_TW"):
    print(merge(lang))
