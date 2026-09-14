#!/usr/bin/env python3
"""Assert two independent firmware peers exchange real service rosters."""
import http.client, json, sys, time, urllib.request

def get(base, path):
    with urllib.request.urlopen(base + path, timeout=3) as r:
        return json.load(r)

a, b = sys.argv[1:3]
last = None
for _ in range(120):
    try:
        sa, sb = get(a, "/p2p/self"), get(b, "/p2p/self")
        aa, bb = get(a, "/p2p/services"), get(b, "/p2p/services")
        last = (sa, sb, aa, bb)
        if sa.get("wire_peer_seen") and sb.get("wire_peer_seen"):
            assert sa["host"] == "metal-11", sa
            assert sb["host"] == "metal-12", sb
            assert sa["zenoh_zid"] != sb["zenoh_zid"], (sa, sb)
            assert sa["neighbor_count"] >= 1 and sb["neighbor_count"] >= 1
            assert sa["remote_services"] >= 2 and sb["remote_services"] >= 2
            assert any(h.get("host") == "metal-12" and not h.get("local") and h.get("alive") for h in aa["hosts"])
            assert any(h.get("host") == "metal-11" and not h.get("local") and h.get("alive") for h in bb["hosts"])
            for roster in (aa, bb):
                assert {s["name"] for s in roster["services"] if s.get("local")} >= {"httpd", "ssh"}
                assert {s["name"] for s in roster["services"] if not s.get("local")} >= {"httpd", "ssh"}
            print("multi-seat: two UEFI peers, Zenoh neighbors and remote services PASS")
            raise SystemExit(0)
    except (OSError, http.client.HTTPException, ValueError, KeyError, AssertionError, IndexError):
        pass
    time.sleep(0.5)
print(json.dumps(last, indent=2), file=sys.stderr)
raise SystemExit("multi-seat Zenoh/service-roster timeout")
