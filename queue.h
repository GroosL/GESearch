#ifndef QUEUE_H
#define QUEUE_H

#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
  char *path;
  uint16_t len;
} QueueItem;

typedef struct {
  QueueItem *items;
  size_t head;
  size_t tail;
  size_t count;
  size_t capacity;
  size_t pending;
  bool done;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} Queue;

void queueInit(Queue *q, size_t cap);
void queuePushBatch(Queue *q, char **paths, uint16_t *lens, size_t batch_size);
void queuePushSingle(Queue *q, const char *path, uint16_t len);
bool queuePop(Queue *q, char **out_path, uint16_t *out_len);
void queueFinishOne(Queue *q);
void queueDestroy(Queue *q);

#endif




