#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compat.h"

#if !defined(_WIN32) && !defined(__CYGWIN__)
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#if defined(__linux__) && !defined(__ANDROID__)
const char *sys_signame[NSIG] = {
    "zero", "HUP",  "INT",  "QUIT", "ILL",    "TRAP",   "ABRT",  "UNUSED", "FPE",  "KILL", "USR1",
    "SEGV", "USR2", "PIPE", "ALRM", "TERM",   "STKFLT", "CHLD",  "CONT",   "STOP", "TSTP", "TTIN",
    "TTOU", "URG",  "XCPU", "XFSZ", "VTALRM", "PROF",   "WINCH", "IO",     "PWR",  "SYS",  NULL};
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#undef NSIG
#define NSIG 33
const char *sys_signame[NSIG] = {
    "zero", "HUP", "INT",  "QUIT", "ILL",    "TRAP", "IOT",   "EMT",  "FPE",  "KILL", "BUS",
    "SEGV", "SYS", "PIPE", "ALRM", "TERM",   "URG",  "STOP",  "TSTP", "CONT", "CHLD", "TTIN",
    "TTOU", "IO",  "XCPU", "XFSZ", "VTALRM", "PROF", "WINCH", "PWR",  "USR1", "USR2", NULL};
#endif

void *xmalloc(size_t size) {
  if (size == 0) return NULL;
  void *p = malloc(size);
  if (!p) abort();
  return p;
}

void *xrealloc(void *p, size_t size) {
  if ((size == 0) && (p == NULL)) return NULL;
  p = realloc(p, size);
  if (!p) abort();
  return p;
}

char *uppercase(char *s) {
  while(*s) {
    *s = (char)toupper((int)*s);
    s++;
  }
  return s;
}

char *lowercase(char *s) {
  while(*s) {
    *s = (char)tolower((int)*s);
    s++;
  }
  return s;
}

bool endswith(const char *str, const char *suffix) {
  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);
  return str_len > suffix_len && !strcmp(str + (str_len - suffix_len), suffix);
}

int get_sig_name(int sig, char *buf, size_t len) {
  int n = snprintf(buf, len, "SIG%s", sig < NSIG ? sys_signame[sig] : "unknown");
  uppercase(buf);
  return n;
}

int get_sig(const char *sig_name) {
  for (int sig = 1; sig < NSIG; sig++) {
    const char *name = sys_signame[sig];
    if (name != NULL && (strcasecmp(name, sig_name) == 0 || strcasecmp(name, sig_name + 3) == 0))
      return sig;
  }
  return atoi(sig_name);
}

int open_uri(char *uri) {
#if defined(_WIN32) || defined(__CYGWIN__)
  return ShellExecute(0, 0, uri, 0, 0, SW_SHOW) > (HINSTANCE)32 ? 0 : 1;
#else
#ifndef __APPLE__
  // check if X server is running
  if (system("xset -q > /dev/null 2>&1")) return 1;
#endif

  pid_t pid = fork();
  if (pid < 0) return 1;

  if (pid == 0) {
    int fd = open("/dev/null", O_WRONLY);
    if (fd >= 0) {
      dup2(fd, STDOUT_FILENO);
      dup2(fd, STDERR_FILENO);
      close(fd);
    }

#ifdef __APPLE__
    char *args[] = {"open", uri, NULL};
#else
    char *args[] = {"xdg-open", uri, NULL};
#endif

    execvp(args[0], args);
    _exit(1);
  }

  int status;
  if (waitpid(pid, &status, 0) < 0) return 1;
  return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
#endif
}

