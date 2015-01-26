all:
	gcc -Wall -std=c99 ./src/proxy.c ./src/protocol.c -o ./proxy -levent -lmysql
