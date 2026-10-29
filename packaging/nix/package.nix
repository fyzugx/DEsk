{
  lib,
  stdenv,
  meson,
  ninja,
  pkg-config,
  python3,
  wayland-scanner,
  makeWrapper,
  desktop-file-utils,
  wlroots,
  cairo,
  fontconfig,
  pixman,
  libGL,
  libgbm,
  wayland,
  wayland-protocols,
  libxkbcommon,
  libdrm,
  systemd,
  libinput,
  libpulseaudio,
  alsa-lib,
  pam,
  libxcb,
  libxcb-wm,
  bash,
  coreutils,
  curl,
  dbus,
  dconf,
  glib,
  gawk,
  gnugrep,
  gnused,
  grim,
  librsvg,
  libnotify,
  tzdata,
  wireplumber,
  wl-clipboard,
  xdg-user-dirs,
  xdg-utils,
  xwayland,
  gsettings-desktop-schemas,
  mate-polkit,
  zenity,
  pciutils,
}:
let
  runtimePath = lib.makeBinPath [
    coreutils
    curl
    dbus
    glib
    gawk
    gnugrep
    gnused
    grim
    librsvg
    libnotify
    systemd
    wireplumber
    wl-clipboard
    xdg-user-dirs
    xdg-utils
    xwayland
    zenity
    pciutils
  ];
