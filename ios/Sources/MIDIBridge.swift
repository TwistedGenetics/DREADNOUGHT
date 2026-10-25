// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT for iPad: CoreMIDI for the pages' Web MIDI (Resources/bridge.js). Every source is listened to; what
// arrives is cut into whole messages (a SysEx frame can come in several packets) and handed to the page; what the
// page sends goes out as one packet list. Copyright (C) 2026 Jamie (TwistedGenetics).

import CoreMIDI
import Foundation
import WebKit

final class MIDIBridge: NSObject, WKScriptMessageHandlerWithReply {
    weak var webView: WKWebView?

    private var client = MIDIClientRef()
    private var inPort = MIDIPortRef()
    private var outPort = MIDIPortRef()
    private var started = false
    private var connected = Set<MIDIEndpointRef>()
    private let lock = NSLock()
    private var parsers: [MIDIEndpointRef: StreamParser] = [:]        // (the read thread's; under `lock`)

    // ---- the page's calls ------------------------------------------------------------------------------------------
    func userContentController(_ ucc: WKUserContentController, didReceive message: WKScriptMessage,
                               replyHandler: @escaping (Any?, String?) -> Void) {
        guard let m = message.body as? [String: Any], let op = m["op"] as? String else {
            replyHandler(nil, "bad message"); return
        }
        switch op {
        case "start":
            if let err = start() { replyHandler(nil, err) } else { replyHandler(ports(), nil) }
        case "send":
            guard let id = m["id"] as? String, let s = m["data"] as? String, let d = Data(base64Encoded: s) else {
                replyHandler(nil, "bad send"); return
            }
            send(id: id, data: d)
            replyHandler(true, nil)
        default:
            replyHandler(nil, "unknown op \(op)")
        }
    }

    private func start() -> String? {
        if started { return nil }
        var st = MIDIClientCreateWithBlock("DREADNOUGHT" as CFString, &client) { [weak self] n in
            let id = n.pointee.messageID
            if id == .msgObjectAdded || id == .msgObjectRemoved || id == .msgSetupChanged || id == .msgPropertyChanged {
                DispatchQueue.main.async { self?.portsChanged() }
            }
        }
        if st != noErr { return "MIDIClientCreate \(st)" }
        st = MIDIInputPortCreateWithBlock(client, "DREADNOUGHT in" as CFString, &inPort) { [weak self] list, refCon in
            self?.received(list, refCon)
        }
        if st != noErr { return "MIDIInputPortCreate \(st)" }
        st = MIDIOutputPortCreate(client, "DREADNOUGHT out" as CFString, &outPort)
        if st != noErr { return "MIDIOutputPortCreate \(st)" }
        started = true
        connectSources()
        return nil
    }

    // ---- the ports -------------------------------------------------------------------------------------------------
    private static func string(_ obj: MIDIObjectRef, _ key: CFString) -> String {
        var s: Unmanaged<CFString>?
        guard MIDIObjectGetStringProperty(obj, key, &s) == noErr, let v = s?.takeRetainedValue() else { return "" }
        return v as String
    }
    private static func uid(_ obj: MIDIObjectRef) -> Int32 {
        var v: Int32 = 0
        MIDIObjectGetIntegerProperty(obj, kMIDIPropertyUniqueID, &v)
        return v
    }
    private static func offline(_ obj: MIDIObjectRef) -> Bool {
        var v: Int32 = 0
        return MIDIObjectGetIntegerProperty(obj, kMIDIPropertyOffline, &v) == noErr && v != 0
    }
    private static func info(_ e: MIDIEndpointRef, _ kind: String) -> [String: String] {
        var name = string(e, kMIDIPropertyDisplayName)
        if name.isEmpty { name = string(e, kMIDIPropertyName) }
        return ["id": "\(kind)\(uid(e))", "name": name, "manufacturer": string(e, kMIDIPropertyManufacturer)]
    }
    private var sources: [MIDIEndpointRef] {
        (0..<MIDIGetNumberOfSources()).map { MIDIGetSource($0) }.filter { $0 != 0 && !Self.offline($0) }
    }
    private var destinations: [MIDIEndpointRef] {
        (0..<MIDIGetNumberOfDestinations()).map { MIDIGetDestination($0) }.filter { $0 != 0 && !Self.offline($0) }
    }
    private func ports() -> [String: Any] {
        ["inputs": sources.map { Self.info($0, "i") }, "outputs": destinations.map { Self.info($0, "o") }]
    }

