#!/usr/bin/env python3
"""构建 dict.tsv（译语输入法词典）
用法: python tools/build_dict.py [语言]
  en  (默认) = CC-CEDICT 全量汉英（10 万+ 条）
  th/hu/...  = packs/{lang}.json 词汇包语言（需先 build_pack.py 生成）
"""
import json, os, re, sys, unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
lang = sys.argv[1] if len(sys.argv) > 1 else "en"


def pinyin_key(py):
    s = unicodedata.normalize("NFD", py)
    s = "".join(c for c in s if not unicodedata.combining(c))
    s = s.replace("ü", "v").replace(" ", "").lower()
    return re.sub(r"[^a-zv]", "", s)


lines = []
if lang == "en":
    src = os.path.join(ROOT, "data", "raw", "cedict.txt")
    if not os.path.exists(src):
        sys.exit("缺少 data/raw/cedict.txt（CC-CEDICT，见 README 下载地址）")
    for line in open(src, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = re.match(r"^(\S+)\s+(\S+)\s+\[([^\]]+)\]\s+/(.*)/$", line)
        if not m:
            continue
        hans, py, en = m.group(2), m.group(3), m.group(4)
        key = "".join(re.sub(r"[0-5]", "", s) for s in py.split())
        key = re.sub(r"[^a-züv]", "", key.lower()).replace("ü", "v")
        if not key:
            continue
        gloss = [s.strip() for s in en.split("/") if s.strip()]
        if not gloss:
            continue
        g = re.sub(r"\([^)]*\)", "", gloss[0]).strip()
        g = re.sub(r"^to\s+", "", g).split(";")[0].strip()
        if not g or len(g) > 30:
            continue
        lines.append(f"{key}\t{g}\t{hans}")
else:
    pack = json.load(open(os.path.join(ROOT, "packs", f"{lang}.json"), encoding="utf-8"))
    for w in pack["words"]:
        lines.append(f"{w['key']}\t{w['word']}\t{w['hans']}")

out = os.path.join(ROOT, "ime", "dict.tsv")
with open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
print(f"dict.tsv: {len(lines)} entries [{lang}]")
