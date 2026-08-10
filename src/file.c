#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "file.h"
#include "server.h"
#include "utils.h"

#ifdef _WIN32
// Windows replacement for realpath with forward slash normalization
static char *win_realpath(const char *path, char *resolved) {
    // Convert forward slashes to backslashes for Windows API
    char win_path[_MAX_PATH];
    int j = 0;
    for (int i = 0; path[i] && j < _MAX_PATH - 1; i++) {
        win_path[j++] = (path[i] == '/') ? '\\' : path[i];
    }
    win_path[j] = '\0';
    
    if (_fullpath(resolved, win_path, _MAX_PATH) != NULL) {
        // Normalize backslashes to forward slashes for consistent comparison
        for (char *p = resolved; *p; p++) {
            if (*p == '\\') *p = '/';
        }
        return resolved;
    }
    return NULL;
}
#define realpath win_realpath
#endif

#define MAX_FILE_SIZE (10 * 1024 * 1024)  // 10MB limit
#define MAX_ENTRIES 1000

// Helper to build full path from base and relative path
static void build_full_path(const char *base_path, const char *relative_path, char *full_path, size_t size) {
    if (relative_path == NULL || strlen(relative_path) == 0) {
        snprintf(full_path, size, "%s", base_path);
        return;
    }
    
    // Check if path is absolute (Unix / or Windows C:)
    int is_absolute = (relative_path[0] == '/' || 
                      (strlen(relative_path) >= 2 && relative_path[1] == ':'));
    
    if (is_absolute) {
        snprintf(full_path, size, "%s", relative_path);
    } else {
        snprintf(full_path, size, "%s/%s", base_path, relative_path);
    }
}

static void append_entry(dir_result_t *result, const char *name, const char *full_path, struct stat *st) {
    if (result->count >= MAX_ENTRIES) return;

    result->entries = xrealloc(result->entries, (result->count + 1) * sizeof(file_entry_t));
    file_entry_t *entry = &result->entries[result->count];

    entry->name = strdup(name);
    entry->path = strdup(full_path);
    entry->is_directory = S_ISDIR(st->st_mode);
    entry->size = st->st_size;
    entry->modified = st->st_mtime;

    result->count++;
}

static int compare_entries(const void *a, const void *b) {
    file_entry_t *ea = (file_entry_t *)a;
    file_entry_t *eb = (file_entry_t *)b;

    // Directories first
    if (ea->is_directory != eb->is_directory) {
        return eb->is_directory - ea->is_directory;
    }

    // Then alphabetical
    return strcasecmp(ea->name, eb->name);
}

