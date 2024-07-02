CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -g
OBJS = main.o vector.o
EXE = shell

$(EXE): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(EXE)

main.o: main.c
	$(CC) $(CFLAGS) -c main.c -o main.o

vector.o: vector.c
	$(CC) $(CFLAGS) -c vector.c -o vector.o

clean:
	rm $(OBJS) $(EXE)
