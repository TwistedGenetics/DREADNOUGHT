// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT for iPad: files for the pages (Resources/bridge.js). Downloads go to the app's folder, which Files shows
// as On My iPad > DREADNOUGHT; the editor's projects folder is Projects inside it. Copyright (C) 2026 Jamie (TwistedGenetics).

import Foundation
import UIKit
import WebKit

final class FilesBridge: NSObject, WKScriptMessageHandlerWithReply {
    /// Shows a line on screen (the view controller's toast).
    var toast: (String) -> Void = { _ in }

    static var documents: URL { FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0] }
    static var projects: URL {
        let u = documents.appendingPathComponent("Projects", isDirectory: true)
        try? FileManager.default.createDirectory(at: u, withIntermediateDirectories: true)
        return u
    }

    // (only a plain name: no folders, nothing hidden)
    private static func clean(_ name: String) -> String? {
        let n = (name as NSString).lastPathComponent.trimmingCharacters(in: .whitespacesAndNewlines)
        if n.isEmpty || n.hasPrefix(".") || n.contains("/") || n.contains(":") { return nil }
        return n
    }

    func userContentController(_ ucc: WKUserContentController, didReceive message: WKScriptMessage,
                               replyHandler: @escaping (Any?, String?) -> Void) {
        guard let m = message.body as? [String: Any], let op = m["op"] as? String else {
            replyHandler(nil, "bad message"); return
        }
        let fm = FileManager.default
        do {
            switch op {
            case "save":                                    // a download: kept beside the others, never overwritten
                guard let raw = m["name"] as? String, let name = Self.clean(raw),
                      let s = m["data"] as? String, let d = Data(base64Encoded: s) else { throw Err("bad save") }
                var url = Self.documents.appendingPathComponent(name)
                let base = (name as NSString).deletingPathExtension, ext = (name as NSString).pathExtension
                var k = 1
                while fm.fileExists(atPath: url.path) {
                    k += 1
                    url = Self.documents.appendingPathComponent(ext.isEmpty ? "\(base) \(k)" : "\(base) \(k).\(ext)")
                }
                try d.write(to: url, options: .atomic)
                toast("Saved \(url.lastPathComponent) in Files › On My iPad › DREADNOUGHT")
                replyHandler(url.lastPathComponent, nil)
            case "list":
                let keys: [URLResourceKey] = [.fileSizeKey, .contentModificationDateKey, .isRegularFileKey]
                let urls = try fm.contentsOfDirectory(at: Self.projects, includingPropertiesForKeys: keys,
                                                      options: [.skipsHiddenFiles])
                let out: [[String: Any]] = urls.compactMap { u in
                    guard let v = try? u.resourceValues(forKeys: Set(keys)), v.isRegularFile == true else { return nil }
                    return ["name": u.lastPathComponent, "size": v.fileSize ?? 0,
                            "mtime": ((v.contentModificationDate ?? Date()).timeIntervalSince1970 * 1000).rounded()]
                }
                replyHandler(out, nil)
            case "read":
                guard let raw = m["name"] as? String, let name = Self.clean(raw) else { throw Err("bad name") }
                let url = Self.projects.appendingPathComponent(name)
                guard fm.fileExists(atPath: url.path) else { replyHandler(nil, nil); return }
                let d = try Data(contentsOf: url)
                let date = (try? url.resourceValues(forKeys: [.contentModificationDateKey]))?.contentModificationDate
                replyHandler(["data": d.base64EncodedString(),
                              "mtime": ((date ?? Date()).timeIntervalSince1970 * 1000).rounded()], nil)
            case "write":
                guard let raw = m["name"] as? String, let name = Self.clean(raw),
                      let s = m["data"] as? String, let d = Data(base64Encoded: s) else { throw Err("bad write") }
                try d.write(to: Self.projects.appendingPathComponent(name), options: .atomic)
                replyHandler(true, nil)
            case "remove":
                guard let raw = m["name"] as? String, let name = Self.clean(raw) else { throw Err("bad name") }
                try? fm.removeItem(at: Self.projects.appendingPathComponent(name))
                replyHandler(true, nil)
            case "clip":
                UIPasteboard.general.string = m["text"] as? String ?? ""
                replyHandler(true, nil)
            case "toast":
                toast(m["text"] as? String ?? "")
                replyHandler(true, nil)
            default:
                throw Err("unknown op \(op)")
            }
        } catch {
            replyHandler(nil, (error as? Err)?.text ?? error.localizedDescription)
        }
    }

    private struct Err: Error {
        let text: String
        init(_ t: String) { text = t }
    }
}