char *generate_unique_filename(const char *dir, const char *filename) {
  // First, check if the original file exists
  size_t dir_len = strlen(dir);
  size_t filename_len = strlen(filename);
  size_t max_path = dir_len + filename_len + 32;  // extra space for " (12345).ext"

  char *filepath = xmalloc(max_path);
  snprintf(filepath, max_path, "%s/%s", dir, filename);

  struct stat st;
  if (stat(filepath, &st) != 0) {
    // File doesn't exist, return original path
    return filepath;
  }

  // File exists, find a unique name
  free(filepath);

  // Split filename into name and extension
  const char *dot = strrchr(filename, '.');
  const char *ext_start = NULL;
  const char *name_end = filename + filename_len;

  if (dot != NULL && dot != filename) {
    ext_start = dot + 1;
    name_end = dot;
  }

  size_t name_part_len = name_end - filename;

  // Try names like "file (1).txt", "file (2).txt", etc.
  for (int i = 1; i <= 99999; i++) {
    size_t needed = dir_len + 1 + name_part_len + 16 + (ext_start ? filename_len - name_part_len : 0) + 1;
    filepath = xmalloc(needed);

    if (ext_start != NULL) {
      snprintf(filepath, needed, "%s/%.*s (%d).%s", dir, (int)name_part_len, filename, i, ext_start);
    } else {
      snprintf(filepath, needed, "%s/%.*s (%d)", dir, (int)name_part_len, filename, i);
    }

    if (stat(filepath, &st) != 0) {
      // This name is available
      return filepath;
    }
    free(filepath);
  }

  // Exhausted all reasonable names
  return NULL;
}

size_t parse_size(const char *size_str) {
  if (size_str == NULL || *size_str == '\0') {
    return 0;
  }

  char *end;
  long long value = strtoll(size_str, &end, 10);

  if (value < 0) {
    return 0;
  }

  size_t multiplier = 1;

  if (*end != '\0') {
    if (strcasecmp(end, "K") == 0 || strcasecmp(end, "KB") == 0) {
      multiplier = 1024;
    } else if (strcasecmp(end, "M") == 0 || strcasecmp(end, "MB") == 0) {
      multiplier = 1024 * 1024;
    } else if (strcasecmp(end, "G") == 0 || strcasecmp(end, "GB") == 0) {
      multiplier = 1024 * 1024 * 1024;
    } else if (strcasecmp(end, "B") == 0) {
      multiplier = 1;
    } else {
      // Unknown suffix, treat as bytes
      multiplier = 1;
    }
  }

  // Check for overflow
  if (value > ((size_t)-1) / multiplier) {
    return (size_t)-1;
  }

  return (size_t)(value * multiplier);
}

#ifdef _WIN32
char *strsep(char **sp, char *sep) {
  char *p, *s;
  if (sp == NULL || *sp == NULL || **sp == '\0') return (NULL);
  s = *sp;
  p = s + strcspn(s, sep);
  if (*p != '\0') *p++ = '\0';
  *sp = p;
  return s;
}

const char *quote_arg(const char *arg) {
  int len = 0, n = 0;
  int force_quotes = 0;
  char *q, *d;
  const char *p = arg;
  if (!*p) force_quotes = 1;
  while (*p) {
    if (isspace(*p) || *p == '*' || *p == '?' || *p == '{' || *p == '\'')
      force_quotes = 1;
    else if (*p == '"')
      n++;
    else if (*p == '\\') {
      int count = 0;
      while (*p == '\\') {
        count++;
        p++;
        len++;
      }
      if (*p == '"' || !*p) n += count * 2 + 1;
      continue;
    }
    len++;
    p++;
  }
  if (!force_quotes && n == 0) return arg;

  d = q = xmalloc(len + n + 3);
  *d++ = '"';
  while (*arg) {
    if (*arg == '"')
      *d++ = '\\';
    else if (*arg == '\\') {
      int count = 0;
      while (*arg == '\\') {
        count++;
        *d++ = *arg++;
      }
      if (*arg == '"' || !*arg) {
        while (count-- > 0) *d++ = '\\';
        if (!*arg) break;
        *d++ = '\\';
      }
    }
    *d++ = *arg++;
  }
  *d++ = '"';
  *d++ = '\0';
  return q;
}

void print_error(char *func) {
  LPVOID buffer;
  DWORD dw = GetLastError();
  FormatMessage(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL, dw, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&buffer, 0, NULL);
  wprintf(L"== %s failed with error %d: %s", func, dw, buffer);
  LocalFree(buffer);
}
#endif
