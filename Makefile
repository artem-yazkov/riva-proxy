all:
	gcc ./src/proxy.c ./src/protocol.c -o ./proxy -levent -lmysql
