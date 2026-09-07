// 译语输入法 YuyinIme — 轻量系统级输入法（单文件，无需注册，任何应用可用）
//
// 开启时拦截所有字母键组拼音（从第一个字母起），数字/空格/回车选词上屏外语词。
// 候选窗定位到文本光标下方（无系统光标的应用回退鼠标位置），自动避让屏幕边缘。
// 智能联想：本地学习用户词频与二元共现（freq.tsv / bigram.tsv），上屏后自动预测下一个词。
// Ctrl+Space 开关；关闭时所有按键 100% 透传。Ctrl+Alt+Q 退出。
// 词典：exe 同目录 dict.tsv（key \t word \t comment）。
//
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>

static HINSTANCE g_hInst;
static HHOOK g_hHook;
static HWND g_hCand;
static HFONT g_hFont, g_hFontSmall;
static NOTIFYICONDATAW g_nid;
static UINT WMAPP_TRAY;

static bool g_enabled = true;
static bool g_armed = false;   // 外语组字模式（按 ` 进入，Esc/上屏完退出）；未激活时完全透传
static std::wstring g_toast;   // 开关提示（闪现）

static void LayoutCand();   // forward
static void Reset();
static void ShowToast(const wchar_t* text);

struct Entry { std::wstring word, comment; };
static std::unordered_map<std::wstring, std::vector<Entry>> g_dict;
static std::wstring g_currentLang = L"英语";

// 语言包设置页
static HWND g_hSettings = nullptr, g_hList = nullptr, g_hCur = nullptr;
static std::vector<std::pair<std::wstring, std::wstring>> g_packList;   // (路径, 语言名)

struct State {
  std::wstring comp;
  std::vector<Entry> cands;
  int page = 0, pageCount = 1, active = 0;
  bool assoc = false;          // 联想模式：显示预测的下一个词
} g_st;

// ---------------- 用户学习：词频 + 二元联想 ----------------
static std::unordered_map<std::wstring, int> g_freq;      // 词 → 上屏次数
static std::unordered_map<std::wstring, std::vector<std::pair<std::wstring, int>>> g_bigram; // 前词 → [后词, 次数]
static std::wstring g_last;                                // 上一个上屏的词
static bool g_userDirty = false;

static std::wstring Utf8ToWide(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
  if (!w.empty()) w.pop_back();
  return w;
}
static std::string WideToUtf8(const std::wstring& w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
  if (!s.empty()) s.pop_back();
  return s;
}

static std::wstring ExeDir() {
  wchar_t path[MAX_PATH]; GetModuleFileNameW(g_hInst, path, MAX_PATH);
  std::wstring d = path; size_t p = d.find_last_of(L'\\');
  return p == std::wstring::npos ? L"." : d.substr(0, p);
}

static void LoadUser() {
  std::wstring dir = ExeDir();
  std::ifstream f(dir + L"\\freq.tsv");
  if (f) {
    std::string l;
    while (std::getline(f, l)) {
      std::istringstream ss(l); std::string w; int c = 0;
      if (std::getline(ss, w, '\t')) { ss >> c; if (c > 0) g_freq[Utf8ToWide(w)] = c; }
    }
  }
  std::ifstream b(dir + L"\\bigram.tsv");
  if (b) {
    std::string l;
    while (std::getline(b, l)) {
      std::istringstream ss(l); std::string a, w; int c = 0;
      if (!std::getline(ss, a, '\t')) continue;
      if (!std::getline(ss, w, '\t')) continue;
      ss >> c;
      if (c > 0) g_bigram[Utf8ToWide(a)].push_back({ Utf8ToWide(w), c });
    }
  }
}

static void SaveUser() {
  std::wstring dir = ExeDir();
  std::ofstream f(dir + L"\\freq.tsv");
  for (auto& kv : g_freq) f << WideToUtf8(kv.first) << "\t" << kv.second << "\n";
  std::ofstream b(dir + L"\\bigram.tsv");
  for (auto& kv : g_bigram)
    for (auto& pr : kv.second)
      b << WideToUtf8(kv.first) << "\t" << WideToUtf8(pr.first) << "\t" << pr.second << "\n";
  g_userDirty = false;
}

