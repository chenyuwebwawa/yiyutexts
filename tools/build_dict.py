#!/usr/bin/env python3
"""构建 dict.tsv（译语输入法词典）v3
词库主体 = jieba 中文词库（35 万词带语料词频，pypinyin 自动注音）—— 覆盖所有常用中文，与有无英文无关
英文释义 = CC-CEDICT 按词条左连接（有则显示，没有也不影响中文候选）
用法: python tools/build_dict.py [语言]
  en  (默认) = 英文释义
  th/hu/...  = packs/{lang}.json 词汇包语言
列格式: 拼音键 \t 外语词(可为空) \t 中文 \t 语料词频
首行: lang:<语言名>
"""
import json, os, re, sys, unicodedata

sys.stdout.reconfigure(encoding="utf-8")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
lang = sys.argv[1] if len(sys.argv) > 1 else "en"
TAB = chr(9)


def load_freq():
    p = os.path.join(ROOT, "data", "raw", "jieba_dict.txt")
    freq = {}
    if not os.path.exists(p):
        sys.exit("缺少 data/raw/jieba_dict.txt（jieba 词库）")
    for line in open(p, encoding="utf-8"):
        parts = line.split()
        if len(parts) >= 2:
            try:
                freq[parts[0]] = int(parts[1])
            except ValueError:
                pass
    return freq


def norm_key(py):
    """带调拼音 → 无调键：去声调符号、ü→v、小写、仅字母"""
    s = unicodedata.normalize("NFD", py)
    s = "".join(c for c in s if not unicodedata.combining(c))
    s = s.replace("ü", "v").replace(":", "").replace(" ", "").lower()
    return re.sub(r"[^a-zv]", "", s)


def clean_en(en):
    s = re.sub(r"\([^)]*\)", " ", en).strip()
    s = re.sub(r"^to\s+", "", s, flags=re.I)
    s = s.split(";")[0].strip()
    return s


FREQ = load_freq()
print(f"jieba 词库: {len(FREQ)} 词")

# ---- CC-CEDICT：中文 → (干净英文释义, 拼音键) ----
gloss_map = {}    # hans -> en
cedict_key = {}   # hans -> pinyin key（权威读音，优先于 pypinyin）
src = os.path.join(ROOT, "data", "raw", "cedict.txt")
if os.path.exists(src):
    for line in open(src, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = re.match(r"^(\S+)\s+(\S+)\s+\[([^\]]+)\]\s+/(.*)/$", line)
        if not m:
            continue
        hans, py, en = m.group(2), m.group(3), m.group(4)
        if hans not in cedict_key:
            cedict_key[hans] = norm_key(py)
        if hans in gloss_map:
            continue
        senses = [x.strip() for x in en.split("/") if x.strip()]
        if not senses:
            continue
        g = clean_en(senses[0])
        if g and len(g) <= 30:
            gloss_map[hans] = g
print(f"CEDICT 英文释义: {len(gloss_map)} 条")

# ---- 外语词汇包（th/hu/...）----
pack_map = {}
if lang != "en":
    pp = os.path.join(ROOT, "packs", f"{lang}.json")
    if os.path.exists(pp):
        pack_map = {w["hans"]: w["word"] for w in
                    json.load(open(pp, encoding="utf-8"))["words"]}

# ---- 组装：中文词库为主体 ----
from pypinyin import pinyin as _py, Style

def py_key(hans):
    if hans in cedict_key and cedict_key[hans]:
        return cedict_key[hans]
    try:
        parts = _py(hans, style=Style.TONE3, neutral_tone_with_five=True)
        return norm_key("".join(x[0] for x in parts))
    except Exception:
        return ""

rows = {}   # (key,hans) -> [word, freq]   词库去重：同键同词保留高词频
for word, f in FREQ.items():
    key = py_key(word)
    if not key or key == word and re.match(r"^[a-z]+$", word):
        continue   # 纯字母的“词”（AT&T、c++ 等）不进拼音词库
    word_out = pack_map.get(word) or gloss_map.get(word, "")
    k2 = (key, word)
    if k2 not in rows or rows[k2][1] < f:
        rows[k2] = [word_out, f]

# CEDICT 独有词条（jieba 未收）补入，词频 0
for hans, g in gloss_map.items():
    if hans in FREQ:
        continue
    key = py_key(hans)
    if not key:
        continue
    word_out = pack_map.get(hans) or g
    k2 = (key, hans)
    if k2 not in rows:
        rows[k2] = [word_out, 0]

out = os.path.join(ROOT, "ime", "dict.tsv")
name = "英语" if lang == "en" else json.load(
    open(os.path.join(ROOT, "packs", f"{lang}.json"), encoding="utf-8"))["langName"]
with open(out, "w", encoding="utf-8", newline="\n") as fout:
    fout.write(f"lang:{name}\n")
    for (key, hans), (word_out, fr) in rows.items():
        fout.write(TAB.join([key, word_out, hans, str(fr)]) + "\n")
print(f"dict.tsv: {len(rows)} entries [{lang} -> {name}]")
