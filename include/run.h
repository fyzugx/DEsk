#pragma once

#include <stdbool.h>
#include <stddef.h>

/* A program run for what it prints (run.c).
 *
 * What argv writes to its standard output, at most max bytes, in a buffer to
 * free() with a NUL after it; NULL if the program could not be run. Its
 * standard input and its words go nowhere, it is looked for on PATH, and it
 * runs with the signal mask of a program of its own, not the one of the
 * thread that asked. *len is how many bytes there are. *ok (if given) is true
 * when that was all of it, not cut short at max, and the program exited 0.
 *
 * It waits for the program, so it is for a thread of its own. */
unsigned char *desk_run_capture(char *const argv[], size_t max, size_t *len, bool *ok);
