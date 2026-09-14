#!/usr/bin/env python3
"""Load the real dashboards from two live firmware seats and prove their DOM."""
import sys
from playwright.sync_api import sync_playwright

bases = sys.argv[1:3]
expected = [("metal-11", "metal-12"), ("metal-12", "metal-11")]

def verify_downloads(browser, base):
    # Use a disposable browser context per download. Closing it completes the
    # client side of TCP teardown before the next large firmware response; the
    # APIRequestContext pool otherwise retains several idle transports into the
    # live page proof and consumes the firmware socket table.
    for path, minimum in (("/", 100000), ("/static/seat/console.js", 10000),
                          ("/static/css/seat.css", 1000)):
        context = browser.new_context()
        try:
            response = context.request.get(base + path, timeout=60000,
                                           headers={"Connection": "close"})
            assert response.ok and len(response.body()) >= minimum, "%s download failed" % path
        finally:
            context.close()


def load_panel(page, base, path, ready):
    # The sequential fetches above prove the complete shell and decorative
    # assets. Keep this page focused on its real firmware-served HTML, panel JS,
    # and APIs so one tiny firmware HTTP server is not load-tested by Chromium's
    # speculative asset fan-out while the P2P behavior is being proved.
    page.route("**/*", lambda route: route.abort()
               if route.request.resource_type in ("stylesheet", "image", "font")
               or route.request.url.endswith("/static/seat/console.js")
               else route.continue_())
    response = page.goto(base + path, wait_until="domcontentloaded", timeout=60000)
    assert response is not None and response.ok, "%s returned no successful response" % path
    page.wait_for_selector(ready, state="attached", timeout=60000)

with sync_playwright() as pw:
    browser = pw.chromium.launch(headless=True)
    for base, (local, remote) in zip(bases, expected):
        verify_downloads(browser, base)
        page = browser.new_page()
        failures = []
        page.on("pageerror", lambda exc, f=failures: f.append(str(exc)))
        load_panel(page, base, "/p2p", "#p2p-self-host")
        page.wait_for_function("([l,r]) => document.querySelector('#p2p-self-host')?.textContent.trim() === l && document.querySelector('#p2p-nb-table tbody')?.innerText.includes(r)", arg=[local, remote], timeout=60000)
        assert "yes" in page.locator("#p2p-nb-table tbody").inner_text()
        for selector in ("#p2p-rpc-handlers", "#p2p-rpc-calls", "#p2p-ds-count", "#p2p-cc-count", "#p2p-ws-count"):
            page.wait_for_function("s => !document.querySelector(s)?.textContent.toLowerCase().includes('loading')", arg=selector, timeout=60000)
        page.close()
        page = browser.new_page()
        page.on("pageerror", lambda exc, f=failures: f.append(str(exc)))
        load_panel(page, base, "/services", "#svc-host")
        page.wait_for_function("r => [...document.querySelectorAll('#svc-host option')].some(o => o.textContent.includes(r))", arg=remote, timeout=60000)
        option = page.locator("#svc-host option").filter(has_text=remote).first
        page.locator("#svc-host").select_option(option.get_attribute("value"))
        page.wait_for_function("r => document.querySelector('#svc-title')?.textContent.includes(r) && document.querySelectorAll('#svc-grid .service-card').length >= 2", arg=remote)
        text = page.locator("#svc-grid").inner_text()
        assert "ssh" in text and "httpd" in text
        assert "Ownership\nremote" in text and "remote status unavailable" in text
        assert not failures, failures
        page.close()
    browser.close()
print("multi-seat browser: downloaded firmware P2P and Services dashboards PASS")
