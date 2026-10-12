# $1 = window title. Typing waits for the window to exist and have focus.
WID=$(xdotool search --sync --name "^$1$" | head -1)
xdotool windowfocus --sync "$WID"
xdotool mousemove --window "$WID" 50 50
xdotool type --delay 40 'Hello, World! 123 #$x'
xdotool key BackSpace BackSpace
xdotool type --delay 40 'ok'
xdotool click 4 click 4 click 4 click 5
sleep 0.3
xdotool key Return
