#ifndef TTYD_UTIL_H
#define TTYD_UTIL_H

#define container_of(ptr, type, member)                \
  ({                                                   \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); \
  })

// malloc with NULL check
void *xmalloc(size_t size);

// realloc with NULL check
void *xrealloc(void *p, size_t size);

// Convert a string to upper case
char *uppercase(char *s);

// Convert a string to lower case
char *lowercase(char *s);

// Check whether str ends with suffix
bool endswith(const char *str, const char *suffix);

// Get human readable signal string
int get_sig_name(int sig, char *buf, size_t len);

// Get signal code from string like SIGHUP
int get_sig(const char *sig_name);

// Open uri with the default application of system
int open_uri(char *uri);

// Generate a unique filename if file exists (e.g., file.txt -> file (1).txt)
// Returns newly allocated string (caller must free), or NULL on error
char *generate_unique_filename(const char *dir, const char *filename);

// Parse size string like "100M" into bytes
size_t parse_size(const char *size_str);

#ifdef _WIN32
char *strsep(char **sp, char *sep);
const char *quote_arg(const char *arg);
void print_error(char *func);
#endif
#endif  // TTYD_UTIL_H
