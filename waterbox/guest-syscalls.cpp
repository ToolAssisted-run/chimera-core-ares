/* Guest-only overrides for libc calls whose syscalls the miniBox surface
 * rejects, by design: a core has files mounted by name and no host filesystem
 * under them. Because the guest is one static link, defining these here means
 * musl's versions are never pulled in.
 *
 * Everything reports a read-only, empty filesystem. That is exactly what ares
 * and mia are looking at: mia hunts for a settings file and a place to write
 * saves, finds neither, and carries on with the paks it was handed - which is
 * all this core ever wanted from it.
 */
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern "C" {

int mkdir(const char *path, mode_t mode) { errno = EROFS; return -1; }
int rmdir(const char *path) { errno = EROFS; return -1; }
int unlink(const char *path) { errno = EROFS; return -1; }
int rename(const char *oldpath, const char *newpath) { errno = EROFS; return -1; }
int chmod(const char *path, mode_t mode) { errno = EROFS; return -1; }
int symlink(const char *target, const char *linkpath) { errno = EROFS; return -1; }

ssize_t readlink(const char *path, char *buf, size_t size) { errno = EINVAL; return -1; }

/* What exists, in a core, is what the host mounted - so the honest answer is
 * whether the name opens. mia asks this twice over: once hunting for its game
 * databases (`Database/Famicom.bml` and friends), which live on a disk a core
 * has not got, and once to check the cartridge it was just handed. Answering a
 * flat "no" satisfies the first and breaks the second.
 *
 * musl's access() is a syscall the sandbox does not answer at all, so without
 * something here the guest traps on the question rather than hearing an answer.
 */
int access(const char *path, int mode)
{
	if (path == nullptr) { errno = EFAULT; return -1; }
	FILE *f = fopen(path, "rb");
	if (f == nullptr) { errno = ENOENT; return -1; }
	fclose(f);
	/* Everything a core can see is readable and nothing is writable or
	 * executable, which is what W_OK and X_OK are being asked about. */
	if (mode & (W_OK | X_OK)) { errno = EACCES; return -1; }
	return 0;
}

int faccessat(int fd, const char *path, int mode, int flags) { return access(path, mode); }

char *getcwd(char *buf, size_t size)
{
	/* The sandbox has one flat namespace; "/" is as true as anything. */
	if (!buf || size < 2) { errno = ERANGE; return nullptr; }
	buf[0] = '/';
	buf[1] = '\0';
	return buf;
}

}  /* extern "C" */
