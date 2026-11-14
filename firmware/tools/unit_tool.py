"""Intercom unit tool: USB firmware updates and first-time setup.

Updates are USB only (no network updates), so this makes each room visit one command.
It never erases flash, so a unit keeps its room number, tuning, web password and
network key across updates.

Run it with PlatformIO's Python (it has pyserial), or use update-unit.bat:

  python unit_tool.py release                 copy the last build to a release folder
  python unit_tool.py info                    show the plugged-in unit's room, IP, version
  python unit_tool.py update                  flash the newest release, check it came back
  python unit_tool.py update --release DIR    flash a specific release
  python unit_tool.py setup --unit 214 --webpass ... --netkey ...   first-time setup
  python unit_tool.py setup --unit 214 --burn-efuse                 also fix the strapping-pin issue

Every update and setup is logged to updates.csv in the releases folder.
"""

import argparse
import csv
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import time

import serial
import serial.tools.list_ports

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.dirname(HERE)
BUILD = os.path.join(FW, ".pio", "build", "intercom")
DEFAULT_RELEASES = r"M:\ESP32S3_Audio_Cam_POE\releases"

# ESP32-S3 flash layout (default_16MB.csv). NVS (settings) at 0x9000 is never written.
IMAGES = [
    (0x0000, "bootloader.bin"),
    (0x8000, "partitions.bin"),
    (0xE000, "boot_app0.bin"),
    (0x10000, "firmware.bin"),
]
ESPRESSIF_USB_VID = 0x303A


def fail(msg):
    print("ERROR: " + msg)
    sys.exit(1)


def find_tool(name):
    """Path to esptool.py / espefuse.py from PlatformIO's tool-esptoolpy package."""
    for base in [r"M:\pio", os.path.expanduser(r"~\.platformio"), os.environ.get("PLATFORMIO_CORE_DIR", "")]:
        p = os.path.join(base, "packages", "tool-esptoolpy", name)
        if base and os.path.exists(p):
            return p
    fail(name + " not found (looked in M:\\pio and ~\\.platformio). Build the firmware once with pio first.")


def source_version():
    with open(os.path.join(FW, "src", "core", "Config.h"), encoding="utf-8") as f:
        m = re.search(r'kFirmwareVersion\s*=\s*"([^"]+)"', f.read())
    return m.group(1) if m else "unknown"


def find_port(port):
    if port:
        return port
    ports = [p for p in serial.tools.list_ports.comports() if p.vid == ESPRESSIF_USB_VID]
    if not ports:
        fail("no intercom unit found on USB. Plug in the unit's USB-C port, or pass --port COMx.")
    if len(ports) > 1:
        fail("more than one unit plugged in (" + ", ".join(p.device for p in ports) + "). Pass --port COMx.")
    return ports[0].device


def console(port, commands, wait=1.2):
    """Send console commands, return everything the unit printed."""
    out = ""
    with serial.Serial(port, 115200, timeout=0.1) as s:
        s.reset_input_buffer()
        s.write(b"\n")
        time.sleep(0.2)
        for c in commands:
            s.write((c + "\n").encode())
            end = time.time() + wait
            while time.time() < end:
                out += s.read(4096).decode(errors="replace")
    return out


def read_info(port, tries=10):
    """Room, IP and version from the unit's console, or None if it doesn't answer."""
    for _ in range(tries):
        try:
            text = console(port, ["version", "unit"])
        except serial.SerialException:
            time.sleep(1)  # port still re-enumerating after a reset
            continue
        unit = re.search(r"unit (\S+) ip (\S+)", text)
        ver = re.search(r"firmware (\S+)", text)
        if unit:
            return {"unit": unit.group(1), "ip": unit.group(2), "version": ver.group(1) if ver else "unknown", "raw": text}
        time.sleep(1)
    return None


