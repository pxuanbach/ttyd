#include <libwebsockets.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

#include "html.h"
#include "server.h"
#include "utils.h"
#include "file.h"
#include "compat.h"

// File API endpoints
static const char *api_directory = "/api/directory";
static const char *api_file = "/api/file";
static const char *api_upload = "/api/upload";
static const char *api_image = "/api/image";

// Upload context for multipart parsing
typedef struct {
    char *target_dir;          // target directory path
    char *filename;            // extracted filename
    char *buffer;              // accumulated file content
    size_t buffer_size;       // current buffer capacity
    size_t buffer_pos;        // current position in buffer
    size_t content_length;     // expected total size
    bool error;               // error flag
    char *error_msg;          // error message
} upload_ctx_t;

enum { AUTH_OK, AUTH_FAIL, AUTH_ERROR };

static char *html_cache = NULL;
static size_t html_cache_len = 0;

static int send_unauthorized(struct lws *wsi, unsigned int code, enum lws_token_indexes header) {
  unsigned char buffer[1024 + LWS_PRE], *p, *end;
  p = buffer + LWS_PRE;
  end = p + sizeof(buffer) - LWS_PRE;

  if (lws_add_http_header_status(wsi, code, &p, end) ||
      lws_add_http_header_by_token(wsi, header, (unsigned char *)"Basic realm=\"ttyd\"", 18, &p, end) ||
      lws_add_http_header_content_length(wsi, 0, &p, end) || lws_finalize_http_header(wsi, &p, end) ||
      lws_write(wsi, buffer + LWS_PRE, p - (buffer + LWS_PRE), LWS_WRITE_HTTP_HEADERS) < 0)
    return AUTH_FAIL;

  return lws_http_transaction_completed(wsi) ? AUTH_FAIL : AUTH_ERROR;
}

static int check_auth(struct lws *wsi, struct pss_http *pss) {
  if (server->auth_header != NULL) {
    if (lws_hdr_custom_length(wsi, server->auth_header, strlen(server->auth_header)) > 0) return AUTH_OK;
    return send_unauthorized(wsi, HTTP_STATUS_PROXY_AUTH_REQUIRED, WSI_TOKEN_HTTP_PROXY_AUTHENTICATE);
  }

  if(server->credential != NULL) {
    char buf[256];
    int len = lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_HTTP_AUTHORIZATION);
    if (len >= 7 && strstr(buf, "Basic ")) {
      if (!strcmp(buf + 6, server->credential)) return AUTH_OK;
    }
    return send_unauthorized(wsi, HTTP_STATUS_UNAUTHORIZED, WSI_TOKEN_HTTP_WWW_AUTHENTICATE);
  }

  return AUTH_OK;
}

static bool accept_gzip(struct lws *wsi) {
  char buf[256];
  int len = lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_HTTP_ACCEPT_ENCODING);
  return len > 0 && strstr(buf, "gzip") != NULL;
}

static bool uncompress_html(char **output, size_t *output_len) {
  if (html_cache == NULL || html_cache_len == 0) {
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    if (inflateInit2(&stream, 16 + 15) != Z_OK) return false;

    html_cache_len = index_html_size;
    html_cache = xmalloc(html_cache_len);

    stream.avail_in = index_html_len;
    stream.avail_out = html_cache_len;
    stream.next_in = (void *)index_html;
    stream.next_out = (void *)html_cache;

    int ret = inflate(&stream, Z_SYNC_FLUSH);
    inflateEnd(&stream);
    if (ret != Z_STREAM_END) {
      free(html_cache);
      html_cache = NULL;
      html_cache_len = 0;
      return false;
    }
  }

  *output = html_cache;
  *output_len = html_cache_len;

  return true;
}

static void pss_buffer_free(struct pss_http *pss) {
  if (pss->buffer != (char *)index_html && pss->buffer != html_cache) free(pss->buffer);
}

static void access_log(struct lws *wsi, const char *path) {
  char rip[50];

  lws_get_peer_simple(lws_get_network_wsi(wsi), rip, sizeof(rip));
  lwsl_notice("HTTP %s - %s\n", path, rip);
}

// Simple URL decode
static void urldecode(const char *src, int len, char *dest) {
  int i, j = 0;
  for (i = 0; i < len; i++) {
    if (src[i] == '%' && i + 2 < len) {
      char hex[3] = {src[i+1], src[i+2], '\0'};
      dest[j++] = (char)strtol(hex, NULL, 16);
      i += 2;
    } else if (src[i] == '+') {
      dest[j++] = ' ';
    } else {
      dest[j++] = src[i];
    }
  }
  dest[j] = '\0';
}