static int FreqOf(const std::wstring& w) {
  auto it = g_freq.find(w);
  return it == g_freq.end() ? 0 : it->second;
}

// ---------------- 词典 ----------------
static void LoadDict() {
  std::wstring dir = ExeDir();
  g_dict.clear();
  std::ifstream f(dir + L"\\dict.tsv");
  if (!f) return;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    if (line.rfind("lang:", 0) == 0) { g_currentLang = Utf8ToWide(line.substr(5)); continue; }
    std::istringstream ss(line);
    std::string key, word, comment;
    if (!std::getline(ss, key, '\t')) continue;
    if (!std::getline(ss, word, '\t')) continue;
    std::getline(ss, comment, '\t');
    g_dict[Utf8ToWide(key)].push_back({ Utf8ToWide(word), Utf8ToWide(comment) });
  }
}

static void QueryCandidates() {
  g_st.cands.clear();
  if (g_st.comp.empty()) { g_st.pageCount = 1; g_st.page = 0; return; }
  auto it = g_dict.find(g_st.comp);
  if (it != g_dict.end()) g_st.cands = it->second;
  for (auto& kv : g_dict) {
    if (kv.first.size() > g_st.comp.size() &&
        kv.first.compare(0, g_st.comp.size(), g_st.comp) == 0) {
      for (auto& e : kv.second) g_st.cands.push_back(e);
      if (g_st.cands.size() > 80) break;
    }
  }
  // 词频动态调整：用户高频上屏的词排前面（稳定排序保持词典原顺序为次序）
  std::stable_sort(g_st.cands.begin(), g_st.cands.end(),
                   [](const Entry& a, const Entry& b) { return FreqOf(a.word) > FreqOf(b.word); });
  if (g_st.cands.size() > 80) g_st.cands.resize(80);
  g_st.pageCount = (int)((g_st.cands.size() + 7) / 8);
  if (g_st.pageCount < 1) g_st.pageCount = 1;
  if (g_st.page >= g_st.pageCount) g_st.page = g_st.pageCount - 1;
}

// 上屏后：按二元共现给出下一个词的联想候选
static void SetupAssoc() {
  g_st.assoc = true;
  g_st.cands.clear();
  auto it = g_bigram.find(g_last);
  if (it != g_bigram.end()) {
    std::vector<std::pair<std::wstring, int>> v = it->second;
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
    for (auto& pr : v) g_st.cands.push_back({ pr.first, L"联想" });
  }
  g_st.page = 0; g_st.active = 0;
  g_st.pageCount = (int)((g_st.cands.size() + 7) / 8);
  if (g_st.pageCount < 1) g_st.pageCount = 1;
  LayoutCand();
}

