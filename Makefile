all:
	gcc -g -c main.c -Wall -Werror -Wpedantic
	gcc -g main.o -lpthread -lm -lbladeRF -Wall -Werror -Wpedantic -o vna