bool is_path_safe(const char *base_path, const char *requested_path) {
    if (requested_path == NULL || base_path == NULL) return false;

    // Check for path traversal attempts
    if (strstr(requested_path, "..") != NULL) return false;

    char resolved_base[PATH_MAX];
    char resolved_req[PATH_MAX];

    // Resolve base path
    if (realpath(base_path, resolved_base) == NULL) return false;

    // Normalize base path (ensure forward slashes)
    for (char *p = resolved_base; *p; p++) {
        if (*p == '\\') *p = '/';
    }

    // Build full path first, then resolve it
    char full_path[PATH_MAX];
    // On Windows, check for absolute path (C: or D: drive letter)
    int is_absolute = (requested_path[0] == '/' || 
                       (strlen(requested_path) >= 2 && requested_path[1] == ':'));
    
    if (is_absolute) {
        // Absolute path - use as is
        snprintf(full_path, sizeof(full_path), "%s", requested_path);
    } else {
        // Relative path - prepend base path
        snprintf(full_path, sizeof(full_path), "%s/%s", base_path, requested_path);
    }
    
    // Normalize full path (replace backslashes with forward slashes for Windows)
    for (char *p = full_path; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    
    // Resolve the full path
    if (realpath(full_path, resolved_req) == NULL) {
        return false;
    }
    
    // Normalize resolved path
    for (char *p = resolved_req; *p; p++) {
        if (*p == '\\') *p = '/';
    }

    // Check if resolved path starts with base path
    size_t base_len = strlen(resolved_base);
    if (strncmp(resolved_req, resolved_base, base_len) != 0) return false;

    // Make sure it's actually inside (either equal or a subdirectory)
    if (resolved_req[base_len] != '/' && resolved_req[base_len] != '\0') return false;

    return true;
}


const char *get_current_directory(void) {
    // Priority: server->cwd > process cwd > HOME > /tmp
    if (server != NULL && server->cwd != NULL && strlen(server->cwd) > 0) {
        return server->cwd;
    }

    // Try to get from environment
    const char *home = getenv("HOME");
    if (home != NULL && strlen(home) > 0) {
        return home;
    }

    return "/tmp";
}

dir_result_t *list_directory(const char *path) {
    dir_result_t *result = xmalloc(sizeof(dir_result_t));
    memset(result, 0, sizeof(dir_result_t));

    const char *base_path = get_current_directory();

    // Build full path
    char full_path[PATH_MAX];
    if (path == NULL || strlen(path) == 0) {
        snprintf(full_path, sizeof(full_path), "%s", base_path);
    } else if (!is_path_safe(base_path, path)) {
        result->error = strdup("Access denied: invalid path");
        return result;
    } else {
        build_full_path(base_path, path, full_path, sizeof(full_path));
    }

    // Validate the constructed path
    char resolved_path[PATH_MAX];
    if (realpath(full_path, resolved_path) == NULL) {
        result->error = strdup("Path not found or inaccessible");
        return result;
    }

    if (!is_path_safe(base_path, resolved_path)) {
        result->error = strdup("Access denied: outside working directory");
        return result;
    }

    DIR *dir = opendir(resolved_path);
    if (dir == NULL) {
        result->error = strdup(strerror(errno));
        return result;
    }

    result->current_path = strdup(resolved_path);
    struct dirent *entry;
    struct stat st;

    while ((entry = readdir(dir)) != NULL) {
        // Skip hidden files/directories
        if (entry->d_name[0] == '.') continue;

        char entry_path[PATH_MAX];
        snprintf(entry_path, sizeof(entry_path), "%s/%s", resolved_path, entry->d_name);

        if (stat(entry_path, &st) == 0) {
            append_entry(result, entry->d_name, entry_path, &st);
        }
    }

    closedir(dir);

    // Sort entries (directories first, then alphabetical)
    qsort(result->entries, result->count, sizeof(file_entry_t), compare_entries);

    return result;
}

file_result_t *read_file(const char *path) {
    file_result_t *result = xmalloc(sizeof(file_result_t));
    memset(result, 0, sizeof(file_result_t));

    const char *base_path = get_current_directory();

    if (path == NULL || strlen(path) == 0) {
        result->error = strdup("No path specified");
        return result;
    }

    if (!is_path_safe(base_path, path)) {
        result->error = strdup("Access denied: invalid path");
        return result;
    }

    // Build full path
    char full_path[PATH_MAX];
    build_full_path(base_path, path, full_path, sizeof(full_path));

    // Resolve and validate
    char resolved_path[PATH_MAX];
    if (realpath(full_path, resolved_path) == NULL) {
        result->error = strdup("File not found");
        return result;
    }

    if (!is_path_safe(base_path, resolved_path)) {
        result->error = strdup("Access denied: outside working directory");
        return result;
    }

    // Check if it's a directory
    struct stat st;
    if (stat(resolved_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        result->error = strdup("Cannot read a directory");
        return result;
    }

    // Open file
    FILE *fp = fopen(resolved_path, "rb");
    if (fp == NULL) {
        result->error = strdup(strerror(errno));
        return result;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size > MAX_FILE_SIZE) {
        fclose(fp);
        result->error = strdup("File too large");
        return result;
    }

    // Read content
    result->content = xmalloc((size_t)file_size + 1);
    size_t bytes_read = fread(result->content, 1, (size_t)file_size, fp);
    result->content[bytes_read] = '\0';
    result->size = bytes_read;

    fclose(fp);
    return result;
}

write_result_t *write_file(const char *path, const char *content, size_t len) {
    write_result_t *result = xmalloc(sizeof(write_result_t));
    memset(result, 0, sizeof(write_result_t));

    const char *base_path = get_current_directory();

    // Build full path
    char full_path[PATH_MAX];
    if (path == NULL || strlen(path) == 0) {
        result->error = strdup("No path specified");
        return result;
    }

    if (!is_path_safe(base_path, path)) {
        result->error = strdup("Access denied: invalid path");
        return result;
    }

    build_full_path(base_path, path, full_path, sizeof(full_path));

    // Resolve and validate
    char resolved_path[PATH_MAX];
    if (realpath(full_path, resolved_path) == NULL) {
        // File doesn't exist yet, check if parent directory is valid
        char parent_dir[PATH_MAX];
        strncpy(parent_dir, full_path, sizeof(parent_dir) - 1);
        char *last_slash = strrchr(parent_dir, '/');
        if (last_slash) {
            *last_slash = '\0';
            if (realpath(parent_dir, resolved_path) == NULL || !is_path_safe(base_path, resolved_path)) {
                result->error = strdup("Parent directory not accessible");
                return result;
            }
        } else {
            result->error = strdup("Invalid path");
            return result;
        }
    } else if (!is_path_safe(base_path, resolved_path)) {
        result->error = strdup("Access denied: outside working directory");
        return result;
    }

    // Check size limit
    if (len > MAX_FILE_SIZE) {
        result->error = strdup("File too large");
        return result;
    }

    // Write file
    FILE *fp = fopen(resolved_path, "wb");
    if (fp == NULL) {
        result->error = strdup(strerror(errno));
        return result;
    }

    if (content != NULL && len > 0) {
        size_t written = fwrite(content, 1, len, fp);
        if (written != len) {
            result->error = strdup("Write failed");
            fclose(fp);
            return result;
        }
    }

    fclose(fp);
    result->success = 1;
    return result;
}

write_result_t *create_file(const char *path) {
    return write_file(path, "", 0);
}

write_result_t *delete_file(const char *path) {
    write_result_t *result = xmalloc(sizeof(write_result_t));
    memset(result, 0, sizeof(write_result_t));

    const char *base_path = get_current_directory();

    // Build full path
    char full_path[PATH_MAX];
    if (path == NULL || strlen(path) == 0) {
        result->error = strdup("No path specified");
        return result;
    }

    if (!is_path_safe(base_path, path)) {
        result->error = strdup("Access denied: invalid path");
        return result;
    }

    build_full_path(base_path, path, full_path, sizeof(full_path));

    // Resolve and validate
    char resolved_path[PATH_MAX];
    if (realpath(full_path, resolved_path) == NULL) {
        result->error = strdup("Path not found");
        return result;
    }

    if (!is_path_safe(base_path, resolved_path)) {
        result->error = strdup("Access denied: outside working directory");
        return result;
    }

    struct stat st;
    if (stat(resolved_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        // Check if directory is empty
        DIR *dir = opendir(resolved_path);
        if (dir != NULL) {
            struct dirent *entry = readdir(dir);
            closedir(dir);
            if (entry != NULL) {
                result->error = strdup("Directory not empty");
                return result;
            }
        }
        if (rmdir(resolved_path) != 0) {
            result->error = strdup(strerror(errno));
            return result;
        }
    } else {
        if (unlink(resolved_path) != 0) {
            result->error = strdup(strerror(errno));
            return result;
        }
    }

    result->success = 1;
    return result;
}

void dir_result_free(dir_result_t *result) {
    if (result == NULL) return;

    for (int i = 0; i < result->count; i++) {
        free(result->entries[i].name);
        free(result->entries[i].path);
    }
    free(result->entries);
    free(result->current_path);
    free(result->error);
    free(result);
}

void file_result_free(file_result_t *result) {
    if (result == NULL) return;
    free(result->content);
    free(result->error);
    free(result);
}

void write_result_free(write_result_t *result) {
    if (result == NULL) return;
    free(result->error);
    free(result);
}
