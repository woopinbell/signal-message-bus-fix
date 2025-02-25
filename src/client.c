#define _POSIX_C_SOURCE 200809L

#include "minitalk.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define SEND_ERROR 1
#define SEND_TIMEOUT 2
#define SEND_REJECTED 3

static int	g_response_socket = -1;
static char	g_client_path[MT_RESPONSE_PATH_SIZE];
static int	g_client_bound;

static void	cleanup_response_socket(void)
{
	if (g_response_socket != -1)
		close(g_response_socket);
	g_response_socket = -1;
	if (g_client_bound && g_client_path[0] != '\0')
		unlink(g_client_path);
	g_client_bound = 0;
	g_client_path[0] = '\0';
}


static int	set_nonblocking_close_on_exec(int fd)
{
	int	flags;

	flags = fcntl(fd, F_GETFL);
	if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
		return (-1);
	flags = fcntl(fd, F_GETFD);
	if (flags == -1 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
		return (-1);
	return (0);
}

static int	remove_stale_socket(const char *path)
{
	struct stat	info;

	if (lstat(path, &info) == -1)
	{
		if (errno == ENOENT)
			return (0);
		return (-1);
	}
	if (!S_ISSOCK(info.st_mode) || info.st_uid != getuid())
	{
		errno = EACCES;
		return (-1);
	}
	return (unlink(path));
}

static int	validate_server_socket(const char *path)
{
	struct stat	info;

	if (lstat(path, &info) == -1)
		return (-1);
	if (!S_ISSOCK(info.st_mode) || info.st_uid != getuid())
	{
		errno = EACCES;
		return (-1);
	}
	return (0);
}

static int	bind_client_socket(void)
{
	struct sockaddr_un	address;

	if (mt_response_path(g_client_path, sizeof(g_client_path), "client",
			getpid()) == -1 || remove_stale_socket(g_client_path) == -1)
		return (-1);
	g_response_socket = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (g_response_socket == -1
		|| set_nonblocking_close_on_exec(g_response_socket) == -1)
		return (-1);
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	if (mt_strlen(g_client_path) >= sizeof(address.sun_path))
	{
		errno = ENAMETOOLONG;
		return (-1);
	}
	memcpy(address.sun_path, g_client_path, mt_strlen(g_client_path) + 1);
	if (bind(g_response_socket, (struct sockaddr *)&address,
			sizeof(address)) == -1)
		return (-1);
	g_client_bound = 1;
	return (0);
}

static int	generate_nonce(uint32_t *nonce)
{
	unsigned char	*bytes;
	size_t			offset;
	ssize_t		count;
	int				random_fd;

	random_fd = open("/dev/urandom", O_RDONLY);
	if (random_fd == -1)
		return (-1);
	bytes = (unsigned char *)nonce;
	offset = 0;
	while (offset < sizeof(*nonce))
	{
		count = read(random_fd, bytes + offset, sizeof(*nonce) - offset);
		if (count == -1 && errno == EINTR)
			continue ;
		if (count <= 0)
		{
			close(random_fd);
			return (-1);
		}
		offset += (size_t)count;
	}
	if (close(random_fd) == -1)
		return (-1);
	if (*nonce == 0)
		*nonce = 1;
	return (0);
}

static struct timespec	time_until(const struct timespec *deadline)
{
	struct timespec	now;
	struct timespec	remaining;

	remaining.tv_sec = 0;
	remaining.tv_nsec = 0;
	if (clock_gettime(CLOCK_MONOTONIC, &now) == -1)
		return (remaining);
	remaining.tv_sec = deadline->tv_sec - now.tv_sec;
	remaining.tv_nsec = deadline->tv_nsec - now.tv_nsec;
	if (remaining.tv_nsec < 0)
	{
		remaining.tv_sec--;
		remaining.tv_nsec += 1000000000L;
	}
	if (remaining.tv_sec < 0)
	{
		remaining.tv_sec = 0;
		remaining.tv_nsec = 0;
	}
	return (remaining);
}

static int	valid_source(const struct sockaddr_un *source,
		const char *server_path)
{
	if (source->sun_family != AF_UNIX)
		return (0);
	if (source->sun_path[sizeof(source->sun_path) - 1] != '\0')
		return (0);
	return (strcmp(source->sun_path, server_path) == 0);
}

static int	read_response(pid_t server_pid, uint32_t kind, uint32_t token,
		const char *server_path)
{
	unsigned char		payload[sizeof(t_mt_response) + 1];
	struct sockaddr_un	source;
	socklen_t			source_size;
	t_mt_response		response;
	ssize_t				size;

	memset(&source, 0, sizeof(source));
	source_size = sizeof(source);
	size = recvfrom(g_response_socket, payload, sizeof(payload), 0,
			(struct sockaddr *)&source, &source_size);
	if (size == -1 && (errno == EAGAIN || errno == EWOULDBLOCK
			|| errno == EINTR))
		return (0);
	if (size == -1)
		return (SEND_ERROR);
	if (size != (ssize_t)sizeof(response))
		return (0);
	memcpy(&response, payload, sizeof(response));
	if (!valid_source(&source, server_path)
		|| response.magic != MT_RESPONSE_MAGIC
		|| response.server_pid != server_pid || response.kind != kind
		|| response.token != token)
		return (0);
	if (response.status == MT_RESPONSE_BUSY)
		return (SEND_REJECTED);
	if (response.status != MT_RESPONSE_OK)
		return (SEND_ERROR);
	return (-1);
}

static int	wait_for_response(pid_t server_pid, uint32_t kind, uint32_t token,
		const char *server_path, const struct timespec *deadline)
{
	struct timespec	remaining;
	fd_set			read_set;
	int				status;

	while (1)
	{
		status = read_response(server_pid, kind, token, server_path);
		if (status != 0)
		{
			if (status == -1)
				return (0);
			return (status);
		}
		remaining = time_until(deadline);
		if (remaining.tv_sec == 0 && remaining.tv_nsec == 0)
			return (SEND_TIMEOUT);
		FD_ZERO(&read_set);
		FD_SET(g_response_socket, &read_set);
		status = pselect(g_response_socket + 1, &read_set, NULL, NULL,
				&remaining, NULL);
		if (status == -1 && errno != EINTR)
			return (SEND_ERROR);
	}
}

static int	request_session(pid_t server_pid, const char *server_path)
{
	struct sockaddr_un	address;
	struct timespec		deadline;
	t_mt_request		request;

	if (generate_nonce(&request.nonce) == -1
		|| validate_server_socket(server_path) == -1
		|| clock_gettime(CLOCK_MONOTONIC, &deadline) == -1)
		return (SEND_ERROR);
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	if (mt_strlen(server_path) >= sizeof(address.sun_path))
		return (SEND_ERROR);
	memcpy(address.sun_path, server_path, mt_strlen(server_path) + 1);
	request.magic = MT_RESPONSE_MAGIC;
	request.kind = MT_REQUEST_ACQUIRE;
	request.client_pid = getpid();
	deadline.tv_sec += MT_ACK_TIMEOUT_SECONDS;
	if (sendto(g_response_socket, &request, sizeof(request), 0,
			(struct sockaddr *)&address, sizeof(address))
		!= (ssize_t)sizeof(request))
		return (SEND_ERROR);
	return (wait_for_response(server_pid, MT_RESPONSE_READY, request.nonce,
			server_path, &deadline));
}

static int	send_bit(pid_t server_pid, int bit, uint32_t sequence,
		const char *server_path)
{
	struct timespec	deadline;
	int				signal;
	int				status;

	signal = MT_ZERO_SIGNAL;
	if (bit != 0)
		signal = MT_ONE_SIGNAL;
	if (validate_server_socket(server_path) == -1)
		return (SEND_ERROR);
	if (clock_gettime(CLOCK_MONOTONIC, &deadline) == -1)
		return (SEND_ERROR);
	deadline.tv_sec += MT_ACK_TIMEOUT_SECONDS;
	if (kill(server_pid, signal) == -1)
		return (SEND_ERROR);
	status = wait_for_response(server_pid, MT_RESPONSE_ACK, sequence,
			server_path, &deadline);
	if (status != 0)
		return (status);
	return (0);
}

static int	send_byte(pid_t server_pid, unsigned char byte,
		uint32_t *sequence, const char *server_path)
{
	int	status;
	int	shift;

	shift = 7;
	while (shift >= 0)
	{
		status = send_bit(server_pid, (byte >> shift) & 1, *sequence,
				server_path);
		if (status != 0)
			return (status);
		(*sequence)++;
		shift--;
	}
	return (0);
}

static int	report_send_status(int status)
{
	if (status == SEND_TIMEOUT)
		mt_putstr_fd("client: timed out waiting for acknowledgement\n",
			STDERR_FILENO);
	else if (status == SEND_REJECTED)
		mt_putstr_fd("client: server is busy with another sender\n",
			STDERR_FILENO);
	else
		mt_putstr_fd("client: failed to send signal\n", STDERR_FILENO);
	return (1);
}

int	main(int argc, char **argv)
{
	pid_t		server_pid;
	int			status;
	size_t		index;
	uint32_t	sequence;
	char		server_path[MT_RESPONSE_PATH_SIZE];

	if (argc != 3)
	{
		mt_putstr_fd("usage: ./client <server_pid> <message>\n", STDERR_FILENO);
		return (1);
	}
	if (!mt_parse_pid(argv[1], &server_pid) || kill(server_pid, 0) == -1
		|| mt_response_path(server_path, sizeof(server_path), "server",
			server_pid) == -1)
	{
		mt_putstr_fd("client: invalid server pid\n", STDERR_FILENO);
		return (1);
	}
	if (bind_client_socket() == -1 || atexit(cleanup_response_socket) != 0)
	{
		cleanup_response_socket();
		mt_putstr_fd("client: failed to create response channel\n",
			STDERR_FILENO);
		return (1);
	}
	if (validate_server_socket(server_path) == -1)
	{
		mt_putstr_fd("client: invalid server pid\n", STDERR_FILENO);
		return (1);
	}
	status = request_session(server_pid, server_path);
	if (status != 0)
		return (report_send_status(status));
	index = 0;
	sequence = 0;
	while (argv[2][index] != '\0')
	{
		status = send_byte(server_pid, (unsigned char)argv[2][index],
				&sequence, server_path);
		if (status != 0)
			return (report_send_status(status));
		index++;
	}
	status = send_byte(server_pid, '\0', &sequence, server_path);
	if (status != 0)
		return (report_send_status(status));
	return (0);
}
