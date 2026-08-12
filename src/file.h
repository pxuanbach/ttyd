#ifndef TTYD_FILE_H
#define TTYD_FILE_H

#include <stdbool.h>

// File/directory entry for JSON serialization
typedef struct {
    char *name;
    char *path;
    int is_directory;
    size_t size;
    time_t modified;
} file_entry_t;

// Directory listing result
typedef struct {
    file_entry_t *entries;
    int count;
    char *current_path;
    char *error;
} dir_result_t;

// File content result
typedef struct {
    char *content;
    size_t size;
    char *error;
} file_result_t;

// Write file result
typedef struct {
    int success;
    char *error;
} write_result_t;

// Upload file result
typedef struct {
    char *path;        // final path where file was saved
    size_t size;       // actual file size written
    char *error;        // NULL on success
} upload_result_t;

// File stream for chunked reads (used by /api/image)
typedef struct file_stream {
    int fd;             // open file descriptor, -1 when not open
    size_t size;        // total file size in bytes
    char *error;        // NULL on success
} file_stream_t;

// Path validation - check if path is safe and within allowed base
bool is_path_safe(const char *base_path, const char *requested_path);

// Get the current working directory from server config
const char *get_current_directory(void);

// List directory contents
dir_result_t *list_directory(const char *path);

// Read file content
file_result_t *read_file(const char *path);

// Write file content
write_result_t *write_file(const char *path, const char *content, size_t len);

// Create new file
write_result_t *create_file(const char *path);

// Delete file or empty directory
write_result_t *delete_file(const char *path);

// Upload file (binary) to target directory with auto-rename on conflict
// Returns upload_result_t with final path and size, or error
upload_result_t *upload_file(const char *dir, const char *filename, const char *data, size_t len);

// Open file for streaming reads (no size cap; caller reads via file_stream_read).
// Returns a stream with fd>=0 on success or error string set on failure.
file_stream_t *open_file_stream(const char *path);

// Read up to len bytes from the stream into buf. Returns bytes read (0 at EOF), or -1 on error.
int file_stream_read(file_stream_t *stream, void *buf, size_t len);

// Close stream and free the handle. Safe to call with NULL.
void close_file_stream(file_stream_t *stream);

// Free results
void dir_result_free(dir_result_t *result);
void file_result_free(file_result_t *result);
void write_result_free(write_result_t *result);
void upload_result_free(upload_result_t *result);

#endif  // TTYD_FILE_H
