import pymergetic.metal as m

if not m.ready():
    raise RuntimeError("ready")
print("upy metal ready")

# Cards resolve from the registry at any depth, with no name list to maintain.
import pymergetic.metal.net.ip as ip

if ip.socket is None:
    raise RuntimeError("native card import")
if m.net.ip is not ip:
    raise RuntimeError("namespace walk")
print("upy native card import")

import pymergetic.metal.inspect as inspect

st = inspect.handle("GET", "/inspect/self")
body = inspect.body()
if st != 200 or '"name":"pymergetic.metal"' not in body:
    raise RuntimeError("inspect self")
st = inspect.handle("GET", "/inspect/reg")
body = inspect.body()
if st != 200 or "pymergetic.metal" not in body:
    raise RuntimeError("inspect reg")
print("upy inspect")
st = inspect.handle("GET", "/capabilities")
body = inspect.body()
if st != 200 or '"asgi":true' not in body or '"microdot":true' not in body or '"zenoh":true' not in body:
    raise RuntimeError("inspect caps")
print("upy inspect caps")
# The AI pane's live read face: /ai/state is the inspect JSON over the ai hub
# (tools/providers/sessions/config/mcp) — the same fill the /ai UI pane fetches.
# Wired on every seat; the fill is identical everywhere, the arrays are just
# short where the hub is idle.
st = inspect.handle("GET", "/ai/state")
body = inspect.body()
if st != 200 or '"note":"live_ai_hub"' not in body:
    raise RuntimeError("ai state")
print("upy ai state")
# AI Factory (write) + Browse (drill-down): create a provider / session, append
# + browse a message, then remove the provider. Same /ai/… the pane posts.
st = inspect.handle("POST", "/ai/provider/set?name=probe&url=u&model=m")
body = inspect.body()
if st != 200 or '"ok":true' not in body:
    raise RuntimeError("ai provider set")
st = inspect.handle("GET", "/ai/provider?name=probe")
body = inspect.body()
if st != 200 or '"name":"probe"' not in body:
    raise RuntimeError("ai provider browse")
st = inspect.handle("POST", "/ai/session/create?name=probe")
body = inspect.body()
if st != 200 or '"ok":true' not in body:
    raise RuntimeError("ai session create")
sid = body.split('"id":')[1].split('}')[0]
st = inspect.handle("POST", "/ai/session/append?id=%s&role=0&content=hello" % sid)
body = inspect.body()
if st != 200 or '"ok":true' not in body:
    raise RuntimeError("ai session append")
st = inspect.handle("GET", "/ai/session?id=%s" % sid)
body = inspect.body()
if st != 200 or 'hello' not in body:
    raise RuntimeError("ai session browse")
st = inspect.handle("POST", "/ai/config/set?key=probe&value=v")
body = inspect.body()
if st != 200 or '"ok":true' not in body:
    raise RuntimeError("ai config set")
st = inspect.handle("POST", "/ai/provider/remove?name=probe")
body = inspect.body()
if st != 200 or '"ok":true' not in body:
    raise RuntimeError("ai provider remove")
print("ai factory+browse")

# metal.build change ledger: the seed materializes on the fs card and the
# /changes read pane serves it - on firmware seats too (arena fs, no POSIX).
st = inspect.handle("GET", "/changes/pymergetic.metal.build")
body = inspect.body()
if st != 200 or 'decision' not in body:
    raise RuntimeError("changes ledger")
print("upy changes ledger")

# metal.edit C editor (Phase 12): parse/locate/set_define on the firmware seat
# - the editor is resident C, so the span splice works on arena memory with no
# POSIX. The write-back gate (no note -> refusal) is the card contract.
import pymergetic.metal.edit as edit

SRC = (
    "#include <stdint.h>\n"
    "#define EDIT_PROBE_BUF 64\n"
    "int32_t edit_probe_add(int32_t a) {\n"
    "    return a + EDIT_PROBE_BUF;\n"
    "}\n"
)
h = edit.parse_c(SRC)
if h is None:
    raise RuntimeError("edit parse")
n = edit.locate(h, "define", "EDIT_PROBE_BUF")
if n is None or n[1] != "EDIT_PROBE_BUF":
    raise RuntimeError("edit locate")
out = edit.set_define(h, "EDIT_PROBE_BUF", "128")
if out is None or "128" not in out:
    raise RuntimeError("edit set_define")
