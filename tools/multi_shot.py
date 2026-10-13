#!/usr/bin/env python3
"""Shoot the two-seat dashboard stills from a live pair.

Saves the four README stills from the two UEFI seats a multi-run keeps up:

    tools/multi_shot.py screenshots http://127.0.0.1:18091 http://127.0.0.1:18092

  p2p-discovery.png         node A's P2P pane, node B in the neighbor table
  p2p-discovery-peer.png    node B's P2P pane, node A in the neighbor table
  services-remote.png       node A browsing node B's advertised services
  services-remote-peer.png  node B browsing node A's advertised services

Everything is shot at 1500x950 @2x (3000x1900, the set's standard) and only
after the DOM holds the same facts tools/multi_browser_e2e.py asserts.

Two traps this tool exists to step over, both learned the hard way:

  * The firmware serves HTTP/1.0 from a small connection table. Every fetch
    goes out with `Connection: close` and each shot retries after the table
    drains — Chromium's default keep-alive burst otherwise wedges the seat
    and the page comes out half-loaded.

  * `document.styleSheets` lists a <link> as soon as it is seen, not when its
    bytes arrived. The shot waits until the sheets actually parse (rule
    count) or the still catches half-painted badges in user-agent defaults.

Developer tooling, owned by tools/ — never a prove seat.
"""

import sys
import time

from playwright.sync_api import sync_playwright

VIEW = {"width": 1500, "height": 950}
SCALE = 2
ATTEMPTS = 3
RULES = 400  # site.css + seat.css + console.css, as served by a full seat


def close_each(route):
    route.continue_(headers={**route.request.headers, "Connection": "close"})


def load(page, base, path, ready):
    page.route("**/*", close_each)
    response = page.goto(base + path, wait_until="commit", timeout=90000)
    assert response is not None and response.ok, "%s returned no successful response" % path
    page.wait_for_selector(ready, state="attached", timeout=90000)
    page.wait_for_function(
        "() => [...document.styleSheets].reduce((n, s) => { try { return n + s.cssRules.length; } catch (e) { return n; } }, 0) >= %d" % RULES,
        timeout=90000)
    # The panels poll their APIs forever, so networkidle never fires: settle
    # on fonts plus a beat for the first paint of the polled data.
    page.evaluate("document.fonts.ready")
    page.wait_for_timeout(3000)


def shoot_p2p(page, base, path, local, remote):
    load(page, base, "/p2p", "#p2p-self-host")
    page.wait_for_function(
        "([l,r]) => document.querySelector('#p2p-self-host')?.textContent.trim() === l"
        " && document.querySelector('#p2p-nb-table tbody')?.innerText.includes(r)"
        " && document.querySelector('#p2p-nb-table tbody')?.innerText.includes('yes')",
        arg=[local, remote], timeout=180000)
    for selector in ("#p2p-rpc-handlers", "#p2p-rpc-calls", "#p2p-ds-count", "#p2p-cc-count", "#p2p-ws-count"):
        page.wait_for_function("s => !document.querySelector(s)?.textContent.toLowerCase().includes('loading')",
                               arg=selector, timeout=90000)
    page.screenshot(path=path)
    print("shot", path, flush=True)


def shoot_services(page, base, path, remote):
    load(page, base, "/services", "#svc-host")
    page.wait_for_function(
        "r => [...document.querySelectorAll('#svc-host option')].some(o => o.textContent.includes(r))",
        arg=remote, timeout=180000)
    option = page.locator("#svc-host option").filter(has_text=remote).first
    page.locator("#svc-host").select_option(option.get_attribute("value", timeout=60000), timeout=60000)
    page.wait_for_function(
        "r => document.querySelector('#svc-title')?.textContent.includes(r)"
        " && document.querySelectorAll('#svc-grid .service-card').length >= 2",
        arg=remote, timeout=180000)
    page.wait_for_function(
        "() => { const t = document.querySelector('#svc-grid')?.innerText || '';"
        " return t.includes('ssh') && t.includes('httpd'); }", timeout=90000)
    page.screenshot(path=path)
    print("shot", path, flush=True)


def attempt(browser, shoot, base, path, *args):
    for try_n in range(1, ATTEMPTS + 1):
        context = browser.new_context(viewport=VIEW, device_scale_factor=SCALE)
        page = context.new_page()
        try:
            shoot(page, base, path, *args)
            context.close()
            return
        except Exception as exc:
            context.close()
            print("attempt %d failed for %s: %s" % (try_n, path, exc), flush=True)
            if try_n == ATTEMPTS:
                raise
            time.sleep(15)  # let the seat reap the aborted connections


def main():
    out, base_a, base_b = sys.argv[1:4]
    with sync_playwright() as pw:
        browser = pw.chromium.launch(headless=True)
        attempt(browser, shoot_p2p, base_a, "%s/p2p-discovery.png" % out, "metal-11", "metal-12")
        attempt(browser, shoot_p2p, base_b, "%s/p2p-discovery-peer.png" % out, "metal-12", "metal-11")
        attempt(browser, shoot_services, base_a, "%s/services-remote.png" % out, "metal-12")
        attempt(browser, shoot_services, base_b, "%s/services-remote-peer.png" % out, "metal-11")
        browser.close()


if __name__ == "__main__":
    main()
