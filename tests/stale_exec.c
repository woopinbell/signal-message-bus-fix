#define _POSIX_C_SOURCE 200809L

#include "minitalk.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static int	create_stale_socket(const char *path)
{
	struct sockaddr_un	address;
	int					socket_fd;

	socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (socket_fd == -1)
		return (-1);
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	memcpy(address.sun_path, path, mt_strlen(path) + 1);
	if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) == -1)
	{
		close(socket_fd);
		return (-1);
	}
	return (close(socket_fd));
}

static int	create_stale_entry(const char *path, const char *mode)
{
	int	fd;

	unlink(path);
	if (strcmp(mode, "socket") == 0)
		return (create_stale_socket(path));
	if (strcmp(mode, "file") != 0)
		return (-1);
	fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd == -1)
		return (-1);
	return (close(fd));
}

static int	child_status(pid_t child)
{
	int	status;

	while (waitpid(child, &status, 0) == -1)
	{
		if (errno != EINTR)
			return (-1);
	}
	if (WIFEXITED(status))
		return (WEXITSTATUS(status));
	return (-1);
}

int	main(int argc, char **argv)
{
	int		gate[2];
	pid_t	child;
	char	path[MT_RESPONSE_PATH_SIZE];
	char	token;
	char	*child_argv[4];
	int		status;

	if (argc != 5 || pipe(gate) == -1)
		return (2);
	child = fork();
	if (child == -1)
		return (1);
	if (child == 0)
	{
		close(gate[1]);
		if (read(gate[0], &token, 1) != 1)
			_exit(126);
		close(gate[0]);
		child_argv[0] = argv[1];
		child_argv[1] = argv[2];
		child_argv[2] = argv[3];
		child_argv[3] = NULL;
		execv(argv[1], child_argv);
		_exit(127);
	}
	close(gate[0]);
	if (mt_response_path(path, sizeof(path), "client", child) == -1
		|| create_stale_entry(path, argv[4]) == -1
		|| write(gate[1], "x", 1) != 1)
	{
		close(gate[1]);
		kill(child, SIGKILL);
		waitpid(child, NULL, 0);
		unlink(path);
		return (1);
	}
	close(gate[1]);
	status = child_status(child);
	if (strcmp(argv[4], "socket") == 0)
	{
		if (status != 0 || lstat(path, &(struct stat){0}) == 0
			|| errno != ENOENT)
			return (1);
		return (0);
	}
	if (status == 0 || lstat(path, &(struct stat){0}) == -1)
		return (1);
	unlink(path);
	return (0);
}
