NAME_SERVER := server
NAME_CLIENT := client

CC := cc
CFLAGS := -Wall -Wextra -Werror -Iinclude
RM := rm -rf

.PHONY: all clean fclean re test

all:
	@printf 'source layout is ready\n'

clean:
	$(RM) obj

fclean: clean
	$(RM) $(NAME_SERVER) $(NAME_CLIENT)

re: fclean all

test: all
	@printf 'tests are added in a later step\n'

