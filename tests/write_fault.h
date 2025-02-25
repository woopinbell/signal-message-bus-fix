#ifndef WRITE_FAULT_H
# define WRITE_FAULT_H

# include <stddef.h>
# include <sys/types.h>

ssize_t	mt_test_write(int fd, const void *buffer, size_t size);
ssize_t	mt_test_event_write(int fd, const void *buffer, size_t size);

#endif
