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
$console = Join-Path $PSScriptRoot "build\intercom-console.exe"
function Start-Console([string]$extra) {
    # the office console as the right-hand window (800 x 720)
    Start-Process -FilePath $console -ArgumentList ("--key $key --test-voice --pos 800,0,800,720 " + $extra) -WindowStyle Normal | Out-Null
}
$edge = "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
$edgeProfile = Join-Path $tmp "edge-profile"
function Start-Page([string]$url, [string]$geom, [string]$extra) {
    # an Edge app window (own throw-away profile, so the user's Edge is not touched); geom = "x,y,w,h"
    # The window is made 60 px taller than asked so nothing behind it (desktop, taskbar) shows in the
    # recorded strip; --test-type hides Edge's warning bar about the test flags.
    $g = $geom.Split(",")
    $h = [int]$g[3] + 60
    Start-Process -FilePath $edge -ArgumentList ("--app=`"$url`" --user-data-dir=`"$edgeProfile`" --no-first-run --test-type " +
        "--window-position=$($g[0]),$($g[1]) --window-size=$($g[2]),$h " + $extra) | Out-Null
}
function Stop-Pages {
    # only the Edge processes started with the throw-away profile
    Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" | Where-Object { $_.CommandLine -like "*edge-profile*" } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}
function Send-Cmd([string]$cmd) {
    Start-Process -FilePath $sim -ArgumentList ("--send `"$cmd`" --key $key") -WindowStyle Hidden -Wait | Out-Null
}
function Caption-Filter($captions) {
    $parts = @("pad=1600:800:0:0:color=0x202020")
    foreach ($c in $captions) {
        $t = $c[2].Replace("'", [string][char]0x2019).Replace(":", "\:").Replace(",", "\,")  # char code: this file is read as ANSI by PowerShell 5
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

# ---- How-to videos (firmware R2) ----------------------------------------------------------------

# 06: the office calls one room; the room answers only the office
Record-Scene "06-office-calls-a-room" 26 @(
    @(0, 6, "Room 214 (left) and the office console (right). The unit starts with its light test."),
    @(6, 11, "The office selects 214 and holds Call selected room: 214 chimes, shows CAL and its CALL light blinks."),
    @(11, 15, "Only 214 hears the call: room 101 (not shown) stays quiet. The calls list shows the call."),
    @(15, 21, "Within 10 s the teacher holds TALK: the answer goes to the office only, not to the whole school."),
    @(21, 26, "The calls list says 'answered by 214'. Every call is also saved to intercom-calls.csv.")
) {
    Start-Unit "214" "--pos 0,0 --auto-talk 15,4 --quit-after 26"
    Start-Unit "101" "--headless --quit-after 26"
    Start-Console "--rooms 101,214 --auto-talk 7,4,214 --quit-after 26"
}

# 07: a room calls the office
Record-Scene "07-calling-the-office" 26 @(
    @(0, 6, "Room 214 (left) needs the office, but not the whole school."),
    @(6, 10, "The teacher holds DOWN for 2 seconds: the display alternates oFC and the room number."),
    @(10, 15, "The office console beeps and shows 214 in blue: CALLING THE OFFICE. Nobody else hears anything."),
    @(15, 21, "The office calls 214 back (select it, hold 'Call selected room'): that answers the call."),
    @(21, 26, "The calls list: '214 -> office, answered by the office after 11 s'. Unanswered calls show MISSED in red.")
) {
    Start-Unit "214" "--pos 0,0 --press D@6.5+2.3 --quit-after 26"
    Start-Console "--rooms 214 --auto-talk 16,4,214 --quit-after 26"
}

# 08: setting the room number on the unit
Record-Scene "08-setting-the-room-number" 25 @(
    @(0, 6, "A replacement unit, still set to 101 (after its light test). It has to become room 305."),
    @(6, 10, "With the front cover off, hold the hidden SETUP button for 3 seconds: the first digit blinks."),
    @(10, 15, "UP / DOWN change the blinking digit (1 -> 3); MODE moves to the next digit (0 stays)."),
    @(15, 20, "UP four times on the last digit (1 -> 5), then MODE on the last digit saves."),
    @(20, 25, "The unit now is 305 (IP 10.42.3.5). The number stays through power cuts. Put the cover back.")
) {
    Start-Unit "101" ("--pos 0,0 --quit-after 25 --press " +
        "S@6+3.3,U@10.5+0.25,U@11.3+0.25,M@12.3+0.25,M@13.6+0.25,U@15+0.2,U@15.6+0.2,U@16.2+0.2,U@16.8+0.2,M@18+0.25")
    Start-Unit "214" "--pos 800,0 --quit-after 25"
}

# 09: the power-up light test
Record-Scene "09-power-up-light-test" 14 @(
    @(0, 2, "Two units powering up. Each one tests every light it has, in about 4 seconds."),
    @(2, 5, "Every display segment in turn, then all on (8.8.8.): a segment that never lights is broken."),
    @(5, 8, "Then L1 white lamp, L2 IR lamp, L3 call light, L4-L6 the RGB light red / green / blue (side lights)."),
    @(8, 14, "Then the room number. boottest run repeats the test; boottest off turns it off.")
) {
    Start-Unit "214" "--pos 0,0 --quit-after 14"
    Start-Sleep -Milliseconds 700
    Start-Unit "101" "--pos 800,0 --quit-after 13"
}

# 10: the school status page (web console) on the office PC
Record-Scene "10-school-status-page" 30 @(
    @(0, 7, "The office PC's browser shows the school page of the office unit: every unit, floor by floor, every 2 s."),
    @(7, 13, "Yellow = talking: room 102 holds TALK."),
    @(13, 19, "Blue = a room calling the office: the teacher in 214 held DOWN for 2 seconds."),
    @(19, 25, "Red = offline: gym unit 298 stopped answering; the card says when it was last heard."),
    @(25, 30, "Click a room to call it, send it commands, or set timed events in the panels below.")
) {
    Start-Unit "101" "--headless --web-port 8101 --web-pass video-demo-pass --quit-after 31"
    Start-Sleep -Seconds 1   # the page needs 101's web server; the first 2.5 s are cut anyway
    Start-Page "http://admin:video-demo-pass@localhost:8101/school" "0,0,1600,720" ""
    Start-Unit "102" "--headless --auto-talk 8,6 --quit-after 31"
    Start-Unit "214" "--headless --press D@13+2.3 --quit-after 31"
    Start-Unit "205" "--headless --battery --no-poe --quit-after 31"
    Start-Unit "299" "--headless --quit-after 31"
    Start-Unit "298" "--headless --quit-after 14"
    Start-Sleep -Seconds 28
    Stop-Pages
}

# 11: talking to the school from the office PC's microphone
Record-Scene "11-talking-from-the-pc" 22 @(
    @(0, 5, "Left: the school page on the office PC. Right: room 214."),
    @(5, 9, "Hold the Talk button (or the space bar) and speak: the meter shows the PC microphone's level."),
    @(9, 14, "Room 214 hears the office (RX light, speaker meter); so does every other room."),
    @(14, 22, "Pick a room or a floor in the list (R2) to call just them. Answers come back on the office unit.")
) {
    Start-Unit "101" "--headless --web-port 8101 --web-pass video-demo-pass --quit-after 23"
    Start-Unit "214" "--pos 800,0 --quit-after 23"
    Start-Sleep -Seconds 1
    # the page loads when the office unit is up; it talks from 1.5 s to 10.5 s after loading
    Start-Page "http://admin:video-demo-pass@localhost:8101/school#test-talk-9" "0,0,800,720" "--use-fake-device-for-media-stream --use-fake-ui-for-media-stream"
    Start-Sleep -Seconds 21
    Stop-Pages
}
# 12: the attention lights when the office calls and nobody answers
Record-Scene "12-call-lights" 33 @(
    @(0, 6, "Room 214 (left) and the office console (right). Watch the CALL, RGB and WHT lights under the display."),
    @(6, 11, "The office calls 214: the white lamp, the blue RGB light and the call light blink together; CAL flashes."),
    @(11, 20, "Nobody answers. The lights keep blinking for the 10 seconds TALK would still answer the office."),
    @(20, 27, "Missed call: a short blink every 2 seconds and CAL alternating with 214, for up to 5 minutes."),
    @(27, 33, "The teacher presses any button: the missed call is cleared. Holding DOWN 2 s would call the office back.")
) {
    Start-Unit "214" "--pos 0,0 --press M@27+0.3 --quit-after 33"
    Start-Console "--rooms 214 --auto-talk 6,4,214 --quit-after 33"
}

Write-Host "done: $out"
