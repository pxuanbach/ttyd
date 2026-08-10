#include <libwebsockets.h>
#include <string.h>
#include <zlib.h>

#include "html.h"
#include "server.h"
#include "utils.h"
#include "file.h"

// File API endpoints
static const char *api_directory = "/api/directory";
static const char *api_file = "/api/file";

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

  if (lws_add_http_header_status(wsi, status, &p, end) ||
      lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
                                   (unsigned char *)"application/json;charset=utf-8", 30, &p, end) ||
      lws_add_http_header_content_length(wsi, (unsigned long)n, &p, end) ||
      lws_finalize_http_header(wsi, &p, end) ||
      lws_write(wsi, buffer + LWS_PRE, p - (buffer + LWS_PRE), LWS_WRITE_HTTP_HEADERS) < 0)
    return 1;

  if (lws_write(wsi, (unsigned char *)body, (size_t)n, LWS_WRITE_HTTP_HEADERS) < (size_t)n)
    return 1;

  return lws_http_transaction_completed(wsi) ? 1 : 0;
}

// Helper to send JSON success response
static int send_json_response(struct lws *wsi, const char *json, size_t len) {
  unsigned char buffer[4096 + LWS_PRE], *p, *end;
  p = buffer + LWS_PRE;
  end = p + sizeof(buffer) - LWS_PRE;

  if (lws_add_http_header_status(wsi, HTTP_STATUS_OK, &p, end) ||
      lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
                                   (unsigned char *)"application/json;charset=utf-8", 30, &p, end) ||
      lws_add_http_header_content_length(wsi, (unsigned long)len, &p, end) ||
      lws_finalize_http_header(wsi, &p, end) ||
      lws_write(wsi, buffer + LWS_PRE, p - (buffer + LWS_PRE), LWS_WRITE_HTTP_HEADERS) < 0)
    return 1;

  if (lws_write(wsi, (unsigned char *)json, len, LWS_WRITE_HTTP_HEADERS) < len)
    return 1;

  return lws_http_transaction_completed(wsi) ? 1 : 0;
}

// Handle GET /api/directory
static int handle_api_directory(struct lws *wsi, const char *path) {
  // Extract path from query string
  const char *query = strchr(path, '?');
  char dir_path[256] = {0};

  if (query != NULL) {
    // Parse path= parameter
    const char *path_param = strstr(query, "path=");
    if (path_param != NULL) {
      path_param += 5;  // skip "path="
      const char *end = strchr(path_param, '&');
      if (end == NULL) end = path_param + strlen(path_param);
      int len = (int)(end - path_param);
      if (len > 0 && len < (int)sizeof(dir_path)) {
        urldecode(path_param, len, dir_path);
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
static int handle_api_file_get(struct lws *wsi, const char *path) {
  // Extract path from query string
  const char *query = strchr(path, '?');
  char file_path[256] = {0};

  if (query != NULL) {
    const char *path_param = strstr(query, "path=");
    if (path_param != NULL) {
      path_param += 5;  // skip "path="
      const char *end = strchr(path_param, '&');
      if (end == NULL) end = path_param + strlen(path_param);
      int len = (int)(end - path_param);
      if (len > 0 && len < (int)sizeof(file_path)) {
        urldecode(path_param, len, file_path);
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
static int handle_api_file_delete(struct lws *wsi, const char *path) {
  const char *query = strchr(path, '?');
  char file_path[256] = {0};

  if (query != NULL) {
    const char *path_param = strstr(query, "path=");
    if (path_param != NULL) {
      path_param += 5;
      const char *end = strchr(path_param, '&');
      if (end == NULL) end = path_param + strlen(path_param);
      int len = (int)(end - path_param);
      if (len > 0 && len < (int)sizeof(file_path)) {
        urldecode(path_param, len, file_path);
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

int callback_http(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
  struct pss_http *pss = (struct pss_http *)user;
  unsigned char buffer[4096 + LWS_PRE], *p, *end;
  char buf[256];
  bool done = false;

  switch (reason) {
    case LWS_CALLBACK_HTTP:
      access_log(wsi, (const char *)in);
      snprintf(pss->path, sizeof(pss->path), "%s", (const char *)in);
      // Reset body collection state
      pss->body = NULL;
      pss->body_len = 0;
      pss->body_pos = 0;

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
        return handle_api_directory(wsi, pss->path);
      }
      if (strncmp(pss->path, api_file, strlen(api_file)) == 0) {
        // GET /api/file?path=...
        return handle_api_file_get(wsi, pss->path);
      }

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
      if (!pss->buffer || pss->len == 0) {
        goto try_to_reuse;
      }

      do {
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
      } while (!lws_send_pipe_choked(wsi) && !done);

      if (!done && pss->ptr < pss->buffer + pss->len) {
        lws_callback_on_writable(wsi);
        break;
      }

      pss_buffer_free(pss);
      goto try_to_reuse;

    case LWS_CALLBACK_HTTP_FILE_COMPLETION:
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
    default:
      break;
  }

  return 0;

  /* if we're on HTTP1.1 or 2.0, will keep the idle connection alive */
try_to_reuse:
  if (lws_http_transaction_completed(wsi)) return -1;

  return 0;
}
