#!/usr/bin/env bash
# Close only the window role owned by this simulator, never the terminal server.
close_sim_windows() {
    [[ -n "${DISPLAY:-}" ]] || return 0
    local id role path
    while read -r id; do
        [[ -n "$id" ]] || continue
        role="$(xprop -id "$id" WM_WINDOW_ROLE 2>/dev/null | sed -n 's/.*= "\([^"]*\)"/\1/p')"
        [[ "$role" == "$SIM_WINDOW_ROLE" ]] || continue
        path="$(xprop -id "$id" _GTK_WINDOW_OBJECT_PATH 2>/dev/null | sed -n 's/.*= "\([^"]*\)"/\1/p')"
        [[ "$path" == /org/gnome/Terminal/window/* ]] || continue
        gdbus call --session --dest org.gnome.Terminal --object-path "$path" \
            --method org.gtk.Actions.Activate close "[<'window'>]" "{}" >/dev/null 2>&1 || true
    done < <(xwininfo -root -tree 2>/dev/null | awk 'index($0,"(\"gnome-terminal-server\" \"Gnome-terminal\")") {print $1}')
    return 0
}
