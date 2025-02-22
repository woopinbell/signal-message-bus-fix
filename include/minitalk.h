#ifndef MINITALK_H
# define MINITALK_H

# include <signal.h>
# include <stddef.h>
# include <sys/types.h>

# define MT_ZERO_SIGNAL SIGUSR1
# define MT_ONE_SIGNAL SIGUSR2
# define MT_ACK_SIGNAL SIGUSR1
# define MT_NACK_SIGNAL SIGUSR2
# define MT_ACK_TIMEOUT_SECONDS 3
# define MT_SIGNAL_GAP_US 5000

void	mt_putstr_fd(const char *text, int fd);
void	mt_putnbr_fd(pid_t number, int fd);
size_t	mt_strlen(const char *text);
int		mt_parse_pid(const char *text, pid_t *pid);

#endif
