#define _POSIX_C_SOURCE 200809L

#include "minitalk.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static int	g_socket = -1;
static char	g_path[MT_RESPONSE_PATH_SIZE];

static void	cleanup(void)
{
	if (g_socket != -1)
		close(g_socket);
	if (g_path[0] != '\0')
		unlink(g_path);
}

static int	set_nonblocking(int fd)
{
	int	flags;

	flags = fcntl(fd, F_GETFL);
	if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
		return (-1);
	return (0);
}

static int	prepare_socket(void)
{
	struct sockaddr_un	address;
	struct stat			info;

	if (mt_response_path(g_path, sizeof(g_path), "client", getpid()) == -1)
		return (-1);
	if (lstat(g_path, &info) == 0)
	{
		if (!S_ISSOCK(info.st_mode) || info.st_uid != getuid()
			|| unlink(g_path) == -1)
			return (-1);
	}
	else if (errno != ENOENT)
		return (-1);
	g_socket = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (g_socket == -1 || set_nonblocking(g_socket) == -1)
		return (-1);
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	memcpy(address.sun_path, g_path, mt_strlen(g_path) + 1);
	if (bind(g_socket, (struct sockaddr *)&address, sizeof(address)) == -1)
		return (-1);
	return (0);
}

static int	response_matches(const t_mt_response *response,
		const struct sockaddr_un *source, pid_t server_pid, uint32_t kind,
		uint32_t token,
		const char *server_path)
{
	return (source->sun_family == AF_UNIX
		&& source->sun_path[sizeof(source->sun_path) - 1] == '\0'
		&& strcmp(source->sun_path, server_path) == 0
		&& response->magic == MT_RESPONSE_MAGIC
		&& response->server_pid == server_pid
		&& response->kind == kind
		&& response->token == token
		&& response->status == MT_RESPONSE_OK);
}

static int	wait_for_response(pid_t server_pid, uint32_t kind, uint32_t token,
		const char *server_path)
{
	struct sockaddr_un	source;
	t_mt_response		response;
	struct timeval		timeout;
	fd_set				read_set;
	socklen_t			source_size;
	ssize_t				size;
	int					status;

	while (1)
	{
		FD_ZERO(&read_set);
		FD_SET(g_socket, &read_set);
		timeout.tv_sec = MT_ACK_TIMEOUT_SECONDS;
		timeout.tv_usec = 0;
		status = select(g_socket + 1, &read_set, NULL, NULL, &timeout);
		if (status == -1 && errno == EINTR)
			continue ;
		if (status <= 0)
			return (-1);
		memset(&source, 0, sizeof(source));
		source_size = sizeof(source);
		size = recvfrom(g_socket, &response, sizeof(response), 0,
				(struct sockaddr *)&source, &source_size);
		if (size == (ssize_t)sizeof(response)
			&& response_matches(&response, &source, server_pid, kind, token,
				server_path))
			return (0);
	}
}

static int	request_session(pid_t server_pid, const char *server_path)
{
	struct sockaddr_un	address;
	t_mt_request		request;

	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	memcpy(address.sun_path, server_path, mt_strlen(server_path) + 1);
	request.magic = MT_RESPONSE_MAGIC;
	request.kind = MT_REQUEST_ACQUIRE;
	request.nonce = (uint32_t)getpid() ^ 0xa5a55a5aU;
	request.client_pid = getpid();
	if (sendto(g_socket, &request, sizeof(request), 0,
			(struct sockaddr *)&address, sizeof(address))
		!= (ssize_t)sizeof(request))
		return (-1);
	return (wait_for_response(server_pid, MT_RESPONSE_READY, request.nonce,
			server_path));
}

static int	send_bit(pid_t server_pid, int bit, uint32_t sequence,
		const char *server_path)
{
	int				signal;

	signal = MT_ZERO_SIGNAL;
	if (bit != 0)
		signal = MT_ONE_SIGNAL;
	if (kill(server_pid, signal) == -1
		|| wait_for_response(server_pid, MT_RESPONSE_ACK, sequence, server_path) == -1)
		return (-1);
	return (0);
}

static int	send_byte(pid_t server_pid, unsigned char byte, uint32_t *sequence,
		const char *server_path)
{
	int	shift;

	shift = 7;
	while (shift >= 0)
	{
		if (send_bit(server_pid, (byte >> shift) & 1, *sequence,
				server_path) == -1)
			return (-1);
		(*sequence)++;
		shift--;
	}
	return (0);
}

static int	send_partial(pid_t server_pid, uint32_t *sequence,
		const char *server_path)
{
	if (send_byte(server_pid, 'X', sequence, server_path) == -1
		|| send_bit(server_pid, 0, (*sequence)++, server_path) == -1
		|| send_bit(server_pid, 1, (*sequence)++, server_path) == -1
		|| send_bit(server_pid, 0, (*sequence)++, server_path) == -1)
		return (-1);
	return (0);
}

int	main(int argc, char **argv)
{
	pid_t		server_pid;
	uint32_t	sequence;
	char		server_path[MT_RESPONSE_PATH_SIZE];

	if (argc != 3 || !mt_parse_pid(argv[1], &server_pid)
		|| mt_response_path(server_path, sizeof(server_path), "server",
			server_pid) == -1 || prepare_socket() == -1 || atexit(cleanup) != 0)
		return (1);
	if (request_session(server_pid, server_path) == -1)
		return (1);
	sequence = 0;
	if (strcmp(argv[2], "bit") == 0)
	{
		if (send_bit(server_pid, 0, sequence, server_path) == -1)
			return (1);
	}
	else if (strcmp(argv[2], "partial") == 0)
	{
		if (send_partial(server_pid, &sequence, server_path) == -1)
			return (1);
	}
	else if (strcmp(argv[2], "reserve") != 0)
		return (1);
	if (strcmp(argv[2], "reserve") == 0)
	{
		write(STDOUT_FILENO, "ready\n", 6);
		while (1)
			pause();
	}
	return (0);
}
