#include "minitalk.h"

static int	mt_is_digit(char character)
{
	return (character >= '0' && character <= '9');
}

int	mt_parse_pid(const char *text, pid_t *pid)
{
	long	value;
	size_t	index;

	if (text == NULL || text[0] == '\0' || pid == NULL)
		return (0);
	value = 0;
	index = 0;
	while (text[index] != '\0')
	{
		if (!mt_is_digit(text[index]))
			return (0);
		value = value * 10 + (text[index] - '0');
		if (value > 999999)
			return (0);
		index++;
	}
	if (value <= 1)
		return (0);
	*pid = (pid_t)value;
	return (1);
}
