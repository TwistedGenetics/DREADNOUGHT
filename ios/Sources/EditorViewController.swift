// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT for iPad: the editor, full screen, with CoreMIDI and Files behind it.
// Copyright (C) 2026 Jamie (TwistedGenetics).

import AVFoundation
import UIKit
import WebKit

final class EditorViewController: UIViewController, WKNavigationDelegate, WKUIDelegate {
    private var webView: WKWebView!
    private let midi = MIDIBridge()
    private let files = FilesBridge()
    private let toastLabel = PaddedLabel()
    private var toastTimer: Timer?
    private let pageBackground = UIColor(red: 0xF4 / 255, green: 0xF3 / 255, blue: 0xEE / 255, alpha: 1)
    private let selfTest = ProcessInfo.processInfo.arguments.contains("-selftest")

    override var prefersStatusBarHidden: Bool { true }
    override var prefersHomeIndicatorAutoHidden: Bool { true }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = pageBackground
        // (the previews play with the ring / silent switch off; nothing else's audio is stopped)
        try? AVAudioSession.sharedInstance().setCategory(.playback, options: [.mixWithOthers])

        let web = Bundle.main.url(forResource: "web", withExtension: nil)!
        let cfg = WKWebViewConfiguration()
        cfg.setURLSchemeHandler(BundleSchemeHandler(root: web), forURLScheme: BundleSchemeHandler.scheme)
        cfg.mediaTypesRequiringUserActionForPlayback = []
        cfg.allowsInlineMediaPlayback = true
        let ucc = cfg.userContentController
        ucc.addScriptMessageHandler(midi, contentWorld: .page, name: "dnmidi")
        ucc.addScriptMessageHandler(files, contentWorld: .page, name: "dnfiles")
        if let u = Bundle.main.url(forResource: "bridge", withExtension: "js"), let js = try? String(contentsOf: u) {
            ucc.addUserScript(WKUserScript(source: js, injectionTime: .atDocumentStart, forMainFrameOnly: true))
        }

        webView = WKWebView(frame: .zero, configuration: cfg)
        webView.navigationDelegate = self
        webView.uiDelegate = self
        webView.allowsBackForwardNavigationGestures = true      // (back from the breaks page or the flasher)
        webView.isOpaque = false
        webView.backgroundColor = pageBackground
        webView.scrollView.backgroundColor = pageBackground
        if #available(iOS 16.4, *) { webView.isInspectable = true }   // (Safari on a Mac can look in)
        midi.webView = webView
        files.toast = { [weak self] in self?.showToast($0) }