// Helper to send JSON error response
static int send_json_error(struct lws *wsi, unsigned int status, const char *error) {
  unsigned char buffer[1024 + LWS_PRE], *p, *end;
  p = buffer + LWS_PRE;
  end = p + sizeof(buffer) - LWS_PRE;

  char body[512];
  int n = snprintf(body, sizeof(body), "{\"error\": \"%s\"}", error);

  // Add headers using libwebsockets API
  if (lws_add_http_header_status(wsi, status, &p, end) ||
      lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
                                   (unsigned char *)"application/json;charset=utf-8", 30, &p, end) ||
      lws_add_http_header_content_length(wsi, (unsigned long)n, &p, end) ||
      lws_finalize_http_header(wsi, &p, end) ||
      lws_write(wsi, buffer + LWS_PRE, p - (buffer + LWS_PRE), LWS_WRITE_HTTP_HEADERS) < 0)
    return 1;

  if (lws_write(wsi, (unsigned char *)body, (size_t)n, LWS_WRITE_HTTP_FINAL) < (int)n)
    return 1;

  return 0;
}

// Helper to send JSON success response
static int send_json_response(struct lws *wsi, const char *json, size_t len) {
  unsigned char buffer[4096 + LWS_PRE], *p, *end;
  p = buffer + LWS_PRE;
  end = p + sizeof(buffer) - LWS_PRE;

  // Add headers using libwebsockets API
  if (lws_add_http_header_status(wsi, HTTP_STATUS_OK, &p, end) ||
      lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
                                   (unsigned char *)"application/json;charset=utf-8", 30, &p, end) ||
      lws_add_http_header_content_length(wsi, (unsigned long)len, &p, end) ||
      lws_finalize_http_header(wsi, &p, end) ||
      lws_write(wsi, buffer + LWS_PRE, p - (buffer + LWS_PRE), LWS_WRITE_HTTP_HEADERS) < 0)
    return 1;

  if (lws_write(wsi, (unsigned char *)json, len, LWS_WRITE_HTTP_FINAL) < (int)len)
    return 1;

  return 0;
}

// Handle GET /api/directory
static int handle_api_directory(struct lws *wsi) {
  // Get the full URI path
  char full_uri[512] = {0};
  int uri_len = lws_hdr_total_length(wsi, WSI_TOKEN_GET_URI);
  if (uri_len > 0 && uri_len < (int)sizeof(full_uri)) {
    lws_hdr_copy(wsi, full_uri, sizeof(full_uri), WSI_TOKEN_GET_URI);
  }
  
  // Extract path after /api/directory
  // Supports: /api/directory or /api/directory/some/path
  char dir_path[256] = {0};
  const char *api_dir = "/api/directory";
  if (strncmp(full_uri, api_dir, strlen(api_dir)) == 0) {
    const char *path_part = full_uri + strlen(api_dir);
    if (*path_part == '/') {
      path_part++;  // skip leading /
    }
    if (*path_part != '\0') {
      // URL decode the path
      int len = strlen(path_part);
      if (len < (int)sizeof(dir_path)) {
        urldecode(path_part, len, dir_path);
      }
    }
  }

  dir_result_t *result = list_directory(dir_path[0] ? dir_path : NULL);

  if (result->error != NULL) {
    int ret = send_json_error(wsi, HTTP_STATUS_BAD_REQUEST, result->error);
    dir_result_free(result);
    return ret;
  }

  // Build JSON response
  size_t estimated_size = 256 + result->count * 200;
  char *json = xmalloc(estimated_size);
  int offset = snprintf(json, estimated_size, "{\"path\": \"%s\", \"entries\": [", result->current_path);

  for (int i = 0; i < result->count; i++) {
    file_entry_t *entry = &result->entries[i];
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&entry->modified));

    if (i > 0) offset += snprintf(json + offset, estimated_size - offset, ", ");
    offset += snprintf(json + offset, estimated_size - offset,
                      "{\"name\": \"%s\", \"path\": \"%s\", \"isDirectory\": %s, \"size\": %lu, \"modified\": \"%s\"}",
                      entry->name, entry->path,
                      entry->is_directory ? "true" : "false",
                      (unsigned long)entry->size,
                      time_str);
  }

  offset += snprintf(json + offset, estimated_size - offset, "]}");

  int ret = send_json_response(wsi, json, (size_t)offset);
  free(json);
  dir_result_free(result);

  return ret;
}

