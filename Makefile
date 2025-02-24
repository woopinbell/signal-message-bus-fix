NAME_SERVER := server
NAME_CLIENT := client
NAME_SESSION_SENDER := tests/session_sender
NAME_MASKED_EXEC := tests/masked_exec
NAME_RESPONSE_SERVER := tests/response_server
NAME_FAULT_SERVER := tests/fault_server

CC := cc
CFLAGS := -Wall -Wextra -Werror -Iinclude
FAULT_CFLAGS := $(CFLAGS) -DMT_WRITE_CALL=mt_test_write -include tests/write_fault.h
RM := rm -rf
OBJ_DIR := obj
FAULT_OBJ_DIR := obj/fault

COMMON_SRC := src/write_utils.c src/parse_pid.c src/response_channel.c
SERVER_SRC := src/server.c $(COMMON_SRC)
CLIENT_SRC := src/client.c $(COMMON_SRC)

COMMON_OBJ := $(COMMON_SRC:%.c=$(OBJ_DIR)/%.o)
SERVER_OBJ := $(OBJ_DIR)/src/server.o $(COMMON_OBJ)
CLIENT_OBJ := $(OBJ_DIR)/src/client.o $(COMMON_OBJ)
SESSION_SENDER_OBJ := $(OBJ_DIR)/tests/session_sender.o $(COMMON_OBJ)
MASKED_EXEC_OBJ := $(OBJ_DIR)/tests/masked_exec.o
RESPONSE_SERVER_OBJ := $(OBJ_DIR)/tests/response_server.o $(COMMON_OBJ)
FAULT_SERVER_OBJ := $(SERVER_SRC:%.c=$(FAULT_OBJ_DIR)/%.o) \
	$(OBJ_DIR)/tests/write_fault.o

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

$(NAME_RESPONSE_SERVER): $(RESPONSE_SERVER_OBJ)
	$(CC) $(CFLAGS) $(RESPONSE_SERVER_OBJ) -o $@

$(NAME_FAULT_SERVER): $(FAULT_SERVER_OBJ)
	$(CC) $(CFLAGS) $(FAULT_SERVER_OBJ) -o $@

$(OBJ_DIR)/%.o: %.c include/minitalk.h
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(FAULT_OBJ_DIR)/%.o: %.c include/minitalk.h tests/write_fault.h
	mkdir -p $(dir $@)
	$(CC) $(FAULT_CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJ_DIR)

fclean: clean
	$(RM) $(NAME_SERVER) $(NAME_CLIENT) $(NAME_SESSION_SENDER) \
		$(NAME_MASKED_EXEC) $(NAME_RESPONSE_SERVER) $(NAME_FAULT_SERVER)

re: fclean all

test: all $(NAME_SESSION_SENDER) $(NAME_MASKED_EXEC) $(NAME_RESPONSE_SERVER) \
		$(NAME_FAULT_SERVER)
	sh tests/smoke.sh
	sh tests/session_ownership.sh
	sh tests/response_validation.sh
	sh tests/output_failure.sh
