#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "queue.h"
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_IGNORE 32
#define PATTERN_MAX 8096

struct linux_dirent64 {
  uint64_t d_ino;
  int64_t d_off;
  unsigned short d_reclen;
  unsigned char d_type;
  char d_name[];
};

static const char *g_pattern;
static int g_patternLength;

static bool g_caseInsensitive;
static bool g_recursive;
static bool g_exact;

typedef enum {
  IGNORE_EXACT_NAME,   // e.g. .git, node_modules (no slash)
  IGNORE_PREFIX_PATH,  // e.g. /home/leonardo/HDD (starts with /)
  IGNORE_SUBSTRING     // e.g. foo/bar (relative path with slash)
} IgnoreType;

typedef struct {
  const char *pattern;
  size_t len;
  IgnoreType type;
} IgnoreRule;

static IgnoreRule g_ignoreRules[MAX_IGNORE];
static uint8_t g_ignoreCount = 0;

static int g_shift[256];
static unsigned char g_lower[256];
static unsigned char g_patternLower[PATTERN_MAX];

#define OUTPUT_BUFFER_SIZE (64 * 1024)

typedef struct {
  char data[OUTPUT_BUFFER_SIZE];
  size_t len;
} OutputBuffer;

static pthread_mutex_t g_stdout_mutex = PTHREAD_MUTEX_INITIALIZER;

void preprocess(void);
static inline bool BMHSearch(const char *string, size_t n);
static inline bool substringMatch(const char *name, size_t name_len);
static void searchDirectory(const char *dir_path, size_t dir_len,
                            Queue *q, OutputBuffer *out);

static inline void flushOutput(OutputBuffer *out) {
  if (out->len == 0)
    return;

  pthread_mutex_lock(&g_stdout_mutex);
  size_t total_written = 0;
  while (total_written < out->len) {
    ssize_t w = write(STDOUT_FILENO, out->data + total_written,
                      out->len - total_written);
    if (w <= 0)
      break;
    total_written += (size_t)w;
  }
  pthread_mutex_unlock(&g_stdout_mutex);
  out->len = 0;
}

static inline void outputPath(OutputBuffer *out, const char *path,
                              size_t pathLen) {
  if (pathLen + 1 > OUTPUT_BUFFER_SIZE) {
    pthread_mutex_lock(&g_stdout_mutex);
    ssize_t ret1 = write(STDOUT_FILENO, path, pathLen);
    ssize_t ret2 = write(STDOUT_FILENO, "\n", 1);
    (void)ret1;
    (void)ret2;
    pthread_mutex_unlock(&g_stdout_mutex);
    return;
  }

  if (out->len + pathLen + 1 > OUTPUT_BUFFER_SIZE)
    flushOutput(out);

  memcpy(out->data + out->len, path, pathLen);
  out->len += pathLen;
  out->data[out->len++] = '\n';
}

static inline bool shouldIgnore(const char *path, size_t path_len,
                                const char *name, size_t name_len) {
  for (uint8_t i = 0; i < g_ignoreCount; i++) {
    const IgnoreRule *r = &g_ignoreRules[i];

    if (r->type == IGNORE_EXACT_NAME) {
      if (name_len == r->len) {
        if (g_caseInsensitive) {
          if (strncasecmp(name, r->pattern, name_len) == 0)
            return true;
        } else {
          if (memcmp(name, r->pattern, name_len) == 0)
            return true;
        }
      }
    } else if (r->type == IGNORE_PREFIX_PATH) {
      if (path_len >= r->len) {
        if (g_caseInsensitive) {
          if (strncasecmp(path, r->pattern, r->len) == 0 &&
              (path[r->len] == '\0' || path[r->len] == '/'))
            return true;
        } else {
          if (memcmp(path, r->pattern, r->len) == 0 &&
              (path[r->len] == '\0' || path[r->len] == '/'))
            return true;
        }
      }
    } else {
      const char *p = path;
      while (1) {
        p = g_caseInsensitive ? strcasestr(p, r->pattern) : strstr(p, r->pattern);
        if (!p)
          break;

        bool left = (p == path) || p[-1] == '/';
        bool right = p[r->len] == '\0' || p[r->len] == '/';

        if (left && right)
          return true;

        p++;
      }
    }
  }
  return false;
}

