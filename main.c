#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/limits.h>
#include <unistd.h>

void computeLPSArray(const char *pattern, int m, int *lps);
int KMPSearch(const char *pattern, const char *string, int caseInsensitive);
void searchFile(const char *directory, const char *pattern,
                int caseInsensitive, int recursive, int exact);

static int strcmpInsensitive(const char* a, const char* b) {
  while (*a && *b) {
    if (tolower(*a) != tolower(*b)) return 0;
    a++;
    b++;
  }
  return *a == *b;
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    puts("Usage: search <directory> <pattern> [-i] [-r] [-e]");
    puts("Flag: -i for case insensitive search");
    puts("Flag: -r for recursive search");
    puts("Flag: -e for exact search");
    exit(1);
  }

  char *dir = argv[1];
  int caseInsensitive = 0, recursive = 0, exact = 0;

  for (int i = 3; i < argc; i++) {
    if (strcmp(argv[i], "-i") == 0)
      caseInsensitive = 1;

    if (strcmp(argv[i], "-r") == 0)
      recursive = 1;

    if (strcmp(argv[i], "-e") == 0)
      exact = 1;
  }

  searchFile(dir, argv[2], caseInsensitive, recursive, exact);

  return 0;
}

void computeLPSArray(const char *pattern, int m, int *lps) {
  int len = 0;
  lps[0] = 0; // Always 0

  int i = 1;
  while (i < m) {
    if (pattern[i] == pattern[len]) {
      len++;
      lps[i] = len;
      i++;
    } else {
      if (len != 0) {
        len = lps[len - 1];
      } else {
        lps[i] = 0;
        i++;
      }
    }
  }
}

int KMPSearch(const char *pattern, const char *string, int caseInsensitive) {
  int m = strlen(pattern);
  int n = strlen(string);

  int lps[m];
  computeLPSArray(pattern, m, lps);

  int i = 0, j = 0;

  while (i < n) {
    int match = caseInsensitive ? (tolower(pattern[j]) == tolower(string[i]))
                                : (pattern[j] == string[i]);
    if (match) {
      j++;
      i++;
    }

    if (j == m) {
      return 1;
      j = lps[j - 1];
    } else if (i < n && !match) {
      if (j != 0)
        j = lps[j - 1];
      else
        i++;
    }
  }
  return 0;
}

void searchFile(const char *directory, const char *pattern,
                int caseInsensitive, int recursive, int exact) {
  struct dirent *at;
  DIR *dr = opendir(directory);

  if (!dr) {
    fprintf(stderr, "Could not open directory %s\n", directory);
    return;
  }
  
  while ((at = readdir(dr))) {
    if (strcmp(at->d_name, ".") == 0 || strcmp(at->d_name, "..") == 0) continue;

    char path[PATH_MAX];
    if (strcmp(directory, "/") == 0) snprintf(path, PATH_MAX, "%s%s", directory, at->d_name);
    else snprintf(path, PATH_MAX, "%s/%s", directory, at->d_name);
    
    int found = 0;
    
    if (exact) {
      if (caseInsensitive)
        found = strcmpInsensitive(at->d_name, pattern);
      else
        found = strcmp(at->d_name, pattern) == 0;
    }
    else {
      found = KMPSearch(pattern, at->d_name, caseInsensitive);
    }

    if (found)
      printf("%s\n", path);
    
    if (at->d_type == DT_DIR && recursive)
      searchFile(path, pattern, caseInsensitive, recursive, exact);
  }

  closedir(dr);
}
