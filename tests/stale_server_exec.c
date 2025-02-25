#define _POSIX_C_SOURCE 200809L

#include "minitalk.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int	wait_for_child(pid_t child)
{
	struct timespec	pause_time;
	int				status;
	int				tries;
	pid_t			result;

	tries = 0;
	while (tries < 50)
	{
		result = waitpid(child, &status, WNOHANG);
		if (result == child)
		{
			if (WIFEXITED(status))
				return (WEXITSTATUS(status));
			return (-1);
		}
		if (result == -1 && errno != EINTR)
			break ;
		pause_time.tv_sec = 0;
		pause_time.tv_nsec = 100000000L;
		while (nanosleep(&pause_time, &pause_time) == -1 && errno == EINTR)
			;
		tries++;
	}
	kill(child, SIGKILL);
	waitpid(child, NULL, 0);
	return (-1);
}

static int bind_stale_socket(const char *path)
{
	struct sockaddr_un address;
	int socket_fd;

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

static int run_client(const char *client_path, pid_t target)
{
	char pid_text[32];
	char *client_argv[4];
	pid_t client;
	int status;

	if (snprintf(pid_text, sizeof(pid_text), "%ld", (long)target) < 0)
		return (-1);
	client = fork();
	if (client == -1)
		return (-1);
	if (client == 0)
	{
		client_argv[0] = (char *)client_path;
		client_argv[1] = pid_text;
		client_argv[2] = "unrelated";
		client_argv[3] = NULL;
		execv(client_path, client_argv);
		_exit(127);
	}
	if (waitpid(client, &status, 0) != client || !WIFEXITED(status))
		return (-1);
	return (WEXITSTATUS(status));
}

static int test_unrelated_process(const char *client_path)
{
	struct timespec pause_time;
	struct stat info;
	char path[MT_RESPONSE_PATH_SIZE];
	char token;
	char *sleep_argv[3];
	pid_t child;
	int gate[2];
	int status;

	if (pipe(gate) == -1)
		return (1);
	child = fork();
	if (child == -1)
		return (1);
	if (child == 0)
	{
		close(gate[1]);
		if (read(gate[0], &token, 1) != 1)
			_exit(126);
		close(gate[0]);
		sleep_argv[0] = "sleep";
		sleep_argv[1] = "30";
		sleep_argv[2] = NULL;
		execv("/bin/sleep", sleep_argv);
		_exit(127);
	}
	close(gate[0]);
	if (mt_response_path(path, sizeof(path), "server", child) == -1)
		return (1);
	unlink(path);
	if (bind_stale_socket(path) == -1 || write(gate[1], "x", 1) != 1)
	{
		close(gate[1]);
		kill(child, SIGKILL);
		waitpid(child, NULL, 0);
		unlink(path);
		return (1);
	}
	close(gate[1]);
	pause_time.tv_sec = 0;
	pause_time.tv_nsec = 100000000L;
	while (nanosleep(&pause_time, &pause_time) == -1 && errno == EINTR)
		;
	status = run_client(client_path, child);
	if (status != 1 || kill(child, 0) == -1 || lstat(path, &info) == -1
		|| !S_ISSOCK(info.st_mode) || info.st_uid != getuid())
	{
		kill(child, SIGKILL);
		waitpid(child, NULL, 0);
		unlink(path);
		return (1);
	}
	kill(child, SIGTERM);
	waitpid(child, NULL, 0);
	unlink(path);
	return (0);
}

int	main(int argc, char **argv)
{
	struct stat	info;
	int			gate[2];
	int			file_fd;
	int			status;
	pid_t		child;
	char		path[MT_RESPONSE_PATH_SIZE];
	char		token;
	char		*child_argv[2];

	if (argc == 3 && strcmp(argv[2], "unrelated") == 0)
		return (test_unrelated_process(argv[1]));
	if (argc != 2 || pipe(gate) == -1)
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
		child_argv[1] = NULL;
		execv(argv[1], child_argv);
		_exit(127);
	}
	close(gate[0]);
	if (mt_response_path(path, sizeof(path), "server", child) == -1)
		return (1);
	unlink(path);
	file_fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (file_fd == -1 || close(file_fd) == -1
		|| write(gate[1], "x", 1) != 1)
	{
		close(gate[1]);
		kill(child, SIGKILL);
		waitpid(child, NULL, 0);
		unlink(path);
		return (1);
	}
	close(gate[1]);
	status = wait_for_child(child);
	if (lstat(path, &info) == -1 || !S_ISREG(info.st_mode)
		|| info.st_uid != getuid())
	{
		unlink(path);
		return (1);
	}
	unlink(path);
	if (status != 1)
		return (1);
	return (0);
}
