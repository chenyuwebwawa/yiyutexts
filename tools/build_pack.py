#!/usr/bin/env python3
"""词汇包生成：HSK1-2 核心词 → 目标语言（MyMemory 免费接口，断点续跑）
用法:
  python tools/build_pack.py th          # 生成泰语包
  python tools/build_pack.py th,hu,tr    # 批量
  python tools/build_pack.py all         # 全部 29 种语言
配额说明: MyMemory 有每日限额；没跑完明天重跑同一命令即自动续传（缓存 packs/_cache/）。
"""
import json, os, re, sys, time, unicodedata, urllib.request, urllib.parse
sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

LANG_CODES = {
    'hu': 'hu', 'fi': 'fi', 'tr': 'tr', 'el': 'el', 'pl': 'pl', 'cs': 'cs',
    'ro': 'ro', 'sr': 'sr-Cyrl', 'bg': 'bg', 'uk': 'uk', 'sq': 'sq', 'vi': 'vi',
    'th': 'th', 'ms': 'ms', 'id': 'id', 'hi': 'hi', 'ur': 'ur', 'bn': 'bn',
    'ne': 'ne', 'si': 'si', 'fa': 'fa', 'he': 'he', 'sw': 'sw', 'ha': 'ha',
    'mn': 'mn', 'fil': 'tl-PH', 'lo': 'lo', 'km': 'km', 'my': 'my',
}
NAMES = {
    'hu': ('匈牙利语', '🇭🇺'), 'fi': ('芬兰语', '🇫🇮'), 'tr': ('土耳其语', '🇹🇷'),
    'el': ('希腊语', '🇬🇷'), 'pl': ('波兰语', '🇵🇱'), 'cs': ('捷克语', '🇨🇿'),
    'ro': ('罗马尼亚语', '🇷🇴'), 'sr': ('塞尔维亚语', '🇷🇸'), 'bg': ('保加利亚语', '🇧🇬'),
    'uk': ('乌克兰语', '🇺🇦'), 'sq': ('阿尔巴尼亚语', '🇦🇱'), 'vi': ('越南语', '🇻🇳'),
    'th': ('泰语', '🇹🇭'), 'ms': ('马来语', '🇲🇾'), 'id': ('印尼语', '🇮🇩'),
    'hi': ('印地语', '🇮🇳'), 'ur': ('乌尔都语', '🇵🇰'), 'bn': ('孟加拉语', '🇧🇩'),
    'ne': ('尼泊尔语', '🇳🇵'), 'si': ('僧伽罗语', '🇱🇰'), 'fa': ('波斯语', '🇮🇷'),
    'he': ('希伯来语', '🇮🇱'), 'sw': ('斯瓦希里语', '🇰🇪'), 'ha': ('豪萨语', '🇳🇬'),
    'mn': ('蒙古语', '🇲🇳'), 'fil': ('菲律宾语', '🇵🇭'), 'lo': ('老挝语', '🇱🇦'),
    'km': ('柬埔寨语', '🇰🇭'), 'my': ('缅甸语', '🇲🇲'),
}

HSK_URL = 'https://raw.githubusercontent.com/LiudmilaLV/json_hsk/master/hsk.json'


def ensure_hsk():
    path = os.path.join(ROOT, 'data', 'raw', 'hsk.json')
    if os.path.exists(path):
        return json.load(open(path, encoding='utf-8'))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    print('下载 HSK 词表...')
    urllib.request.urlretrieve(HSK_URL, path)
    return json.load(open(path, encoding='utf-8'))


def pinyin_key(py):
    s = unicodedata.normalize('NFD', py)
    s = ''.join(c for c in s if not unicodedata.combining(c))
    s = s.replace('ü', 'v').replace(' ', '').lower()
    return re.sub(r'[^a-zv]', '', s)