// Handle GET /api/file
static int handle_api_file_get(struct lws *wsi) {
  // Get the full URI path
  char full_uri[512] = {0};
  int uri_len = lws_hdr_total_length(wsi, WSI_TOKEN_GET_URI);
  if (uri_len > 0 && uri_len < (int)sizeof(full_uri)) {
    lws_hdr_copy(wsi, full_uri, sizeof(full_uri), WSI_TOKEN_GET_URI);
  }
  
  // Extract path after /api/file
  // Supports: /api/file/path/to/file.txt
  char file_path[256] = {0};
  const char *api_file = "/api/file";
  if (strncmp(full_uri, api_file, strlen(api_file)) == 0) {
    const char *path_part = full_uri + strlen(api_file);
    if (*path_part == '/') {
      path_part++;  // skip leading /
    }
    if (*path_part != '\0') {
      // URL decode the path
      int len = strlen(path_part);
      if (len < (int)sizeof(file_path)) {
        urldecode(path_part, len, file_path);
      }
    }
  }

  if (!file_path[0]) {
    return send_json_error(wsi, HTTP_STATUS_BAD_REQUEST, "Missing path parameter");
  }

  file_result_t *result = read_file(file_path);

  if (result->error != NULL) {
    int ret = send_json_error(wsi, HTTP_STATUS_BAD_REQUEST, result->error);
    file_result_free(result);
    return ret;
  }

  // Escape content for JSON
  size_t json_size = result->size * 2 + 256;
  char *json = xmalloc(json_size);
  int offset = snprintf(json, json_size, "{\"path\": \"%s\", \"size\": %lu, \"content\": \"",
                        file_path, (unsigned long)result->size);

  // JSON escape the content
  for (size_t i = 0; i < result->size && offset < (int)json_size - 10; i++) {
    char c = result->content[i];
    switch (c) {
      case '\\': offset += snprintf(json + offset, json_size - offset, "\\\\"); break;
      case '"':  offset += snprintf(json + offset, json_size - offset, "\\\""); break;
      case '\n':  offset += snprintf(json + offset, json_size - offset, "\\n"); break;
      case '\r':  offset += snprintf(json + offset, json_size - offset, "\\r"); break;
      case '\t':  offset += snprintf(json + offset, json_size - offset, "\\t"); break;
      default:
        if (c >= 32 && c < 127) {
          json[offset++] = c;
        } else {
          offset += snprintf(json + offset, json_size - offset, "\\u%04x", (unsigned char)c);
        }
    }
  }

  offset += snprintf(json + offset, json_size - offset, "\"}");

  int ret = send_json_response(wsi, json, (size_t)offset);
  free(json);
  file_result_free(result);

  return ret;
}

// Handle POST /api/file (create or update)
static int handle_api_file_post(struct lws *wsi, char *body, size_t len) {
  // Parse JSON body: {"path": "...", "content": "..."}
  char file_path[256] = {0};
  char content[11 * 1024 * 1024];  // 10MB max
  size_t content_len = 0;

  // Find path
  const char *path_start = strstr(body, "\"path\":");
  if (path_start != NULL) {
    path_start = strchr(path_start, '"');
    if (path_start != NULL) {
      path_start++;
      const char *path_end = strchr(path_start, '"');
      if (path_end != NULL && (size_t)(path_end - path_start) < sizeof(file_path)) {
        memcpy(file_path, path_start, path_end - path_start);
        file_path[path_end - path_start] = '\0';
      }
    }
  }

  // Find content
  const char *content_start = strstr(body, "\"content\":");
  if (content_start != NULL) {
    content_start = strchr(content_start, '"');
    if (content_start != NULL) {
      content_start++;
      const char *p = content_start;
      while (*p && p < body + len) {
        if (*p == '\\' && p + 1 < body + len) {
          p++;
          switch (*p) {
            case 'n': content[content_len++] = '\n'; break;
            case 'r': content[content_len++] = '\r'; break;
            case 't': content[content_len++] = '\t'; break;
            case '\\': content[content_len++] = '\\'; break;
            case '"': content[content_len++] = '"'; break;
            case 'u':
              if (p + 4 < body + len) {
                char hex[5] = {p[1], p[2], p[3], p[4], '\0'};
                content[content_len++] = (char)strtol(hex, NULL, 16);
                p += 4;
              }
              break;
            default: content[content_len++] = *p; break;
          }
        } else if (*p == '"') {
          break;
        } else {
          content[content_len++] = *p;
        }
        p++;
      }
    }
  }

  if (!file_path[0]) {
    return send_json_error(wsi, HTTP_STATUS_BAD_REQUEST, "Missing path parameter");
  }

  write_result_t *result = write_file(file_path, content, content_len);

  if (result->error != NULL) {
    int ret = send_json_error(wsi, HTTP_STATUS_BAD_REQUEST, result->error);
    write_result_free(result);
    return ret;
  }

  int ret = send_json_response(wsi, "{\"success\": true}", 17);
  write_result_free(result);

  return ret;
}

// Handle DELETE /api/file
static int handle_api_file_delete(struct lws *wsi) {
  // Get the full URI path
  char full_uri[512] = {0};
  int uri_len = lws_hdr_total_length(wsi, WSI_TOKEN_GET_URI);
  if (uri_len > 0 && uri_len < (int)sizeof(full_uri)) {
    lws_hdr_copy(wsi, full_uri, sizeof(full_uri), WSI_TOKEN_GET_URI);
  }
  
  // Extract path after /api/file
  char file_path[256] = {0};
  const char *api_file = "/api/file";
  if (strncmp(full_uri, api_file, strlen(api_file)) == 0) {
    const char *path_part = full_uri + strlen(api_file);
    if (*path_part == '/') {
      path_part++;  // skip leading /
    }
    if (*path_part != '\0') {
      int len = strlen(path_part);
      if (len < (int)sizeof(file_path)) {
        urldecode(path_part, len, file_path);
      }
    }
  }

  if (!file_path[0]) {
    return send_json_error(wsi, HTTP_STATUS_BAD_REQUEST, "Missing path parameter");
  }

  write_result_t *result = delete_file(file_path);

  if (result->error != NULL) {
    int ret = send_json_error(wsi, HTTP_STATUS_BAD_REQUEST, result->error);
    write_result_free(result);
    return ret;
  }

  int ret = send_json_response(wsi, "{\"success\": true}", 17);
  write_result_free(result);

  return ret;
}

