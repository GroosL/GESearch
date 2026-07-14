#include <linux/limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct {
	char path[PATH_MAX];
	int dirfd;
} Entry;

typedef struct {
	Entry *entries;
	size_t head;
	size_t tail;
	size_t capacity;
	size_t count;

	pthread_mutex_t mutex;
	pthread_cond_t cond;
} Queue;

void queueInit(Queue* q, size_t cap);
void queuePush(Queue* q, const char* path, int fd);
bool queuePop(Queue* q, Entry* e);
void queueGrow(Queue *q);
void queueDestroy(Queue *q);