// ---------------- 设置页：语言切换 ----------------
static void ScanPacks() {
  g_packList.clear();
  std::wstring dir = ExeDir() + L"\\packs";
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW((dir + L"\\*.tsv").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    std::wstring file = dir + L"\\" + fd.cFileName;
    std::ifstream f(file);
    std::string line;
    std::wstring name = fd.cFileName;
    if (std::getline(f, line) && line.rfind("lang:", 0) == 0)
      name = Utf8ToWide(line.substr(5));
    g_packList.push_back({ file, name });
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  std::sort(g_packList.begin(), g_packList.end(),
            [](auto& a, auto& b) { return a.second < b.second; });
}

static void ApplyLang(int idx) {
  if (idx < 0 || idx >= (int)g_packList.size()) return;
  std::ifstream in(g_packList[idx].first);
  std::ofstream out(ExeDir() + L"\\dict.tsv");
  out << in.rdbuf();
  LoadDict();
  Reset();
  wchar_t tip[128];
  swprintf(tip, 128, L"已切换到 %s", g_currentLang.c_str());
  ShowToast(tip);
}

static LRESULT CALLBACK SettingsProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_CREATE) {
    CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE,
                    14, 12, 330, 22, h, (HMENU)1, g_hInst, nullptr);
    g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS,
                    14, 40, 330, 300, h, (HMENU)2, g_hInst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"应用", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                    14, 352, 100, 32, h, (HMENU)3, g_hInst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"刷新", WS_CHILD | WS_VISIBLE,
                    126, 352, 100, 32, h, (HMENU)4, g_hInst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE,
                    244, 352, 100, 32, h, (HMENU)5, g_hInst, nullptr);
    EnumChildWindows(h, [](HWND c, LPARAM font) -> BOOL {
      SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE); return TRUE;
    }, (LPARAM)g_hFont);
    return 0;
  }
  if (m == WM_COMMAND) {
    int id = LOWORD(w), code = HIWORD(w);
    if (id == 5 || (id == 2 && code == LBN_KILLFOCUS)) { DestroyWindow(h); return 0; }
    if ((id == 3 && code == BN_CLICKED) || (id == 2 && code == LBN_DBLCLK)) {
      int sel = (int)SendMessageW(g_hList, LB_GETCURSEL, 0, 0);
      ApplyLang(sel);
      SendMessageW(g_hCur, WM_SETTEXT, 0, (LPARAM)((std::wstring(L"当前语言：") + g_currentLang).c_str()));
      // 刷新列表（当前项标记）
      int n = (int)SendMessageW(g_hList, LB_GETCOUNT, 0, 0);
      for (int i = 0; i < n; i++) {
        wchar_t txt[96]; SendMessageW(g_hList, LB_GETTEXT, i, (LPARAM)txt);
        std::wstring t = txt;
        size_t p = t.find(L"（当前）");
        if (p != std::wstring::npos) t = t.substr(0, p);
        if (i == sel) t += L"（当前）";
        SendMessageW(g_hList, LB_DELETESTRING, i, 0);
        SendMessageW(g_hList, LB_INSERTSTRING, i, (LPARAM)t.c_str());
      }
      SendMessageW(g_hList, LB_SETCURSEL, sel, 0);
      return 0;
    }
    if (id == 4 && code == BN_CLICKED) {
      ScanPacks();
      SendMessageW(g_hList, LB_RESETCONTENT, 0, 0);
      for (auto& pk : g_packList) {
        std::wstring label = pk.second + (pk.second == g_currentLang ? L"（当前）" : L"");
        SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)label.c_str());
      }
      return 0;
    }
  }
  if (m == WM_CLOSE) { DestroyWindow(h); return 0; }
  if (m == WM_DESTROY) { g_hSettings = nullptr; return 0; }
  return DefWindowProcW(h, m, w, l);
}

static void OpenSettings() {
  if (g_hSettings) { SetForegroundWindow(g_hSettings); return; }
  WNDCLASSW wc = {};
  wc.lpfnWndProc = SettingsProc; wc.hInstance = g_hInst;
  wc.lpszClassName = L"YuyinSettings"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  RegisterClassW(&wc);
  ScanPacks();
  g_hSettings = CreateWindowExW(0, L"YuyinSettings",
      (std::wstring(L"译语输入法 — 目标语言（已内置 ") + std::to_wstring(g_packList.size()) + L" 个语言包）").c_str(),
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, 372, 440, nullptr, nullptr, g_hInst, nullptr);
  g_hCur = GetDlgItem(g_hSettings, 1);
  SendMessageW(g_hCur, WM_SETTEXT, 0, (LPARAM)((std::wstring(L"当前语言：") + g_currentLang + L"　词汇包目录：packs\\").c_str()));
  for (auto& pk : g_packList) {
    std::wstring label = pk.second + (pk.second == g_currentLang ? L"（当前）" : L"");
    SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)label.c_str());
  }
  ShowWindow(g_hSettings, SW_SHOW);
}

// ---------------- 主候选窗 ----------------
static bool IsDark() {
  DWORD v = 1, cb = 4;
  RegGetValueW(HKEY_CURRENT_USER,
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
    L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &cb);
  return v == 0;
}

// 定位：优先文本光标；无系统光标的应用锚定在触发那一刻的位置（组字期间不乱跑）
static POINT g_anchor;
static bool g_anchorSet = false;

static bool TryCaretPoint(POINT* out) {
  GUITHREADINFO gti; memset(&gti, 0, sizeof(gti)); gti.cbSize = sizeof(gti);
  if (GetGUIThreadInfo(0, &gti) && gti.hwndCaret) {
    RECT rc = gti.rcCaret;
    if (rc.bottom > rc.top) {
      POINT pt = { rc.left, rc.bottom };
      if (ClientToScreen(gti.hwndCaret, &pt)) { *out = pt; return true; }
    }
  }
  return false;
}

