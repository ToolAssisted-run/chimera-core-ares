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

char *getcwd(char *buf, size_t size)
{
	/* The sandbox has one flat namespace; "/" is as true as anything. */
	if (!buf || size < 2) { errno = ERANGE; return nullptr; }
	buf[0] = '/';
	buf[1] = '\0';
	return buf;
}

}  /* extern "C" */
