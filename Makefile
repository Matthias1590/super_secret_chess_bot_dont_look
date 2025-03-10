NAME	:= chessbot
CFLAGS	:= -Wall -Wextra -g -O3
# CFLAGS := -Wall -Wextra -pedantic -std=c89 -O3 -flto -march=native

HEADERS := $(wildcard include/*.h)

build/%.o: src/%.c $(HEADERS) Makefile
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $< -o $@ -c -Iinclude

$(NAME): build/uci.o build/perft.o build/search.o build/evaluate.o build/generate.o build/move.o build/position.o build/parse.o build/main.o
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -rf build/

fclean:
	rm -rf build/
	rm -f $(NAME)

re:
	${MAKE} fclean
	${MAKE}