static POINT GetAnchorPoint() {
  POINT pt;
  if (TryCaretPoint(&pt)) { g_anchor = pt; g_anchorSet = true; return pt; }
  if (!g_anchorSet) { GetCursorPos(&pt); g_anchor = pt; g_anchorSet = true; }
  return g_anchor;
}

static void LayoutCand() {
  if (!g_hCand) return;
  if (!g_toast.empty()) {
    POINT pt; GetCursorPos(&pt);
    SetWindowPos(g_hCand, HWND_TOPMOST, pt.x, pt.y + 24, 320, 44, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_hCand, nullptr, TRUE);
    return;
  }
  if (!g_armed && g_st.comp.empty()) { ShowWindow(g_hCand, SW_HIDE); return; }
  int rows = (int)g_st.cands.size() - g_st.page * 8;
  if (rows > 8) rows = 8;
  if (rows < 0) rows = 0;
  int w = 340, h = (rows > 0 ? rows * 25 + 26 : 26) + 24;
  POINT pt = GetAnchorPoint();
  // 屏幕边缘避让
  HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi; mi.cbSize = sizeof(mi);
  if (GetMonitorInfoW(mon, &mi)) {
    if (pt.x + w > mi.rcWork.right) pt.x = mi.rcWork.right - w;
    if (pt.y + h > mi.rcWork.bottom) pt.y = pt.y - h - 22;   // 放到光标上方
    if (pt.y < mi.rcWork.top) pt.y = mi.rcWork.top;
    if (pt.x < mi.rcWork.left) pt.x = mi.rcWork.left;
  }
  SetWindowPos(g_hCand, HWND_TOPMOST, pt.x, pt.y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(g_hCand, nullptr, TRUE);
}

static void SendText(const std::wstring& w) {
  INPUT in[2] = {};
  in[0].type = INPUT_KEYBOARD; in[1].type = INPUT_KEYBOARD;
  in[0].ki.dwFlags = KEYEVENTF_UNICODE;
  in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
  for (wchar_t ch : w) {
    in[0].ki.wScan = ch; in[1].ki.wScan = ch;
    SendInput(2, in, sizeof(INPUT));
  }
}

static void Reset() {
  g_st.comp.clear(); g_st.cands.clear();
  g_st.page = 0; g_st.active = 0; g_st.assoc = false;
  g_armed = false;
  g_anchorSet = false;
  if (g_toast.empty()) ShowWindow(g_hCand, SW_HIDE);
}

// 上屏一个词：更新词频/二元学习 → 弹出下一词联想
static void Pick(int idx) {
  if (idx < 0 || idx >= (int)g_st.cands.size()) return;
  std::wstring word = g_st.cands[idx].word;
  g_st.comp.clear(); g_st.cands.clear();
  g_st.page = 0; g_st.active = 0;
  g_freq[word]++;
  if (!g_last.empty() && g_last != word) {
    auto& v = g_bigram[g_last];
    bool found = false;
    for (auto& pr : v) if (pr.first == word) { pr.second++; found = true; break; }
    if (!found) v.push_back({ word, 1 });
    if (v.size() > 30) {
      std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
      v.resize(30);
    }
  }
  g_last = word;
  g_userDirty = true;   // 钩子线程内不做文件 IO（会被 Windows 判超时移除钩子），由定时器落盘
  SendText(word);
  SetupAssoc();   // 联想下一个词
}

static void CommitRaw() {
  std::wstring raw = g_st.comp;
  Reset();
  g_last.clear();          // 原样上屏不参与联想学习
  SendText(raw);
}

static void ShowToast(const wchar_t* text) {
  g_toast = text;
  LayoutCand();
  SetTimer(g_hCand, 1, 900, nullptr);
}

static LRESULT CALLBACK CandProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_PAINT) {
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
    RECT rc; GetClientRect(h, &rc);
    bool dark = IsDark();
    COLORREF bgc = dark ? RGB(43,43,46)   : RGB(250,250,252);
    COLORREF fgc = dark ? RGB(240,240,245): RGB(30,30,32);
    COLORREF mut = dark ? RGB(150,150,158): RGB(130,130,136);
    COLORREF acc = dark ? RGB(10,132,255) : RGB(0,113,227);
    HBRUSH bg = CreateSolidBrush(bgc); FillRect(dc, &rc, bg); DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    if (!g_toast.empty()) {
      SelectObject(dc, g_hFont);
      SetTextColor(dc, g_enabled ? acc : mut);
      TextOutW(dc, 12, 13, g_toast.c_str(), (int)g_toast.size());
      EndPaint(h, &ps);
      return 0;
    }
    wchar_t pre[256];
    if (g_st.assoc) swprintf(pre, 256, L"[ 联想 ← %s ]  译语输入法", g_last.c_str());
    else if (g_st.comp.empty()) swprintf(pre, 256, L"[ 输入拼音 ]  译语输入法  Esc 退出");
    else swprintf(pre, 256, L"[ %s ]  译语输入法", g_st.comp.c_str());
    SelectObject(dc, g_hFontSmall); SetTextColor(dc, mut);
    TextOutW(dc, 8, 6, pre, (int)wcslen(pre));
    int y = 28, start = g_st.page * 8;
    SelectObject(dc, g_hFont);
    for (int i = 0; i < 8 && start + i < (int)g_st.cands.size(); i++) {
      Entry& e = g_st.cands[start + i];
      wchar_t line[512];
      swprintf(line, 512, L"%d  %s   %s", i + 1, e.comment.c_str(), e.word.c_str());
      if (i == g_st.active) {
        RECT hl = { 4, y - 3, rc.right - 4, y + 22 };
        HBRUSH hb = CreateSolidBrush(acc); FillRect(dc, &hl, hb); DeleteObject(hb);
        SetTextColor(dc, RGB(255,255,255));
      } else SetTextColor(dc, fgc);
      TextOutW(dc, 8, y, line, (int)wcslen(line));
      y += 25;
    }
    SelectObject(dc, g_hFontSmall); SetTextColor(dc, mut);
    wchar_t foot[64];
    swprintf(foot, 64, L"%d / %d   < > 翻页", g_st.page + 1, g_st.pageCount);
    TextOutW(dc, 8, rc.bottom - 18, foot, (int)wcslen(foot));
    EndPaint(h, &ps);
    return 0;
  }
  if (m == WM_ERASEBKGND) return 1;
  if (m == WM_TIMER) {
    if (w == 1) {                 // toast 隐藏
      KillTimer(h, 1);
      g_toast.clear();
      if (g_st.comp.empty()) ShowWindow(h, SW_HIDE);
    } else if (w == 2) {          // 学习数据落盘（避开钩子线程）
      if (g_userDirty) SaveUser();
    }
    return 0;
  }
  if (m == WMAPP_TRAY) {
    if (l == WM_RBUTTONUP || l == WM_LBUTTONDBLCLK) {
      HMENU menu = CreatePopupMenu();
      AppendMenuW(menu, MF_STRING, 1, g_enabled ? L"暂停输入 (Ctrl+Space)" : L"开始输入 (Ctrl+Space)");
      AppendMenuW(menu, MF_STRING, 3, L"切换语言...");
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(menu, MF_STRING, 2, L"退出 (Ctrl+Alt+Q)");
      POINT pt; GetCursorPos(&pt);
      SetForegroundWindow(h);
      int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, h, nullptr);
      DestroyMenu(menu);
      if (cmd == 3) { OpenSettings(); return 0; }
      if (cmd == 1) {
        g_enabled = !g_enabled;
        Reset();
        wcscpy(g_nid.szTip, g_enabled ? L"译语输入法 - 打字中 (Ctrl+Space 暂停)"
                                      : L"译语输入法 - 已暂停 (Ctrl+Space 开启)");
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        ShowToast(g_enabled ? L"译语输入法 已开启 — 直接打拼音" : L"译语输入法 已暂停 — 键盘恢复原样");
      }
      if (cmd == 2) { SaveUser(); PostQuitMessage(0); }
    }
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

