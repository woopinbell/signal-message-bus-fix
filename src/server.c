#include "minitalk.h"

#include <errno.h>
#include <unistd.h>

static volatile sig_atomic_t	g_current_byte;
static volatile sig_atomic_t	g_received_bits;
static volatile sig_atomic_t	g_client_pid;
static volatile sig_atomic_t	g_line_started;

static void	reset_session(int close_partial_line)
{
	if (close_partial_line && g_line_started)
		write(STDOUT_FILENO, "\n", 1);
	g_current_byte = 0;
	g_received_bits = 0;
	g_client_pid = 0;
	g_line_started = 0;
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

static void	handle_bit(int signal, siginfo_t *info, void *context)
{
	unsigned char	output;
	int				saved_errno;

	saved_errno = errno;
	(void)context;
	if (info == NULL || info->si_pid <= 0)
	{
		errno = saved_errno;
		return ;
	}
	if (g_client_pid != 0 && g_client_pid != info->si_pid)
	{
		if (kill((pid_t)g_client_pid, 0) == -1 && errno == ESRCH)
			reset_session(1);
		else
		{
			kill(info->si_pid, MT_NACK_SIGNAL);
			errno = saved_errno;
			return ;
		}
	}
	if (g_client_pid == 0)
		g_client_pid = info->si_pid;
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
	if (kill(info->si_pid, MT_ACK_SIGNAL) == -1 && errno == ESRCH)
		reset_session(1);
	errno = saved_errno;
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

int	main(void)
{
	if (install_signal_handlers() == -1)
	{
		mt_putstr_fd("server: failed to install signal handlers\n", STDERR_FILENO);
		return (1);
	}
	mt_putnbr_fd(getpid(), STDOUT_FILENO);
	write(STDOUT_FILENO, "\n", 1);
	while (1)
		pause();
	return (0);
}