    private func connectSources() {
        let now = Set(sources)
        for src in now.subtracting(connected) {
            // (the endpoint itself rides along as the connection's refCon: the read block gets it back)
            if MIDIPortConnectSource(inPort, src, UnsafeMutableRawPointer(bitPattern: UInt(src))) == noErr {
                connected.insert(src)
            }
        }
        for gone in connected.subtracting(now) {
            MIDIPortDisconnectSource(inPort, gone)
            connected.remove(gone)
            lock.lock(); parsers[gone] = nil; lock.unlock()
        }
    }

    private func portsChanged() {
        guard started else { return }
        connectSources()
        guard let json = try? JSONSerialization.data(withJSONObject: ports()),
              let s = String(data: json, encoding: .utf8) else { return }
        webView?.evaluateJavaScript("window.__dnMIDI && window.__dnMIDI.ports(\(s))", completionHandler: nil)
    }

    // ---- in --------------------------------------------------------------------------------------------------------
    private func received(_ list: UnsafePointer<MIDIPacketList>, _ refCon: UnsafeMutableRawPointer?) {
        guard let refCon else { return }
        let src = MIDIEndpointRef(UInt(bitPattern: refCon))
        var frames: [String] = []
        lock.lock()
        let parser = parsers[src] ?? StreamParser()
        for packet in list.unsafeSequence() {
            let n = Int(packet.pointee.length)
            let start = UnsafeRawPointer(packet).advanced(by: MemoryLayout<MIDIPacket>.offset(of: \MIDIPacket.data) ?? 10)
            let bytes = UnsafeRawBufferPointer(start: start, count: n)
            for b in bytes { parser.feed(b) { frames.append(Data($0).base64EncodedString()) } }
        }
        parsers[src] = parser
        lock.unlock()
        if frames.isEmpty { return }
        let id = "i\(Self.uid(src))"
        guard let json = try? JSONSerialization.data(withJSONObject: frames),
              let s = String(data: json, encoding: .utf8) else { return }
        DispatchQueue.main.async { [weak self] in
            self?.webView?.evaluateJavaScript("window.__dnMIDI && window.__dnMIDI.recv(\"\(id)\", \(s))",
                                              completionHandler: nil)
        }
    }

    // ---- out -------------------------------------------------------------------------------------------------------
    private func send(id: String, data: Data) {
        guard started, let dest = destinations.first(where: { "o\(Self.uid($0))" == id }), !data.isEmpty else { return }
        let chunk = 256
        let chunks = (data.count + chunk - 1) / chunk
        let size = MemoryLayout<MIDIPacketList>.size + chunks * MemoryLayout<MIDIPacket>.size + data.count + 64
        let raw = UnsafeMutableRawPointer.allocate(byteCount: size, alignment: MemoryLayout<MIDIPacketList>.alignment)
        defer { raw.deallocate() }
        let list = raw.bindMemory(to: MIDIPacketList.self, capacity: 1)
        var pkt: UnsafeMutablePointer<MIDIPacket>? = MIDIPacketListInit(list)
        data.withUnsafeBytes { (buf: UnsafeRawBufferPointer) in
            guard let base = buf.bindMemory(to: UInt8.self).baseAddress else { return }
            var off = 0
            while off < data.count, let p = pkt {
                let n = min(chunk, data.count - off)
                pkt = MIDIPacketListAdd(list, size, p, 0, n, base + off)
                off += n
            }
        }
        if pkt != nil { MIDISend(outPort, dest, list) }
    }
}

// A MIDI byte stream cut into whole messages: SysEx kept whole however it was split, running status, real-time bytes
// passed straight through (even from inside a SysEx).
final class StreamParser {
    private var sysex: [UInt8]?
    private var status: UInt8 = 0
    private var data: [UInt8] = []

    private static func dataLength(_ s: UInt8) -> Int {
        switch s {
        case 0x80...0xBF, 0xE0...0xEF, 0xF2: return 2
        case 0xC0...0xDF, 0xF1, 0xF3: return 1
        default: return 0
        }
    }

    func feed(_ b: UInt8, emit: ([UInt8]) -> Void) {
        if b >= 0xF8 { emit([b]); return }
        if b == 0xF0 { sysex = [0xF0]; status = 0; data = []; return }
        if b == 0xF7 {
            if var s = sysex { s.append(0xF7); emit(s) }
            sysex = nil
            return
        }
        if sysex != nil {
            if b < 0x80 { sysex!.append(b); return }
            sysex = nil                                      // (a status byte cut the SysEx short: it's dropped)
        }
        if b >= 0x80 {
            status = b
            data = []
            if Self.dataLength(b) == 0 { emit([b]); if b >= 0xF0 { status = 0 } }
            return
        }
        guard status != 0 else { return }
        data.append(b)
        if data.count == Self.dataLength(status) {
            emit([status] + data)
            data = []
            if status >= 0xF0 { status = 0 }
        }
    }
}