// ---------------- 按键处理 ----------------
static bool HandleKey(DWORD vk, bool* eaten) {
  *eaten = false;
  if (!g_enabled) return false;
  if (vk == 0xE7) return false;          // VK_PACKET：我们自己上屏的事件，透传
  if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT) return false;

  bool isLetter = (vk >= 'A' && vk <= 'Z');
  bool hasComp = !g_st.comp.empty();
  bool assoc = g_st.assoc && !hasComp && !g_st.cands.empty();

  // 未按 ` 进入外语模式时：一个键都不拦，中文输入法完全正常
  if (!g_armed) return false;

  if (isLetter) {
    g_st.assoc = false;
    if (g_st.comp.size() < 24) g_st.comp += (wchar_t)tolower(vk);
    QueryCandidates(); LayoutCand();
    *eaten = true; return true;
  }

  if (assoc) {
    // 联想候选的选择（数字/空格/回车/翻页），Esc 退出联想
    switch (vk) {
    case VK_ESCAPE:
      Reset();   // Esc 退出外语模式，回到正常打字
      *eaten = true; return true;
    case VK_SPACE: case VK_RETURN:
      Pick(g_st.page * 8 + g_st.active); *eaten = true; return true;
    case VK_UP: case VK_DOWN: {
      if (vk == VK_DOWN) { g_st.active++; if (g_st.active > 7) { g_st.page = (g_st.page + 1) % g_st.pageCount; g_st.active = 0; } }
      else { g_st.active--; if (g_st.active < 0) { g_st.page = (g_st.page - 1 + g_st.pageCount) % g_st.pageCount; g_st.active = 7; } }
      LayoutCand(); *eaten = true; return true;
    }
    case 0xBC: case 0xBE: {
      int dir = (vk == 0xBC) ? -1 : 1;
      g_st.page = (g_st.page + dir + g_st.pageCount) % g_st.pageCount;
      g_st.active = 0; LayoutCand(); *eaten = true; return true;
    }
    default:
      if (vk >= '1' && vk <= '8') {
        int idx = g_st.page * 8 + (vk - '1');
        if (idx < (int)g_st.cands.size()) Pick(idx);
        *eaten = true; return true;
      }
      // 其他键：退出联想回到"输入拼音"提示（仍在外语模式），原键透传
      g_st.assoc = false; g_st.cands.clear();
      LayoutCand();
      return false;
    }
  }

  // ---- 组字模式 ----
  if (vk == VK_ESCAPE) { Reset(); *eaten = true; return true; }
  if (vk == VK_BACK) {
    if (!g_st.comp.empty()) g_st.comp.pop_back();
    QueryCandidates(); LayoutCand();
    *eaten = true; return true;
  }
  if (vk == VK_SPACE || vk == VK_RETURN) {
    if (!g_st.cands.empty()) Pick(g_st.page * 8 + g_st.active);
    else CommitRaw();
    *eaten = true; return true;
  }
  if (vk == VK_UP || vk == VK_DOWN) {
    if (g_st.cands.empty()) return false;   // 无候选时不吃方向键，光标移动照常
    if (vk == VK_DOWN) { g_st.active++; if (g_st.active > 7) { g_st.page = (g_st.page + 1) % g_st.pageCount; g_st.active = 0; } }
    else { g_st.active--; if (g_st.active < 0) { g_st.page = (g_st.page - 1 + g_st.pageCount) % g_st.pageCount; g_st.active = 7; } }
    int pageLen = (int)g_st.cands.size() - g_st.page * 8; if (pageLen > 8) pageLen = 8;
    if (g_st.active >= pageLen) g_st.active = pageLen - 1;
    LayoutCand(); *eaten = true; return true;
  }
  if (vk == 0xBC || vk == 0xBE) {   // < >
    int dir = (vk == 0xBC) ? -1 : 1;
    g_st.page = (g_st.page + dir + g_st.pageCount) % g_st.pageCount;
    g_st.active = 0; LayoutCand(); *eaten = true; return true;
  }
  if (vk >= '1' && vk <= '8') {
    int idx = g_st.page * 8 + (vk - '1');
    if (idx < (int)g_st.cands.size()) Pick(idx);
    else CommitRaw();
    *eaten = true; return true;
  }
  // 其他键（标点等）：拼音原样上屏后透传
  CommitRaw();
  return false;
}

