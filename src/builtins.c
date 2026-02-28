#include "runtime.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// Socket Operations using WinSock2
// ============================================================

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

static int wsa_initialized = 0;

static void ensure_wsa(void) {
  if (!wsa_initialized) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    wsa_initialized = 1;
  }
}

StolaValue *stola_socket_connect(StolaValue *host, StolaValue *port) {
  if (!host || host->type != STOLA_STRING || !port)
    return stola_new_int(-1);

  ensure_wsa();

  const char *hostname = host->as.str_val;
  int port_num = (int)port->as.int_val;

  struct addrinfo hints, *result = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", port_num);

  if (getaddrinfo(hostname, port_str, &hints, &result) != 0) {
    return stola_new_int(-1);
  }

  SOCKET sock =
      socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (sock == INVALID_SOCKET) {
    freeaddrinfo(result);
    return stola_new_int(-1);
  }

  if (connect(sock, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
    closesocket(sock);
    freeaddrinfo(result);
    return stola_new_int(-1);
  }

  freeaddrinfo(result);
  return stola_new_int((int64_t)sock);
}

StolaValue *stola_socket_send(StolaValue *fd, StolaValue *data) {
  if (!fd || !data || data->type != STOLA_STRING)
    return stola_new_int(-1);

  SOCKET sock = (SOCKET)fd->as.int_val;
  const char *buf = data->as.str_val;
  int len = (int)strlen(buf);
  int sent = send(sock, buf, len, 0);
  return stola_new_int(sent);
}

StolaValue *stola_socket_receive(StolaValue *fd) {
  if (!fd)
    return stola_new_string("");

  SOCKET sock = (SOCKET)fd->as.int_val;

  size_t total = 0;
  size_t cap = 4096;
  char *buf = (char *)malloc(cap);

  while (1) {
    if (total + 4096 > cap) {
      cap *= 2;
      buf = (char *)realloc(buf, cap);
    }
    int received = recv(sock, buf + total, 4096, 0);
    if (received <= 0)
      break;
    total += received;
  }

  buf[total] = '\0';
  return stola_new_string_owned(buf);
}

void stola_socket_close(StolaValue *fd) {
  if (!fd)
    return;
  closesocket((SOCKET)fd->as.int_val);
}

// ============================================================
// Native HTTP GET using WinHTTP (supports HTTPS!)
// ============================================================

#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

StolaValue *stola_http_fetch(StolaValue *url_val) {
  if (!url_val || url_val->type != STOLA_STRING)
    return stola_new_null();

  const char *url_str = url_val->as.str_val;

  // Convert URL to wide string
  int wlen = MultiByteToWideChar(65001, 0, url_str, -1, NULL, 0);
  wchar_t *wurl = (wchar_t *)malloc(wlen * sizeof(wchar_t));
  MultiByteToWideChar(65001, 0, url_str, -1, wurl, wlen);

  // Crack the URL
  URL_COMPONENTS uc;
  memset(&uc, 0, sizeof(uc));
  uc.dwStructSize = sizeof(uc);

  wchar_t host[256] = {0};
  wchar_t path[2048] = {0};
  uc.lpszHostName = host;
  uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 2048;

  if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) {
    free(wurl);
    stola_throw(stola_new_string("Invalid URL format for http_fetch"));
    return stola_new_null();
  }

  HINTERNET hSession =
      WinHttpOpen(L"StolasScript/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) {
    free(wurl);
    stola_throw(stola_new_string("winhttp module: WinHttpOpen failed"));
    return stola_new_null();
  }

  HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
  if (!hConnect) {
    WinHttpCloseHandle(hSession);
    free(wurl);
    stola_throw(stola_new_string("HTTP Connection Failed: Host not found"));
    return stola_new_null();
  }

  DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET hRequest =
      WinHttpOpenRequest(hConnect, L"GET", path, NULL, WINHTTP_NO_REFERER,
                         WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!hRequest) {
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    free(wurl);
    stola_throw(stola_new_string("Failed to initialize HTTP GET Request"));
    return stola_new_null();
  }

  if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    free(wurl);
    stola_throw(stola_new_string("Failed to dispatch HTTP HTTP GET Request"));
    return stola_new_null();
  }

  if (!WinHttpReceiveResponse(hRequest, NULL)) {
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    free(wurl);
    stola_throw(
        stola_new_string("Network Error: WinHttpReceiveResponse Timeout"));
    return stola_new_null();
  }

  // Read status code
  DWORD status = 0;
  DWORD status_size = sizeof(status);
  WinHttpQueryHeaders(hRequest,
                      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                      WINHTTP_NO_HEADER_INDEX);

  // Read body
  size_t total = 0;
  size_t cap = 8192;
  char *body = (char *)malloc(cap);

  DWORD bytes_available = 0;
  while (WinHttpQueryDataAvailable(hRequest, &bytes_available) &&
         bytes_available > 0) {
    if (total + bytes_available + 1 > cap) {
      cap = (total + bytes_available + 1) * 2;
      body = (char *)realloc(body, cap);
    }
    DWORD bytes_read = 0;
    WinHttpReadData(hRequest, body + total, bytes_available, &bytes_read);
    total += bytes_read;
  }
  body[total] = '\0';

  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  free(wurl);

  // Build response dict: {status: N, body: "..."}
  StolaValue *result = stola_new_dict();
  stola_dict_set(result, stola_new_string("status"),
                 stola_new_int((int64_t)status));
  stola_dict_set(result, stola_new_string("body"),
                 stola_new_string_owned(body));
  return result;
}

