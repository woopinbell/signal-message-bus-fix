#include "minitalk.h"

#include <unistd.h>

int	main(int argc, char **argv)
{
	(void)argv;
	if (argc != 3)
	{
		mt_putstr_fd("usage: ./client <server_pid> <message>\n", STDERR_FILENO);
		return (1);
	}
	return (0);
}