// Helper to send upload success response
static int send_upload_response(struct lws *wsi, const char *path, size_t size) {
  unsigned char buffer[1024 + LWS_PRE], *p, *end;
  p = buffer + LWS_PRE;
  end = p + sizeof(buffer) - LWS_PRE;

  char body[512];
  int n = snprintf(body, sizeof(body), "{\"success\": true, \"path\": \"%s\", \"size\": %lu}",
                   path, (unsigned long)size);

  // Add headers using libwebsockets API
  if (lws_add_http_header_status(wsi, HTTP_STATUS_OK, &p, end) ||
      lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
                                   (unsigned char *)"application/json;charset=utf-8", 30, &p, end) ||
      lws_add_http_header_content_length(wsi, (unsigned long)n, &p, end) ||
      lws_finalize_http_header(wsi, &p, end) ||
      lws_write(wsi, buffer + LWS_PRE, p - (buffer + LWS_PRE), LWS_WRITE_HTTP_HEADERS) < 0)
    return 1;

  if (lws_write(wsi, (unsigned char *)body, (size_t)n, LWS_WRITE_HTTP_FINAL) < (int)n)
    return 1;

  return 0;
}

// Handle POST /api/upload (multipart file upload)
static int handle_api_upload(struct lws *wsi, const char *full_uri, char *body, size_t len) {
  // Check if upload is enabled
  if (server == NULL || !server->upload_enabled) {
    return send_json_error(wsi, 503, "File upload is disabled");
  }

  // Use the full_uri passed from pss->uri (contains ?path=...)
  // Extract target directory from query parameter
  char target_dir[256] = {0};
  const char *query = strchr(full_uri, '?');
  lwsl_err("UPLOAD: full_uri=%s, query=%s\n", full_uri, query ? query : "NULL");
  if (query != NULL) {
    const char *path_param = strstr(query, "path=");
    if (path_param != NULL) {
      path_param += 5;  // skip "path="
      const char *end = strpbrk(path_param, "& ");
      int path_len = end ? (int)(end - path_param) : (int)strlen(path_param);
      if (path_len > 0 && path_len < (int)sizeof(target_dir)) {
        urldecode(path_param, path_len, target_dir);
      }
    }
  }
  lwsl_err("UPLOAD: target_dir=%s\n", target_dir);

  // Parse multipart form data manually (simple implementation)
  // Format: --boundary\r\nContent-Disposition: form-data; name="file"; filename="test.txt"\r\n\r\n<content>\r\n--boundary--
  char *filename = NULL;
  char *file_content = NULL;
  size_t file_size = 0;

  // Simple multipart parsing
  const char *boundary = NULL;
  const char *body_end = body + len;

  // Find boundary
  const char *content_type = NULL;
  char content_type_hdr[128] = {0};
  int ct_len = lws_hdr_copy(wsi, content_type_hdr, sizeof(content_type_hdr), WSI_TOKEN_HTTP_CONTENT_TYPE);
  lwsl_err("UPLOAD: Content-Type header: %s\n", content_type_hdr);
  if (ct_len > 0) {
    content_type = strstr(content_type_hdr, "boundary=");
    if (content_type != NULL) {
      boundary = content_type + 9;
    }
  }
  lwsl_err("UPLOAD: boundary=%s\n", boundary ? boundary : "NULL");

  if (boundary == NULL) {
    return send_json_error(wsi, HTTP_STATUS_BAD_REQUEST, "Missing boundary in multipart form");
  }

  // Find filename in Content-Disposition
  const char *cd_start = strstr(body, "filename=\"");
  if (cd_start != NULL) {
    cd_start += 10;  // skip filename=""
    const char *cd_end = strchr(cd_start, '"');
    if (cd_end != NULL && cd_end > cd_start) {
      size_t name_len = cd_end - cd_start;
      filename = xmalloc(name_len + 1);
      memcpy(filename, cd_start, name_len);
      filename[name_len] = '\0';
    }
  }

  if (filename == NULL) {
    return send_json_error(wsi, HTTP_STATUS_BAD_REQUEST, "No file provided");
  }

  // Find file content (after \r\n\r\n following headers)
  const char *header_end = strstr(body, "\r\n\r\n");
  if (header_end != NULL) {
    header_end += 4;  // skip \r\n\r\n
    // Find the closing boundary
    const char *closing_boundary = strstr(header_end, "\r\n");
    if (closing_boundary != NULL) {
      file_size = closing_boundary - header_end;
    } else {
      file_size = body_end - header_end;
    }

    file_content = (char *)header_end;
  }

  if (file_content == NULL || file_size == 0) {
    free(filename);
    return send_json_error(wsi, HTTP_STATUS_BAD_REQUEST, "Empty file");
  }

  // Check size limit
  size_t max_size = server->upload_max_size;
  if (file_size > max_size) {
    free(filename);
    return send_json_error(wsi, HTTP_STATUS_BAD_REQUEST, "File too large");
  }

  lwsl_err("UPLOAD: calling upload_file dir=%s file=%s size=%zu\n", target_dir[0] ? target_dir : "/", filename ? filename : "NULL", file_size);

  // Upload the file
  upload_result_t *result = upload_file(target_dir[0] ? target_dir : "/", filename, file_content, file_size);
  free(filename);
  lwsl_err("UPLOAD: result error=%s\n", result->error ? result->error : "NONE");

  if (result->error != NULL) {
    int ret = send_json_error(wsi, HTTP_STATUS_BAD_REQUEST, result->error);
    upload_result_free(result);
    return ret;
  }

  int ret = send_upload_response(wsi, result->path, result->size);
  upload_result_free(result);

  return ret;
}

