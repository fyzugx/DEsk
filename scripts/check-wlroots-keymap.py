#!/usr/bin/env python3
"""Execute the actual Wayland keymap callback against real xkb/wlroots objects.
Discarding a parent map, retaining an old map, accepting invalid input or leaking
its fd must fail this test. The callback is compiled from production source.
"""
from pathlib import Path
import argparse, os, resource, shlex, subprocess, tempfile
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True, help='wlroots 0.18 source directory')
parser.add_argument('--prefix', type=Path, help='private installed wlroots prefix (optional)')
args = parser.parse_args()
source = args.source.resolve() / 'backend/wayland/seat.c'
src = source.read_text()
start = src.index('static void keyboard_handle_keymap(')
brace = src.index('{', start)
level = 1
end = brace + 1
while level:
    level += (src[end] == '{') - (src[end] == '}')
    end += 1
callback = src[start:end]
harness = r'''
#define _GNU_SOURCE
#define WLR_USE_UNSTABLE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/util/log.h>
CALLBACK
static int fixture_fd(const char *symbol, uint32_t *size) {
    char text[1024];
    snprintf(text, sizeof text,
        "xkb_keymap { xkb_keycodes { minimum=8; maximum=255; <TEST>=8; };"
        " xkb_types { include \"complete\" };"
        " xkb_compatibility { include \"complete\" };"
        " xkb_symbols { key <TEST> { [ %s ] }; }; };", symbol);
    int fd = memfd_create("parent-keymap-test", MFD_CLOEXEC);
    assert(fd >= 0);
    *size = strlen(text) + 1;
    assert(write(fd, text, *size) == *size);
    return fd;
}
static void closed(int fd) {
    errno = 0;
    assert(fcntl(fd, F_GETFD) == -1 && errno == EBADF);
}
int main(void) {
    struct wlr_keyboard keyboard;
    struct wlr_keyboard_impl impl = {.name="keymap-regression"};
    wlr_keyboard_init(&keyboard, &impl, "test");
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    assert(ctx);
    struct xkb_rule_names names = {.layout="us"};
    struct xkb_keymap *initial = xkb_keymap_new_from_names(ctx, &names, 0);
    assert(initial && wlr_keyboard_set_keymap(&keyboard, initial));
    xkb_keymap_unref(initial);
    uint32_t size;
    int fd = fixture_fd("U0041", &size);
    keyboard_handle_keymap(&keyboard, NULL, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
    closed(fd);
    assert(xkb_state_key_get_one_sym(keyboard.xkb_state, 8) == XKB_KEY_A);
    fd = fixture_fd("U03A9", &size);
    keyboard_handle_keymap(&keyboard, NULL, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
    closed(fd);
    assert(xkb_state_key_get_utf32(keyboard.xkb_state, 8) == 0x03A9);
    fd = fixture_fd("U0041", &size);
    keyboard_handle_keymap(&keyboard, NULL, WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP, fd, size);
    closed(fd);
    assert(xkb_state_key_get_utf32(keyboard.xkb_state, 8) == 0x03A9);
    fd = fixture_fd("U0041", &size);
    keyboard_handle_keymap(&keyboard, NULL, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, 0);
    closed(fd);
    assert(xkb_state_key_get_utf32(keyboard.xkb_state, 8) == 0x03A9);
    fd = fixture_fd("U0041", &size);
    assert(ftruncate(fd, 2) == 0);
    keyboard_handle_keymap(&keyboard, NULL, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
    closed(fd);
    assert(xkb_state_key_get_utf32(keyboard.xkb_state, 8) == 0x03A9);
    /* Malformed input is deliberately rejected; keep its expected parser log quiet. */
    setenv("XKB_LOG_LEVEL", "critical", 1);
    fd = memfd_create("invalid-keymap", MFD_CLOEXEC);
    assert(write(fd, "invalid\0", 8) == 8);
    keyboard_handle_keymap(&keyboard, NULL, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, 8);
    closed(fd);
    assert(xkb_state_key_get_utf32(keyboard.xkb_state, 8) == 0x03A9);
    wlr_keyboard_finish(&keyboard);
    xkb_context_unref(ctx);
    puts("PASS: parent maps applied/replaced, invalid maps preserve last map, every fd closed");
}
'''.replace('CALLBACK', callback)
env = dict(os.environ)
if args.prefix:
    prefix = args.prefix.resolve()
    env['PKG_CONFIG_PATH'] = str(prefix / 'lib/pkgconfig') + os.pathsep + env.get('PKG_CONFIG_PATH', '')
    env['LD_LIBRARY_PATH'] = str(prefix / 'lib') + os.pathsep + env.get('LD_LIBRARY_PATH', '')
flags = shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','wlroots-0.18','wayland-client','xkbcommon'],env=env,text=True))
with tempfile.TemporaryDirectory(prefix='desk-keymap-') as tmp:
    path = Path(tmp)
    (path/'test.c').write_text(harness)
    subprocess.run(['cc','-Wall','-Wextra','-Wno-unused-parameter',str(path/'test.c'),'-o',str(path/'test'),*flags],env=env,check=True)
    subprocess.run([str(path/'test')],env=env,check=True)
