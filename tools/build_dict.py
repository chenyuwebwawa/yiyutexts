#!/usr/bin/env python3
"""构建 dict.tsv（译语输入法词典）
用法: python tools/build_dict.py [语言]
  en  (默认) = CC-CEDICT 全量汉英（10 万+ 条）
  th/hu/...  = packs/{lang}.json 词汇包语言（需先 build_pack.py 生成）
列格式: 拼音键 \t 外语词 \t 中文 \t 语料词频
首行: lang:<语言名>（设置页显示当前语言用）
"""
import json, os, re, sys, unicodedata

sys.stdout.reconfigure(encoding="utf-8")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
lang = sys.argv[1] if len(sys.argv) > 1 else "en"
TAB = chr(9)


def load_freq():
    """jieba 词频表：词 → 语料频率（常用字排序依据）"""
    p = os.path.join(ROOT, "data", "raw", "jieba_dict.txt")
    freq = {}
    if not os.path.exists(p):
        print("警告：缺少 jieba 词频表，候选将按词典原序")
        return freq
    for line in open(p, encoding="utf-8"):
        parts = line.split()
        if len(parts) >= 2:
            try:
                freq[parts[0]] = int(parts[1])
            except ValueError:
                pass
    return freq


def pinyin_key(py):
    s = unicodedata.normalize("NFD", py)
    s = "".join(c for c in s if not unicodedata.combining(c))
    s = s.replace("ü", "v").replace(" ", "").lower()
    return re.sub(r"[^a-zv]", "", s)


FREQ = load_freq()
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
        lines.append(TAB.join([key, g, hans, str(FREQ.get(hans, 0))]))
else:
    pack = json.load(open(os.path.join(ROOT, "packs", f"{lang}.json"), encoding="utf-8"))
    for w in pack["words"]:
        lines.append(TAB.join([w["key"], w["word"], w["hans"], str(FREQ.get(w["hans"], 0))]))

name = "英语" if lang == "en" else json.load(
    open(os.path.join(ROOT, "packs", f"{lang}.json"), encoding="utf-8"))["langName"]
out = os.path.join(ROOT, "ime", "dict.tsv")
with open(out, "w", encoding="utf-8", newline="\n") as f:
    f.write(f"lang:{name}\n" + "\n".join(lines))
print(f"dict.tsv: {len(lines)} entries [{lang} -> {name}]")