// Map a lowercase file extension to an image Content-Type. Returns NULL if unsupported.
static const char *get_image_content_type(const char *ext) {
  if (ext == NULL) return NULL;
  if (strcasecmp(ext, "png") == 0) return "image/png";
  if (strcasecmp(ext, "jpg") == 0) return "image/jpeg";
  if (strcasecmp(ext, "jpeg") == 0) return "image/jpeg";
  if (strcasecmp(ext, "gif") == 0) return "image/gif";
  if (strcasecmp(ext, "webp") == 0) return "image/webp";
  return NULL;
}

// Extract lowercase extension from path into out buffer.
static void get_extension(const char *path, char *out, size_t out_len) {
  if (out == NULL || out_len == 0) return;
  out[0] = '\0';
  if (path == NULL) return;
  const char *dot = strrchr(path, '.');
  if (dot == NULL || dot == path || dot[1] == '\0') return;
  dot++;
  size_t i = 0;
  while (dot[i] && i < out_len - 1) {
    out[i] = (char)tolower((unsigned char)dot[i]);
    i++;
  }
  out[i] = '\0';
}

// Handle GET /api/image/{path} - streams image bytes via LWS_CALLBACK_HTTP_WRITEABLE chunks.
// On success returns 0 and stashes the open stream in pss->image_stream; on failure sends an error response and returns non-zero.
static int handle_api_image(struct lws *wsi, struct pss_http *pss) {
  char full_uri[512] = {0};
  int uri_len = lws_hdr_total_length(wsi, WSI_TOKEN_GET_URI);
  if (uri_len > 0 && uri_len < (int)sizeof(full_uri)) {
    lws_hdr_copy(wsi, full_uri, sizeof(full_uri), WSI_TOKEN_GET_URI);
  }

  char file_path[256] = {0};
  if (strncmp(full_uri, api_image, strlen(api_image)) == 0) {
    const char *path_part = full_uri + strlen(api_image);
    if (*path_part == '/') path_part++;
    if (*path_part != '\0') {
      int len = (int)strlen(path_part);
      if (len < (int)sizeof(file_path)) {
        urldecode(path_part, len, file_path);
      }
    }
  }

  if (!file_path[0]) {
    return send_json_error(wsi, HTTP_STATUS_BAD_REQUEST, "Missing path parameter");
  }

  char ext[16];
  get_extension(file_path, ext, sizeof(ext));
  const char *content_type = get_image_content_type(ext);
  if (content_type == NULL) {
    return send_json_error(wsi, HTTP_STATUS_BAD_REQUEST, "Unsupported image format");
  }

  file_stream_t *stream = open_file_stream(file_path);
  if (stream->error != NULL) {
    int ret = send_json_error(wsi, HTTP_STATUS_NOT_FOUND, stream->error);
    close_file_stream(stream);
    return ret;
  }

  // Write response headers (status, content-type, content-length).
  unsigned char hdr_buf[1024 + LWS_PRE], *hp, *hend;
  hp = hdr_buf + LWS_PRE;
  hend = hp + sizeof(hdr_buf) - LWS_PRE;
  size_t ct_len = strlen(content_type);

  if (lws_add_http_header_status(wsi, HTTP_STATUS_OK, &hp, hend) ||
      lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE, (const unsigned char *)content_type, ct_len,
                                   &hp, hend) ||
      lws_add_http_header_content_length(wsi, (unsigned long)stream->size, &hp, hend) ||
      lws_finalize_http_header(wsi, &hp, hend) ||
      lws_write(wsi, hdr_buf + LWS_PRE, hp - (hdr_buf + LWS_PRE), LWS_WRITE_HTTP_HEADERS) < 0) {
    close_file_stream(stream);
    return 1;
  }

  // Stash the stream for chunked writing; close_file_stream() will free it later.
  pss->image_stream = stream;
  pss->image_sent = 0;

  // Defer body writes to LWS_CALLBACK_HTTP_WRITEABLE.
  lws_callback_on_writable(wsi);
  return 0;
}


