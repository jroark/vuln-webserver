# To deal with the Tabs getting converted to spaces, run vim as "vim -u NONE ./Makefile"
# $@ - left side of rule.
# $^ - right side of rule.
# $< - first prerequisite (usually the source file)

CC = gcc -pthread -std=gnu99 -O2

DEPS = threadpool/threadpool.h threadpool/threads.h utils.h string_util.h

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $<

all: server bruteforce

OBJ_S = server.o threadpool/threadpool.o
server: $(OBJ_S)
	$(CC) -o $@ $^

OBJ_C = client.o
client: $(OBJ_C)
	$(CC) -o $@ $^

OBJ_B = bruteforce.o
bruteforce: $(OBJ_B)
	$(CC) -lcurl -o $@ $^

clean:
	rm -rf ./server
	rm -rf ./client
	rm -rf ./bruteforce
	find . -name "*.o" -type f -delete
