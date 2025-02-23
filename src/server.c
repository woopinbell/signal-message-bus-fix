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
#include <unistd.h>

typedef struct s_response_request
{
	pid_t		client_pid;
	uint32_t	kind;
	uint32_t	token;
	int32_t		status;
}	t_response_request;

static volatile sig_atomic_t	g_current_byte;
static volatile sig_atomic_t	g_received_bits;
static volatile sig_atomic_t	g_client_pid;
static volatile sig_atomic_t	g_line_started;
static volatile sig_atomic_t	g_response_overflow;
static volatile sig_atomic_t	g_sequence;
static int					g_response_pipe[2] = {-1, -1};
static int					g_response_socket = -1;
static int					g_server_bound;
static char					g_server_path[MT_RESPONSE_PATH_SIZE];

static void	cleanup_server(void)
{
	if (g_response_pipe[0] != -1)
		close(g_response_pipe[0]);
	if (g_response_pipe[1] != -1)
		close(g_response_pipe[1]);
	if (g_response_socket != -1)
		close(g_response_socket);
	g_response_pipe[0] = -1;
	g_response_pipe[1] = -1;
	g_response_socket = -1;
	if (g_server_bound && g_server_path[0] != '\0')
		unlink(g_server_path);
	g_server_bound = 0;
	g_server_path[0] = '\0';
}

static void	reset_session(int close_partial_line)
{
	if (close_partial_line && g_line_started)
		write(STDOUT_FILENO, "\n", 1);
	g_current_byte = 0;
	g_received_bits = 0;
	g_client_pid = 0;
	g_line_started = 0;
	g_sequence = 0;
}

static void	flush_byte(unsigned char output)
{
	if (output == '\0')
	{
		write(STDOUT_FILENO, "\n", 1);
		reset_session(0);
	}
	else
	{
		write(STDOUT_FILENO, &output, 1);
		g_line_started = 1;
	}
}

static void	queue_response(pid_t client_pid, uint32_t kind,
		uint32_t token, int status)
{
	t_response_request	request;

	request.client_pid = client_pid;
	request.kind = kind;
	request.token = token;
	request.status = status;
	if (write(g_response_pipe[1], &request, sizeof(request))
		!= (ssize_t)sizeof(request))
		g_response_overflow = 1;
}

