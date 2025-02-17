#include "minitalk.h"

#include <unistd.h>

int	main(void)
{
	mt_putnbr_fd(getpid(), STDOUT_FILENO);
	write(STDOUT_FILENO, "\n", 1);
	while (1)
		pause();
	return (0);
}