in
stdenv.mkDerivation (finalAttrs: {
  pname = "desk";
  version = "0.1.3";

  # Build the source alongside this packaging, not a separate release download.
  src = lib.cleanSourceWith {
    src = ../..;
    filter = path: type: lib.cleanSourceFilter path type && baseNameOf path != "packaging";
  };

  strictDeps = true;
  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    python3
    wayland-scanner
    makeWrapper
  ];
  nativeInstallCheckInputs = [ desktop-file-utils ];
  buildInputs = [
    wlroots
    cairo
    fontconfig
    pixman
    libGL
    libgbm
    wayland
    wayland-protocols
    libxkbcommon
    libdrm
    systemd
    libinput
    libpulseaudio
    pam
    libxcb
    libxcb-wm
  ];

  postPatch = ''
    # A derivation cannot write to the host's /etc. The NixOS module installs
    # portal configuration and creates the PAM service declaratively.
    substituteInPlace meson.build \
      --replace-fail "install_dir: '/etc/xdg/xdg-desktop-portal-wlr'" \
        "install_dir: get_option('datadir') / 'desk/xdg-desktop-portal-wlr'" \
      --replace-fail "install_dir: '/etc/pam.d'" \
        "install_dir: get_option('datadir') / 'desk/pam'"

    substituteInPlace src/wm.c \
      --replace-fail '"/bin/sh"' '"${lib.getExe bash}"' \
      --replace-fail /usr/bin/Xwayland ${xwayland}/bin/Xwayland \
      --replace-fail '"/usr/bin/grim"' '"${lib.getExe grim}"' \
      --replace-fail '"/bin/grim"' '"${lib.getExe grim}"'
    substituteInPlace src/panel.c \
      --replace-fail /usr/bin/wpctl ${wireplumber}/bin/wpctl
    substituteInPlace src/apps.c \
      --replace-fail /usr/bin/rsvg-convert ${librsvg}/bin/rsvg-convert
    substituteInPlace src/occasion.c \
      --replace-fail /usr/share/zoneinfo ${tzdata}/share/zoneinfo
    substituteInPlace src/sound.c \
      --replace-fail '"libasound.so.2"' '"${lib.getLib alsa-lib}/lib/libasound.so.2"'
    substituteInPlace scripts/desk-session-helper \
      --replace-fail /usr/libexec/polkit-mate-authentication-agent-1 \
        ${mate-polkit}/libexec/polkit-mate-authentication-agent-1
    substituteInPlace scripts/desk-report \
      --replace-fail 'pkg_version xdg-desktop-portal-wlr' \
        'printf "xdg-desktop-portal-wlr: provided by the NixOS configuration\\n"'

    # Upstream still has system icon fallbacks outside XDG_DATA_DIRS.
    substituteInPlace src/apps.c \
      --replace-fail /usr/local/share/icons "$out/share/icons" \
      --replace-fail /usr/share/icons /run/current-system/sw/share/icons \
      --replace-fail /usr/local/share/pixmaps "$out/share/pixmaps" \
      --replace-fail /usr/share/pixmaps /run/current-system/sw/share/pixmaps

    substituteInPlace session/desk.desktop \
      --replace-fail Exec=desk-session Exec=$out/bin/desk-session
    substituteInPlace session/desk-window.desktop \
      --replace-fail Exec=desk-window Exec=$out/bin/desk-window
    # A TTY may inherit another desktop's name from the user's environment.
    substituteInPlace session/desk-session \
      --replace-fail 'export XDG_CURRENT_DESKTOP="''${XDG_CURRENT_DESKTOP:-DEsk}"' \
        'export XDG_CURRENT_DESKTOP=DEsk'
  '';

  mesonBuildType = "release";
  mesonFlags = [
    "-Dxwayland=enabled"
    "-Dpost=enabled"
    "-Dsound=enabled"
  ];
  doCheck = true;

  postInstall = ''
    # desk-ctl verifies /proc/$pid/comm == desk. Keep the real compositor's
    # basename unchanged instead of renaming it to .desk-wrapped.
    mkdir -p "$out/libexec"
    mv "$out/bin/desk" "$out/libexec/desk"
    for program in "$out"/bin/*; do
      wrapProgram "$program" \
        --prefix PATH : "$out/bin" \
        --suffix PATH : "${runtimePath}" \
        --prefix XDG_DATA_DIRS : "$out/share:${gsettings-desktop-schemas}/share/gsettings-schemas/${gsettings-desktop-schemas.name}:/run/current-system/sw/share:/usr/local/share:/usr/share" \
        --prefix GIO_EXTRA_MODULES : "${lib.getLib dconf}/lib/gio/modules" \
        --set-default WLR_XWAYLAND "${xwayland}/bin/Xwayland"
    done
    makeWrapper "$out/libexec/desk" "$out/bin/desk" \
      --prefix PATH : "$out/bin" \
      --suffix PATH : "${runtimePath}" \
      --prefix XDG_DATA_DIRS : "$out/share:${gsettings-desktop-schemas}/share/gsettings-schemas/${gsettings-desktop-schemas.name}:/run/current-system/sw/share:/usr/local/share:/usr/share" \
      --prefix GIO_EXTRA_MODULES : "${lib.getLib dconf}/lib/gio/modules" \
      --set-default WLR_XWAYLAND "${xwayland}/bin/Xwayland"
  '';

  doInstallCheck = true;
  installCheckPhase = ''
    runHook preInstallCheck
    "$out/bin/desk" --version | grep -F 'DEsk v${finalAttrs.version}'
    # DesktopNames is a display-manager extension, not an application key
    # accepted by desktop-file-validate. Retain it in the installed session.
    sed '/^DesktopNames=/d' "$out/share/wayland-sessions/desk.desktop" > "$TMPDIR/desk-session.desktop"
    desktop-file-validate "$TMPDIR/desk-session.desktop"
    grep -Fx "Exec=$out/bin/desk-session" "$out/share/wayland-sessions/desk.desktop"
    grep -Fx "TryExec=$out/bin/desk-session" "$out/share/wayland-sessions/desk.desktop"
    for entry in "$out"/share/applications/*.desktop; do
      # DEsk is a new desktop identifier, not in the validator's registry.
      # Keep OnlyShowIn=DEsk in the real entry so other desktops hide it.
      sed '/^OnlyShowIn=DEsk;$/d' "$entry" > "$TMPDIR/$(basename "$entry")"
      desktop-file-validate "$TMPDIR/$(basename "$entry")"
    done
    test -f "$out/share/desk/xdg-desktop-portal-wlr/DEsk"
    grep -Fx 'chooser_type=dmenu' "$out/share/desk/xdg-desktop-portal-wlr/DEsk"
    grep -Fx "chooser_cmd=$out/bin/desk-screen-chooser" "$out/share/desk/xdg-desktop-portal-wlr/DEsk"
    test -x "$out/bin/desk-screen-chooser"
    test -x "$out/bin/desk-report"
    test "$(printf 'HEADLESS-1\n' | "$out/bin/desk-screen-chooser")" = HEADLESS-1
    "$out/bin/desk-report" --help | grep -F desk-report
    test -f "$out/share/desk/pam/desk"
    test -x "${mate-polkit}/libexec/polkit-mate-authentication-agent-1"
    runHook postInstallCheck
  '';

  passthru.providedSessions = [ "desk" ];

  meta = {
    description = "Skeuomorphic Wayland desktop with a ray-traced desk";
    homepage = "https://github.com/fyzugx/DEsk";
    changelog = "https://github.com/fyzugx/DEsk/blob/v${finalAttrs.version}/RELEASE-NOTES.md";
    license = lib.licenses.mit;
    platforms = lib.platforms.linux;
    mainProgram = "desk-window";
  };
})