int callback_http(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
  struct pss_http *pss = (struct pss_http *)user;
  unsigned char buffer[4096 + LWS_PRE], *p, *end;
  char buf[256];
  bool done = false;

  switch (reason) {
    case LWS_CALLBACK_HTTP:
      access_log(wsi, (const char *)in);
      snprintf(pss->path, sizeof(pss->path), "%s", (const char *)in);
      // Save full URI with query string for later use in POST handlers
      char tmp_uri[256] = {0};
      lws_hdr_copy(wsi, tmp_uri, sizeof(tmp_uri), WSI_TOKEN_GET_URI);
      // Also get URI args (query string part)
      char uri_args[256] = {0};
      int args_len = lws_hdr_copy(wsi, uri_args, sizeof(uri_args), WSI_TOKEN_HTTP_URI_ARGS);
      // Build full URI
      if (args_len > 0) {
        snprintf(pss->uri, sizeof(pss->uri), "%s?%s", tmp_uri, uri_args);
      } else {
        snprintf(pss->uri, sizeof(pss->uri), "%s", tmp_uri);
      }
      // Pre-allocate body buffer for POST requests if we know Content-Length
      if (pss->body == NULL) {
        char cl_buf[32] = {0};
        int cl_len = lws_hdr_copy(wsi, cl_buf, sizeof(cl_buf), WSI_TOKEN_HTTP_CONTENT_LENGTH);
        if (cl_len > 0) {
          size_t content_len = (size_t)atol(cl_buf);
          if (content_len > 0 && content_len <= 100 * 1024 * 1024) {
            pss->body = xmalloc(content_len + 1);
            pss->body_len = 0;
          }
        }
      }
      // Reset body collection state
      pss->body = NULL;
      pss->body_len = 0;
      pss->body_pos = 0;
      pss->pending_result = 0;
      pss->upload_response_len = 0;

      switch (check_auth(wsi, pss)) {
        case AUTH_OK:
          break;
        case AUTH_FAIL:
          return 0;
        case AUTH_ERROR:
        default:
          return 1;
      }

      p = buffer + LWS_PRE;
      end = p + sizeof(buffer) - LWS_PRE;

      if (strcmp(pss->path, endpoints.token) == 0) {
        const char *credential = server->credential != NULL ? server->credential : "";
        size_t n = snprintf(buf, sizeof(buf), "{\"token\": \"%s\"}", credential);
        if (lws_add_http_header_status(wsi, HTTP_STATUS_OK, &p, end) ||
            lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
                                         (unsigned char *)"application/json;charset=utf-8", 30, &p, end) ||
            lws_add_http_header_content_length(wsi, (unsigned long)n, &p, end) ||
            lws_finalize_http_header(wsi, &p, end) ||
            lws_write(wsi, buffer + LWS_PRE, p - (buffer + LWS_PRE), LWS_WRITE_HTTP_HEADERS) < 0)
          return 1;

        pss->buffer = pss->ptr = strdup(buf);
        pss->len = n;
        lws_callback_on_writable(wsi);
        break;
      }

      // File API routing
      if (strncmp(pss->path, api_directory, strlen(api_directory)) == 0) {
        // GET /api/directory?path=...
        int ret = handle_api_directory(wsi);
        if (ret == 0) {
          // Response sent, complete the transaction for keep-alive
          goto try_to_reuse;
        }
        return ret;
      }
      if (strncmp(pss->path, api_file, strlen(api_file)) == 0) {
        // GET /api/file?path=... or POST /api/file
        // Check if it's a POST request by looking for body
        if (pss->body != NULL && pss->body_len > 0) {
          int ret = handle_api_file_post(wsi, pss->body, pss->body_len);
          if (ret == 0) goto try_to_reuse;
          return ret;
        }
        int ret = handle_api_file_get(wsi);
        if (ret == 0) goto try_to_reuse;
        return ret;
      }
      if (strncmp(pss->path, api_upload, strlen(api_upload)) == 0) {
        // POST /api/upload - return 0 to receive body via LWS_CALLBACK_HTTP_BODY
        return 0;
      }
      if (strncmp(pss->path, api_image, strlen(api_image)) == 0) {
        // GET /api/image/{path} - handler schedules LWS_CALLBACK_HTTP_WRITEABLE for body chunks.
        // On success return 0 without try_to_reuse so the writable callback can fire.
        return handle_api_image(wsi, pss);
      }
      
      // Log WebSocket upgrade attempts
      lwsl_notice("HTTP callback: path='%s', ws_endpoint='%s'\n", pss->path, endpoints.ws);

      // redirects `/base-path` to `/base-path/`
      if (strcmp(pss->path, endpoints.parent) == 0) {
        if (lws_add_http_header_status(wsi, HTTP_STATUS_FOUND, &p, end) ||
            lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_LOCATION, (unsigned char *)endpoints.index,
                                         (int)strlen(endpoints.index), &p, end) ||
            lws_add_http_header_content_length(wsi, 0, &p, end) || lws_finalize_http_header(wsi, &p, end) ||
            lws_write(wsi, buffer + LWS_PRE, p - (buffer + LWS_PRE), LWS_WRITE_HTTP_HEADERS) < 0)
          return 1;
        goto try_to_reuse;
      }

      if (strcmp(pss->path, endpoints.index) != 0) {
        lws_return_http_status(wsi, HTTP_STATUS_NOT_FOUND, NULL);
        goto try_to_reuse;
      }

      const char *content_type = "text/html";
      if (server->index != NULL) {
        int n = lws_serve_http_file(wsi, server->index, content_type, NULL, 0);
        if (n < 0 || (n > 0 && lws_http_transaction_completed(wsi))) return 1;
      } else {
        char *output = (char *)index_html;
        size_t output_len = index_html_len;
        if (lws_add_http_header_status(wsi, HTTP_STATUS_OK, &p, end) ||
            lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE, (const unsigned char *)content_type, 9, &p,
                                         end))
          return 1;