def log_event(releases, row):
    os.makedirs(releases, exist_ok=True)
    path = os.path.join(releases, "updates.csv")
    new = not os.path.exists(path)
    with open(path, "a", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        if new:
            w.writerow(["time", "action", "port", "unit", "old_version", "new_version", "result"])
        w.writerow([datetime.datetime.now().isoformat(timespec="seconds")] + row)


def latest_release(releases):
    if not os.path.isdir(releases):
        fail("no releases folder at " + releases + ". Run: unit_tool.py release")
    dirs = [d for d in os.listdir(releases) if os.path.exists(os.path.join(releases, d, "manifest.json"))]
    if not dirs:
        fail("no releases in " + releases + ". Run: unit_tool.py release")

    def key(d):
        return [int(x) if x.isdigit() else x for x in re.split(r"[.\-]", d)]

    return os.path.join(releases, sorted(dirs, key=key)[-1])


def cmd_release(a):
    ver = source_version()
    dest = os.path.join(a.releases, ver)
    if os.path.exists(dest) and not a.force:
        fail("release " + ver + " already exists at " + dest + ". Bump kFirmwareVersion in Config.h, or use --force.")
    fw = os.path.join(BUILD, "firmware.bin")
    if not os.path.exists(fw):
        fail("no build at " + BUILD + ". Run: pio run")
    os.makedirs(dest, exist_ok=True)
    boot_app0 = None
    for base in [r"M:\pio", os.path.expanduser(r"~\.platformio")]:
        p = os.path.join(base, "packages", "framework-arduinoespressif32", "tools", "partitions", "boot_app0.bin")
        if os.path.exists(p):
            boot_app0 = p
            break
    if not boot_app0:
        fail("boot_app0.bin not found in the Arduino framework package")
    for _, name in IMAGES:
        src = boot_app0 if name == "boot_app0.bin" else os.path.join(BUILD, name)
        shutil.copy2(src, os.path.join(dest, name))
    git = subprocess.run(["git", "-C", FW, "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip()
    manifest = {"version": ver, "git": git, "built": datetime.datetime.now().isoformat(timespec="seconds"),
                "images": [{"offset": hex(o), "file": n} for o, n in IMAGES]}
    with open(os.path.join(dest, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
    print("release %s (git %s) written to %s" % (ver, git, dest))


def cmd_info(a):
    port = find_port(a.port)
    info = read_info(port, tries=3)
    if not info:
        fail("unit on " + port + " didn't answer on the console")
    print("port %s: unit %s, ip %s, firmware %s" % (port, info["unit"], info["ip"], info["version"]))


def flash(port, release):
    with open(os.path.join(release, "manifest.json"), encoding="utf-8") as f:
        manifest = json.load(f)
    args = [sys.executable, find_tool("esptool.py"), "--chip", "esp32s3", "--port", port, "--baud", "921600",
            "--before", "default_reset", "--after", "hard_reset", "write_flash", "-z"]
    for img in manifest["images"]:
        args += [img["offset"], os.path.join(release, img["file"])]
    print("flashing %s to %s ..." % (manifest["version"], port))
    r = subprocess.run(args)
    return r.returncode == 0, manifest["version"]


def cmd_update(a):
    port = find_port(a.port)
    release = a.release or latest_release(a.releases)
    before = read_info(port, tries=3)
    if before:
        print("before: unit %s, ip %s, firmware %s" % (before["unit"], before["ip"], before["version"]))
    else:
        print("unit didn't answer before the update (blank or crashed unit?), flashing anyway")
    ok, new_ver = flash(port, release)
    if not ok:
        log_event(a.releases, ["update", port, before["unit"] if before else "?", before["version"] if before else "?", new_ver, "FLASH FAILED"])
        fail("flashing failed. The unit still has its old firmware or is blank; run update again.")
    print("waiting for the unit to restart ...")
    time.sleep(3)
    after = read_info(port, tries=15)
    result = "ok"
    if not after:
        result = "NO ANSWER AFTER UPDATE"
    elif after["version"] != new_ver:
        result = "VERSION MISMATCH (%s)" % after["version"]
    elif before and after["unit"] != before["unit"]:
        result = "ROOM CHANGED %s -> %s" % (before["unit"], after["unit"])
    log_event(a.releases, ["update", port, after["unit"] if after else "?", before["version"] if before else "?", new_ver, result])
    if result != "ok":
        fail(result)
    print("OK: unit %s, ip %s, now firmware %s (settings kept)" % (after["unit"], after["ip"], after["version"]))


def burn_efuse(port, yes):
    summary = subprocess.run([sys.executable, find_tool("espefuse.py"), "--chip", "esp32s3", "--port", port, "summary"],
                             capture_output=True, text=True).stdout
    if re.search(r"VDD_SPI_FORCE\s.*=\s*True", summary):
        print("flash-voltage eFuse already burned")
        return True
    print("About to burn the VDD_SPI eFuses (flash fixed at 3.3 V). This is PERMANENT.")
    print("It stops the camera on GPIO45 from picking 1.8 V flash at reset. Only for the 3.3 V-flash module (N16R8).")
    if not yes and input("Type BURN to continue: ").strip() != "BURN":
        print("not burned")
        return False
    r = subprocess.run([sys.executable, find_tool("espefuse.py"), "--chip", "esp32s3", "--port", port, "--do-not-confirm",
                        "set_flash_voltage", "3.3V"])
    return r.returncode == 0


def cmd_setup(a):
    port = find_port(a.port)
    if a.burn_efuse and not burn_efuse(port, a.yes):
        fail("eFuse step not done")
    if a.flash:
        ok, _ = flash(port, a.release or latest_release(a.releases))
        if not ok:
            fail("flashing failed")
        time.sleep(3)
    info = read_info(port, tries=15)
    if not info:
        fail("unit didn't answer on the console")
    cmds = []
    if a.unit:
        cmds.append("unit " + a.unit)
    if a.webpass:
        cmds.append("webpass " + a.webpass)
    if a.netkey:
        cmds.append("netkey " + a.netkey)
    if cmds:
        text = console(port, cmds)
        for line in text.splitlines():
            if a.webpass:
                line = line.replace(a.webpass, "****")
            if a.netkey:
                line = line.replace(a.netkey, "****")
            if line.strip():
                print("  " + line)
    if a.selftest:
        print("running the board self-test (the unit plays a short tone) ...")
        print(console(port, ["selftest"], wait=4.0).strip())
    info = read_info(port, tries=5)
    log_event(a.releases, ["setup", port, info["unit"] if info else "?", "", info["version"] if info else "?", "ok" if info else "NO ANSWER"])
    if info:
        print("unit %s, ip %s, firmware %s" % (info["unit"], info["ip"], info["version"]))


def main():
    p = argparse.ArgumentParser(description="Intercom unit USB tool (updates and first-time setup)")
    p.add_argument("--port", help="serial port, e.g. COM5 (default: the one plugged-in unit)")
    p.add_argument("--releases", default=DEFAULT_RELEASES, help="releases folder (default %(default)s)")
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("release", help="copy the last build into the releases folder")
    r.add_argument("--force", action="store_true", help="overwrite an existing release with the same version")
    sub.add_parser("info", help="show the plugged-in unit's room, IP and firmware version")
    u = sub.add_parser("update", help="flash a release and check the unit comes back with its settings")
    u.add_argument("--release", help="release folder (default: newest)")
    s = sub.add_parser("setup", help="first-time setup of a new unit")
    s.add_argument("--unit", help="room number, e.g. 214 or 150a")
    s.add_argument("--webpass", help="web interface password (8+ characters)")
    s.add_argument("--netkey", help="network command key, shared by the office console (16+ characters)")
    s.add_argument("--flash", action="store_true", help="flash the newest release first")
    s.add_argument("--release", help="release folder for --flash")
    s.add_argument("--burn-efuse", action="store_true", help="burn the flash-voltage eFuse (permanent; see Docs)")
    s.add_argument("--yes", action="store_true", help="don't ask before burning the eFuse")
    s.add_argument("--selftest", action="store_true", help="run the board self-test at the end")
    a = p.parse_args()
    {"release": cmd_release, "info": cmd_info, "update": cmd_update, "setup": cmd_setup}[a.cmd](a)


if __name__ == "__main__":
    main()
