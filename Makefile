custom-joycon-serviced: main.c config.gen.inc.c
	gcc -Wall -Wextra -O3 -g main.c -lxcb -ludev -o custom-joycon-serviced

config.gen.inc.c: config.awk config.in
	./config.awk config.in > config.gen.inc.c

install: custom-joycon-serviced custom-joycon-user@.service 72-custom-joycon.rules
	install -m 755 custom-joycon-serviced -t /usr/local/bin
	install -m 644 custom-joycon-user@.service -t /etc/systemd/user
	install -m 644 72-custom-joycon.rules -t /etc/udev/rules.d
