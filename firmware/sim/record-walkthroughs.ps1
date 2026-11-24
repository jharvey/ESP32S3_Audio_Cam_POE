# Record the simulator walkthrough videos (Docs/videos/*.mp4).
#   powershell -ExecutionPolicy Bypass -File firmware\sim\record-walkthroughs.ps1 [-Only 01-all-call]
# Needs: the simulator built (build-sim.bat) and ffmpeg on the PATH. Each scene starts two unit windows side by
# side at the top-left of the screen (800 x 720 each) plus any headless units, records only that 1600 x 720 area,
# then adds a caption bar underneath. Don't use the PC while it records: anything that covers the windows is
# recorded. The units exit by themselves (--quit-after).
param([string]$Only = "")
$ErrorActionPreference = "Stop"
$sim = Join-Path $PSScriptRoot "build\intercom-sim.exe"
$out = Join-Path $PSScriptRoot "..\..\Docs\videos"
$tmp = Join-Path $env:TEMP "intercom-walkthroughs"
New-Item -ItemType Directory -Force -Path $out, $tmp | Out-Null
if (-not (Test-Path $sim)) { throw "build the simulator first (firmware\sim\build-sim.bat)" }
$key = "intercom-demo-key"
$font = "C\:/Windows/Fonts/arial.ttf"

function Start-Unit([string]$name, [string]$extra) {
    Start-Process -FilePath $sim -ArgumentList ("--name $name --fresh --net-key $key " + $extra) -WindowStyle Normal | Out-Null
}
function Send-Cmd([string]$cmd) {
    Start-Process -FilePath $sim -ArgumentList ("--send `"$cmd`" --key $key") -WindowStyle Hidden -Wait | Out-Null
}
function Caption-Filter($captions) {
    $parts = @("pad=1600:800:0:0:color=0x202020")
    foreach ($c in $captions) {
        $t = $c[2].Replace("'", "’").Replace(":", "\:").Replace(",", "\,")
        $parts += "drawtext=fontfile='$font':text='$t':x=24:y=742:fontsize=26:fontcolor=white:enable='between(t\,$($c[0])\,$($c[1]))'"
    }
    return ($parts -join ",")
}
function Record-Scene([string]$id, [int]$seconds, $captions, [scriptblock]$actions) {
    if ($Only -and $Only -ne $id) { return }
    Write-Host "== $id ($seconds s)"
    $raw = Join-Path $tmp "$id-raw.mp4"
    $ff = Start-Process -FilePath "ffmpeg" -PassThru -WindowStyle Hidden -ArgumentList @(
        "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "15", "-offset_x", "0", "-offset_y", "0",
        "-video_size", "1600x720", "-draw_mouse", "0", "-i", "desktop", "-t", "$seconds", "-c:v", "libx264",
        "-preset", "veryfast", "-crf", "20", $raw)
    & $actions
    $ff.WaitForExit()
    $vf = Caption-Filter $captions
    # PRIVACY: the windows take a moment to open and close by themselves at the end, and anything behind
    # them (the user's desktop, other apps) would be recorded. Keep only the part where they are up:
    # drop the first $lead s and stop $tail s before the end. Caption times stay in recording time.
    $lead = 2.5; $tail = 1.5
    $keep = [string]::Format([Globalization.CultureInfo]::InvariantCulture, "{0:0.0}", $seconds - $lead - $tail)
    & ffmpeg -y -loglevel error -i $raw -vf $vf -ss $lead -t $keep -c:v libx264 -crf 30 -pix_fmt yuv420p -movflags +faststart (Join-Path $out "$id.mp4")
    Start-Sleep -Seconds 2
}

# 01: all-call, several talkers mixed
Record-Scene "01-all-call" 22 @(
    @(0, 5, "Two classroom units on the network: room 101 (10.42.1.1) and room 214 (10.42.2.14)."),
    @(5, 11, "101 holds TALK: 214 hears it at once (RX light, speaker meter). About 100 ms mouth to ear."),
    @(11, 17, "214 and a third room (205, not shown) talk together: every unit mixes them; up to 4 talkers."),
    @(17, 22, "Audio is Opus-compressed: about 24 kbit/s per talker instead of 256 kbit/s raw.")
) {
    Start-Unit "101" "--pos 0,0 --auto-talk 5,5 --quit-after 22"
    Start-Unit "214" "--pos 800,0 --auto-talk 11,5 --quit-after 21"
    Start-Unit "205" "--headless --auto-talk 11,5 --quit-after 21"
}

# 02: gym units in one room group
Record-Scene "02-gym-group" 24 @(
    @(0, 6, "Two gym units, 299 and 298, joined in room group 299 (group 299 sent from the office console)."),
    @(6, 12, "299 talks: 298 stays quiet (same room, it would only feed back); the rest of the school hears it."),
    @(12, 18, "Room 101 (not shown) talks: both gym units play it."),
    @(18, 24, "Without the group, two gym speakers would play each other's voice back into the room.")
) {
    Start-Unit "299" "--pos 0,0 --echo gym --auto-talk 6,6 --quit-after 24"
    Start-Unit "298" "--pos 800,0 --echo gym --quit-after 23"
    Start-Unit "101" "--headless --auto-talk 12,6 --quit-after 23"
    Start-Sleep -Seconds 3
    Send-Cmd "299 group 299"
    Send-Cmd "298 group 299"
}

# 03: duplicate room number
Record-Scene "03-duplicate-number" 16 @(
    @(0, 5, "A replacement unit is set to 214, but the old 214 is still on the network."),
    @(5, 16, "Each unit sends an ID from its MAC: both see another 214 and alternate dUP with the number.")
) {
    Start-Unit "214" "--pos 0,0 --quit-after 16"
    Start-Sleep -Seconds 1
    Start-Unit "214" "--pos 800,0 --quit-after 15"
}

# 04: auto-tune of the echo canceller
Record-Scene "04-auto-tune" 24 @(
    @(0, 4, "Room 214 after install. The office starts auto-tune (cal); the same works on USB or by holding UP 5 s."),
    @(4, 14, "The unit plays a short noise burst and measures delay, reverb and echo: the display counts c00 to c99."),
    @(14, 24, "Result: e = echo reduced by that many dB; settings saved. tune get / tune set copy them to other rooms.")
) {
    Start-Unit "214" "--pos 0,0 --quit-after 24"
    Start-Unit "101" "--pos 800,0 --quit-after 23"
    Start-Sleep -Seconds 4
    Send-Cmd "214 cal"
}

# 05: portable unit on Wi-Fi and battery
Record-Scene "05-portable-unit" 20 @(
    @(0, 6, "A portable unit (999) on battery, off its PoE cable: it uses Wi-Fi with the same IP."),
    @(6, 14, "Room 101 talks: the portable unit still hears it over Wi-Fi."),
    @(14, 20, "Plugged back in, it moves back to Ethernet within 200 ms; a low battery shows Lo.")
) {
    Start-Unit "999" "--pos 0,0 --battery --no-poe --quit-after 20"
    Start-Unit "101" "--pos 800,0 --auto-talk 6,6 --quit-after 19"
}
Write-Host "done: $out"