void *worker(void *arg) {
  Queue *q = (Queue *)arg;
  char *path;
  uint16_t len;
  OutputBuffer out = {0};

  while (queuePop(q, &path, &len)) {
    searchDirectory(path, len, q, &out);
    free(path);
    queueFinishOne(q);
  }

  flushOutput(&out);
  return NULL;
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    puts("Usage: search <path> <pattern> [-s] [-r] [-e] [-i <dir>]");
    puts("Flag: -s for case insensitive search");
    puts("Flag: -r for recursive search");
    puts("Flag: -e for exact search");
    puts("Flag: -i <dir> to ignore a path");
    exit(1);
  }
  g_exact = false;
  g_recursive = false;
  g_caseInsensitive = false;

  char path[PATH_MAX];
  strncpy(path, argv[1], sizeof(path) - 1);
  path[sizeof(path) - 1] = '\0';

  size_t plen = strlen(path);
  while (plen > 1 && path[plen - 1] == '/') {
    path[--plen] = '\0';
  }

  g_pattern = argv[2];
  g_patternLength = strlen(g_pattern);

  if (g_patternLength > PATTERN_MAX) {
    fprintf(stderr, "Pattern too long!\n");
    return 1;
  }

  for (int i = 3; i < argc; i++) {
    if (strcmp(argv[i], "-s") == 0) {
      g_caseInsensitive = true;
    } else if (strcmp(argv[i], "-r") == 0) {
      g_recursive = true;
    } else if (strcmp(argv[i], "-e") == 0) {
      g_exact = true;
    } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--ignore") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        if (g_ignoreCount < MAX_IGNORE) {
          const char *ign = argv[++i];
          size_t ign_len = strlen(ign);
          while (ign_len > 1 && ign[ign_len - 1] == '/')
            ign_len--;

          g_ignoreRules[g_ignoreCount].pattern = ign;
          g_ignoreRules[g_ignoreCount].len = ign_len;

          if (ign[0] == '/') {
            g_ignoreRules[g_ignoreCount].type = IGNORE_PREFIX_PATH;
          } else if (strchr(ign, '/') == NULL) {
            g_ignoreRules[g_ignoreCount].type = IGNORE_EXACT_NAME;
          } else {
            g_ignoreRules[g_ignoreCount].type = IGNORE_SUBSTRING;
          }
          g_ignoreCount++;
        } else {
          fprintf(stderr, "Warning: maximum ignore list size (%d) reached\n",
                  MAX_IGNORE);
        }
      } else {
        g_caseInsensitive = true;
      }
    }
  }

  preprocess();

  int n = (int)sysconf(_SC_NPROCESSORS_ONLN);
  if (n < 1)
    n = 1;

  Queue q;
  queueInit(&q, 1024);
  queuePushSingle(&q, path, (uint16_t)plen);

  pthread_t threads[n];

  for (int i = 0; i < n; i++) {
    pthread_create(&threads[i], NULL, worker, &q);
  }

  for (int i = 0; i < n; i++) {
    pthread_join(threads[i], NULL);
  }

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

static inline bool BMHSearch(const char *string, size_t n) {
  if (n < (size_t)g_patternLength)
    return false;

  size_t i = 0;
  size_t limit = n - g_patternLength;

  while (i <= limit) {
    int j = g_patternLength - 1;

    if (g_caseInsensitive) {
      while (j >= 0 &&
             g_lower[(unsigned char)string[i + j]] == g_patternLower[j]) {
        j--;
      }

      if (j < 0)
        return true;

      i += g_shift[g_lower[(unsigned char)string[i + g_patternLength - 1]]];
    } else {
      while (j >= 0 && string[i + j] == g_pattern[j]) {
        j--;
      }

      if (j < 0)
        return true;

      i += g_shift[(unsigned char)string[i + g_patternLength - 1]];
    }
  }

  return false;
}

#if defined(__AVX2__)
#include <immintrin.h>

static inline bool avx2SubstringMatch(const char *text, size_t text_len,
                                      const char *pat, size_t pat_len) {
  if (text_len < pat_len) return false;
  if (pat_len == 0) return true;
  if (pat_len == 1) return memchr(text, pat[0], text_len) != NULL;

  char first = pat[0];
  char last = pat[pat_len - 1];

  __m256i v_first = _mm256_set1_epi8(first);
  __m256i v_last = _mm256_set1_epi8(last);

  size_t limit = text_len - pat_len + 1;
  size_t i = 0;

  while (i + 32 <= limit) {
    __m256i block_first = _mm256_loadu_si256((const __m256i *)(text + i));
    __m256i block_last = _mm256_loadu_si256((const __m256i *)(text + i + pat_len - 1));

    __m256i eq_first = _mm256_cmpeq_epi8(block_first, v_first);
    __m256i eq_last = _mm256_cmpeq_epi8(block_last, v_last);

    uint32_t mask = (uint32_t)_mm256_movemask_epi8(_mm256_and_si256(eq_first, eq_last));

    while (mask != 0) {
      int bit = __builtin_ctz(mask);
      if (pat_len <= 2 || memcmp(text + i + bit + 1, pat + 1, pat_len - 2) == 0) {
        return true;
      }
      mask &= mask - 1;
    }

    i += 32;
  }

  if (i < limit) {
    return memmem(text + i, text_len - i, pat, pat_len) != NULL;
  }

  return false;
}

