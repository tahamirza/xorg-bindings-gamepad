main: main.c config.gen.inc.c
	gcc -Wall -Wextra -O3 main.c -lxcb -ludev -o main

config.gen.inc.c: config.awk config.in
	./config.awk config.in > config.gen.inc.c
