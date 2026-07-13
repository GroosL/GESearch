#include <ctype.h>
#include <dirent.h>
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

static bool g_caseInsensitive;
static bool g_recursive;
static bool g_exact;
static const char *g_pattern;
static const char *g_ignoreList[MAX_IGNORE];
static uint8_t g_ignoreCount = 0;
static int g_lps[PATTERN_MAX];
static int g_patternLength;
static int g_shift[PATTERN_MAX];

void computeLPSArray();
int KMPSearch(const char *string);
void searchFile(const char *directory);
void preprocess();

static int strcmpInsensitive(const char *a, const char *b) {
  while (*a && *b) {
    if (tolower((unsigned char)(*a)) != tolower((unsigned char)(*b)))
      return 0;
    a++;
    b++;
  }
  return *a == *b;
}

static bool shouldIgnore(const char *name) {
  for (uint8_t i = 0; i < g_ignoreCount; i++) {
    if (g_caseInsensitive) {
      if (strcmpInsensitive(name, g_ignoreList[i]))
        return true;
    } else {
      if (strcmp(name, g_ignoreList[i]) == 0)
        return true;
    }
  }
  return false;
}

int main(int argc, char *argv[]) {
	// setvbuf(stdout, stdoutBuffer, _IOFBF, sizeof(stdoutBuffer));
  if (argc < 3) {
    puts("Usage: search <directory> <g_pattern> [-s] [-r] [-e] [-i <dir>]");
    puts("Flag: -s for case insensitive search");
    puts("Flag: -r for g_recursive search");
    puts("Flag: -e for g_exact search");
    puts("Flag: -i <dir> to ignore a directory");
    exit(1);
  }
  g_exact = 0;
  g_recursive = 0;
  g_caseInsensitive = 0;
  char *dir = argv[1];
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

  // computeLPSArray();
	preprocess();
  searchFile(dir);

  return 0;
}

void preprocess() {
	for (int i = 0; i < 256; i++)
		g_shift[i] = g_patternLength;

	for (int i = 0; i < g_patternLength - 1; i++) {
		g_shift[(unsigned char)g_pattern[i]] = g_patternLength - 1 - i;
	}
}

int BMHSearch(const char* string) {
	int n = strlen(string);
	
	size_t i = 0, j;
	while (i <= n - g_patternLength) {
		j = g_patternLength;
		while (j > 0 && string[i + j -1] == g_pattern[j-1]) {
			j--;
		}
		if (j == 0) return 1;
		i += g_shift[(unsigned char)string[i + g_patternLength - 1]];
	}
	return 0;
}

void computeLPSArray() {
  int len = 0;
  g_lps[0] = 0; // Always 0

  int i = 1;
  while (i < g_patternLength) {
    int equal = g_caseInsensitive
                    ? (tolower((unsigned char)(g_pattern[i])) == tolower((unsigned char)(g_pattern[len])))
                    : (g_pattern[i] == g_pattern[len]);
    if (equal) {
      len++;
      g_lps[i] = len;
      i++;
    } else {
      if (len != 0) {
        len = g_lps[len - 1];
      } else {
        g_lps[i] = 0;
        i++;
      }
    }
  }
}

int KMPSearch(const char *string) {
  int n = strlen(string);

  int i = 0, j = 0;

  while (i < n) {
    int match = g_caseInsensitive
                    ? (tolower((unsigned char)(g_pattern[j])) == tolower((unsigned char)string[i]))
                    : (g_pattern[j] == string[i]);
    if (match) {
      j++;
      i++;
    }

    if (j == g_patternLength) {
      return 1;
    } else if (i < n && !match) {
      if (j != 0)
        j = g_lps[j - 1];
      else
        i++;
    }
  }
  return 0;
}

void searchFile(const char *directory) {
  struct dirent *at;
  DIR *dr = opendir(directory);

  if (!dr) {
    fprintf(stderr, "Could not open directory %s\n", directory);
    return;
  }

  while ((at = readdir(dr))) {
    if (strcmp(at->d_name, ".") == 0 || strcmp(at->d_name, "..") == 0)
      continue;
		
		size_t len = strlen(at->d_name);

    char path[PATH_MAX];
    if (strcmp(directory, "/") == 0)
      snprintf(path, PATH_MAX, "%s%s", directory, at->d_name);
    else
      snprintf(path, PATH_MAX, "%s/%s", directory, at->d_name);

    int found = 0;

    if (g_exact) {
      if (g_caseInsensitive)
        found = strcmpInsensitive(at->d_name, g_pattern);
      else
        found = strcmp(at->d_name, g_pattern) == 0;
    } else {
			found = BMHSearch(at->d_name);
      // found = KMPSearch(at->d_name);
    }

    if (found)
      printf("%s\n", path);

    if (at->d_type == DT_DIR && g_recursive)
      if (!shouldIgnore(path))
        searchFile(path);
  }

  closedir(dr);
}
