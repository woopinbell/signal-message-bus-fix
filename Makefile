NAME_SERVER := server
NAME_CLIENT := client
NAME_SESSION_SENDER := tests/session_sender
NAME_MASKED_EXEC := tests/masked_exec

CC := cc
CFLAGS := -Wall -Wextra -Werror -Iinclude
RM := rm -rf
OBJ_DIR := obj

COMMON_SRC := src/write_utils.c src/parse_pid.c
SERVER_SRC := src/server.c $(COMMON_SRC)
CLIENT_SRC := src/client.c $(COMMON_SRC)

COMMON_OBJ := $(COMMON_SRC:%.c=$(OBJ_DIR)/%.o)
SERVER_OBJ := $(OBJ_DIR)/src/server.o $(COMMON_OBJ)
CLIENT_OBJ := $(OBJ_DIR)/src/client.o $(COMMON_OBJ)
SESSION_SENDER_OBJ := $(OBJ_DIR)/tests/session_sender.o $(COMMON_OBJ)
MASKED_EXEC_OBJ := $(OBJ_DIR)/tests/masked_exec.o

.PHONY: all clean fclean re test

all: $(NAME_SERVER) $(NAME_CLIENT)

$(NAME_SERVER): $(SERVER_OBJ)
	$(CC) $(CFLAGS) $(SERVER_OBJ) -o $@

$(NAME_CLIENT): $(CLIENT_OBJ)
	$(CC) $(CFLAGS) $(CLIENT_OBJ) -o $@

$(NAME_SESSION_SENDER): $(SESSION_SENDER_OBJ)
	$(CC) $(CFLAGS) $(SESSION_SENDER_OBJ) -o $@

$(NAME_MASKED_EXEC): $(MASKED_EXEC_OBJ)
	$(CC) $(CFLAGS) $(MASKED_EXEC_OBJ) -o $@

$(OBJ_DIR)/%.o: %.c include/minitalk.h
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) obj

fclean: clean
	$(RM) $(NAME_SERVER) $(NAME_CLIENT) $(NAME_SESSION_SENDER) \
		$(NAME_MASKED_EXEC)

re: fclean all

test: all $(NAME_SESSION_SENDER) $(NAME_MASKED_EXEC)
	sh tests/smoke.sh
	sh tests/session_ownership.sh