#ifdef LWS_WITH_HTTP_STREAM_COMPRESSION
        if (!uncompress_html(&output, &output_len)) return 1;
#else
        if (accept_gzip(wsi)) {
          if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_ENCODING, (unsigned char *)"gzip", 4, &p, end))
            return 1;
        } else {
          if (!uncompress_html(&output, &output_len)) return 1;
        }
#endif

        if (lws_add_http_header_content_length(wsi, (unsigned long)output_len, &p, end) ||
            lws_finalize_http_header(wsi, &p, end) ||
            lws_write(wsi, buffer + LWS_PRE, p - (buffer + LWS_PRE), LWS_WRITE_HTTP_HEADERS) < 0)
          return 1;

        pss->buffer = pss->ptr = output;
        pss->len = output_len;
        lws_callback_on_writable(wsi);
      }
      break;

    case LWS_CALLBACK_HTTP_WRITEABLE:
      // First: stream image bytes from /api/image (chunked read from fd).
      if (pss->image_stream != NULL && pss->image_stream->fd >= 0) {
        int n = sizeof(buffer) - LWS_PRE;
        int m = lws_get_peer_write_allowance(wsi);
        if (m == 0) {
          lws_callback_on_writable(wsi);
          return 0;
        } else if (m != -1 && m < n) {
          n = m;
        }
        size_t remaining = pss->image_stream->size - pss->image_sent;
        if ((size_t)n > remaining) n = (int)remaining;
        if (n <= 0) {
          close_file_stream(pss->image_stream);
          pss->image_stream = NULL;
          goto try_to_reuse;
        }
        ssize_t bytes_read = (ssize_t)file_stream_read(pss->image_stream, buffer + LWS_PRE, (size_t)n);
        if (bytes_read <= 0) {
          close_file_stream(pss->image_stream);
          pss->image_stream = NULL;
          return -1;
        }
        if (lws_write_http(wsi, buffer + LWS_PRE, (size_t)bytes_read) < bytes_read) {
          close_file_stream(pss->image_stream);
          pss->image_stream = NULL;
          return -1;
        }
        pss->image_sent += (size_t)bytes_read;
        if (pss->image_sent < pss->image_stream->size) {
          lws_callback_on_writable(wsi);
          return 0;
        }
        close_file_stream(pss->image_stream);
        pss->image_stream = NULL;
        goto try_to_reuse;
      }
      // Second: existing page content path (from LWS_CALLBACK_HTTP)
      if (pss->buffer && pss->len > 0) {
        // Send page content
        int n = sizeof(buffer) - LWS_PRE;
        int m = lws_get_peer_write_allowance(wsi);
        if (m == 0) {
          lws_callback_on_writable(wsi);
          return 0;
        } else if (m != -1 && m < n) {
          n = m;
        }
        if (pss->ptr + n > pss->buffer + pss->len) {
          n = (int)(pss->len - (pss->ptr - pss->buffer));
          done = true;
        }
        memcpy(buffer + LWS_PRE, pss->ptr, n);
        pss->ptr += n;
        if (lws_write_http(wsi, buffer + LWS_PRE, (size_t)n) < n) {
          pss_buffer_free(pss);
          return -1;
        }
        if (!done && pss->ptr < pss->buffer + pss->len) {
          lws_callback_on_writable(wsi);
          return 0;
        }
        pss_buffer_free(pss);
        goto try_to_reuse;
      }
      // Handle pending POST result
      if (pss->pending_result != 0) {
        lwsl_err("HTTP_WRITEABLE: returning error %d\n", pss->pending_result);
        pss->pending_result = 0;
        return pss->pending_result;
      }
      // Handle pending upload response - send it now
      if (pss->upload_response_len > 0) {
        lwsl_err("HTTP_WRITEABLE: sending upload response\n");
        int n = lws_write(wsi, (unsigned char *)pss->upload_response, pss->upload_response_len, LWS_WRITE_HTTP_FINAL);
        pss->upload_response_len = 0;
        if (n < 0) {
          return -1;
        }
        pss->pending_result = 0;
        goto try_to_reuse;
      }
      goto try_to_reuse;

    case LWS_CALLBACK_HTTP_FILE_COMPLETION:
      goto try_to_reuse;

    case LWS_CALLBACK_HTTP_BODY:
      lwsl_err("HTTP_BODY: received len=%zu, total=%zu\n", len, pss->body_len);
      // Collect POST body data with dynamic reallocation
      size_t max_size = 100 * 1024 * 1024;  // 100MB
      size_t needed = pss->body_len + len + 1;
      
      if (needed > max_size) {
        free(pss->body);
        pss->body = NULL;
        return 1;
      }
      
      if (pss->body == NULL) {
        lwsl_err("HTTP_BODY: first allocation\n");
        pss->body = xmalloc(needed > 32768 ? needed : 32768);
        pss->body_len = 0;
      } else {
        // Check if we need to grow
        size_t current_capacity = 32768;
        while (current_capacity < pss->body_len && current_capacity < max_size) {
          current_capacity *= 2;
        }
        if (needed > current_capacity && current_capacity < max_size) {
          size_t new_capacity = current_capacity * 2;
          if (new_capacity > max_size) new_capacity = max_size;
          if (needed > new_capacity) {
            free(pss->body);
            pss->body = NULL;
            return 1;
          }
          lwsl_err("HTTP_BODY: realloc to %zu\n", new_capacity);
          char *new_body = xrealloc(pss->body, new_capacity);
          if (!new_body) {
            free(pss->body);
            pss->body = NULL;
            return 1;
          }
          pss->body = new_body;
        }
      }
      memcpy(pss->body + pss->body_len, in, len);
      pss->body_len += len;
      break;

    case LWS_CALLBACK_HTTP_BODY_COMPLETION:
      // Body fully received, null-terminate it
      if (pss->body != NULL) {
        pss->body[pss->body_len] = '\0';
        lwsl_err("BODY_COMPLETION: path=%s, body_len=%zu\n", pss->path, pss->body_len);
        int ret = 0;
        // Process based on path
        if (strncmp(pss->path, api_upload, strlen(api_upload)) == 0) {
          lwsl_err("BODY_COMPLETION: calling handle_api_upload\n");
          ret = handle_api_upload(wsi, pss->uri, pss->body, pss->body_len);
          lwsl_err("BODY_COMPLETION: handle_api_upload returned %d\n", ret);
        } else if (strncmp(pss->path, api_file, strlen(api_file)) == 0) {
          ret = handle_api_file_post(wsi, pss->body, pss->body_len);
        }
        // Free body buffer
        free(pss->body);
        pss->body = NULL;
        pss->body_len = 0;
        // Response already sent by handler, complete the transaction
        lwsl_err("BODY_COMPLETION: calling try_to_reuse\n");
        fflush(stderr);
        goto try_to_reuse;
      }
      goto try_to_reuse;

