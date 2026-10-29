# DEsk v0.1.3 uses the wlroots 0.18 API. New nixpkgs releases have removed
# that version. Keep it private to this flake; never replace the system's
# wlroots used by other compositors with this older version.
{
  lib,
  stdenv,
  fetchFromGitLab,
  meson,
  ninja,
  pkg-config,
  wayland-scanner,
  hwdata,
  libdrm,
  libdisplay-info,
  libliftoff,
  libGL,
  libgbm,
  libcap,
  libinput,
  libxkbcommon,
  pixman,
  seatd,
  wayland,
  wayland-protocols,
  libx11,
  libxcb,
  libxcb-errors,
  libxcb-image,
  libxcb-render-util,
  libxcb-wm,
  lcms2,
  xwayland,
}:
stdenv.mkDerivation {
  pname = "desk-wlroots";
  version = "0.18.3";

  src = fetchFromGitLab {
    domain = "gitlab.freedesktop.org";
    owner = "wlroots";
    repo = "wlroots";
    rev = "0.18.3";
    hash = "sha256-D8RapSeH+5JpTtq+OU8PyVZubLhjcebbCBPuSO5Q7kU=";
  };

  strictDeps = true;
  depsBuildBuild = [ pkg-config ];
  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    wayland-scanner
    hwdata
  ];
  buildInputs = [
    libdrm
    libdisplay-info
    libliftoff
    libGL
    libgbm
    libcap
    libinput
    libxkbcommon
    pixman
    seatd
    wayland
    wayland-protocols
    libx11
    libxcb
    libxcb-errors
    libxcb-image
    libxcb-render-util
    libxcb-wm
    lcms2
    xwayland
  ];

  mesonFlags = [
    "-Dexamples=false"
    "-Dwerror=false"
    "-Dxwayland=enabled"
    "-Dbackends=drm,libinput,x11"
    "-Drenderers=gles2"
    "-Dsession=enabled"
    "-Dcolor-management=enabled"
    "-Dlibliftoff=enabled"
  ];

  meta = {
    description = "Private wlroots 0.18 compatibility library for DEsk";
    homepage = "https://gitlab.freedesktop.org/wlroots/wlroots";
    license = lib.licenses.mit;
    platforms = lib.platforms.linux;
  };
}
