CC = cc
CFLAGS = -Wall -O3
TARGET = search

SRC = main.c queue.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) -lpthread

debug: CFLAGS = -Wall -g -O0
debug: $(TARGET)

clean:
	rm -f *.o $(TARGET)
