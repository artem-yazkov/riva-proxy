all:
	gcc -Wall -std=gnu99 ./src/proxy.c ./src/protocol.c ./src/session.c ./src/aux.c -o ./proxy -levent -lmysql
