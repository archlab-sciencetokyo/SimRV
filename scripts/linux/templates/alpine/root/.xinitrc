#!/bin/sh
export DISPLAY=:0
export XAUTHORITY=/root/.Xauthority
xhost + 2>/dev/null || true
xterm -fn 6x13 -geometry 60x18+10+10 -bg '#1e293b' -fg '#f8fafc' -title "SimRV Terminal" &
exec jwm