        webView.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(webView)
        NSLayoutConstraint.activate([
            webView.leadingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.leadingAnchor),
            webView.trailingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.trailingAnchor),
            webView.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor),
            webView.bottomAnchor.constraint(equalTo: view.bottomAnchor),
        ])

        // a line of news (saved files): big, dark text on white, as the rest of the page
        toastLabel.font = .systemFont(ofSize: 20, weight: .semibold)
        toastLabel.textColor = UIColor(red: 0x10 / 255, green: 0x11 / 255, blue: 0x14 / 255, alpha: 1)
        toastLabel.backgroundColor = .white
        toastLabel.layer.borderColor = UIColor(red: 0xFF / 255, green: 0x6A / 255, blue: 0x2B / 255, alpha: 1).cgColor
        toastLabel.layer.borderWidth = 3
        toastLabel.layer.cornerRadius = 10
        toastLabel.layer.masksToBounds = true
        toastLabel.numberOfLines = 0
        toastLabel.textAlignment = .center
        toastLabel.alpha = 0
        toastLabel.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(toastLabel)
        NSLayoutConstraint.activate([
            toastLabel.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            toastLabel.bottomAnchor.constraint(equalTo: view.safeAreaLayoutGuide.bottomAnchor, constant: -24),
            toastLabel.widthAnchor.constraint(lessThanOrEqualTo: view.widthAnchor, constant: -48),
        ])

        load()
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        // (a long transfer to the FM-1 mustn't be cut off by the screen locking)
        UIApplication.shared.isIdleTimerDisabled = true
    }

    private func load() {
        let q = selfTest ? "?demo" : ""
        webView.load(URLRequest(url: URL(string: "\(BundleSchemeHandler.scheme)://app/editor/index.html\(q)")!))
    }

    private func showToast(_ text: String) {
        toastLabel.text = text
        toastTimer?.invalidate()
        UIView.animate(withDuration: 0.2) { self.toastLabel.alpha = 1 }
        toastTimer = Timer.scheduledTimer(withTimeInterval: 3.5, repeats: false) { [weak self] _ in
            UIView.animate(withDuration: 0.4) { self?.toastLabel.alpha = 0 }
        }
    }

    // ---- links: the pages stay here, anything on the web opens in Safari ---------------------------------------------
    func webView(_ webView: WKWebView, decidePolicyFor action: WKNavigationAction,
                 decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
        guard let url = action.request.url, let scheme = url.scheme?.lowercased() else { return decisionHandler(.cancel) }
        if scheme == BundleSchemeHandler.scheme || scheme == "about" || scheme == "blob" || scheme == "data" {
            return decisionHandler(.allow)
        }
        if action.navigationType == .linkActivated || action.targetFrame == nil {
            UIApplication.shared.open(url)
        }
        decisionHandler(.cancel)
    }

    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        if selfTest { runSelfTest() }
    }

    func webViewWebContentProcessDidTerminate(_ webView: WKWebView) { webView.reload() }

    // links that open a new window (target=_blank): here instead, or Safari for the web
    func webView(_ webView: WKWebView, createWebViewWith configuration: WKWebViewConfiguration,
                 for action: WKNavigationAction, windowFeatures: WKWindowFeatures) -> WKWebView? {
        if let url = action.request.url {
            if url.scheme == BundleSchemeHandler.scheme { webView.load(action.request) } else { UIApplication.shared.open(url) }
        }
        return nil
    }

    // ---- the pages' alert / confirm / prompt (the flasher asks before it installs) ------------------------------------
    func webView(_ webView: WKWebView, runJavaScriptAlertPanelWithMessage message: String,
                 initiatedByFrame frame: WKFrameInfo, completionHandler: @escaping () -> Void) {
        let a = UIAlertController(title: nil, message: message, preferredStyle: .alert)
        a.addAction(UIAlertAction(title: "OK", style: .default) { _ in completionHandler() })
        present(a, animated: true)
    }

    func webView(_ webView: WKWebView, runJavaScriptConfirmPanelWithMessage message: String,
                 initiatedByFrame frame: WKFrameInfo, completionHandler: @escaping (Bool) -> Void) {
        let a = UIAlertController(title: nil, message: message, preferredStyle: .alert)
        a.addAction(UIAlertAction(title: "Cancel", style: .cancel) { _ in completionHandler(false) })
        a.addAction(UIAlertAction(title: "OK", style: .default) { _ in completionHandler(true) })
        present(a, animated: true)
    }

    func webView(_ webView: WKWebView, runJavaScriptTextInputPanelWithPrompt prompt: String, defaultText: String?,
                 initiatedByFrame frame: WKFrameInfo, completionHandler: @escaping (String?) -> Void) {
        let a = UIAlertController(title: nil, message: prompt, preferredStyle: .alert)
        a.addTextField { $0.text = defaultText }
        a.addAction(UIAlertAction(title: "Cancel", style: .cancel) { _ in completionHandler(nil) })
        a.addAction(UIAlertAction(title: "OK", style: .default) { _ in completionHandler(a.textFields?.first?.text) })
        present(a, animated: true)
    }

    // ---- CI's check (launched with -selftest): the editor in demo mode, its state and a picture, in Documents --------
    private func runSelfTest() {
        DispatchQueue.main.asyncAfter(deadline: .now() + 8) { [weak self] in
            guard let self else { return }
            let js = """
            (async () => {
              const r = { status: document.getElementById("status")?.textContent || "",
                          demo: !!(window.__dn && window.__dn.mock), midi: typeof navigator.requestMIDIAccess,
                          dir: typeof window.showDirectoryPicker, panel: !!window.__panel };
              try { const a = await navigator.requestMIDIAccess({ sysex: true });
                    r.ports = { inputs: [...a.inputs.values()].map((p) => p.name),
                                outputs: [...a.outputs.values()].map((p) => p.name) }; }
              catch (e) { r.midiError = String(e); }
              return JSON.stringify(r);
            })()
            """
            self.webView.callAsyncJavaScript("return await \(js)", in: nil, in: .page) { result in
                var text = "{}"
                switch result {
                case .success(let v): text = v as? String ?? "\(v)"
                case .failure(let e): text = "{\"error\": \(String(reflecting: e.localizedDescription))}"
                }
                try? text.write(to: FilesBridge.documents.appendingPathComponent("selftest.json"), atomically: true,
                                encoding: .utf8)
                self.webView.takeSnapshot(with: nil) { img, _ in
                    if let png = img?.pngData() {
                        try? png.write(to: FilesBridge.documents.appendingPathComponent("selftest.png"))
                    }
                }
            }
        }
    }
}

final class PaddedLabel: UILabel {
    var insets = UIEdgeInsets(top: 14, left: 22, bottom: 14, right: 22)
    override func drawText(in rect: CGRect) { super.drawText(in: rect.inset(by: insets)) }
    override var intrinsicContentSize: CGSize {
        let s = super.intrinsicContentSize
        return CGSize(width: s.width + insets.left + insets.right, height: s.height + insets.top + insets.bottom)
    }
    override func textRect(forBounds bounds: CGRect, limitedToNumberOfLines n: Int) -> CGRect {
        let r = super.textRect(forBounds: bounds.inset(by: insets), limitedToNumberOfLines: n)
        return r.inset(by: UIEdgeInsets(top: -insets.top, left: -insets.left, bottom: -insets.bottom, right: -insets.right))
    }
}
