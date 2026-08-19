#!/bin/bash
BIN=/home/ladmin/Devel/os-sdk/packages/metalpython/ports/unix/build-metal/micropython
cd /home/ladmin/Devel/os-sdk/packages/metalpython/extmod/metal
pkill -9 -f "build-metal/micropython" 2>/dev/null
sleep 1
# Keep stdin a real tty so the REPL/console stays alive; otherwise the boot
# console reads EOF and exits. Run inside tmux with METAL_SERVE=1.
tmux kill-session -t ui 2>/dev/null
tmux new-session -d -s ui -x 120 -y 40 "METAL_SERVE=1 $BIN"
# wait for renderer
for i in $(seq 1 40); do
  if tmux capture-pane -t ui -p 2>/dev/null | grep -q "packs rendering"; then
    echo "RENDERER_UP"; break
  fi
  sleep 1
done
# warm one request
curl -s -m 3 -o /dev/null -w "page=%{http_code}\n" http://127.0.0.1:8090/packs/pymergetic.metal
curl -s -m 3 -o /tmp/probe_page.html -w "page=%{http_code} bytes=%{size_download}\n" http://127.0.0.1:8090/packs/pymergetic.metal
curl -s -m 3 -o /tmp/probe_site.css -w "sitecss=%{http_code} bytes=%{size_download}\n" http://127.0.0.1:8090/static/site.css
curl -s -m 3 -o /tmp/probe_js.js -w "inspectjs=%{http_code} bytes=%{size_download}\n" http://127.0.0.1:8090/inspect/js/inspect.js
echo "PAGE_LINKS:"
grep -oE '(href|src)="[^"]+"' /tmp/probe_page.html | head -40
echo "SITE_CSS_HEAD:"
head -3 /tmp/probe_site.css
echo "DONE"