def clean_en(en):
    s = re.sub(r'\([^)]*\)', ' ', en).strip()
    s = re.sub(r'^to\s+', '', s, flags=re.I)
    return s or en


def junk(t, src):
    if not t:
        return True
    if re.search(r' in the |http|message|below|above|translation|see |said of', t, re.I):
        return True
    return len(t) > len(src) * 4 + 12


def mt(en, target):
    q = urllib.parse.quote(en)
    url = (f'https://api.mymemory.translated.net/get?q={q}'
           f'&langpair=en|{target}&de=yuyinime@outlook.com')
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=10) as r:
                d = json.loads(r.read().decode())
            t = (d.get('responseData') or {}).get('translatedText')
            if t and 'MYMEMORY WARNING' not in t.upper():
                return None if junk(t, en) else t
            if 'LIMIT' in (t or '').upper():
                raise RuntimeError('QUOTA')
            return None
        except RuntimeError:
            raise
        except Exception:
            time.sleep(1 + attempt)
    return None


def main():
    arg = sys.argv[1] if len(sys.argv) > 1 else 'th'
    langs = list(LANG_CODES) if arg == 'all' else [x.strip() for x in arg.split(',') if x.strip()]

    hsk = ensure_hsk()
    base = []
    seen = set()
    for w in hsk:
        if w.get('level', 9) > 2 or not w.get('translations', {}).get('eng'):
            continue
        en = clean_en(w['translations']['eng'][0])
        key = pinyin_key(w['pinyin'])
        if not en or not key or en in seen:
            continue
        seen.add(en)
        base.append({'key': key, 'hans': w['hanzi'], 'py': w['pinyin'], 'en': en})
    print(f'基础词: {len(base)} (HSK 1-2)')

    packs = os.path.join(ROOT, 'packs')
    cache_d = os.path.join(packs, '_cache')
    os.makedirs(cache_d, exist_ok=True)

    for lang in langs:
        target = LANG_CODES.get(lang)
        if not target:
            print(f'未知语言: {lang}'); continue
        cfile = os.path.join(cache_d, f'{lang}.json')
        cache = json.load(open(cfile, encoding='utf-8')) if os.path.exists(cfile) else {}
        todo = [w for w in base if w['en'] not in cache]
        print(f'[{lang}] 缓存 {len(cache)}/{len(base)}，待翻译 {len(todo)}')
        quota = False
        for i, w in enumerate(todo):
            try:
                t = mt(w['en'], target)
            except RuntimeError:
                print(f'[{lang}] 今日配额已用完 — 缓存已保留，明天重跑同一命令自动续传')
                quota = True
                break
            cache[w['en']] = t            # None 也写缓存避免反复重试坏词
            if i % 25 == 0:
                json.dump(cache, open(cfile, 'w', encoding='utf-8'), ensure_ascii=False)
            time.sleep(0.15)
        json.dump(cache, open(cfile, 'w', encoding='utf-8'), ensure_ascii=False)

        words = [dict(w, word=cache[w['en']]) for w in base if cache.get(w['en'])]
        name, flag = NAMES[lang]
        json.dump({'format': 1, 'lang': lang, 'langName': name, 'flag': flag,
                   'level': 'HSK 1-2', 'count': len(words), 'words': words},
                  open(os.path.join(packs, f'{lang}.json'), 'w', encoding='utf-8'),
                  ensure_ascii=False)
        # 同步生成输入法可直接使用的 tsv（lang: 头 + key/word/hans）
        with open(os.path.join(packs, f'{lang}.tsv'), 'w', encoding='utf-8') as f:
            f.write(f'lang:{name}\n')
            for w in words:
                f.write(f"{w['key']}\t{w['word']}\t{w['hans']}\n")
        print(f'[{lang}] packs/{lang}.json + {lang}.tsv: {len(words)} 词'
              + ('（配额中断，可续传）' if quota else ''))
        if quota:
            break
    print('done')


if __name__ == '__main__':
    main()
