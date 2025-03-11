NAME	:= chessbot
# CFLAGS	:= -Wall -Wextra -g3 -O3
CFLAGS := -Wall -Wextra -O3 -flto -march=native

HEADERS := $(wildcard include/*.h)
SRCS := $(wildcard src/*.c)
OBJS := $(patsubst src/%.c,build/%.o,$(SRCS))

build/%.o: src/%.c $(HEADERS) Makefile
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $< -o $@ -c -Iinclude

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

cutechess: $(NAME)
	./cute_chess

clean:
	rm -rf build/

fclean:
	rm -rf build/
	rm -f $(NAME)

re:
	${MAKE} fclean
	${MAKE}
