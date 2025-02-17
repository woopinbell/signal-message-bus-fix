#include "minitalk.h"

#include <unistd.h>

static volatile sig_atomic_t	g_current_byte;
static volatile sig_atomic_t	g_received_bits;

static void	handle_bit(int signal, siginfo_t *info, void *context)
{
	unsigned char	output;

	(void)context;
	if (info == NULL || info->si_pid <= 0)
		return ;
	g_current_byte <<= 1;
	if (signal == MT_ONE_SIGNAL)
		g_current_byte |= 1;
	g_received_bits++;
	if (g_received_bits == 8)
	{
		output = (unsigned char)g_current_byte;
		write(STDOUT_FILENO, &output, 1);
		g_current_byte = 0;
		g_received_bits = 0;
	}
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
