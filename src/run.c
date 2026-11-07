/* A program run for what it prints (run.h). */

/* pipe2 and posix_spawn's company are glibc's and the BSDs' */
#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include "run.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

unsigned char *desk_run_capture(char *const argv[], size_t max, size_t *len, bool *ok)
{
	*len = 0;
	if (ok) {
		*ok = false;
	}
	/* close-on-exec from the start: another thread that forks a program in
	 * the moment between pipe and fcntl would keep the write end open, and the
	 * read below would not end until that program did */
	int fd[2];
	if (pipe2(fd, O_CLOEXEC) < 0) {
		return NULL;
	}
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_adddup2(&fa, fd[1], STDOUT_FILENO);
	posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
	posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
	/* not the compositor's signal mask: the child's own */
	posix_spawnattr_t at;
	posix_spawnattr_init(&at);
	sigset_t none;
	sigemptyset(&none);
	posix_spawnattr_setsigmask(&at, &none);
	posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGMASK);
	pid_t pid;
	int err = posix_spawnp(&pid, argv[0], &fa, &at, argv, environ);
	posix_spawn_file_actions_destroy(&fa);
	posix_spawnattr_destroy(&at);
	close(fd[1]);
	if (err != 0) {
		close(fd[0]);
		return NULL;
	}
	/* grown as it comes, so that a program that prints a little costs a
	 * little, whatever max allows */
	unsigned char *buf = NULL;
	size_t cap = 0, got = 0;
	bool whole = false;
	for (;;) {
		if (got == cap && cap < max) {
			size_t more = cap ? cap * 2 : 65536;
			more = more > max ? max : more;
			unsigned char *grown = realloc(buf, more + 1);
			if (!grown) {
				break;
			}
			buf = grown;
			cap = more;
		}
		/* at max, one more byte is read to see whether that was all */
		unsigned char spare;
		size_t room = cap - got;
		ssize_t r = read(fd[0], room ? buf + got : &spare, room ? room : 1);
		if (r < 0 && errno == EINTR) {
			continue;
		}
		if (r < 0) {
			break;
		}
		if (r == 0) {
			whole = true;
			break;
		}
		if (room == 0) {
			break;
		}
		got += (size_t)r;
	}
	close(fd[0]);
	int st = 0;
	pid_t w;
	while ((w = waitpid(pid, &st, 0)) < 0 && errno == EINTR) {
		;
	}
	if (!buf) {
		buf = malloc(1);
		if (!buf) {
			return NULL;
		}
	}
	buf[got] = '\0';
	*len = got;
	if (ok) {
		*ok = whole && w == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0;
	}
	return buf;
}
