main: main.c
	gcc -Wall -Wextra -O3 main.c -lxcb -ludev -o main
