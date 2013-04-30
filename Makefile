flags=-std=gnu99 -Wall -Wextra -Werror -pedantic

santa: santa.o
	gcc $(flags) santa.o -pthread -o santa

santa.o: santa.c
	gcc $(flags) -c santa.c

clean:
	rm -f santa.o santa *.out

pack:
	zip xvokra00 santa.c Makefile
