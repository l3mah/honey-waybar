#!/usr/bin/env bash
# w3ld-gamma: waybar night-light control driving w3ld's integrated gamma tool
# (`w3ldctl gamma <temp> [brightness]`), the port of the hyprsunset script.
#
# Off = neutral (w3ldctl gamma off). On = a warm temperature at a brightness the
# scroll wheel adjusts. State is a percentage (5-100) mapped to w3ld's 0-1
# brightness. Waybar module usage: exec this for status, on-click toggle,
# on-scroll-up/down gamma-up/gamma-down.

MODE_FILE="/tmp/w3ld-gamma.mode"
GAMMA_FILE="/tmp/w3ld-gamma.gamma"

MIN_GAMMA=5
MAX_GAMMA=100
STEP=5

ON_TEMP=5000
ON_DEFAULT_GAMMA=60

get_mode() {
	[[ -f "$MODE_FILE" ]] && cat "$MODE_FILE" || echo "off"
}

set_mode() {
	echo "$1" > "$MODE_FILE"
}

get_gamma() {
	[[ -f "$GAMMA_FILE" ]] && cat "$GAMMA_FILE" || echo "$ON_DEFAULT_GAMMA"
}

set_gamma() {
	echo "$1" > "$GAMMA_FILE"
}

clamp_gamma() {
	local gamma=$1
	if (( gamma > MAX_GAMMA )); then gamma=$MAX_GAMMA; fi
	if (( gamma < MIN_GAMMA )); then gamma=$MIN_GAMMA; fi
	echo "$gamma"
}

# Apply a warm temperature at gamma% brightness (w3ld brightness is 0.0-1.0).
apply_on() {
	local gamma=$1
	local brightness
	brightness=$(awk "BEGIN { printf \"%.2f\", $gamma / 100 }")
	w3ldctl gamma "$ON_TEMP" "$brightness"
}

apply_off() {
	w3ldctl gamma off
}

toggle() {
	if [[ "$(get_mode)" == "on" ]]; then
		apply_off
		set_mode "off"
	else
		local g
		g=$(clamp_gamma "$(get_gamma)")
		apply_on "$g"
		set_mode "on"
		set_gamma "$g"
	fi
}

gamma_step() {
	[[ "$(get_mode)" == "off" ]] && return
	local g
	g=$(clamp_gamma "$(( $(get_gamma) + $1 ))")
	apply_on "$g"
	set_gamma "$g"
}

status() {
	if [[ "$(get_mode)" == "off" ]]; then
		echo '{"text":"off","class":"off"}'
	else
		echo "{\"text\":\"$(get_gamma)%\",\"class\":\"on\"}"
	fi
}

init() {
	echo "off" > "$MODE_FILE"
	echo "$ON_DEFAULT_GAMMA" > "$GAMMA_FILE"
	apply_off
}

case "$1" in
	init)       init ;;
	toggle)     toggle ;;
	gamma-up)   gamma_step "$STEP" ;;
	gamma-down) gamma_step "-$STEP" ;;
	*)          status ;;
esac
