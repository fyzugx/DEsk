{ pkgs, desk }:
pkgs.runCommand "desk-headless-smoke-test"
  {
    nativeBuildInputs = [
      desk
      pkgs.coreutils
      pkgs.python3
      pkgs.dbus
    ];
  }
  ''
    export HOME="$TMPDIR/home"
    export XDG_RUNTIME_DIR="$TMPDIR/runtime"
    export XDG_CONFIG_HOME="$HOME/.config"
    export XDG_CACHE_HOME="$HOME/.cache"
    export XDG_DATA_HOME="$HOME/.local/share"
    export XDG_STATE_HOME="$HOME/.local/state"
    export XDG_CONFIG_DIRS="$TMPDIR/config"
    export WLR_BACKENDS=headless
    export WLR_RENDERER=pixman
    export FONTCONFIG_FILE=${
      pkgs.makeFontsConf {
        fontDirectories = [
          pkgs.tex-gyre.pagella
          pkgs.tex-gyre.heros
        ];
      }
    }
    export DESK_LOOK=0
    unset WAYLAND_DISPLAY DISPLAY DBUS_SESSION_BUS_ADDRESS DESK_SESSION
    mkdir -p "$HOME" "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME/desk" "$XDG_CONFIG_DIRS"
    chmod 700 "$XDG_RUNTIME_DIR"
    # Avoid spending the check ray-tracing a full-size room.
    printf 'graphics=2d\n' > "$XDG_CONFIG_HOME/desk/shell.conf"
    mkdir -p "$XDG_DATA_HOME/icons/hicolor/scalable/apps" "$XDG_DATA_HOME/applications"
    cat > "$XDG_DATA_HOME/icons/hicolor/scalable/apps/desk-test.svg" <<'SVG'
    <svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"><rect width="32" height="32" fill="red"/></svg>
    SVG
    cat > "$XDG_DATA_HOME/applications/desk-test.desktop" <<'DESKTOP'
    [Desktop Entry]
    Type=Application
    Name=DEsk icon test
    Exec=${pkgs.coreutils}/bin/true
    Icon=desk-test
    DESKTOP
    mkdir -p "$out"
    timeout 120 desk --headless --size 640x400 \
      --screenshot "$out/desktop.png" --delay 3000 --exit-after-screenshot \
      > "$out/session.log" 2>&1 &
    job=$!
    trap 'kill "$job" 2>/dev/null || true' EXIT
    for attempt in $(seq 1 100); do
      grep -q 'DEsk running on WAYLAND_DISPLAY=' "$out/session.log" && break
      sleep 0.1
    done
    pid=$(head -n 1 "$XDG_RUNTIME_DIR/desk.lock")
    test "$(cat "/proc/$pid/comm")" = desk
    desk-ctl reset
    wait "$job" || { cat "$out/session.log"; exit 1; }
    trap - EXIT
    python3 - "$out/desktop.png" <<'PY'
    import struct
    import sys
    with open(sys.argv[1], "rb") as image:
        header = image.read(24)
    assert header[:8] == b"\x89PNG\r\n\x1a\n", "Missing PNG screenshot"
    assert struct.unpack(">II", header[16:24]) == (640, 400), "Wrong screenshot size"
    PY
    grep -F 'capturing screenshot to' "$out/session.log"
    test -n "$(find "$XDG_CACHE_HOME/desk/icons" -name '*.png' -print -quit)"

    # Run the installed login wrapper on a private bus; never contact the
    # real user's bus or systemd session while checking its exit path.
    unset WLR_BACKENDS
    XDG_CURRENT_DESKTOP=KDE DESK_LOG=- dbus-run-session \
      --config-file=${pkgs.dbus}/share/dbus-1/session.conf -- \
      timeout 120 desk-session --headless --size 640x400 \
        --screenshot "$out/session-desktop.png" --delay 1000 --exit-after-screenshot \
        > "$out/login-session.log" 2>&1 || { cat "$out/login-session.log"; exit 1; }
    test -s "$out/session-desktop.png"
    grep -F 'D-Bus and systemd told where the display is' "$out/login-session.log"
  ''
