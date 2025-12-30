#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void computeLPSArray(char *pattern, int m, int *lps);
int KMPSearch(char *pattern, char *string, int caseInsensitive);

int main(int argc, char *argv[]) {
  if (argc < 3 || argc > 4) {
    puts("Usage: search <directory> <pattern> [-i]");
    puts("Flag: -i for case insensitive search");
    exit(1);
  }
  struct dirent *at;
  char *dir = argv[1];
  DIR *dr = opendir(dir);

  if (!dr) {
    printf("Could not open directory %s\n", dir);
    exit(1);
  }

  int flag = 0;
  if (argc == 4 && strcmp(argv[3], "-i") == 0)
    flag = 1;
  while ((at = readdir(dr))) {
    if (KMPSearch(argv[2], at->d_name, flag))
      printf("%s\n", at->d_name);
  }

  closedir(dr);
  return 0;
}

void computeLPSArray(char *pattern, int m, int *lps) {
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

int KMPSearch(char *pattern, char *string, int caseInsensitive) {
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
