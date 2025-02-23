#define _POSIX_C_SOURCE 200809L

#include "minitalk.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int	validate_runtime_dir(const char *path)
{
	struct stat	info;

	if (lstat(path, &info) == -1)
		return (-1);
	if (!S_ISDIR(info.st_mode) || info.st_uid != getuid()
		|| (info.st_mode & 077) != 0)
	{
		errno = EACCES;
		return (-1);
	}
	return (0);
}

int	mt_runtime_dir(char *buffer, size_t size)
{
	int	length;

	if (buffer == NULL || size == 0)
	{
		errno = EINVAL;
		return (-1);
	}
	length = snprintf(buffer, size, "/tmp/signal-message-bus-%lu",
		(unsigned long)getuid());
	if (length < 0 || (size_t)length >= size)
	{
		errno = ENAMETOOLONG;
		return (-1);
	}
	if (mkdir(buffer, 0700) == -1 && errno != EEXIST)
		return (-1);
	return (validate_runtime_dir(buffer));
}

int	mt_response_path(char *buffer, size_t size, const char *role, pid_t pid)
{
	char	directory[MT_RESPONSE_PATH_SIZE];
	int		length;

	if (buffer == NULL || role == NULL || pid <= 1
		|| mt_runtime_dir(directory, sizeof(directory)) == -1)
		return (-1);
	if (strcmp(role, "server") != 0 && strcmp(role, "client") != 0
		&& strcmp(role, "forger") != 0)
	{
		errno = EINVAL;
		return (-1);
	}
	length = snprintf(buffer, size, "%s/%s-%ld.sock", directory, role,
		(long)pid);
	if (length < 0 || (size_t)length >= size)
	{
		errno = ENAMETOOLONG;
		return (-1);
	}
	return (0);
}
