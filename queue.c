#include "queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void queueInit(Queue *q, size_t cap) {
  q->entries = (Entry *)malloc(cap * sizeof(Entry));
  if (!q->entries) {
    fprintf(stderr, "queueInit: out of memory\n");
    exit(1);
  }
  q->head = 0;
  q->tail = 0;
  q->count = 0;
	q->pending = 0;
  q->capacity = cap;
	q->done = 0;

  pthread_mutex_init(&q->mutex, NULL);
  pthread_cond_init(&q->cond, NULL);
}

void queuePush(Queue *q, const char *path) {
	pthread_mutex_lock(&q->mutex);

  if (q->count == q->capacity)
    queueGrow(q);

  strcpy(q->entries[q->tail].path, path);
  q->tail = (q->tail + 1) % q->capacity;
  q->count++;
	q->done = false;

	pthread_cond_signal(&q->cond);
	pthread_mutex_unlock(&q->mutex);
}

bool queuePop(Queue *q, Entry *out) {
	pthread_mutex_lock(&q->mutex);

	while (q->count == 0 && !q->done)
        pthread_cond_wait(&q->cond, &q->mutex);

  if (q->count == 0 && q->done) {
		pthread_mutex_unlock(&q->mutex);
    return false;
	}

  *out = q->entries[q->head];

  q->head = (q->head + 1) % q->capacity;
  q->count--;
	q->pending++;

	pthread_mutex_unlock(&q->mutex);

  return true;
}

void queueGrow(Queue *q) {
  size_t newCapacity = q->capacity * 2;

  Entry *newEntries = (Entry *)malloc(newCapacity * sizeof(Entry));
  if (!newEntries) {
    fprintf(stderr, "queueGrow: out of memory\n");
    exit(1);
  }

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

  pthread_mutex_destroy(&q->mutex);
  pthread_cond_destroy(&q->cond);
}