static LRESULT CALLBACK HookProc(int code, WPARAM w, LPARAM l) {
  if (code == HC_ACTION && w == WM_KEYDOWN) {
    PKBDLLHOOKSTRUCT k = (PKBDLLHOOKSTRUCT)l;
    bool ctrl = GetAsyncKeyState(VK_CONTROL) & 0x8000;
    bool alt  = GetAsyncKeyState(VK_MENU) & 0x8000;
    if (k->vkCode == VK_SPACE && ctrl && !alt) {
      g_enabled = !g_enabled;
      Reset();
      wcscpy(g_nid.szTip, g_enabled ? L"译语输入法 - 打字中 (Ctrl+Space 暂停)"
                                    : L"译语输入法 - 已暂停 (Ctrl+Space 开启)");
      Shell_NotifyIconW(NIM_MODIFY, &g_nid);
      ShowToast(g_enabled ? L"译语输入法 已开启 — 直接打拼音" : L"译语输入法 已暂停 — 键盘恢复原样");
      return 1;
    }
    if (k->vkCode == 'Q' && ctrl && alt) { PostQuitMessage(0); return 1; }
    // ` (反引号，Tab 上方)：进入/退出外语组字；平时键盘 100% 透传
    if (k->vkCode == VK_OEM_3 && !ctrl && !alt && g_enabled) {
      g_armed = !g_armed;
      if (g_armed) {
        g_st.comp.clear(); g_st.cands.clear();
        g_st.page = 0; g_st.active = 0;
        g_anchorSet = false;   // 重新锚定到当前位置
        if (!g_last.empty() && g_bigram.count(g_last)) SetupAssoc();
        else { g_st.assoc = false; LayoutCand(); }   // 显示"输入拼音"提示
      } else {
        g_st.comp.clear(); g_st.cands.clear(); g_st.assoc = false;
        ShowWindow(g_hCand, SW_HIDE);
      }
      return 1;
    }
    bool eaten = false;
    HandleKey((DWORD)k->vkCode, &eaten);
    if (eaten) return 1;
  }
  return CallNextHookEx(g_hHook, code, w, l);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
  g_hInst = hInst;
  LoadDict();
  LoadUser();

  WNDCLASSW wc = {};
  wc.lpfnWndProc = CandProc; wc.hInstance = hInst;
  wc.lpszClassName = L"YuyinImeCand"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  RegisterClassW(&wc);
  g_hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH, L"Microsoft YaHei UI");
  g_hFontSmall = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH, L"Microsoft YaHei UI");
  g_hCand = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                            L"YuyinImeCand", L"", WS_POPUP,
                            100, 100, 340, 250, nullptr, nullptr, hInst, nullptr);
  SetClassLongPtrW(g_hCand, GCL_STYLE, CS_DROPSHADOW);
  typedef HRESULT (WINAPI *PFN)(HWND, DWORD, LPCVOID, DWORD);
  HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
  if (dwm) { auto f = (PFN)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (f) { UINT pref = 2; f(g_hCand, 33, &pref, 4); } FreeLibrary(dwm); }

  g_nid.cbSize = sizeof(g_nid);
  g_nid.hWnd = g_hCand;
  g_nid.uID = 1;
  g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  g_nid.uCallbackMessage = WMAPP_TRAY = WM_APP + 7;
  g_nid.hIcon = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
  wcscpy(g_nid.szTip, L"译语输入法 - 打字中 (Ctrl+Space 暂停)");
  Shell_NotifyIconW(NIM_ADD, &g_nid);

  g_hHook = SetWindowsHookExW(WH_KEYBOARD_LL, HookProc, hInst, 0);
  SetTimer(g_hCand, 2, 3000, nullptr);   // 学习数据定时落盘
  ShowToast(L"译语输入法 已开启 — 直接打拼音试试 nihao");

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  UnhookWindowsHookEx(g_hHook);
  SaveUser();                              // 退出前在主线程落盘
  Shell_NotifyIconW(NIM_DELETE, &g_nid);
  return 0;
}
