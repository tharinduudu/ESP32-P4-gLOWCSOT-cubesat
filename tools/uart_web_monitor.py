#!/usr/bin/env python3
"""Local flight dashboard for the ESP32-P4's CH343 USB-UART port."""

from __future__ import annotations

import argparse
import fcntl
import glob
import json
import os
import select
import struct
import termios
import threading
import time
import urllib.parse
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


INDEX_HTML = r"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Muon Flight Monitor</title><style>
:root{color-scheme:light;--ink:#17212b;--muted:#65717d;--line:#d8dee4;--green:#177245;--red:#b42318;--blue:#135f8c;--panel:#f5f7f8}
*{box-sizing:border-box}body{margin:0;font:15px system-ui,-apple-system,sans-serif;color:var(--ink);background:#fff}main{max-width:1100px;margin:auto;padding:18px}
header{display:flex;align-items:end;justify-content:space-between;border-bottom:2px solid var(--ink);padding-bottom:12px;margin-bottom:18px}h1{font-size:24px;margin:0}h2{font-size:16px;margin:0 0 12px}p{margin:4px 0}.muted{color:var(--muted)}
.bad{color:var(--red)}.ok{color:var(--green)}.latest{border-top:4px solid var(--green);padding:14px 0 20px;margin-bottom:18px}.latest-time{color:var(--muted);margin-bottom:8px}.latest-line{font:700 24px/1.45 ui-monospace,SFMono-Regular,Menlo,monospace;overflow-wrap:anywhere}.latest-label,.latest-sep{color:#7b858f;font-weight:600}.latest-value{color:var(--ink);font-weight:800}
.bands{display:grid;grid-template-columns:1fr 1fr;gap:18px;margin-bottom:20px}section{border-top:1px solid var(--line);padding-top:14px}.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin:8px 0}
button,input{font:inherit;height:36px;border:1px solid #9da7b1;background:#fff;padding:0 10px}button{cursor:pointer}button.primary{background:var(--blue);color:#fff;border-color:var(--blue)}button.danger{color:var(--red);border-color:#d92d20}input{min-width:90px;flex:1}.status{font-family:ui-monospace,SFMono-Regular,monospace;font-size:13px}
table{width:100%;border-collapse:collapse}th,td{text-align:left;border-bottom:1px solid var(--line);padding:8px 6px;white-space:nowrap}th{font-size:12px;color:var(--muted)}.table-scroll{overflow-x:auto}.newest-row{font-weight:700;background:#f2faf5}a{color:var(--blue)}
@media(max-width:700px){.bands{grid-template-columns:1fr}header{align-items:start;flex-direction:column;gap:8px}.latest-line{font-size:18px}}
</style></head><body><main>
<header><div><h1>Muon Flight Monitor</h1><p class="muted">Wired UART link</p></div><div id="link" class="status">Connecting...</div></header>
<section class="latest"><h2>Latest Recorded Minute</h2><div id="latestTime" class="latest-time">Waiting for the first completed minute...</div><div id="latestLine" class="latest-line"></div></section>
<section><h2>Last 10 Recorded Minutes</h2><div class="table-scroll"><table><thead id="historyHead"></thead><tbody id="history"></tbody></table></div></section>
<div class="bands"><section><h2>Detector Status</h2><div id="status" class="status"></div></section>
<section><h2>Run Settings</h2><div class="row"><input id="label" maxlength="32" placeholder="Flight or location label"><button onclick="setLabel()">Apply</button></div>
<div class="row"><input id="hv" value="EA" maxlength="2" aria-label="HV byte"><button onclick="setHv()">Set HV</button><button onclick="action('/api/hv?off=1')">HV Off</button></div>
<div class="row"><button class="primary" onclick="wifiOff()">Turn Wi-Fi Off</button><button class="danger" onclick="flashFpga()">Reflash FPGA</button></div><p id="message" class="muted"></p></section></div>
<section><h2>SD Files</h2><table><thead><tr><th>File</th><th>Modified</th><th>Size</th></tr></thead><tbody id="files"></tbody></table></section>
</main><script>
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const fmt=n=>n<1024?n+' B':n<1048576?(n/1024).toFixed(1)+' KB':(n/1048576).toFixed(1)+' MB';
async function api(url){let r=await fetch(url,{cache:'no-store'});let t=await r.text();if(!r.ok)throw Error(t);return t}
async function syncTime(){await api('/api/time?epoch='+Math.floor(Date.now()/1000)+'&tzoff='+new Date().getTimezoneOffset())}
const channelLabel=name=>({'ch12_p13':'1-2','ch13_p12':'1-3','ch23_p11':'2-3','ch1_p31':'ch1','ch2_p29':'ch2','ch3_p36':'ch3'}[name]||name);
function renderRecords(j){let records=(j.records||[]).slice(0,10),channels=j.channels||[];
document.getElementById('historyHead').innerHTML='<tr><th>Time</th>'+channels.map(c=>'<th>'+esc(channelLabel(c.name))+'</th>').join('')+'</tr>';
if(!records.length){document.getElementById('latestTime').textContent='Waiting for the first completed minute...';document.getElementById('latestLine').textContent='';document.getElementById('history').innerHTML='<tr><td colspan="'+(channels.length+1)+'" class="muted">No completed minute records yet</td></tr>';return}
let latest=records[0];document.getElementById('latestTime').textContent=new Date(latest.epoch*1000).toLocaleString();document.getElementById('latestLine').innerHTML=latest.counts.map((v,i)=>'<span class="latest-label">'+esc(channelLabel(channels[i]?.name||String(i)))+':</span> <span class="latest-value">'+esc(v)+'</span>').join('<span class="latest-sep"> | </span>');
let chronological=[...records].reverse();document.getElementById('history').innerHTML=chronological.map((r,i)=>'<tr class="'+(i===chronological.length-1?'newest-row':'')+'"><td>'+esc(new Date(r.epoch*1000).toLocaleString())+'</td>'+r.counts.map(v=>'<td>'+v+'</td>').join('')+'</tr>').join('')}
async function refresh(){try{let r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw Error(await r.text());let j=await r.json();
document.getElementById('link').innerHTML='<span class="ok">UART connected</span>';
renderRecords(j);
let sd=j.sd_mounted?'<span class="ok">ready</span>':'<span class="bad">not ready</span>';let fpga=j.fpga_done?'<span class="ok">ready</span>':'<span class="bad">not ready</span>';
document.getElementById('status').innerHTML='<p>SD: '+sd+'</p><p>FPGA: '+fpga+'</p><p>HV: 0x'+j.hv_byte+'</p><p>Counting: '+(j.counting_enabled?'enabled':'settling/off')+'</p><p>Time: '+(j.time_set?new Date(j.epoch*1000).toLocaleString():'not set')+'</p><p>Environment: '+(j.env_latest.valid?j.env_latest.temp_c.toFixed(2)+' C, '+j.env_latest.pressure_hpa.toFixed(1)+' hPa, '+j.env_latest.humidity_pct.toFixed(1)+'%':'unavailable')+'</p><p>Wi-Fi: '+(j.power_save_mode?'off':'on')+'</p>';
if(document.activeElement!==document.getElementById('label'))document.getElementById('label').value=j.run_label||'';
}catch(e){document.getElementById('link').innerHTML='<span class="bad">UART unavailable</span>';document.getElementById('message').textContent=e.message}}
async function loadFiles(){try{let j=JSON.parse(await api('/api/files'));document.getElementById('files').innerHTML=j.files.map(f=>'<tr><td><a href="/api/file?name='+encodeURIComponent(f.name)+'">'+esc(f.name)+'</a>'+(f.current?' <span class="ok">active</span>':'')+'</td><td>'+new Date(f.mtime*1000).toLocaleString()+'</td><td>'+fmt(f.size)+'</td></tr>').join('')}catch(e){}}
async function action(url){try{document.getElementById('message').textContent=await api(url);await refresh()}catch(e){document.getElementById('message').textContent=e.message}}
function setLabel(){action('/api/label?value='+encodeURIComponent(document.getElementById('label').value)).then(loadFiles)}
function setHv(){action('/api/hv?byte='+encodeURIComponent(document.getElementById('hv').value))}
function wifiOff(){if(confirm('Turn Wi-Fi off? HV will cycle off briefly, then restart and settle.'))action('/api/power_save')}
function flashFpga(){if(confirm('HV will turn off before the FPGA is flashed, then DAC and HV will restart. Continue?'))action('/api/fpga')}
(async()=>{try{await syncTime()}catch(e){}await refresh();await loadFiles();setInterval(refresh,2000);setInterval(loadFiles,30000)})();
</script></body></html>"""


class SerialLink:
    def __init__(self, requested_port: str | None):
        self.requested_port = requested_port
        self.fd: int | None = None
        self.path: str | None = None
        self.buffer = bytearray()
        self.lock = threading.Lock()

    def _find_port(self) -> str:
        if self.requested_port:
            return self.requested_port
        candidates = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.wchusb*"))
        preferred = [path for path in candidates if "5B" in os.path.basename(path).upper()]
        if preferred:
            return preferred[0]
        if len(candidates) == 1:
            return candidates[0]
        raise OSError("USB-UART port not found; use --port /dev/cu.usbmodem...")

    def _open(self) -> None:
        if self.fd is not None:
            return
        path = self._find_port()
        fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(fd)
        attrs[0] = 0
        attrs[1] = 0
        attrs[2] = (attrs[2] | termios.CLOCAL | termios.CREAD) & ~termios.HUPCL
        attrs[3] = 0
        attrs[4] = termios.B115200
        attrs[5] = termios.B115200
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        # Keep the CH343 auto-reset outputs inactive. The detector power comes
        # from its battery; this process only needs TX, RX, and ground.
        if hasattr(termios, "TIOCMBIC"):
            bits = getattr(termios, "TIOCM_DTR", 0) | getattr(termios, "TIOCM_RTS", 0)
            try:
                fcntl.ioctl(fd, termios.TIOCMBIC, struct.pack("i", bits))
            except OSError:
                # Not every tty driver implements modem-control ioctls. HUPCL
                # is still disabled above, and ordinary UART I/O remains safe.
                pass
        self.fd = fd
        self.path = path
        self.buffer.clear()
        # A cable can be opened while the P4 is still finishing its boot
        # sequence. Give the firmware time to install its UART task before the
        # first request; later requests reuse this open descriptor with no wait.
        time.sleep(3)
        termios.tcflush(fd, termios.TCIFLUSH)

    def _close(self) -> None:
        if self.fd is not None:
            try:
                os.close(self.fd)
            except OSError:
                pass
        self.fd = None
        self.buffer.clear()

    def _write(self, data: bytes) -> None:
        assert self.fd is not None
        view = memoryview(data)
        while view:
            _, writable, _ = select.select([], [self.fd], [], 2)
            if not writable:
                raise TimeoutError("UART write timeout")
            count = os.write(self.fd, view)
            view = view[count:]

    def _fill(self, deadline: float) -> None:
        assert self.fd is not None
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("No reply from detector")
        readable, _, _ = select.select([self.fd], [], [], min(remaining, 0.5))
        if readable:
            chunk = os.read(self.fd, 4096)
            if not chunk:
                raise OSError("UART disconnected")
            self.buffer.extend(chunk)

    def _line(self, deadline: float) -> bytes:
        while True:
            pos = self.buffer.find(b"\n")
            if pos >= 0:
                line = bytes(self.buffer[:pos]).rstrip(b"\r")
                del self.buffer[: pos + 1]
                return line
            self._fill(deadline)

    def _exact(self, size: int, deadline: float) -> bytes:
        while len(self.buffer) < size:
            self._fill(deadline)
        data = bytes(self.buffer[:size])
        del self.buffer[:size]
        return data

    def _begin(self, command: str) -> float:
        self._open()
        self.buffer.clear()
        self._write(command.encode("utf-8") + b"\n")
        return time.monotonic() + 15

    def status(self) -> dict:
        with self.lock:
            try:
                deadline = self._begin("@status")
                while True:
                    line = self._line(deadline)
                    if line.startswith(b"@json\t"):
                        return json.loads(line[6:])
                    if line.startswith(b"@error\t"):
                        raise RuntimeError(line.decode())
            except Exception:
                self._close()
                raise

    def action(self, command: str, timeout: float = 30) -> str:
        with self.lock:
            try:
                deadline = self._begin(command) + max(0, timeout - 15)
                while True:
                    line = self._line(deadline)
                    if line.startswith(b"@ok\t"):
                        return line[4:].decode("utf-8", "replace")
                    if line.startswith(b"@error\t"):
                        raise RuntimeError(line[7:].decode("utf-8", "replace"))
            except Exception:
                self._close()
                raise

    def files(self) -> list[dict]:
        with self.lock:
            try:
                deadline = self._begin("@files")
                result = []
                while True:
                    line = self._line(deadline)
                    if line == b"@end":
                        return sorted(result, key=lambda item: (item["mtime"], item["name"]), reverse=True)
                    if line.startswith(b"@file\t"):
                        _, name, size, mtime, current = line.decode().split("\t")
                        result.append({"name": name, "size": int(size), "mtime": int(mtime), "current": current == "1"})
                    elif line.startswith(b"@error\t"):
                        raise RuntimeError(line[7:].decode())
            except Exception:
                self._close()
                raise

    def file(self, name: str) -> bytes:
        with self.lock:
            try:
                deadline = self._begin("@get " + name) + 120
                while True:
                    line = self._line(deadline)
                    if line.startswith(b"@data\t"):
                        _, returned_name, size = line.decode().split("\t")
                        if returned_name != name:
                            raise RuntimeError("Detector returned the wrong file")
                        data = self._exact(int(size), deadline)
                        while self._line(deadline) != b"@end":
                            pass
                        return data
                    if line.startswith(b"@error\t"):
                        raise RuntimeError(line[7:].decode())
            except Exception:
                self._close()
                raise


class MonitorHandler(BaseHTTPRequestHandler):
    server_version = "MuonUART/1.0"

    def log_message(self, fmt: str, *args) -> None:
        return

    def _send(self, status: int, content_type: str, body: bytes, headers: dict[str, str] | None = None) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        self.wfile.write(body)

    def _text(self, text: str, status: int = 200) -> None:
        self._send(status, "text/plain; charset=utf-8", text.encode())

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        query = urllib.parse.parse_qs(parsed.query)
        link: SerialLink = self.server.link  # type: ignore[attr-defined]
        try:
            if parsed.path == "/":
                self._send(200, "text/html; charset=utf-8", INDEX_HTML.encode())
            elif parsed.path == "/api/status":
                self._send(200, "application/json", json.dumps(link.status()).encode())
            elif parsed.path == "/api/files":
                self._send(200, "application/json", json.dumps({"files": link.files()}).encode())
            elif parsed.path == "/api/file":
                name = query.get("name", [""])[0]
                data = link.file(name)
                self._send(200, "text/csv", data, {"Content-Disposition": f'attachment; filename="{name}"'})
            elif parsed.path == "/api/time":
                epoch = int(query.get("epoch", ["0"])[0])
                tzoff = int(query.get("tzoff", ["0"])[0])
                self._text(link.action(f"@time {epoch} {tzoff}"))
            elif parsed.path == "/api/label":
                self._text(link.action("@label " + query.get("value", [""])[0]))
            elif parsed.path == "/api/hv":
                if "off" in query:
                    self._text(link.action("@hv off"))
                else:
                    self._text(link.action("@hv " + query.get("byte", [""])[0]))
            elif parsed.path == "/api/dac":
                self._text(link.action("@dac " + query.get("ch", [""])[0] + " " + query.get("value", [""])[0]))
            elif parsed.path == "/api/fpga":
                self._text(link.action("@fpga", timeout=60))
            elif parsed.path == "/api/power_save":
                self._text(link.action("@wifi off"))
            else:
                self._text("not found", 404)
        except Exception as exc:
            self._text(str(exc), 503)


def main() -> None:
    parser = argparse.ArgumentParser(description="ESP32-P4 wired UART flight dashboard")
    parser.add_argument("--port", help="USB-UART device; auto-detected when omitted")
    parser.add_argument("--listen", default="127.0.0.1", help="HTTP listen address")
    parser.add_argument("--http-port", type=int, default=8080, help="HTTP port")
    parser.add_argument("--open-browser", action="store_true", help="open the local dashboard automatically")
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.listen, args.http_port), MonitorHandler)
    server.link = SerialLink(args.port)  # type: ignore[attr-defined]
    dashboard_url = f"http://{args.listen}:{args.http_port}"
    print(f"Muon UART monitor: {dashboard_url}", flush=True)
    print("Press Ctrl-C to stop. Detector logging continues if this program or cable disconnects.", flush=True)
    if args.open_browser:
        threading.Timer(0.5, webbrowser.open, args=(dashboard_url,)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
