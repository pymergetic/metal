#!/usr/bin/env python3
"""Host HTTP server + unix µPy CDN fetch + pack import prove."""
import http.server
import pathlib
import subprocess
import sys
import threading

WASM = b"\x00asm\x01\x00\x00\x00"
PACKS = pathlib.Path(__file__).resolve().parents[1] / "wasmmod" / "examples" / "packs"
HELLO = PACKS / "pymergetic.wasmmod_examples.hello.wasm"
HELLO_BYTES = HELLO.read_bytes()
# A second pack, for the prove that a load refuses a pack larger than what the
# seat's loader.image knob will take: it has to be one this seat has not
# already imported.
TEST_A_BYTES = (PACKS / "pymergetic.wasmmod_examples.test_a.wasm").read_bytes()


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.headers.get("Authorization") != "Bearer tok-cdn" or self.headers.get(
            "X-Shell-Session-Id"
        ) != "sess-1":
            self.send_error(401)
            return
        if self.path == "/artifacts/lead/hello.wasm":
            body = WASM
        elif self.path == "/artifacts/lead/pymergetic.wasmmod_examples.hello.wasm":
            body = HELLO_BYTES
        elif self.path == "/artifacts/lead/pymergetic.wasmmod_examples.test_a.wasm":
            body = TEST_A_BYTES
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/wasm")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        del format, args


def main():
    upy = sys.argv[1]
    py = sys.argv[2]
    server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
    port = server.server_address[1]
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        r = subprocess.run([upy, py, "http://127.0.0.1:%u" % port], check=False)
    finally:
        server.shutdown()
        thread.join()
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
