// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT for iPad: serves the web pages (the repo's web/ folder, copied into the app) at dreadnought://app/, so
// they load as they do from GitHub Pages: the editor at /editor/, its links to /breaks/ and /flasher/ work, and its
// modules load. The flasher's release list and firmware files aren't in the app: those few come from the live site.
// Copyright (C) 2026 Jamie (TwistedGenetics).

import Foundation
import WebKit

final class BundleSchemeHandler: NSObject, WKURLSchemeHandler {
    static let scheme = "dreadnought"
    static let site = URL(string: "https://twistedgenetics.github.io/DREADNOUGHT/")!
    private let root: URL
    private var live = Set<ObjectIdentifier>()                        // (tasks not yet stopped by WebKit)

    init(root: URL) { self.root = root.standardizedFileURL }

    private static let types: [String: String] = [
        "html": "text/html; charset=utf-8", "js": "text/javascript; charset=utf-8", "mjs": "text/javascript; charset=utf-8",
        "css": "text/css; charset=utf-8", "json": "application/json", "svg": "image/svg+xml", "png": "image/png",
        "jpg": "image/jpeg", "ico": "image/x-icon", "ttf": "font/ttf", "otf": "font/otf", "woff": "font/woff",
        "woff2": "font/woff2", "wav": "audio/wav", "txt": "text/plain; charset=utf-8", "fwsc": "application/octet-stream",
    ]

    func webView(_ webView: WKWebView, start task: WKURLSchemeTask) {
        live.insert(ObjectIdentifier(task))
        guard let url = task.request.url else { return fail(task, 400) }
        var path = url.path.removingPercentEncoding ?? url.path
        while path.hasPrefix("/") { path.removeFirst() }
        if path.isEmpty || path.hasSuffix("/") { path += "index.html" }

        var file = root.appendingPathComponent(path).standardizedFileURL
        var isDir: ObjCBool = false
        if FileManager.default.fileExists(atPath: file.path, isDirectory: &isDir), isDir.boolValue {
            file = file.appendingPathComponent("index.html")
        }
        // (nothing outside the pages' folder)
        guard file.path.hasPrefix(root.path + "/") else { return fail(task, 403) }
        if let data = try? Data(contentsOf: file) {
            return reply(task, url, 200, Self.types[file.pathExtension.lowercased()] ?? "application/octet-stream", data)
        }
        if path == "flasher/firmware.json" || path.hasPrefix("fw/") {
            return fetchLive(task, url, path)
        }
        fail(task, 404)
    }

    func webView(_ webView: WKWebView, stop task: WKURLSchemeTask) {
        live.remove(ObjectIdentifier(task))
    }

    private func fetchLive(_ task: WKURLSchemeTask, _ url: URL, _ path: String) {
        guard let remote = URL(string: path, relativeTo: Self.site) else { return fail(task, 404) }
        var req = URLRequest(url: remote)
        req.cachePolicy = .reloadIgnoringLocalCacheData
        URLSession.shared.dataTask(with: req) { [weak self] data, resp, _ in
            DispatchQueue.main.async {
                guard let self else { return }
                let code = (resp as? HTTPURLResponse)?.statusCode ?? 502
                guard let data, code == 200 else { return self.fail(task, code == 200 ? 502 : code) }
                let ext = (path as NSString).pathExtension.lowercased()
                self.reply(task, url, 200, Self.types[ext] ?? "application/octet-stream", data)
            }
        }.resume()
    }

    private func reply(_ task: WKURLSchemeTask, _ url: URL, _ code: Int, _ type: String, _ data: Data) {
        guard live.contains(ObjectIdentifier(task)) else { return }
        let headers = ["Content-Type": type, "Content-Length": "\(data.count)", "Cache-Control": "no-cache",
                       "Access-Control-Allow-Origin": "*"]
        let resp = HTTPURLResponse(url: url, statusCode: code, httpVersion: "HTTP/1.1", headerFields: headers)!
        task.didReceive(resp)
        task.didReceive(data)
        task.didFinish()
        live.remove(ObjectIdentifier(task))
    }

    private func fail(_ task: WKURLSchemeTask, _ code: Int) {
        guard let url = task.request.url else { return }
        reply(task, url, code, "text/plain; charset=utf-8", Data("\(code)".utf8))
    }
}
