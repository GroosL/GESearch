CC ?= cc
CFLAGS ?= -Wall -Wextra -O3 -march=x86-64-v3 -mavx2 -flto -falign-functions=32 -falign-loops=32 -fno-semantic-interposition -D_GNU_SOURCE
LDFLAGS ?= -flto -lpthread
TARGET = search

SRC = main.c queue.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

pgo:
	@echo "==> Building instrumented binary for profile generation..."
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) -fprofile-generate" LDFLAGS="$(LDFLAGS) -fprofile-generate"
	@echo "==> Collecting execution profile..."
	./$(TARGET) . search -r > /dev/null 2>&1 || true
	@echo "==> Rebuilding binary with profile-guided optimization..."
	$(CC) $(CFLAGS) -fprofile-use -fprofile-correction -o $(TARGET) $(SRC) $(LDFLAGS)
	@rm -f *.gcda *.gcno
	@echo "==> PGO build complete!"

debug: CFLAGS = -Wall -Wextra -g -O0 -D_GNU_SOURCE
debug: LDFLAGS = -lpthread
debug: $(TARGET)

clean:
	rm -f *.o *.gcda *.gcno $(TARGET)

.PHONY: all pgo debug clean


