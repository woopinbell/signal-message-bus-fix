#define _POSIX_C_SOURCE 200809L

#include "minitalk.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

typedef struct s_bit_event
{
	pid_t	sender;
	int		signal;
}	t_bit_event;

static int	g_pipe[2] = {-1, -1};
static int	g_server_socket = -1;
static int	g_forger_socket = -1;
static char	g_server_path[MT_RESPONSE_PATH_SIZE];
static char	g_forger_path[MT_RESPONSE_PATH_SIZE];

static void	cleanup(void)
{
	if (g_pipe[0] != -1)
		close(g_pipe[0]);
	if (g_pipe[1] != -1)
		close(g_pipe[1]);
	if (g_server_socket != -1)
		close(g_server_socket);
	if (g_forger_socket != -1)
		close(g_forger_socket);
	if (g_server_path[0] != '\0')
		unlink(g_server_path);
	if (g_forger_path[0] != '\0')
		unlink(g_forger_path);
}

static int	set_nonblocking(int fd)
{
	int	flags;

	flags = fcntl(fd, F_GETFL);
	if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
		return (-1);
	return (0);
}

static int	bind_path(int fd, const char *path)
{
	struct sockaddr_un	address;
	struct stat			info;

	if (lstat(path, &info) == 0)
	{
		if (!S_ISSOCK(info.st_mode) || info.st_uid != getuid()
			|| unlink(path) == -1)
			return (-1);
	}
	else if (errno != ENOENT)
		return (-1);
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	memcpy(address.sun_path, path, mt_strlen(path) + 1);
	return (bind(fd, (struct sockaddr *)&address, sizeof(address)));
}

static int	prepare(void)
{
	struct sigaction	action;

	if (pipe(g_pipe) == -1 || set_nonblocking(g_pipe[1]) == -1)
		return (-1);
	g_server_socket = socket(AF_UNIX, SOCK_DGRAM, 0);
	g_forger_socket = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (g_server_socket == -1 || g_forger_socket == -1
		|| mt_response_path(g_server_path, sizeof(g_server_path), "server",
			getpid()) == -1
		|| mt_response_path(g_forger_path, sizeof(g_forger_path), "forger",
			getpid()) == -1 || bind_path(g_server_socket, g_server_path) == -1
		|| bind_path(g_forger_socket, g_forger_path) == -1)
		return (-1);
	memset(&action, 0, sizeof(action));
	sigemptyset(&action.sa_mask);
	sigaddset(&action.sa_mask, MT_ZERO_SIGNAL);
	sigaddset(&action.sa_mask, MT_ONE_SIGNAL);
	action.sa_flags = SA_SIGINFO;
	return (0);
}

static void	handle_bit(int signal, siginfo_t *info, void *context)
{
	t_bit_event	event;
	int				saved_errno;

	saved_errno = errno;
	(void)context;
	if (info != NULL)
	{
		event.sender = info->si_pid;
		event.signal = signal;
		write(g_pipe[1], &event, sizeof(event));
	}
	errno = saved_errno;
}

static int	install_handlers(void)
{
	struct sigaction	action;

	memset(&action, 0, sizeof(action));
	action.sa_sigaction = handle_bit;
	sigemptyset(&action.sa_mask);
	sigaddset(&action.sa_mask, MT_ZERO_SIGNAL);
	sigaddset(&action.sa_mask, MT_ONE_SIGNAL);
	action.sa_flags = SA_SIGINFO;
	if (sigaction(MT_ZERO_SIGNAL, &action, NULL) == -1
		|| sigaction(MT_ONE_SIGNAL, &action, NULL) == -1)
		return (-1);
	return (0);
}

static int	send_response(int socket_fd, pid_t client_pid,
		t_mt_response *response)
{
	struct sockaddr_un	address;
	char				path[MT_RESPONSE_PATH_SIZE];

	if (mt_response_path(path, sizeof(path), "client", client_pid) == -1)
		return (-1);
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	memcpy(address.sun_path, path, mt_strlen(path) + 1);
	if (sendto(socket_fd, response, sizeof(*response), 0,
			(struct sockaddr *)&address, sizeof(address))
		!= (ssize_t)sizeof(*response))
		return (-1);
	return (0);
}

