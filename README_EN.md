# YuyinIme

> Type to learn words — type pinyin, commit foreign words.

[中文](README.md) | **English**

A lightweight system-level IME that **actually types Chinese**: type pinyin, get Chinese candidates with foreign glosses side by side. Space commits the Chinese word (like any IME); number keys commit the foreign word — that's the learning moment.

The Windows core is a **single 260KB executable** — no registration, no admin rights, no dependencies. Launch and type.

## ✨ Features

- **System-level**: works everywhere — browsers, WeChat, Notepad, Office
- **Learn by typing**: `nihao` → `hello`, `weiji` → `crisis`; every commit is a word learned
- **Full dictionary**: 104k English entries (CC-CEDICT), fully offline
- **Smart prediction**: after committing a word, the next word is predicted (local bigram model)
- **Frequency learning**: words you commit float to the top of candidates over time (all data local)
- **Caret positioning**: the candidate window sits right under the text caret, avoids screen edges, adapts to dark/light mode
- **Mixed input**: unmatched pinyin commits as-is — no mode switching between Chinese and English
- **29 languages**: English is fully built in; other languages via downloadable word packs (Thai, Hungarian, Turkish, Swahili…)

## ⌨️ Keys

| Key | Action |
| --- | --- |
| letters | compose pinyin; Chinese candidates with foreign glosses appear |
| `Space` / `Enter` | **commit the Chinese** word (normal IME behavior), then next-word prediction |
| `1`–`8` | **commit the foreign word** (the learning action) |
| `<` `>` / `↑` `↓` | page / move highlight |
| `Esc` | cancel composition |
| `Backspace` | delete letter (falls through to the system when empty) |
| `Ctrl+Space` | master toggle (pause / resume the whole IME) |
| `Ctrl+Alt+Q` | quit |

Typical flow: type `nihao` → candidates show 你好 hello → Space commits 你好 for real typing → press `1` whenever you want the foreign word `hello`. The prediction panel keeps the sentence flowing.

## 📦 Install

### Windows (one-click)

1. Download `YuyinIme-Setup.exe` from [Releases](../../releases)
2. Double-click → Next → Done (auto-starts, registers autostart, desktop shortcut)
3. Any input field: type `nihao` → Space commits 你好, press `1` to commit `hello` — Chinese and foreign both in your control

**Upgrade**: just run the new `YuyinIme-Setup.exe` — the installer stops the old process, replaces files, **keeps your learning data** (frequency and association memory carry over), and restarts the IME. No uninstall needed.

**Uninstall** (any of three; all clean up completely):
- Start Menu → 译语输入法 → 卸载译语输入法
- Windows Settings → Apps → Installed apps → YuyinIme
- Run `%LOCALAPPDATA%\YuyinIme\Uninstall.exe`

### macOS

1. Download `YuyinIme-macOS.dmg`, drag YuyinIme.app into Applications
2. First launch: enable YuyinIme under System Settings → Privacy & Security → Input Monitoring (the installer opens this pane for you)
3. A 「译」 icon in the menu bar means it's running

**Uninstall**:
```bash
./mac-setup.sh uninstall
```

### Build from source

```bash
git clone https://github.com/chenyuwebwawa/yiyutexts.git
cd yiyutexts

# Windows (requires VS2022 Build Tools, C++ workload)
python tools/compile.py

# macOS (requires Xcode Command Line Tools)
./mac-setup.sh
```

Installers (Windows NSIS / macOS dmg) are built in the cloud by GitHub Actions — push a `v*` tag.

## 🌍 Languages

```bash
python tools/build_dict.py en      # English full (default, 104,834 entries)
python tools/build_pack.py all     # generate all 29 language packs (resumable)
python tools/build_dict.py th      # build dict.tsv from the Thai pack
```

**GUI switching (recommended)**: right-click the tray icon → "切换语言..." (Switch language) — double-click any installed pack to switch instantly. Installers ship with the packs (`packs/` folder), fully offline.

Command line (for custom dictionaries): place the generated `dict.tsv` next to the IME binary and restart. Packs are generated with the MyMemory translation API and cached in `packs/_cache/` — if the daily quota runs out, rerun the same command the next day to resume.

Supported: Thai, Vietnamese, Burmese, Lao, Khmer, Malay, Indonesian, Hindi, Urdu, Bengali, Nepali, Sinhala, Hungarian, Finnish, Turkish, Greek, Polish, Czech, Romanian, Serbian, Bulgarian, Ukrainian, Albanian, Persian, Hebrew, Swahili, Hausa, Mongolian, Filipino.

Dictionary format (TSV): `pinyin-key \t foreign-word \t chinese`

## 🏗️ Architecture

```
keys → global interception (WH_KEYBOARD_LL / CGEventTap) → pinyin buffer
     → dictionary lookup (in-memory hash, frequency-weighted ranking)
     → after commit: bigram prediction of the next word
     → candidate panel (caret-anchored, layered window, dark/light adaptive)
     → number key → synthesized keystrokes commit the word
```

- Single process, no services, no registry (except one optional autostart entry)
- Learning data (`freq.tsv` / `bigram.tsv`) stays 100% on your machine
- Windows and macOS share the same dictionary file and the same interaction model

## 🗺️ Roadmap

- [ ] Grow word packs to HSK 1-6 (5000 words per language)
- [ ] Sentence-level input (whole-sentence pinyin → whole-sentence translation)
- [ ] Cloud dictionary sync (end-to-end encrypted)
- [ ] Vocabulary review mode (spaced repetition)

## 📄 License

MIT (dictionary data under its own license, see below)

- English glosses based on [CC-CEDICT](https://www.mdbg.net/chinese/dictionary?page=cc-cedict) (CC-BY-SA 4.0)
