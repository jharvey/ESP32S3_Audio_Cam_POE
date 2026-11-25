"""Update intercom units over the network (R3.0). Plain Python 3, nothing to install.

Each unit must have a web password ("webpass") and network updates switched on ("netupdate on"),
both set once over USB (unit_tool.py setup does it). The unit checks the image (size, SHA-256,
"is this intercom firmware") and only then restarts into it; a failed transfer changes nothing.
Calls come first: while a unit talks or plays, the transfer waits.

  python net_update.py firmware.bin --rooms 214                 one unit
  python net_update.py firmware.bin --rooms 101-112,201-212     several, one after the other
  python net_update.py firmware.bin --rooms 214 --host 127.0.0.1:8214   another address (testing)
  python net_update.py --rooms 101-112 --info                   only show versions

The password comes from --password, the INTERCOM_WEBPASS environment variable, or a prompt.
One unit at a time on purpose: if an image turns out bad, it stops at the first unit.
"""

import argparse
import base64
import getpass
import hashlib
import json
import os
import sys
import time
import urllib.error
import urllib.request

PIECE = 4096


def rooms_of(text):
    out = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        a, _, b = part.partition("-")
        lo, hi = int(a), int(b or a)
        if not (1 <= lo <= hi <= 999):
            raise SystemExit("bad room range: " + part)
        out.extend(range(lo, hi + 1))
    return out


def ip_of(room):
    return "10.42.%d.%d" % (room // 100, room % 100)


class Unit:
    def __init__(self, host, password):
        self.base = "http://" + host
        self.auth = "Basic " + base64.b64encode(("admin:" + password).encode()).decode()

    def call(self, path, body=None, timeout=8):
        """-> (status, text). Network errors come back as status 0."""
        req = urllib.request.Request(self.base + path, data=body, method="POST" if body is not None else "GET")
        req.add_header("Authorization", self.auth)
        req.add_header("X-Intercom", "1")
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return r.status, r.read().decode("utf-8", "replace")
        except urllib.error.HTTPError as e:
            return e.code, e.read().decode("utf-8", "replace")
        except (urllib.error.URLError, OSError) as e:
            return 0, str(e)

    def version(self):
        st, text = self.call("/api/fw")
        if st != 200:
            return None, text
        return json.loads(text), ""


def update(unit, image, name):
    info, err = unit.version()
    if info is None:
        return "no answer: " + err
    if not info.get("available"):
        return "this unit cannot be updated over the network"
    if not info.get("allowed"):
        return 'updates are off on this unit (USB console: "netupdate on")'
    old = info.get("version", "?")
    sha = hashlib.sha256(image).hexdigest()
    st, text = unit.call("/api/fw/begin?size=%d&sha=%s" % (len(image), sha), b"")
    if st != 200:
        return "not started: " + text
    off, errors, t0 = 0, 0, time.time()
    while off < len(image):
        st, text = unit.call("/api/fw/data?off=%d" % off, base64.b64encode(image[off:off + PIECE]))
        if st == 200:
            off = min(off + PIECE, len(image))
            errors = 0
            print("\r  %s: %3d %%" % (name, off * 100 // len(image)), end="", flush=True)
        elif st == 503:  # in a call
            print("\r  %s: waiting, the unit is in a call " % name, end="", flush=True)
            time.sleep(0.7)
        elif st == 409 and text.startswith("expected offset "):
            off = int(text[16:])
        elif st == 0 and errors < 20:
            errors += 1
            time.sleep(1)
        else:
            unit.call("/api/fw/abort", b"")
            print()
            return "stopped at %d of %d bytes: %s" % (off, len(image), text)
    st, text = unit.call("/api/fw/end", b"")
    print()
    if st != 200:
        return "refused: " + text
    # The unit restarts (about 0.5 s later); wait for it to answer again and compare versions.
    time.sleep(3)
    for _ in range(30):
        info, _ = unit.version()
        if info is not None:
            print("  %s: %s -> %s in %.0f s" % (name, old, info.get("version", "?"), time.time() - t0))
            return ""
        time.sleep(1)
    return "written, but the unit did not come back within 30 s"


def main():
    p = argparse.ArgumentParser(description="Update intercom units over the network")
    p.add_argument("image", nargs="?", help="firmware.bin (from .pio/build/intercom or a release folder)")
    p.add_argument("--rooms", required=True, help="214 or 101-112,201-212")
    p.add_argument("--host", help="address[:port] to use instead of 10.42.<floor>.<room> (one room only)")
    p.add_argument("--password", help="web password (or INTERCOM_WEBPASS, or a prompt)")
    p.add_argument("--info", action="store_true", help="only show each unit's firmware version")
    p.add_argument("--keep-going", action="store_true", help="carry on with the next unit after a failure")
    a = p.parse_args()
    rooms = rooms_of(a.rooms)
    if a.host and len(rooms) != 1:
        raise SystemExit("--host needs exactly one room")
    password = a.password or os.environ.get("INTERCOM_WEBPASS") or getpass.getpass("Web password: ")
    image = None
    if not a.info:
        if not a.image:
            raise SystemExit("give a firmware.bin, or --info")
        with open(a.image, "rb") as f:
            image = f.read()
        if len(image) < 65536 or image[0] != 0xE9:
            raise SystemExit("%s does not look like an ESP32 firmware image" % a.image)
        print("%s: %d bytes, sha256 %s" % (os.path.basename(a.image), len(image), hashlib.sha256(image).hexdigest()))
    failed = []
    for room in rooms:
        unit = Unit(a.host or ip_of(room), password)
        name = "room %d" % room
        if a.info:
            info, err = unit.version()
            print("  %s: %s" % (name, ("firmware %s, updates %s" % (info.get("version"), "on" if info.get("allowed") else "off"))
                                if info else "no answer: " + err))
            continue
        err = update(unit, image, name)
        if err:
            print("  %s: FAILED: %s" % (name, err))
            failed.append(room)
            if not a.keep_going:
                rest = rooms[rooms.index(room) + 1:]
                if rest:
                    print("stopped; not tried: " + ", ".join(str(r) for r in rest))
                break
    if failed:
        sys.exit(1)


if __name__ == "__main__":
    main()
