// 译语输入法 YuyinIme — macOS 系统级输入法守护（Swift / CGEventTap）
// CGEventTap 可真正拦截按键（NSEvent 全局监听只能观察，无法做输入法）。
// 需要授权：系统设置 → 隐私与安全性 → 输入监控（首次运行会引导）。
// 交互与 Windows 版一致：拼音 → 候选窗（8 行/页）→ 数字/空格选词 → 上屏外语词。
// 学习：freq.tsv / bigram.tsv 本地词频与二元联想，与 Windows 版同格式。

import AppKit

let shared = IME()

func tapCallback(proxy: CGEventTapProxy, type: CGEventType, event: CGEvent,
                 refcon: UnsafeMutableRawPointer?) -> CGEvent? {
    shared.handleEvent(type: type, event: event)
}

final class IME: NSObject, NSApplicationDelegate {
    var enabled = true
    var comp = ""
    var cands: [(word: String, comment: String)] = []
    var page = 0, pageCount = 1, active = 0
    var assoc = false
    var dict: [String: [(word: String, comment: String)]] = [:]
    var freq: [String: Int] = [:]
    var bigram: [String: [String: Int]] = [:]
    var lastWord = ""
    var committing = false
    var panel: NSPanel!
    var label: NSTextField!
    var tap: CFMachPort?

    // ---------- 生命周期 ----------
    func applicationDidFinishLaunching(_ n: Notification) {
        loadDict()
        loadUser()
        buildPanel()
        statusBar()
        installTap()
        showToast("译语输入法已开启 — 直接打拼音试试 nihao")
    }

    func exeDir() -> String {
        if let res = Bundle.main.resourceURL { return res.path }
        let exe = CommandLine.arguments[0] as NSString
        return exe.deletingLastPathComponent
    }

    func loadDict() {
        var path = Bundle.main.path(forResource: "dict", ofType: "tsv") ?? ""
        if path.isEmpty { path = exeDir() + "/dict.tsv" }
        guard let s = try? String(contentsOfFile: path, encoding: .utf8) else { return }
        for line in s.components(separatedBy: "\n") {
            let p = line.split(separator: "\t", omittingEmptySubsequences: false).map(String.init)
            if p.count >= 2 { dict[p[0]].append((p[1], p.count > 2 ? p[2] : "")) }
        }
    }

    func loadUser() {
        let d = exeDir()
        if let s = try? String(contentsOfFile: d + "/freq.tsv", encoding: .utf8) {
            for line in s.components(separatedBy: "\n") {
                let p = line.split(separator: "\t").map(String.init)
                if p.count == 2, let c = Int(p[1]) { freq[p[0]] = c }
            }
        }
        if let s = try? String(contentsOfFile: d + "/bigram.tsv", encoding: .utf8) {
            for line in s.components(separatedBy: "\n") {
                let p = line.split(separator: "\t").map(String.init)
                if p.count == 3, let c = Int(p[2]) { bigram[p[0], default: [:]][p[1]] = c }
            }
        }
    }

    func saveUser() {
        let d = exeDir()
        var f = ""
        for (w, c) in freq { f += w + "\t" + String(c) + "\n" }
        try? f.write(toFile: d + "/freq.tsv", atomically: true, encoding: .utf8)
        var b = ""
        for (prev, m) in bigram {
            for (w, c) in m { b += prev + "\t" + w + "\t" + String(c) + "\n" }
        }
        try? b.write(toFile: d + "/bigram.tsv", atomically: true, encoding: .utf8)
    }

    // ---------- UI ----------
    func buildPanel() {
        panel = NSPanel(contentRect: NSRect(x: 100, y: 600, width: 340, height: 130),
                        styleMask: [.borderless, .nonactivatingPanel],
                        backing: .buffered, defer: false)
        panel.level = .floating
        panel.backgroundColor = NSColor.windowBackgroundColor
        panel.alphaValue = 0.96
        label = NSTextField(labelWithString: "")
        label.frame = NSRect(x: 10, y: 8, width: 322, height: 116)
        label.font = NSFont.systemFont(ofSize: 13)
        label.lineBreakMode = .byWordWrapping
        label.textColor = .labelColor
        panel.contentView?.addSubview(label)
    }