// ============================================================
// Real Parallelism (Windows Threads & Mutexes)
// ============================================================

typedef StolaValue *(*ThreadFunc)(StolaValue *, StolaValue *, StolaValue *,
                                  StolaValue *);

typedef struct {
  ThreadFunc func;
  StolaValue *arg;
} ThreadData;

static DWORD WINAPI stola_thread_start_routine(LPVOID lpParam) {
  ThreadData *data = (ThreadData *)lpParam;
  StolaValue *null_val = stola_new_null();
  data->func(data->arg, null_val, null_val, null_val);
  free(data);
  return 0;
}

StolaValue *stola_thread_spawn(void *func_ptr, StolaValue *arg) {
  ThreadData *data = (ThreadData *)malloc(sizeof(ThreadData));
  data->func = (ThreadFunc)func_ptr;
  data->arg = arg;
  HANDLE hThread =
      CreateThread(NULL, 0, stola_thread_start_routine, data, 0, NULL);
  return stola_new_int((int64_t)hThread);
}

StolaValue *stola_thread_join(StolaValue *hThread_val) {
  if (!hThread_val || hThread_val->type != STOLA_INT)
    return stola_new_null();
  HANDLE hThread = (HANDLE)hThread_val->as.int_val;
  WaitForSingleObject(hThread, INFINITE);
  CloseHandle(hThread);
  return stola_new_null();
}

StolaValue *stola_mutex_create(void) {
  HANDLE hMutex = CreateMutex(NULL, FALSE, NULL);
  return stola_new_int((int64_t)hMutex);
}

StolaValue *stola_mutex_lock(StolaValue *hMutex_val) {
  if (!hMutex_val || hMutex_val->type != STOLA_INT)
    return stola_new_null();
  HANDLE hMutex = (HANDLE)hMutex_val->as.int_val;
  WaitForSingleObject(hMutex, INFINITE);
  return stola_new_null();
}

StolaValue *stola_mutex_unlock(StolaValue *hMutex_val) {
  if (!hMutex_val || hMutex_val->type != STOLA_INT)
    return stola_new_null();
  HANDLE hMutex = (HANDLE)hMutex_val->as.int_val;
  ReleaseMutex(hMutex);
  return stola_new_null();
}

#else
// Stubs for non-Windows
StolaValue *stola_socket_connect(StolaValue *h, StolaValue *p) {
  (void)h;
  (void)p;
  return stola_new_int(-1);
}
StolaValue *stola_socket_send(StolaValue *f, StolaValue *d) {
  (void)f;
  (void)d;
  return stola_new_int(-1);
}
StolaValue *stola_socket_receive(StolaValue *f) {
  (void)f;
  return stola_new_string("");
}
void stola_socket_close(StolaValue *f) { (void)f; }
StolaValue *stola_http_fetch(StolaValue *u) {
  (void)u;
  return stola_new_null();
}

StolaValue *stola_thread_spawn(void *func_ptr, StolaValue *arg) {
  (void)func_ptr;
  (void)arg;
  return stola_new_null();
}
StolaValue *stola_thread_join(StolaValue *t) {
  (void)t;
  return stola_new_null();
}
StolaValue *stola_mutex_create(void) { return stola_new_null(); }
StolaValue *stola_mutex_lock(StolaValue *m) {
  (void)m;
  return stola_new_null();
}
StolaValue *stola_mutex_unlock(StolaValue *m) {
  (void)m;
  return stola_new_null();
}

#endif
