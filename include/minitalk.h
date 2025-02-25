#ifndef MINITALK_H
# define MINITALK_H

# include <signal.h>
# include <stdint.h>
# include <stddef.h>
# include <sys/types.h>

# define MT_ZERO_SIGNAL SIGUSR1
# define MT_ONE_SIGNAL SIGUSR2
# define MT_ACK_TIMEOUT_SECONDS 3
# define MT_RESPONSE_MAGIC 0x4d54414bU
# define MT_REQUEST_ACQUIRE 1U
# define MT_RESPONSE_READY 1U
# define MT_RESPONSE_ACK 2U
# define MT_RESPONSE_OK 0
# define MT_RESPONSE_BUSY 1
# define MT_RESPONSE_PATH_SIZE 104

typedef struct s_mt_request
{
	uint32_t	magic;
	uint32_t	kind;
	uint32_t	nonce;
	pid_t		client_pid;
}	t_mt_request;

typedef struct s_mt_response
{
	uint32_t	magic;
	uint32_t	kind;
	uint32_t	token;
	int32_t		status;
	pid_t		server_pid;
}	t_mt_response;

void	mt_putstr_fd(const char *text, int fd);
void	mt_putnbr_fd(pid_t number, int fd);
size_t	mt_strlen(const char *text);
int		mt_parse_pid(const char *text, pid_t *pid);
int		mt_write_all(int fd, const void *buffer, size_t size);
int		mt_runtime_dir(char *buffer, size_t size);
int		mt_response_path(char *buffer, size_t size, const char *role,
			pid_t pid);

#endif