    func statusBar() {
        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        item.button?.title = "译"
        let menu = NSMenu()
        let t = NSMenuItem(title: "开关输入", action: #selector(toggle), keyEquivalent: "")
        t.target = self
        let q = NSMenuItem(title: "退出", action: #selector(quit), keyEquivalent: "q")
        q.target = self
        menu.addItem(t)
        menu.addItem(q)
        item.menu = menu
    }

    @objc func toggle() {
        enabled.toggle()
        comp = ""; cands = []; assoc = false
        if !enabled { panel.orderOut(nil) }
        showToast(enabled ? "译语输入法已开启 — 直接打拼音" : "译语输入法已暂停 — 键盘恢复原样")
    }

    @objc func quit() {
        saveUser()
        NSApp.terminate(nil)
    }

    // ---------- 事件拦截 ----------
    func installTap() {
        let mask = CGEventMask(1 << CGEventType.keyDown.rawValue)
        guard let t = CGEvent.tapCreate(tap: .cghidEventTap, place: .headInsertEventTap,
                                        options: .defaultTap, eventsOfInterest: mask,
                                        callback: tapCallback, userInfo: nil) else {
            let alert = NSAlert()
            alert.messageText = "译语输入法需要授权"
            alert.informativeText = "请在「系统设置 → 隐私与安全性 → 输入监控」中勾选 YuyinIme，然后重新打开程序。"
            alert.addButton(withTitle: "打开设置")
            alert.addButton(withTitle: "稍后")
            if alert.runModal() == .alertFirstButtonReturn {
                NSWorkspace.shared.open(URL(string:
                    "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility")!)
            }
            return
        }
        tap = t
        let src = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, t, 0)
        CFRunLoopAddSource(CFRunLoopGetCurrent(), src, .commonModes)
        CGEvent.tapEnable(tap: t, enable: true)
    }

    func handleEvent(type: CGEventType, event: CGEvent) -> CGEvent? {
        if type == .tapDisabledByTimeout || type == .tapDisabledByUserInput {
            if let t = tap { CGEvent.tapEnable(tap: t, enable: true) }
            return event
        }
        guard type == .keyDown, !committing, enabled else { return event }
        let mods = event.flags.intersection(.deviceIndependentFlagsMask)
        if mods.contains(.maskCommand) || mods.contains(.maskControl)
            || mods.contains(.maskAlternate) || mods.contains(.maskFunction) { return event }
        guard let chars = event.charactersIgnoringModifiers?.lowercased(),
              chars.count == 1 else { return event }

        let isLetter = chars >= "a" && chars <= "z"
        let hasComp = !comp.isEmpty
        let hasAssoc = assoc && !cands.isEmpty

        if !hasComp && !isLetter {
            if hasAssoc {
                switch chars {
                case "1", "2", "3", "4", "5", "6", "7", "8":
                    let i = page * 8 + (Int(chars)! - 1)
                    if i < cands.count { pick(i) }
                    return nil
                case " ", "\r":
                    pick(page * 8 + active)
                    return nil
                case "\u{1B}":
                    assoc = false; cands = []; panel.orderOut(nil)
                    return nil
                case "<":
                    page = (page - 1 + pageCount) % pageCount; active = 0; layout()
                    return nil
                case ">":
                    page = (page + 1) % pageCount; active = 0; layout()
                    return nil
                default:
                    assoc = false; cands = []; panel.orderOut(nil)
                    return event
                }
            }
            return event
        }

        if isLetter {
            assoc = false
            if comp.count < 24 { comp += chars }
            query(); layout()
            return nil
        }

        switch chars {
        case " ", "\r":
            if !cands.isEmpty { pick(page * 8 + active) } else { commitRaw() }
            return nil
        case "\u{7F}":
            if !comp.isEmpty { comp.removeLast() }
            query(); layout()
            return nil
        case "\u{1B}":
            comp = ""; cands = []; panel.orderOut(nil)
            return nil
        case "<":
            page = (page - 1 + pageCount) % pageCount; active = 0; layout()
            return nil
        case ">":
            page = (page + 1) % pageCount; active = 0; layout()
            return nil
        case "1", "2", "3", "4", "5", "6", "7", "8":
            let i = page * 8 + (Int(chars)! - 1)
            if i < cands.count { pick(i) } else { commitRaw() }
            return nil
        default:
            commitRaw()
            return event
        }
    }

    // ---------- 候选与学习 ----------
    func query() {
        cands = dict[comp] ?? []
        if cands.count < 80 {
            var pref: [(word: String, comment: String)] = []
            for (k, v) in dict where k.hasPrefix(comp) && k.count > comp.count {
                pref.append(contentsOf: v)
                if pref.count > 80 { break }
            }
            cands.append(contentsOf: pref)
        }
        if cands.count > 80 { cands = Array(cands.prefix(80)) }
        cands.sort { (freq[$0.word] ?? 0) > (freq[$1.word] ?? 0) }
        pageCount = max(1, (cands.count + 7) / 8)
        if page >= pageCount { page = pageCount - 1 }
    }

    func showAssoc() {
        assoc = true
        cands = []
        if let m = bigram[lastWord] {
            for (w, _) in m.sorted(by: { $0.value > $1.value }).prefix(24) {
                cands.append((w, "联想"))
            }
        }
        page = 0; active = 0
        pageCount = max(1, (cands.count + 7) / 8)
        if cands.isEmpty { panel.orderOut(nil) } else { layout() }
    }

    func bump(_ word: String) {
        freq[word, default: 0] += 1
        if !lastWord.isEmpty && lastWord != word {
            bigram[lastWord, default: [:]][word, default: 0] += 1
        }
        lastWord = word
        saveUser()
    }

    func pick(_ idx: Int) {
        guard idx >= 0 && idx < cands.count else { return }
        let word = cands[idx].word
        comp = ""; cands = []; page = 0; active = 0
        commit(word)
        bump(word)
        showAssoc()
    }

    func commitRaw() {
        let raw = comp
        comp = ""; cands = []; assoc = false
        lastWord = ""
        commit(raw)
        panel.orderOut(nil)
    }

    func commit(_ w: String) {
        committing = true
        let src = CGEventSource(stateID: .hidSystemState)
        for ch in w.unicodeScalars {
            var u = [UniChar(ch.value)]
            if let down = CGEvent(keyboardEventSource: src, virtualKey: 0, keyDown: true) {
                down.keyboardSetUnicodeString(stringLength: 1, unicodeString: &u)
                down.post(tap: .cghidEventTap)
            }
            if let up = CGEvent(keyboardEventSource: src, virtualKey: 0, keyDown: false) {
                up.keyboardSetUnicodeString(stringLength: 1, unicodeString: &u)
                up.post(tap: .cghidEventTap)
            }
            usleep(1500)
        }
        committing = false
    }

    // ---------- 候选窗 ----------
    func positionPanel(height: CGFloat) {
        let mouse = NSEvent.mouseLocation
        panel.setFrameTopLeftPoint(NSPoint(x: mouse.x, y: mouse.y - 6))
        panel.setFrameSize(NSSize(width: 340, height: height))
    }

    func showToast(_ text: String) {
        label.stringValue = text
        positionPanel(height: 40)
        panel.orderFrontRegardless()
        Timer.scheduledTimer(withTimeInterval: 1.0, repeats: false) { [weak self] _ in
            guard let s = self else { return }
            if s.comp.isEmpty && s.cands.isEmpty { s.panel.orderOut(nil) }
        }
    }

    func layout() {
        if cands.isEmpty { panel.orderOut(nil); return }
        var lines: [String] = []
        if assoc { lines.append("[ 联想 ← \(lastWord) ]  译语输入法") }
        else { lines.append("[ \(comp) ]  译语输入法") }
        let start = page * 8
        for i in 0..<8 where start + i < cands.count {
            let mark = i == active ? "▸" : " "
            lines.append("\(mark) \(i + 1)  \(cands[start + i].comment)  \(cands[start + i].word)")
        }
        lines.append("\(page + 1)/\(pageCount)   < > 翻页")
        label.stringValue = lines.joined(separator: "\n")
        let rows = min(8, cands.count - page * 8)
        positionPanel(height: CGFloat(30 + rows * 18 + 22))
        panel.orderFrontRegardless()
    }
}

let app = NSApplication.shared
app.setActivationPolicy(.accessory)
app.delegate = shared
app.run()