#if (defined(LWS_OPENSSL_SUPPORT) || defined(LWS_WITH_TLS)) && !defined(LWS_WITH_MBEDTLS)
    case LWS_CALLBACK_OPENSSL_PERFORM_CLIENT_CERT_VERIFICATION:
      if (!len || (SSL_get_verify_result((SSL *)in) != X509_V_OK)) {
        int err = X509_STORE_CTX_get_error((X509_STORE_CTX *)user);
        int depth = X509_STORE_CTX_get_error_depth((X509_STORE_CTX *)user);
        const char *msg = X509_verify_cert_error_string(err);
        lwsl_err("client certificate verification error: %s (%d), depth: %d\n", msg, err, depth);
        return 1;
      }
      break;
#endif
    case LWS_CALLBACK_HTTP_DROP_PROTOCOL:
      // Clean up image-stream handle if the session is torn down mid-stream.
      if (pss->image_stream != NULL) {
        close_file_stream(pss->image_stream);
        pss->image_stream = NULL;
      }
      break;
    default:
      break;
  }

  return 0;

  /* if we're on HTTP1.1 or 2.0, will keep the idle connection alive */
try_to_reuse:
  lwsl_err("TRY_TO_REUSE: before transaction_completed\n");
  fflush(stderr);
  if (lws_http_transaction_completed(wsi)) {
    lwsl_err("TRY_TO_REUSE: transaction_completed returned -1\n");
    return -1;
  }
  lwsl_err("TRY_TO_REUSE: done, returning 0\n");
  fflush(stderr);
  return 0;
}