static inline bool avx2CaseSubstringMatch(const char *text, size_t text_len,
                                          const char *pat_lower, size_t pat_len) {
  if (text_len < pat_len) return false;
  if (pat_len == 0) return true;

  char f_low = pat_lower[0];
  char f_up = (char)toupper((unsigned char)f_low);
  char l_low = pat_lower[pat_len - 1];
  char l_up = (char)toupper((unsigned char)l_low);

  __m256i vf_low = _mm256_set1_epi8(f_low);
  __m256i vf_up = _mm256_set1_epi8(f_up);
  __m256i vl_low = _mm256_set1_epi8(l_low);
  __m256i vl_up = _mm256_set1_epi8(l_up);

  size_t limit = text_len - pat_len + 1;
  size_t i = 0;

  while (i + 32 <= limit) {
    __m256i bf = _mm256_loadu_si256((const __m256i *)(text + i));
    __m256i bl = _mm256_loadu_si256((const __m256i *)(text + i + pat_len - 1));

    __m256i eq_f = _mm256_or_si256(_mm256_cmpeq_epi8(bf, vf_low), _mm256_cmpeq_epi8(bf, vf_up));
    __m256i eq_l = _mm256_or_si256(_mm256_cmpeq_epi8(bl, vl_low), _mm256_cmpeq_epi8(bl, vl_up));

    uint32_t mask = (uint32_t)_mm256_movemask_epi8(_mm256_and_si256(eq_f, eq_l));

    while (mask != 0) {
      int bit = __builtin_ctz(mask);
      if (pat_len <= 2 || strncasecmp(text + i + bit + 1, (const char *)pat_lower + 1, pat_len - 2) == 0) {
        return true;
      }
      mask &= mask - 1;
    }

    i += 32;
  }

  if (i < limit) {
    return BMHSearch(text + i, text_len - i);
  }

  return false;
}
#endif

static inline bool substringMatch(const char *name, size_t name_len) {
  if (name_len < (size_t)g_patternLength)
    return false;

#if defined(__AVX2__)
  if (!g_caseInsensitive) {
    return avx2SubstringMatch(name, name_len, g_pattern, g_patternLength);
  } else {
    return avx2CaseSubstringMatch(name, name_len, (const char *)g_patternLower, g_patternLength);
  }
#else
  if (!g_caseInsensitive) {
    if (g_patternLength == 1) {
      return memchr(name, g_pattern[0], name_len) != NULL;
    }
    return memmem(name, name_len, g_pattern, g_patternLength) != NULL;
  } else {
    if (g_patternLength == 1) {
      unsigned char target = g_patternLower[0];
      for (size_t i = 0; i < name_len; i++) {
        if (g_lower[(unsigned char)name[i]] == target)
          return true;
      }
      return false;
    }
    return BMHSearch(name, name_len);
  }
#endif
}

static void searchDirectory(const char *dir_path, size_t dir_len,
                            Queue *q, OutputBuffer *out) {
  int fd = open(dir_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOATIME);
  if (fd < 0) {
    fd = open(dir_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
      return;
  }

  char full_path[PATH_MAX];
  if (dir_len >= PATH_MAX - 1) {
    close(fd);
    return;
  }
  memcpy(full_path, dir_path, dir_len);

  size_t prefix_len = dir_len;
  if (!(prefix_len == 1 && full_path[0] == '/')) {
    full_path[prefix_len++] = '/';
  }

  char *subdirs[1024];
  uint16_t subdir_lens[1024];
  size_t subdir_count = 0;

  char d_buf[128 * 1024];
  long nread;

  while ((nread = syscall(SYS_getdents64, fd, d_buf, sizeof(d_buf))) > 0) {
    for (long bpos = 0; bpos < nread;) {
      struct linux_dirent64 *d = (struct linux_dirent64 *)(d_buf + bpos);
      bpos += d->d_reclen;

      const char *name = d->d_name;
      if (name[0] == '.') {
        if (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))
          continue;
      }

      size_t name_len = strlen(name);

      bool found = false;
      if (g_exact) {
        if (name_len == (size_t)g_patternLength) {
          found = g_caseInsensitive
                      ? (strncasecmp(name, g_pattern, name_len) == 0)
                      : (memcmp(name, g_pattern, name_len) == 0);
        }
      } else {
        found = substringMatch(name, name_len);
      }

      if (found) {
        if (prefix_len + name_len < PATH_MAX) {
          memcpy(full_path + prefix_len, name, name_len + 1);
          outputPath(out, full_path, prefix_len + name_len);
        }
      }

      // DO NOT follow symlinks - only recurse into DT_DIR (exact match to original behavior)
      if (d->d_type == DT_DIR && g_recursive) {
        if (prefix_len + name_len < PATH_MAX) {
          memcpy(full_path + prefix_len, name, name_len + 1);
          size_t total_len = prefix_len + name_len;
          if (!shouldIgnore(full_path, total_len, name, name_len)) {
            if (subdir_count >= 1024) {
              queuePushBatch(q, subdirs, subdir_lens, subdir_count);
              subdir_count = 0;
            }
            subdirs[subdir_count] = strndup(full_path, total_len);
            subdir_lens[subdir_count] = (uint16_t)total_len;
            subdir_count++;
          }
        }
      }
    }
  }

  close(fd);

  if (subdir_count > 0) {
    queuePushBatch(q, subdirs, subdir_lens, subdir_count);
  }
}






