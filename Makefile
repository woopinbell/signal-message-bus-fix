NAME_SERVER := server
NAME_CLIENT := client

CC := cc
CFLAGS := -Wall -Wextra -Werror -Iinclude
RM := rm -rf
OBJ_DIR := obj

COMMON_SRC := src/write_utils.c
SERVER_SRC := src/server.c $(COMMON_SRC)
CLIENT_SRC := src/client.c $(COMMON_SRC)

SERVER_OBJ := $(SERVER_SRC:%.c=$(OBJ_DIR)/%.o)
CLIENT_OBJ := $(CLIENT_SRC:%.c=$(OBJ_DIR)/%.o)

.PHONY: all clean fclean re test

all: $(NAME_SERVER) $(NAME_CLIENT)

$(NAME_SERVER): $(SERVER_OBJ)
	$(CC) $(CFLAGS) $(SERVER_OBJ) -o $@

$(NAME_CLIENT): $(CLIENT_OBJ)
	$(CC) $(CFLAGS) $(CLIENT_OBJ) -o $@

$(OBJ_DIR)/%.o: %.c include/minitalk.h
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) obj

fclean: clean
	$(RM) $(NAME_SERVER) $(NAME_CLIENT)

re: fclean all

test: all
	@printf 'tests are added in a later step\n'
