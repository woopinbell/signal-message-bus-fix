#include "minitalk.h"

#include <errno.h>
#include <unistd.h>

#ifndef MT_WRITE_CALL
# define MT_WRITE_CALL write
#endif

size_t	mt_strlen(const char *text)
{
	size_t	length;

	length = 0;
	while (text != NULL && text[length] != '\0')
		length++;
	return (length);
}

int	mt_write_all(int fd, const void *buffer, size_t size)
{
	const unsigned char	*bytes;
	size_t				offset;
	ssize_t				written;

	bytes = (const unsigned char *)buffer;
	offset = 0;
	while (offset < size)
	{
		written = MT_WRITE_CALL(fd, bytes + offset, size - offset);
		if (written == -1 && errno == EINTR)
			continue ;
		if (written == -1)
			return (-1);
		if (written == 0)
		{
			errno = EIO;
			return (-1);
		}
		offset += (size_t)written;
	}
	return (0);
}

void	mt_putstr_fd(const char *text, int fd)
{
	if (text == NULL)
		return ;
	mt_write_all(fd, text, mt_strlen(text));
}

void	mt_putnbr_fd(pid_t number, int fd)
{
	char	buffer[32];
	int		index;
	long	value;

	value = (long)number;
	if (value == 0)
	{
		mt_write_all(fd, "0", 1);
		return ;
	}
	if (value < 0)
	{
		mt_write_all(fd, "-", 1);
		value = -value;
	}
	index = 0;
	while (value > 0)
	{
		buffer[index++] = (char)('0' + (value % 10));
		value /= 10;
	}
	while (index > 0)
		mt_write_all(fd, &buffer[--index], 1);
}
