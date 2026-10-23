#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""End-to-end test of the flasher page against a pretend FM-1 (test/mock-fm1.js), in headless Chromium.

    pip install playwright && playwright install chromium
    python3 web/flasher/test/e2e_mock.py

Checks: the device is found, the checks gate the Install button, a full install writes every byte of
the logical image through the loader, and an install cut off mid-write resumes and completes.
"""
import functools
import http.server
import tempfile
import threading
from pathlib import Path

from playwright.sync_api import sync_playwright

WEB = Path(__file__).resolve().parents[2]          # web/
MOCK = (Path(__file__).parent / "mock-fm1.js").read_text()


def fake_package(identity="FM-1_901", size=12000):
    b = bytearray((i * 7) & 0xFF for i in range(size))
    for i in range(20):
        b[i * 0x30 + 0x2F] = (ord(identity[i]) + i + 1) & 0xFF if i < len(identity) else 0x7D
    return bytes(b)


def logical(pkg):
    out = bytearray()
    for i in range(20):
        out += pkg[i * 0x30:i * 0x30 + 0x2F]
    return bytes(out + pkg[20 * 0x30:])


def serve():
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *a):
            pass
    handler = functools.partial(Quiet, directory=str(WEB))
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


def run(page, base, pkg_path, image, opts):
    page.add_init_script(f"window.__fm1opts = {opts};\n" + MOCK)
    page.on("dialog", lambda d: d.accept())
    page.goto(base + "/flasher/")
    assert page.is_disabled("#go"), "Install must start disabled"
    page.click("#detect")
    page.wait_for_function("document.getElementById('device').textContent.includes('FM-1_900')")
    page.set_input_files("#file", str(pkg_path))
    page.wait_for_function("document.getElementById('pkg').textContent.includes('FM-1_901')")
    assert page.is_disabled("#go"), "Install must wait for the backup and power ticks"
    page.check("#backed-up")
    page.check("#power")
    assert page.is_enabled("#go")
    page.evaluate(f"window.__fm1target = 'FM-1_901'; window.__fm1imageLen = {len(image)}")
    page.click("#go")


def written(page):
    got = page.evaluate("Array.from(window.__fm1.received.entries()).filter(([a]) => a < 0xE0000000)")
    buf = bytearray()
    for addr, data in sorted(got):
        if addr == len(buf):
            buf += bytes(data)
    return bytes(buf)


def main():
    srv = serve()
    base = f"http://127.0.0.1:{srv.server_address[1]}"
    pkg = fake_package()
    image = logical(pkg)
    with tempfile.TemporaryDirectory() as d, sync_playwright() as p:
        path = Path(d) / "dreadnought-test.fwsc"
        path.write_bytes(pkg)
        browser = p.chromium.launch()

        # 1. a clean install
        page = browser.new_page()
        run(page, base, path, image, "{start: 'FM-1_900'}")
        page.wait_for_function("document.getElementById('status').textContent.startsWith('Done')", timeout=60000)
        assert written(page) == image, "the loader did not receive the exact image"
        assert page.evaluate("window.__fm1.identity") == "FM-1_901"
        print("ok  full install: every byte of the image written, device reports FM-1_901")
        page.close()

        # 2. cut off half-way, then press Install again
        page = browser.new_page()
        run(page, base, path, image, "{start: 'FM-1_900', cutAfter: 10}")
        page.wait_for_function("document.getElementById('status').className === 'bad'", timeout=60000)
        print("ok  cut-off install reports:", page.text_content("#status"))
        page.wait_for_timeout(1500)
        page.click("#go")
        page.wait_for_function("document.getElementById('status').textContent.startsWith('Done')", timeout=60000)
        assert written(page) == image, "after resuming, the image is not complete"
        print("ok  resumed install completed:", page.text_content("#status"))
        page.close()
        browser.close()
    srv.shutdown()


if __name__ == "__main__":
    main()
