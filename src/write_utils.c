#include "minitalk.h"

#include <unistd.h>

size_t	mt_strlen(const char *text)
{
	size_t	length;

	length = 0;
	while (text != NULL && text[length] != '\0')
		length++;
	return (length);
}

void	mt_putstr_fd(const char *text, int fd)
{
	if (text == NULL)
		return ;
	write(fd, text, mt_strlen(text));
}

void	mt_putnbr_fd(pid_t number, int fd)
{
	char	buffer[32];
	int		index;
	long	value;

	value = (long)number;
	if (value == 0)
	{
		write(fd, "0", 1);
		return ;
	}
	if (value < 0)
	{
		write(fd, "-", 1);
		value = -value;
	}
	index = 0;
	while (value > 0)
	{
		buffer[index++] = (char)('0' + (value % 10));
		value /= 10;
	}
	while (index > 0)
		write(fd, &buffer[--index], 1);
}
