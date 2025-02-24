#define _POSIX_C_SOURCE 200809L

#include "write_fault.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int		g_initialized;
static size_t	g_max_write;
static int		g_interrupt_once;
static int		g_interrupted;
static int		g_zero_once;
static int		g_zeroed;
static int		g_fail_byte;
static int		g_fail_errno;
static long		g_fail_newline_number;
static long		g_newline_count;

static long	read_number(const char *name, long fallback)
{
	const char	*text;
	char		*end;
	long		value;

	text = getenv(name);
	if (text == NULL || text[0] == '\0')
		return (fallback);
	errno = 0;
	value = strtol(text, &end, 10);
	if (errno != 0 || *end != '\0')
		return (fallback);
	return (value);
}

static void	initialize(void)
{
	const char	*text;
	long		value;

	g_initialized = 1;
	value = read_number("MT_TEST_MAX_WRITE", 0);
	if (value > 0)
		g_max_write = (size_t)value;
	g_interrupt_once = read_number("MT_TEST_EINTR_ONCE", 0) == 1;
	g_zero_once = read_number("MT_TEST_ZERO_ONCE", 0) == 1;
	text = getenv("MT_TEST_FAIL_BYTE");
	if (text != NULL && text[0] != '\0' && text[1] == '\0')
		g_fail_byte = (unsigned char)text[0];
	g_fail_errno = (int)read_number("MT_TEST_FAIL_ERRNO", EIO);
	if (read_number("MT_TEST_FAIL_EPIPE", 0) == 1)
		g_fail_errno = EPIPE;
	g_fail_newline_number = read_number("MT_TEST_FAIL_NEWLINE_NUMBER", 0);
}

static int	should_fail(int fd, const unsigned char *bytes, size_t size)
{
	size_t	index;

	if (fd != STDOUT_FILENO)
		return (0);
	index = 0;
	while (index < size)
	{
		if (g_fail_byte != 0 && bytes[index] == (unsigned char)g_fail_byte)
			return (1);
		if (bytes[index] == '\n')
		{
			g_newline_count++;
			if (g_fail_newline_number > 0
				&& g_newline_count == g_fail_newline_number)
				return (1);
		}
		index++;
	}
	return (0);
}

ssize_t	mt_test_write(int fd, const void *buffer, size_t size)
{
	if (!g_initialized)
		initialize();
	if (fd == STDOUT_FILENO && g_zero_once && !g_zeroed)
	{
		g_zeroed = 1;
		return (0);
	}
	if (fd == STDOUT_FILENO && g_interrupt_once && !g_interrupted)
	{
		g_interrupted = 1;
		errno = EINTR;
		return (-1);
	}
	if (should_fail(fd, (const unsigned char *)buffer, size))
	{
		errno = g_fail_errno;
		return (-1);
	}
	if (g_max_write > 0 && size > g_max_write)
		size = g_max_write;
	return (write(fd, buffer, size));
}
