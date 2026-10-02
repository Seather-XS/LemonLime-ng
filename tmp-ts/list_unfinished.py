"""列出 .ts 里还没译完的条目。"""

import pathlib
import re

REPO = pathlib.Path(r"d:\gengen-tuack")

for language in ("zh_CN", "zh_TW", "en_US"):
    text = (REPO / "translations" / f"{language}.ts").read_text(encoding="utf-8")
    print(f"=== {language} ===")
    for match in re.finditer(r"<message>(.*?)</message>", text, re.S):
        block = match.group(1)
        if 'type="unfinished"' in block or 'type="vanished"' in block:
            source = re.search(r"<source>(.*?)</source>", block, re.S)
            print("  ", re.sub(r"\s+", " ", source.group(1))[:110] if source else "?")
