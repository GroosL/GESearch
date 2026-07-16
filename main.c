#include "queue.h"
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_IGNORE 32
#define PATTERN_MAX 8096

static char stdoutBuffer[1 << 20];

static const char *g_pattern;
static int g_patternLength;

static bool g_caseInsensitive;
static bool g_recursive;
static bool g_exact;

static const char *g_ignoreList[MAX_IGNORE];
static uint8_t g_ignoreCount = 0;

static int g_shift[256];
static unsigned char g_lower[256];
static unsigned char g_patternLower[PATTERN_MAX];

#define OUTPUT_BUFFER_SIZE (64 * 1024)

typedef struct {
  char data[OUTPUT_BUFFER_SIZE];
  size_t len;
} OutputBuffer;

void preprocess();
int BMHSearch(const char *string);
void searchFile(char *path, size_t len, Queue *q, OutputBuffer *out);

static inline void flushOutput(OutputBuffer *out) {
  if (out->len == 0)
    return;

  write(STDOUT_FILENO, out->data, out->len);
  out->len = 0;
}

static inline void outputPath(OutputBuffer *out, const char *path,
                              size_t pathLen) {
  if (pathLen + 1 > OUTPUT_BUFFER_SIZE) {
    write(STDOUT_FILENO, path, pathLen);
    write(STDOUT_FILENO, "\n", 1);
    return;
  }

  if (out->len + pathLen + 1 > OUTPUT_BUFFER_SIZE)
    flushOutput(out);

  memcpy(out->data + out->len, path, pathLen);
  out->len += pathLen;
  out->data[out->len++] = '\n';
}

static bool pathContains(const char *path, const char *ignore,
                         bool caseInsensitive) {
  size_t n = strlen(ignore);
  const char *p = path;

  while (1) {
    p = caseInsensitive ? strcasestr(p, ignore) : strstr(p, ignore);
    if (!p)
      return false;

    bool left = (p == path) || p[-1] == '/';
    bool right = p[n] == '\0' || p[n] == '/';

    if (left && right)
      return true;

    p++;
  }
}

static bool shouldIgnore(const char *path) {
  for (uint8_t i = 0; i < g_ignoreCount; i++) {
    if (pathContains(path, g_ignoreList[i], g_caseInsensitive))
      return true;
  }
  return false;
}

void *worker(void *arg) {
  Queue *q = arg;
  Entry e;
	
	OutputBuffer out = {0};

  while (queuePop(q, &e)) {
    searchFile(e.path, strlen(e.path), q, &out);

    pthread_mutex_lock(&q->mutex);

    q->pending--;

    if (q->count == 0 && q->pending == 0) {
      q->done = true;
      pthread_cond_broadcast(&q->cond);
    }

    pthread_mutex_unlock(&q->mutex);
  }

	flushOutput(&out);
  return NULL;
}

int main(int argc, char *argv[]) {
  setvbuf(stdout, stdoutBuffer, _IOFBF, sizeof(stdoutBuffer));
  if (argc < 3) {
    puts("Usage: search <path> <g_pattern> [-s] [-r] [-e] [-i <dir>]");
    puts("Flag: -s for case insensitive search");
    puts("Flag: -r for g_recursive search");
    puts("Flag: -e for g_exact search");
    puts("Flag: -i <dir> to ignore a path");
    exit(1);
  }
  g_exact = 0;
  g_recursive = 0;
  g_caseInsensitive = 0;

  char path[PATH_MAX];
  strcpy(path, argv[1]);

  g_pattern = argv[2];
  g_patternLength = strlen(g_pattern);

  if (g_patternLength > PATTERN_MAX) {
    fprintf(stderr, "Pattern too long!\n");
    return 1;
  }

  for (int i = 3; i < argc; i++) {
    if (strcmp(argv[i], "-s") == 0)
      g_caseInsensitive = 1;

    if (strcmp(argv[i], "-r") == 0)
      g_recursive = 1;

    if (strcmp(argv[i], "-e") == 0)
      g_exact = 1;

    if (strcmp(argv[i], "-i") == 0) {
      if (g_ignoreCount < MAX_IGNORE && i + 1 < argc)
        g_ignoreList[g_ignoreCount++] = argv[++i];
      else
        fprintf(stderr, "Warning: maximum ignore list size (%d) reached\n",
                MAX_IGNORE);
    }
  }

  preprocess();

  Queue q;
  queueInit(&q, 256);
  queuePush(&q, path);

  int n = (int)sysconf(_SC_NPROCESSORS_ONLN);
  if (n < 1)
    n = 1;

  pthread_t threads[n];

  for (int i = 0; i < n; i++)
    pthread_create(&threads[i], NULL, worker, &q);

  for (int i = 0; i < n; i++)
    pthread_join(threads[i], NULL);

  queueDestroy(&q);

  return 0;
}

void preprocess(void) {
  for (int i = 0; i < 256; i++)
    g_lower[i] = (unsigned char)tolower(i);

  if (g_caseInsensitive) {
    for (int i = 0; i < g_patternLength; i++)
      g_patternLower[i] = g_lower[(unsigned char)g_pattern[i]];
  }

  for (int i = 0; i < 256; i++)
    g_shift[i] = g_patternLength;

  if (g_caseInsensitive) {
    for (int i = 0; i < g_patternLength - 1; i++)
      g_shift[g_patternLower[i]] = g_patternLength - 1 - i;
  } else {
    for (int i = 0; i < g_patternLength - 1; i++)
      g_shift[(unsigned char)g_pattern[i]] = g_patternLength - 1 - i;
  }
}

int BMHSearch(const char *string) {
  int n = strlen(string);

  if (n < g_patternLength)
    return 0;

  size_t i = 0;

  while (i <= (size_t)(n - g_patternLength)) {
    int j = g_patternLength - 1;

    if (g_caseInsensitive) {
      while (j >= 0 &&
             g_lower[(unsigned char)string[i + j]] == g_patternLower[j]) {
        j--;
      }

      if (j < 0)
        return 1;

      i += g_shift[g_lower[(unsigned char)string[i + g_patternLength - 1]]];
    } else {
      while (j >= 0 && string[i + j] == g_pattern[j]) {
        j--;
      }

      if (j < 0)
        return 1;

      i += g_shift[(unsigned char)string[i + g_patternLength - 1]];
    }
  }

  return 0;
}

void searchFile(char *path, size_t len, Queue *q, OutputBuffer *out) {
  DIR *dr = opendir(path);

  if (!dr) {
    fprintf(stderr, "Could not open path %s\n", path);
    return;
  }

  struct dirent *at;

  while ((at = readdir(dr))) {
    if (strcmp(at->d_name, ".") == 0 || strcmp(at->d_name, "..") == 0)
      continue;

    size_t oldLen = len;
    if (!(len == 1 && path[0] == '/'))
      path[len++] = '/';

    size_t nameLen = strlen(at->d_name);
    if (len + nameLen >= PATH_MAX) {
      path[oldLen] = '\0';
      len = oldLen;
      continue;
    }
    memcpy(path + len, at->d_name, nameLen + 1);
    len += nameLen;

    int found = 0;
    if (g_exact) {
      found = g_caseInsensitive ? strcasecmp(at->d_name, g_pattern) == 0
                                : strcmp(at->d_name, g_pattern) == 0;
    } else {
      found = BMHSearch(at->d_name);
    }

    if (found) {
			outputPath(out, path, len);
    }

    if (at->d_type == DT_DIR && g_recursive && !shouldIgnore(path)) {
      queuePush(q, path);
    }

    path[oldLen] = '\0';
    len = oldLen;
  }

  closedir(dr);
}
