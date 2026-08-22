#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void queueInit(Queue *q, size_t cap) {
  q->items = malloc(cap * sizeof(QueueItem));
  if (!q->items) {
    fprintf(stderr, "queueInit: out of memory\n");
    exit(1);
  }
  q->head = 0;
  q->tail = 0;
  q->count = 0;
  q->capacity = cap;
  q->pending = 0;
  q->done = false;

  pthread_mutex_init(&q->mutex, NULL);
  pthread_cond_init(&q->cond, NULL);
}

void queuePushSingle(Queue *q, const char *path, uint16_t len) {
  pthread_mutex_lock(&q->mutex);

  if (q->count == q->capacity) {
    size_t new_cap = q->capacity * 2;
    QueueItem *new_items = malloc(new_cap * sizeof(QueueItem));
    if (!new_items) {
      fprintf(stderr, "queueGrow: out of memory\n");
      exit(1);
    }
    for (size_t i = 0; i < q->count; i++) {
      new_items[i] = q->items[(q->head + i) % q->capacity];
    }
    free(q->items);
    q->items = new_items;
    q->head = 0;
    q->tail = q->count;
    q->capacity = new_cap;
  }

  q->items[q->tail].path = strndup(path, len);
  q->items[q->tail].len = len;
  q->tail = (q->tail + 1) % q->capacity;
  q->count++;
  q->done = false;

  pthread_cond_signal(&q->cond);
  pthread_mutex_unlock(&q->mutex);
}

void queuePushBatch(Queue *q, char **paths, uint16_t *lens, size_t batch_size) {
  if (batch_size == 0)
    return;

  pthread_mutex_lock(&q->mutex);

  while (q->count + batch_size > q->capacity) {
    size_t new_cap = q->capacity * 2;
    if (new_cap < q->count + batch_size)
      new_cap = q->count + batch_size + 256;

    QueueItem *new_items = malloc(new_cap * sizeof(QueueItem));
    if (!new_items) {
      fprintf(stderr, "queueGrow: out of memory\n");
      exit(1);
    }
    for (size_t i = 0; i < q->count; i++) {
      new_items[i] = q->items[(q->head + i) % q->capacity];
    }
    free(q->items);
    q->items = new_items;
    q->head = 0;
    q->tail = q->count;
    q->capacity = new_cap;
  }

  for (size_t i = 0; i < batch_size; i++) {
    q->items[q->tail].path = paths[i];
    q->items[q->tail].len = lens[i];
    q->tail = (q->tail + 1) % q->capacity;
  }
  q->count += batch_size;
  q->done = false;

  if (batch_size > 1) {
    pthread_cond_broadcast(&q->cond);
  } else {
    pthread_cond_signal(&q->cond);
  }
  pthread_mutex_unlock(&q->mutex);
}

bool queuePop(Queue *q, char **out_path, uint16_t *out_len) {
  pthread_mutex_lock(&q->mutex);

  while (q->count == 0 && !q->done) {
    pthread_cond_wait(&q->cond, &q->mutex);
  }

  if (q->count == 0 && q->done) {
    pthread_mutex_unlock(&q->mutex);
    return false;
  }

  *out_path = q->items[q->head].path;
  *out_len = q->items[q->head].len;
  q->head = (q->head + 1) % q->capacity;
  q->count--;
  q->pending++;

  pthread_mutex_unlock(&q->mutex);
  return true;
}

void queueFinishOne(Queue *q) {
  pthread_mutex_lock(&q->mutex);
  q->pending--;
  if (q->count == 0 && q->pending == 0) {
    q->done = true;
    pthread_cond_broadcast(&q->cond);
  }
  pthread_mutex_unlock(&q->mutex);
}

void queueDestroy(Queue *q) {
  for (size_t i = 0; i < q->count; i++) {
    free(q->items[(q->head + i) % q->capacity].path);
  }
  free(q->items);
  pthread_mutex_destroy(&q->mutex);
  pthread_cond_destroy(&q->cond);
}




