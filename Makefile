NAME	:= chessbot
CFLAGS	:= -Wall -Wextra -g -O3
# CFLAGS := -Wall -Wextra -pedantic -std=c89 -O3 -flto -march=native

HEADERS := $(wildcard include/*.h)
SRCS := $(wildcard src/*.c)
OBJS := $(patsubst src/%.c,build/%.o,$(SRCS))

build/%.o: src/%.c $(HEADERS) Makefile
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $< -o $@ -c -Iinclude

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

run: $(NAME)
	./cute_chess

clean:
	rm -rf build/

fclean:
	rm -rf build/
	rm -f $(NAME)

re:
	${MAKE} fclean
	${MAKE}