static void	handle_bit(int signal, siginfo_t *info, void *context)
{
	unsigned char	output;
	uint32_t		sequence;
	int				saved_errno;

	saved_errno = errno;
	(void)context;
	if (info == NULL || info->si_pid <= 0)
	{
		errno = saved_errno;
		return ;
	}
	if (g_client_pid == 0 || g_client_pid != info->si_pid)
	{
		errno = saved_errno;
		return ;
	}
	sequence = (uint32_t)g_sequence;
	g_current_byte <<= 1;
	if (signal == MT_ONE_SIGNAL)
		g_current_byte |= 1;
	g_received_bits++;
	if (g_received_bits == 8)
	{
		output = (unsigned char)g_current_byte;
		flush_byte(output);
		g_current_byte = 0;
		g_received_bits = 0;
	}
	if (g_client_pid != 0)
		g_sequence++;
	queue_response(info->si_pid, MT_RESPONSE_ACK, sequence, MT_RESPONSE_OK);
	errno = saved_errno;
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

static int	prepare_response_channel(void)
{
	struct sockaddr_un	address;

	if (pipe(g_response_pipe) == -1
		|| set_nonblocking_close_on_exec(g_response_pipe[1]) == -1)
		return (-1);
	if (mt_response_path(g_server_path, sizeof(g_server_path), "server",
			getpid()) == -1 || remove_stale_socket(g_server_path) == -1)
		return (-1);
	g_response_socket = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (g_response_socket == -1
		|| set_nonblocking_close_on_exec(g_response_socket) == -1)
		return (-1);
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	if (mt_strlen(g_server_path) >= sizeof(address.sun_path))
	{
		errno = ENAMETOOLONG;
		return (-1);
	}
	memcpy(address.sun_path, g_server_path, mt_strlen(g_server_path) + 1);
	if (bind(g_response_socket, (struct sockaddr *)&address,
			sizeof(address)) == -1)
		return (-1);
	g_server_bound = 1;
	return (0);
}

static int	install_signal_handlers(void)
{
	struct sigaction	action;

	action.sa_sigaction = handle_bit;
	sigemptyset(&action.sa_mask);
	sigaddset(&action.sa_mask, MT_ZERO_SIGNAL);
	sigaddset(&action.sa_mask, MT_ONE_SIGNAL);
	action.sa_flags = SA_SIGINFO;
	if (sigaction(MT_ZERO_SIGNAL, &action, NULL) == -1)
		return (-1);
	if (sigaction(MT_ONE_SIGNAL, &action, NULL) == -1)
		return (-1);
	return (0);
}

static int	valid_client_socket(const char *path)
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

static int	send_response(const t_response_request *request)
{
	struct sockaddr_un	address;
	t_mt_response		response;
	char				client_path[MT_RESPONSE_PATH_SIZE];

	if (mt_response_path(client_path, sizeof(client_path), "client",
			request->client_pid) == -1 || valid_client_socket(client_path) == -1)
		return (-1);
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	if (mt_strlen(client_path) >= sizeof(address.sun_path))
	{
		errno = ENAMETOOLONG;
		return (-1);
	}
	memcpy(address.sun_path, client_path, mt_strlen(client_path) + 1);
	response.magic = MT_RESPONSE_MAGIC;
	response.kind = request->kind;
	response.token = request->token;
	response.status = request->status;
	response.server_pid = getpid();
	if (sendto(g_response_socket, &response, sizeof(response), 0,
			(struct sockaddr *)&address, sizeof(address))
		!= (ssize_t)sizeof(response))
		return (-1);
	return (0);
}

static int	valid_request_source(const struct sockaddr_un *source,
		const t_mt_request *request)
{
	char	client_path[MT_RESPONSE_PATH_SIZE];

	if (request->magic != MT_RESPONSE_MAGIC
		|| request->kind != MT_REQUEST_ACQUIRE || request->client_pid <= 1
		|| source->sun_family != AF_UNIX
		|| source->sun_path[sizeof(source->sun_path) - 1] != '\0'
		|| mt_response_path(client_path, sizeof(client_path), "client",
			request->client_pid) == -1
		|| strcmp(source->sun_path, client_path) != 0
		|| valid_client_socket(client_path) == -1
		|| kill(request->client_pid, 0) == -1)
		return (0);
	return (1);
}

static int	read_session_request(t_mt_request *request)
{
	unsigned char		payload[sizeof(*request) + 1];
	struct sockaddr_un	source;
	socklen_t			source_size;
	ssize_t				size;

	memset(&source, 0, sizeof(source));
	source_size = sizeof(source);
	size = recvfrom(g_response_socket, payload, sizeof(payload), 0,
			(struct sockaddr *)&source, &source_size);
	if (size == -1 && (errno == EAGAIN || errno == EWOULDBLOCK
			|| errno == EINTR))
		return (0);
	if (size == -1)
		return (-1);
	if (size != (ssize_t)sizeof(*request))
		return (0);
	memcpy(request, payload, sizeof(*request));
	return (valid_request_source(&source, request));
}

static int	handle_session_request(void)
{
	t_response_request	response;
	t_mt_request		request;
	int					new_owner;
	int					status;

	status = read_session_request(&request);
	if (status <= 0)
		return (status);
	new_owner = 0;
	response.status = MT_RESPONSE_OK;
	if (g_client_pid != 0 && g_client_pid != request.client_pid)
	{
		if (kill((pid_t)g_client_pid, 0) == -1 && errno == ESRCH)
			reset_session(1);
		else
			response.status = MT_RESPONSE_BUSY;
	}
	if (response.status == MT_RESPONSE_OK && g_client_pid == 0)
	{
		g_client_pid = request.client_pid;
		new_owner = 1;
	}
	response.client_pid = request.client_pid;
	response.kind = MT_RESPONSE_READY;
	response.token = request.nonce;
	if (send_response(&response) == -1 && new_owner)
		reset_session(0);
	return (0);
}

static int	respond_to_bit(void)
{
	t_response_request	request;
	ssize_t				size;

	size = read(g_response_pipe[0], &request, sizeof(request));
	if (size == -1 && errno == EINTR)
		return (0);
	if (size != (ssize_t)sizeof(request) || g_response_overflow)
		return (-1);
	if (send_response(&request) == -1 && request.status == MT_RESPONSE_OK
		&& request.client_pid == (pid_t)g_client_pid)
		reset_session(1);
	return (0);
}

static int	run_response_loop(void)
{
	fd_set	read_set;
	int		max_fd;
	int		status;

	max_fd = g_response_pipe[0];
	if (g_response_socket > max_fd)
		max_fd = g_response_socket;
	while (1)
	{
		FD_ZERO(&read_set);
		FD_SET(g_response_pipe[0], &read_set);
		FD_SET(g_response_socket, &read_set);
		status = pselect(max_fd + 1, &read_set, NULL, NULL, NULL, NULL);
		if (status == -1 && errno == EINTR)
			continue ;
		if (status == -1 || g_response_overflow)
			return (-1);
		if (FD_ISSET(g_response_socket, &read_set)
			&& handle_session_request() == -1)
			return (-1);
		if (FD_ISSET(g_response_pipe[0], &read_set)
			&& respond_to_bit() == -1)
			return (-1);
	}
}

int	main(void)
{
	if (prepare_response_channel() == -1 || atexit(cleanup_server) != 0)
	{
		cleanup_server();
		mt_putstr_fd("server: failed to create response channel\n",
			STDERR_FILENO);
		return (1);
	}
	if (install_signal_handlers() == -1)
	{
		mt_putstr_fd("server: failed to install signal handlers\n",
			STDERR_FILENO);
		return (1);
	}
	mt_putnbr_fd(getpid(), STDOUT_FILENO);
	write(STDOUT_FILENO, "\n", 1);
	if (run_response_loop() == -1)
	{
		mt_putstr_fd("server: response channel failed\n", STDERR_FILENO);
		return (1);
	}
	return (0);
}