wb = edit.write_back("fw.edit.probe", "/src/fw_edit_probe.c", out)
if wb is None or wb[0] == 0:
    raise RuntimeError("edit write gate")
print("upy editor")
if m.net.dns.resolve is None:
    raise RuntimeError("dns")
print("upy dns")
print("upy socket")

# pymergetic.metal.ai hub + its four subcards (config/provider/session/mcp):
# boot-wired on every seat, so the registry faces below are live here exactly
# as everywhere else. The fn-pointer and struct faces (tool_register, chat,
# provider.send) are C/RS CONNECT-only by design and take their coverage from
# the card's host C cases; this scalar/string subset is the Python face and
# must behave the same on every seat.
import pymergetic.metal.ai as ai
import pymergetic.metal.ai.config as ai_config
import pymergetic.metal.ai.provider as ai_provider
import pymergetic.metal.ai.session as ai_session
import pymergetic.metal.ai.mcp as ai_mcp

if ai.tool_count is None:
    raise RuntimeError("ai faces")
if ai.tool_count() != 0:
    raise RuntimeError("ai tools empty")
if ai.tool_unregister("seat-probe-missing") != -1:
    raise RuntimeError("ai tool unregister miss")
# The HTTP face test earlier (POST /ai/config/set?key=probe&value=v) persists
# its param in the same store the Python face reads below. Clear it so the
# roundtrip starts from an empty table (count 1 after set, 0 after delete).
ai_config.param_delete("probe")
if ai_config.param_set("seat-ai-probe", "1") != 0:
    raise RuntimeError("ai config param set")
if ai_config.param_count() != 1:
    raise RuntimeError("ai config param count")
if ai_config.param_delete("seat-ai-probe") != 0:
    raise RuntimeError("ai config param delete")
if ai_config.param_count() != 0:
    raise RuntimeError("ai config param empty")
if ai_config.provider_delete("seat-probe-missing") != -1:
    raise RuntimeError("ai config provider miss")
if ai_provider.count() != 0:
    raise RuntimeError("ai providers empty")
if ai_provider.remove("seat-probe-missing") != -1:
    raise RuntimeError("ai provider remove miss")
# The HTTP face test earlier left its session open (there is no close route),
# so assert the delta, not the absolute count.
_sbase = ai_session.count()
_sid = ai_session.create("seat-ai-probe")
if _sid[0] != 0 or ai_session.count() != _sbase + 1:
    raise RuntimeError("ai session create")
if ai_session.close(_sid[1]) != 0 or ai_session.count() != _sbase:
    raise RuntimeError("ai session close")
if ai_mcp.server_count() != 0:
    raise RuntimeError("ai mcp servers empty")
if ai_mcp.server_status("seat-probe-missing") != -1:
    raise RuntimeError("ai mcp status miss")
if ai_mcp.server_stop("seat-probe-missing") != -1:
    raise RuntimeError("ai mcp stop miss")
if ai_mcp.discover_all() != 0:
    raise RuntimeError("ai mcp discover")
print("upy ai")

# net.swarm.*: the three fleet cards must mount on firmware too. Callback faces
# (offer, declare) are host-C only; the off-on defers, the membership node
# identity, and the discovery arm/teardown must hold with no peer present.
import pymergetic.metal.net.swarm.membership as sm
import pymergetic.metal.net.swarm.task as st
import pymergetic.metal.net.swarm.discovery as sd

if sm is None or st is None or sd is None:
    raise RuntimeError("swarm card import")
if sm.alive() != 0 or sm.stop() != 0 or sm.start("fleet") != 0:
    raise RuntimeError("swarm membership not-open")
nid = sm.node_id()
if not isinstance(nid, bytes) or len(nid) != 32:
    raise RuntimeError("swarm membership node_id")
if st.offering() != 0 or st.declaring() != 0 or st.done() != 0:
    raise RuntimeError("swarm task not-open")
if st.dispatch("render", b"\x01\x02\x03") != 0:
    raise RuntimeError("swarm task dispatch defer")
sr = sd.scout()
if not isinstance(sr, tuple) or len(sr) != 2 or sr[0] != 0:
    raise RuntimeError("swarm discovery scout no-peer")
if sd.answer_on() != 1 or sd.answer_on() != 0:
    raise RuntimeError("swarm discovery answer")
sd.answer_off()
if sd.pump() != 0:
    raise RuntimeError("swarm discovery pump")
print("upy swarm")
