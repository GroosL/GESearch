CC = cc
CFLAGS = -Wall -O2
TARGET = search

all: $(TARGET)

$(TARGET): main.o
	$(CC) $(CFLAGS) -o $(TARGET) main.o

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

debug: CFLAGS = -Wall -g -O0
debug: $(TARGET)

clean:
	rm -f *.o $(TARGET)
