default: server client
server: server.c 
	gcc -g -W server.c -o server

deliver: client.c
	gcc -g -W deliver.c -o client
