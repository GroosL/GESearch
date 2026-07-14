#include "queue.h"
#include <stdlib.h>
#include <string.h>
void queueInit(Queue* q, size_t cap) {
	q->entries = (Entry*)malloc(cap * sizeof(Entry));
	q->head = 0;
	q->tail = 0;
	q->count = 0;
	q->capacity = cap;
}

void queuePush(Queue *q, const char *path, int fd) {
	if (q->count == q->capacity)
		queueGrow(q);
	strcpy(q->entries[q->tail].path, path);
	q->entries[q->tail].dirfd = fd;
	q->tail = (q->tail + 1) % q->capacity;
	q->count++;
}

bool queuePop(Queue *q, Entry *out) {
    if (q->count == 0)
        return false;

    *out = q->entries[q->head];

    q->head = (q->head + 1) % q->capacity;
    q->count--;

    return true;
}

void queueGrow(Queue *q) {
    size_t newCapacity = q->capacity * 2;

    Entry *newEntries = (Entry*)malloc(newCapacity * sizeof(Entry));

    for (size_t i = 0; i < q->count; i++) {
        newEntries[i] = q->entries[(q->head + i) % q->capacity];
    }

    free(q->entries);

    q->entries = newEntries;
    q->capacity = newCapacity;
    q->head = 0;
    q->tail = q->count;
}

void queueDestroy(Queue *q) {
	free(q->entries);
}