static int	reply_with_invalid_events(pid_t client_pid, uint32_t kind,
		uint32_t token)
{
	t_mt_response	response;
	struct timespec	pause_time;

	response.magic = MT_RESPONSE_MAGIC;
	response.kind = kind;
	response.token = token;
	response.status = MT_RESPONSE_OK;
	response.server_pid = getpid();
	if (send_response(g_forger_socket, client_pid, &response) == -1)
		return (-1);
	response.token = token + 1;
	if (send_response(g_server_socket, client_pid, &response) == -1)
		return (-1);
	response.token = token;
	response.magic = 0;
	if (send_response(g_server_socket, client_pid, &response) == -1)
		return (-1);
	response.magic = MT_RESPONSE_MAGIC;
	response.server_pid = getpid() + 1;
	if (send_response(g_server_socket, client_pid, &response) == -1)
		return (-1);
	pause_time.tv_sec = 0;
	pause_time.tv_nsec = 20000000L;
	while (nanosleep(&pause_time, &pause_time) == -1 && errno == EINTR)
		;
	response.server_pid = getpid();
	return (send_response(g_server_socket, client_pid, &response));
}

static int	receive_session_request(t_mt_request *request)
{
	unsigned char		payload[sizeof(*request) + 1];
	struct sockaddr_un	source;
	char				client_path[MT_RESPONSE_PATH_SIZE];
	socklen_t			source_size;
	ssize_t				size;

	memset(&source, 0, sizeof(source));
	source_size = sizeof(source);
	size = recvfrom(g_server_socket, payload, sizeof(payload), 0,
			(struct sockaddr *)&source, &source_size);
	if (size != (ssize_t)sizeof(*request))
		return (-1);
	memcpy(request, payload, sizeof(*request));
	if (request->magic != MT_RESPONSE_MAGIC
		|| request->kind != MT_REQUEST_ACQUIRE
		|| request->client_pid <= 1
		|| mt_response_path(client_path, sizeof(client_path), "client",
			request->client_pid) == -1
		|| source.sun_family != AF_UNIX
		|| strcmp(source.sun_path, client_path) != 0)
		return (-1);
	return (0);
}

static void	wait_for_client_cleanup(pid_t client_pid)
{
	struct timespec	pause_time;
	struct stat		info;
	char				path[MT_RESPONSE_PATH_SIZE];
	int					tries;

	if (mt_response_path(path, sizeof(path), "client", client_pid) == -1)
		return ;
	tries = 0;
	while (tries < 50 && lstat(path, &info) == 0)
	{
		pause_time.tv_sec = 0;
		pause_time.tv_nsec = 10000000L;
		while (nanosleep(&pause_time, &pause_time) == -1 && errno == EINTR)
			;
		tries++;
	}
}

int	main(void)
{
	t_bit_event		event;
	t_mt_request	request;
	t_mt_response	response;
	uint32_t		sequence;
	unsigned char	byte;
	int				bits;
	ssize_t			size;

	if (prepare() == -1 || install_handlers() == -1 || atexit(cleanup) != 0)
		return (1);
	mt_putnbr_fd(getpid(), STDOUT_FILENO);
	write(STDOUT_FILENO, "\n", 1);
	if (receive_session_request(&request) == -1
		|| reply_with_invalid_events(request.client_pid, MT_RESPONSE_READY,
			request.nonce) == -1)
		return (1);
	sequence = 0;
	byte = 0;
	bits = 0;
	while (1)
	{
		size = read(g_pipe[0], &event, sizeof(event));
		if (size == -1 && errno == EINTR)
			continue ;
		if (size != (ssize_t)sizeof(event))
			return (1);
		byte <<= 1;
		if (event.signal == MT_ONE_SIGNAL)
			byte |= 1;
		bits++;
		response.magic = MT_RESPONSE_MAGIC;
		response.kind = MT_RESPONSE_ACK;
		response.token = sequence;
		response.status = MT_RESPONSE_OK;
		response.server_pid = getpid();
		if ((sequence == 0 && reply_with_invalid_events(event.sender,
				MT_RESPONSE_ACK, sequence) == -1) || (sequence != 0
				&& send_response(g_server_socket, event.sender, &response) == -1))
			return (1);
		sequence++;
		if (bits == 8)
		{
			if (byte == '\0')
			{
				write(STDOUT_FILENO, "\n", 1);
				wait_for_client_cleanup(event.sender);
				return (0);
			}
			write(STDOUT_FILENO, &byte, 1);
			byte = 0;
			bits = 0;
		}
	}
	return (1);
}
