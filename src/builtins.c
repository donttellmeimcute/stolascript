#include "runtime.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// Shared helpers: SHA-1 and Base64 (RFC 6455 WebSocket)
// ============================================================

static void ws_sha1(const unsigned char *data, size_t len, unsigned char out[20]) {
  uint32_t h[5] = {0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476,0xC3D2E1F0};
  size_t padded_len = ((len + 8) / 64 + 1) * 64;
  unsigned char *msg = (unsigned char *)calloc(padded_len, 1);
  memcpy(msg, data, len);
  msg[len] = 0x80;
  uint64_t bit_len = (uint64_t)len * 8;
  for (int i = 0; i < 8; i++)
    msg[padded_len - 8 + i] = (unsigned char)(bit_len >> (56 - i * 8));
  for (size_t off = 0; off < padded_len; off += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
      w[i] = ((uint32_t)msg[off+i*4]<<24)|((uint32_t)msg[off+i*4+1]<<16)
            |((uint32_t)msg[off+i*4+2]<<8)|(uint32_t)msg[off+i*4+3];
    for (int i = 16; i < 80; i++) {
      uint32_t v = w[i-3]^w[i-8]^w[i-14]^w[i-16];
      w[i] = (v<<1)|(v>>31);
    }
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f,k;
      if (i<20){f=(b&c)|(~b&d);k=0x5A827999;}
      else if(i<40){f=b^c^d;k=0x6ED9EBA1;}
      else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;}
      else{f=b^c^d;k=0xCA62C1D6;}
      uint32_t t=((a<<5)|(a>>27))+f+e+k+w[i];
      e=d;d=c;c=(b<<30)|(b>>2);b=a;a=t;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;
  }
  free(msg);
  for (int i=0;i<5;i++){
    out[i*4]=(h[i]>>24)&0xFF;out[i*4+1]=(h[i]>>16)&0xFF;
    out[i*4+2]=(h[i]>>8)&0xFF;out[i*4+3]=h[i]&0xFF;
  }
}

static const char ws_b64chars[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *ws_base64_encode(const unsigned char *data, size_t len) {
  size_t out_len = 4*((len+2)/3);
  char *out = (char *)malloc(out_len+1);
  size_t j=0;
  for (size_t i=0;i<len;i+=3) {
    unsigned char b0=data[i],b1=(i+1<len)?data[i+1]:0,b2=(i+2<len)?data[i+2]:0;
    out[j++]=ws_b64chars[(b0>>2)&0x3F];
    out[j++]=ws_b64chars[((b0&0x3)<<4)|((b1>>4)&0xF)];
    out[j++]=(i+1<len)?ws_b64chars[((b1&0xF)<<2)|((b2>>6)&0x3)]:'=';
    out[j++]=(i+2<len)?ws_b64chars[b2&0x3F]:'=';
  }
  out[j]='\0';
  return out;
}

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

// ============================================================
// WebSocket Support (RFC 6455) - Windows
// ============================================================

// Send text frame client→server (with mask)
static int ws_send_frame(SOCKET sock, const char *payload, size_t plen) {
  unsigned char mask[4];
  srand((unsigned int)GetTickCount());
  for (int i=0;i<4;i++) mask[i]=rand()&0xFF;
  unsigned char hdr[14]; int hlen=0;
  hdr[hlen++]=0x81;
  if (plen<=125) hdr[hlen++]=0x80|(unsigned char)plen;
  else if (plen<=65535) {hdr[hlen++]=0x80|126;hdr[hlen++]=(plen>>8)&0xFF;hdr[hlen++]=plen&0xFF;}
  else {hdr[hlen++]=0x80|127;for(int i=7;i>=0;i--)hdr[hlen++]=(plen>>(i*8))&0xFF;}
  memcpy(hdr+hlen,mask,4); hlen+=4;
  unsigned char *frame=(unsigned char*)malloc(hlen+plen);
  memcpy(frame,hdr,hlen);
  for (size_t i=0;i<plen;i++) frame[hlen+i]=((unsigned char)payload[i])^mask[i%4];
  int sent=send(sock,(const char*)frame,(int)(hlen+plen),0);
  free(frame); return sent;
}

// Receive one frame server→client
static char *ws_recv_frame(SOCKET sock) {
  unsigned char hdr[2];
  if (recv(sock,(char*)hdr,2,MSG_WAITALL)!=2) return NULL;
  int opcode=hdr[0]&0x0F;
  int masked=(hdr[1]>>7)&1;
  uint64_t plen=hdr[1]&0x7F;
  if (plen==126) {unsigned char e[2];if(recv(sock,(char*)e,2,MSG_WAITALL)!=2)return NULL;plen=((uint64_t)e[0]<<8)|e[1];}
  else if(plen==127){unsigned char e[8];if(recv(sock,(char*)e,8,MSG_WAITALL)!=8)return NULL;plen=0;for(int i=0;i<8;i++)plen=(plen<<8)|e[i];}
  unsigned char mask[4]={0};
  if (masked && recv(sock,(char*)mask,4,MSG_WAITALL)!=4) return NULL;
  if (opcode==0x8) return NULL; // close
  char *payload=(char*)malloc(plen+1);
  size_t got=0;
  while (got<plen) {int r=recv(sock,payload+got,(int)(plen-got),0);if(r<=0){free(payload);return NULL;}got+=r;}
  payload[plen]='\0';
  if (masked) for(size_t i=0;i<plen;i++) payload[i]^=mask[i%4];
  if (opcode==0x9) { // ping → pong
    unsigned char pong[2]={0x8A,0x00}; send(sock,(const char*)pong,2,0);
    free(payload); return ws_recv_frame(sock);
  }
  return payload;
}

StolaValue *stola_ws_connect(StolaValue *url_val) {
  if (!url_val||url_val->type!=STOLA_STRING) return stola_new_int(-1);
  ensure_wsa();
  const char *url=url_val->as.str_val;
  char host[256]={0}; int port=80; char path[1024]="/";
  const char *p=url;
  if (strncmp(p,"ws://",5)==0) p+=5;
  else if (strncmp(p,"wss://",6)==0){p+=6;port=443;}
  const char *slash=strchr(p,'/'), *colon=strchr(p,':');
  if (colon&&(!slash||colon<slash)){
    size_t hl=colon-p; strncpy(host,p,hl<255?hl:255);
    port=atoi(colon+1);
  } else if (slash) strncpy(host,p,(slash-p)<255?(slash-p):255);
  else strncpy(host,p,255);
  if (slash) strncpy(path,slash,1023);
  char port_str[16]; snprintf(port_str,sizeof(port_str),"%d",port);
  struct addrinfo hints,*res=NULL; memset(&hints,0,sizeof(hints));
  hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM;
  if (getaddrinfo(host,port_str,&hints,&res)!=0) return stola_new_int(-1);
  SOCKET sock=socket(res->ai_family,res->ai_socktype,res->ai_protocol);
  if (sock==INVALID_SOCKET){freeaddrinfo(res);return stola_new_int(-1);}
  if (connect(sock,res->ai_addr,(int)res->ai_addrlen)==SOCKET_ERROR){closesocket(sock);freeaddrinfo(res);return stola_new_int(-1);}
  freeaddrinfo(res);
  unsigned char key_bytes[16]; srand((unsigned int)GetTickCount());
  for (int i=0;i<16;i++) key_bytes[i]=rand()&0xFF;
  char *key=ws_base64_encode(key_bytes,16);
  char req[1024];
  snprintf(req,sizeof(req),
    "GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\n"
    "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
    "Sec-WebSocket-Version: 13\r\n\r\n",path,host,port,key);
  free(key);
  send(sock,req,(int)strlen(req),0);
  char resp[2048]={0}; recv(sock,resp,sizeof(resp)-1,0);
  if (!strstr(resp,"101")){closesocket(sock);return stola_new_int(-1);}
  return stola_new_int((int64_t)sock);
}

StolaValue *stola_ws_send(StolaValue *handle, StolaValue *msg) {
  if (!handle||handle->type!=STOLA_INT) return stola_new_int(-1);
  if (!msg||msg->type!=STOLA_STRING) return stola_new_int(-1);
  SOCKET sock=(SOCKET)handle->as.int_val;
  const char *payload=msg->as.str_val;
  return stola_new_int(ws_send_frame(sock,payload,strlen(payload)));
}

StolaValue *stola_ws_receive(StolaValue *handle) {
  if (!handle||handle->type!=STOLA_INT) return stola_new_null();
  char *payload=ws_recv_frame((SOCKET)handle->as.int_val);
  if (!payload) return stola_new_null();
  return stola_new_string_owned(payload);
}

StolaValue *stola_ws_close(StolaValue *handle) {
  if (!handle||handle->type!=STOLA_INT) return stola_new_null();
  unsigned char cf[2]={0x88,0x00};
  send((SOCKET)handle->as.int_val,(const char*)cf,2,0);
  closesocket((SOCKET)handle->as.int_val);
  return stola_new_null();
}

StolaValue *stola_ws_server_create(StolaValue *port_val) {
  if (!port_val||port_val->type!=STOLA_INT) return stola_new_int(-1);
  ensure_wsa();
  SOCKET server=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
  if (server==INVALID_SOCKET) return stola_new_int(-1);
  int opt=1;
  setsockopt(server,SOL_SOCKET,SO_REUSEADDR,(const char*)&opt,sizeof(opt));
  struct sockaddr_in addr; memset(&addr,0,sizeof(addr));
  addr.sin_family=AF_INET; addr.sin_addr.s_addr=INADDR_ANY;
  addr.sin_port=htons((unsigned short)port_val->as.int_val);
  if (bind(server,(struct sockaddr*)&addr,sizeof(addr))==SOCKET_ERROR){closesocket(server);return stola_new_int(-1);}
  if (listen(server,SOMAXCONN)==SOCKET_ERROR){closesocket(server);return stola_new_int(-1);}
  return stola_new_int((int64_t)server);
}

StolaValue *stola_ws_server_accept(StolaValue *server_val) {
  if (!server_val||server_val->type!=STOLA_INT) return stola_new_int(-1);
  SOCKET client=accept((SOCKET)server_val->as.int_val,NULL,NULL);
  if (client==INVALID_SOCKET) return stola_new_int(-1);
  char buf[4096]={0}; recv(client,buf,sizeof(buf)-1,0);
  char *kh=strstr(buf,"Sec-WebSocket-Key:");
  if (!kh){closesocket(client);return stola_new_int(-1);}
  kh+=18; while(*kh==' ')kh++;
  char key[64]={0}; int ki=0;
  while(*kh&&*kh!='\r'&&*kh!='\n'&&ki<63) key[ki++]=*kh++;
  char combined[256];
  snprintf(combined,sizeof(combined),"%s%s",key,"258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
  unsigned char sha1_out[20];
  ws_sha1((unsigned char*)combined,strlen(combined),sha1_out);
  char *accept_key=ws_base64_encode(sha1_out,20);
  char response[512];
  snprintf(response,sizeof(response),
    "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
    "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",accept_key);
  free(accept_key);
  send(client,response,(int)strlen(response),0);
  return stola_new_int((int64_t)client);
}

StolaValue *stola_ws_server_close(StolaValue *server_val) {
  if (!server_val||server_val->type!=STOLA_INT) return stola_new_null();
  closesocket((SOCKET)server_val->as.int_val);
  return stola_new_null();
}

// ── I/O Multiplexing ─────────────────────────────────────────────────────────
// stola_ws_select(handles: array<int>, timeout_ms: int) -> array<int>
// Returns the subset of socket handles that have data ready to read.
// timeout_ms < 0 → block; timeout_ms == 0 → non-blocking poll.
StolaValue *stola_ws_select(StolaValue *handles, StolaValue *timeout_ms_val) {
  StolaValue *result = stola_new_array();
  if (!handles || handles->type != STOLA_ARRAY) return result;
  int64_t timeout_ms = (timeout_ms_val && timeout_ms_val->type == STOLA_INT)
                       ? timeout_ms_val->as.int_val : -1;
  fd_set rfds;
  FD_ZERO(&rfds);
  int count = (int)handles->as.array_val.count;
  for (int i = 0; i < count; i++) {
    StolaValue *h = handles->as.array_val.items[i];
    if (h && h->type == STOLA_INT)
      FD_SET((SOCKET)h->as.int_val, &rfds);
  }
  struct timeval tv, *tvp = NULL;
  if (timeout_ms >= 0) {
    tv.tv_sec  = (long)(timeout_ms / 1000);
    tv.tv_usec = (long)((timeout_ms % 1000) * 1000);
    tvp = &tv;
  }
  // On Windows, the first argument to select() is ignored.
  int r = select(0, &rfds, NULL, NULL, tvp);
  if (r <= 0) return result;
  for (int i = 0; i < count; i++) {
    StolaValue *h = handles->as.array_val.items[i];
    if (h && h->type == STOLA_INT &&
        FD_ISSET((SOCKET)h->as.int_val, &rfds))
      stola_push(result, stola_new_int(h->as.int_val));
  }
  return result;
}

#else
// ============================================================
// POSIX / Linux implementation
// ============================================================
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <time.h>

// ---- Sockets ----
StolaValue *stola_socket_connect(StolaValue *host, StolaValue *port) {
  if (!host || host->type != STOLA_STRING || !port) return stola_new_int(-1);
  const char *hostname = host->as.str_val;
  int port_num = (int)port->as.int_val;
  char port_str[16]; snprintf(port_str, sizeof(port_str), "%d", port_num);
  struct addrinfo hints, *result = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(hostname, port_str, &hints, &result) != 0) return stola_new_int(-1);
  int sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (sock < 0) { freeaddrinfo(result); return stola_new_int(-1); }
  if (connect(sock, result->ai_addr, result->ai_addrlen) < 0) {
    close(sock); freeaddrinfo(result); return stola_new_int(-1);
  }
  freeaddrinfo(result);
  return stola_new_int((int64_t)sock);
}

StolaValue *stola_socket_send(StolaValue *fd, StolaValue *data) {
  if (!fd || !data || data->type != STOLA_STRING) return stola_new_int(-1);
  int sock = (int)fd->as.int_val;
  const char *buf = data->as.str_val;
  return stola_new_int((int64_t)send(sock, buf, strlen(buf), 0));
}

StolaValue *stola_socket_receive(StolaValue *fd) {
  if (!fd) return stola_new_string("");
  int sock = (int)fd->as.int_val;
  size_t total = 0, cap = 4096;
  char *buf = (char *)malloc(cap);
  while (1) {
    if (total + 4096 > cap) { cap *= 2; buf = (char *)realloc(buf, cap); }
    int r = (int)recv(sock, buf + total, 4096, 0);
    if (r <= 0) break;
    total += r;
  }
  buf[total] = '\0';
  return stola_new_string_owned(buf);
}

void stola_socket_close(StolaValue *fd) {
  if (!fd) return;
  close((int)fd->as.int_val);
}

// ---- HTTP fetch (raw POSIX, HTTP/1.1) ----
StolaValue *stola_http_fetch(StolaValue *url_val) {
  if (!url_val || url_val->type != STOLA_STRING) return stola_new_null();
  const char *url = url_val->as.str_val;
  char host[256] = {0}; char path[2048] = "/"; int port = 80;
  const char *p = url;
  if (strncmp(p, "http://", 7) == 0) p += 7;
  else if (strncmp(p, "https://", 8) == 0) { p += 8; port = 443; }
  const char *slash = strchr(p, '/'), *colon = strchr(p, ':');
  if (colon && (!slash || colon < slash)) {
    size_t hl = colon - p; strncpy(host, p, hl < 255 ? hl : 255);
    port = atoi(colon + 1);
  } else if (slash) strncpy(host, p, (slash-p) < 255 ? (slash-p) : 255);
  else strncpy(host, p, 255);
  if (slash) strncpy(path, slash, 2047);
  char port_str[16]; snprintf(port_str, sizeof(port_str), "%d", port);
  struct addrinfo hints, *res = NULL; memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host, port_str, &hints, &res) != 0) {
    stola_throw(stola_new_string("http_fetch: host not found")); return stola_new_null();
  }
  int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sock < 0) { freeaddrinfo(res); stola_throw(stola_new_string("http_fetch: socket failed")); return stola_new_null(); }
  if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
    close(sock); freeaddrinfo(res); stola_throw(stola_new_string("http_fetch: connect failed")); return stola_new_null();
  }
  freeaddrinfo(res);
  char req[3096];
  snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
  send(sock, req, strlen(req), 0);
  size_t total = 0, cap = 8192;
  char *buf = (char *)malloc(cap);
  while (1) {
    if (total + 4096 > cap) { cap *= 2; buf = (char *)realloc(buf, cap); }
    int r = (int)recv(sock, buf + total, 4096, 0);
    if (r <= 0) break; total += r;
  }
  close(sock);
  buf[total] = '\0';
  int status = 0;
  if (strncmp(buf, "HTTP/", 5) == 0) { const char *sp = strchr(buf, ' '); if (sp) status = atoi(sp+1); }
  char *body_start = strstr(buf, "\r\n\r\n");
  char *body_copy = body_start ? strdup(body_start + 4) : strdup("");
  free(buf);
  StolaValue *result = stola_new_dict();
  stola_dict_set(result, stola_new_string("status"), stola_new_int((int64_t)status));
  stola_dict_set(result, stola_new_string("body"), stola_new_string_owned(body_copy));
  return result;
}

// ---- Threads (pthreads) ----
typedef StolaValue *(*LinuxThreadFunc)(StolaValue *, StolaValue *, StolaValue *, StolaValue *);
typedef struct { LinuxThreadFunc func; StolaValue *arg; } LinuxThreadData;

static void *stola_thread_start_linux(void *p) {
  LinuxThreadData *d = (LinuxThreadData *)p;
  StolaValue *nv = stola_new_null();
  d->func(d->arg, nv, nv, nv);
  free(d); return NULL;
}

StolaValue *stola_thread_spawn(void *func_ptr, StolaValue *arg) {
  LinuxThreadData *d = (LinuxThreadData *)malloc(sizeof(LinuxThreadData));
  d->func = (LinuxThreadFunc)func_ptr; d->arg = arg;
  pthread_t *tid = (pthread_t *)malloc(sizeof(pthread_t));
  pthread_create(tid, NULL, stola_thread_start_linux, d);
  return stola_new_int((int64_t)(uintptr_t)tid);
}

StolaValue *stola_thread_join(StolaValue *t) {
  if (!t || t->type != STOLA_INT) return stola_new_null();
  pthread_t *tid = (pthread_t *)(uintptr_t)t->as.int_val;
  pthread_join(*tid, NULL); free(tid); return stola_new_null();
}

StolaValue *stola_mutex_create(void) {
  pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init(m, NULL);
  return stola_new_int((int64_t)(uintptr_t)m);
}

StolaValue *stola_mutex_lock(StolaValue *v) {
  if (!v || v->type != STOLA_INT) return stola_new_null();
  pthread_mutex_lock((pthread_mutex_t *)(uintptr_t)v->as.int_val);
  return stola_new_null();
}

StolaValue *stola_mutex_unlock(StolaValue *v) {
  if (!v || v->type != STOLA_INT) return stola_new_null();
  pthread_mutex_unlock((pthread_mutex_t *)(uintptr_t)v->as.int_val);
  return stola_new_null();
}

// ---- WebSocket (POSIX, RFC 6455) ----
static int ws_send_frame_posix(int sock, const char *payload, size_t plen) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  srand((unsigned int)(ts.tv_nsec));
  unsigned char mask[4]; for (int i=0;i<4;i++) mask[i]=rand()&0xFF;
  unsigned char hdr[14]; int hlen=0;
  hdr[hlen++]=0x81;
  if (plen<=125) hdr[hlen++]=0x80|(unsigned char)plen;
  else if(plen<=65535){hdr[hlen++]=0x80|126;hdr[hlen++]=(plen>>8)&0xFF;hdr[hlen++]=plen&0xFF;}
  else{hdr[hlen++]=0x80|127;for(int i=7;i>=0;i--)hdr[hlen++]=(plen>>(i*8))&0xFF;}
  memcpy(hdr+hlen,mask,4); hlen+=4;
  unsigned char *frame=(unsigned char*)malloc(hlen+plen);
  memcpy(frame,hdr,hlen);
  for (size_t i=0;i<plen;i++) frame[hlen+i]=((unsigned char)payload[i])^mask[i%4];
  int sent=(int)send(sock,(const char*)frame,hlen+plen,0);
  free(frame); return sent;
}

static char *ws_recv_frame_posix(int sock) {
  unsigned char hdr[2];
  if (recv(sock,(char*)hdr,2,MSG_WAITALL)!=2) return NULL;
  int opcode=hdr[0]&0x0F, masked=(hdr[1]>>7)&1;
  uint64_t plen=hdr[1]&0x7F;
  if (plen==126){unsigned char e[2];if(recv(sock,(char*)e,2,MSG_WAITALL)!=2)return NULL;plen=((uint64_t)e[0]<<8)|e[1];}
  else if(plen==127){unsigned char e[8];if(recv(sock,(char*)e,8,MSG_WAITALL)!=8)return NULL;plen=0;for(int i=0;i<8;i++)plen=(plen<<8)|e[i];}
  unsigned char mask[4]={0};
  if (masked && recv(sock,(char*)mask,4,MSG_WAITALL)!=4) return NULL;
  if (opcode==0x8) return NULL;
  char *payload=(char*)malloc(plen+1);
  size_t got=0;
  while (got<plen){int r=(int)recv(sock,payload+got,(int)(plen-got),0);if(r<=0){free(payload);return NULL;}got+=r;}
  payload[plen]='\0';
  if (masked) for(size_t i=0;i<plen;i++) payload[i]^=mask[i%4];
  if (opcode==0x9){unsigned char pong[2]={0x8A,0x00};send(sock,(const char*)pong,2,0);free(payload);return ws_recv_frame_posix(sock);}
  return payload;
}

StolaValue *stola_ws_connect(StolaValue *url_val) {
  if (!url_val||url_val->type!=STOLA_STRING) return stola_new_int(-1);
  const char *url=url_val->as.str_val;
  char host[256]={0}; int port=80; char path[1024]="/";
  const char *p=url;
  if (strncmp(p,"ws://",5)==0) p+=5;
  else if(strncmp(p,"wss://",6)==0){p+=6;port=443;}
  const char *slash=strchr(p,'/'), *colon=strchr(p,':');
  if (colon&&(!slash||colon<slash)){size_t hl=colon-p;strncpy(host,p,hl<255?hl:255);port=atoi(colon+1);}
  else if(slash) strncpy(host,p,(slash-p)<255?(slash-p):255);
  else strncpy(host,p,255);
  if (slash) strncpy(path,slash,1023);
  char port_str[16]; snprintf(port_str,sizeof(port_str),"%d",port);
  struct addrinfo hints,*res=NULL; memset(&hints,0,sizeof(hints));
  hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM;
  if (getaddrinfo(host,port_str,&hints,&res)!=0) return stola_new_int(-1);
  int sock=socket(res->ai_family,res->ai_socktype,res->ai_protocol);
  if (sock<0){freeaddrinfo(res);return stola_new_int(-1);}
  if (connect(sock,res->ai_addr,res->ai_addrlen)<0){close(sock);freeaddrinfo(res);return stola_new_int(-1);}
  freeaddrinfo(res);
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); srand((unsigned int)(ts.tv_nsec));
  unsigned char key_bytes[16]; for(int i=0;i<16;i++) key_bytes[i]=rand()&0xFF;
  char *key=ws_base64_encode(key_bytes,16);
  char req[1024];
  snprintf(req,sizeof(req),"GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\n"
    "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",
    path,host,port,key);
  free(key);
  send(sock,req,strlen(req),0);
  char resp[2048]={0}; recv(sock,resp,sizeof(resp)-1,0);
  if (!strstr(resp,"101")){close(sock);return stola_new_int(-1);}
  return stola_new_int((int64_t)sock);
}

StolaValue *stola_ws_send(StolaValue *handle, StolaValue *msg) {
  if (!handle||handle->type!=STOLA_INT) return stola_new_int(-1);
  if (!msg||msg->type!=STOLA_STRING) return stola_new_int(-1);
  return stola_new_int(ws_send_frame_posix((int)handle->as.int_val,
    msg->as.str_val, strlen(msg->as.str_val)));
}

StolaValue *stola_ws_receive(StolaValue *handle) {
  if (!handle||handle->type!=STOLA_INT) return stola_new_null();
  char *payload=ws_recv_frame_posix((int)handle->as.int_val);
  if (!payload) return stola_new_null();
  return stola_new_string_owned(payload);
}

StolaValue *stola_ws_close(StolaValue *handle) {
  if (!handle||handle->type!=STOLA_INT) return stola_new_null();
  unsigned char cf[2]={0x88,0x00};
  send((int)handle->as.int_val,(const char*)cf,2,0);
  close((int)handle->as.int_val);
  return stola_new_null();
}

StolaValue *stola_ws_server_create(StolaValue *port_val) {
  if (!port_val||port_val->type!=STOLA_INT) return stola_new_int(-1);
  int server=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
  if (server<0) return stola_new_int(-1);
  int opt=1; setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
  struct sockaddr_in addr; memset(&addr,0,sizeof(addr));
  addr.sin_family=AF_INET; addr.sin_addr.s_addr=INADDR_ANY;
  addr.sin_port=htons((unsigned short)port_val->as.int_val);
  if (bind(server,(struct sockaddr*)&addr,sizeof(addr))<0){close(server);return stola_new_int(-1);}
  if (listen(server,SOMAXCONN)<0){close(server);return stola_new_int(-1);}
  return stola_new_int((int64_t)server);
}

StolaValue *stola_ws_server_accept(StolaValue *server_val) {
  if (!server_val||server_val->type!=STOLA_INT) return stola_new_int(-1);
  int client=accept((int)server_val->as.int_val,NULL,NULL);
  if (client<0) return stola_new_int(-1);
  char buf[4096]={0}; recv(client,buf,sizeof(buf)-1,0);
  char *kh=strstr(buf,"Sec-WebSocket-Key:");
  if (!kh){close(client);return stola_new_int(-1);}
  kh+=18; while(*kh==' ')kh++;
  char key[64]={0}; int ki=0;
  while(*kh&&*kh!='\r'&&*kh!='\n'&&ki<63) key[ki++]=*kh++;
  char combined[256];
  snprintf(combined,sizeof(combined),"%s%s",key,"258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
  unsigned char sha1_out[20];
  ws_sha1((unsigned char*)combined,strlen(combined),sha1_out);
  char *accept_key=ws_base64_encode(sha1_out,20);
  char response[512];
  snprintf(response,sizeof(response),"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
    "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",accept_key);
  free(accept_key);
  send(client,response,strlen(response),0);
  return stola_new_int((int64_t)client);
}

StolaValue *stola_ws_server_close(StolaValue *server_val) {
  if (!server_val||server_val->type!=STOLA_INT) return stola_new_null();
  close((int)server_val->as.int_val);
  return stola_new_null();
}

// ── I/O Multiplexing ─────────────────────────────────────────────────────────
// stola_ws_select(handles: array<int>, timeout_ms: int) -> array<int>
// Returns the subset of socket handles that have data ready to read.
// timeout_ms < 0 → block; timeout_ms == 0 → non-blocking poll.
#include <sys/select.h>
StolaValue *stola_ws_select(StolaValue *handles, StolaValue *timeout_ms_val) {
  StolaValue *result = stola_new_array();
  if (!handles || handles->type != STOLA_ARRAY) return result;
  int64_t timeout_ms = (timeout_ms_val && timeout_ms_val->type == STOLA_INT)
                       ? timeout_ms_val->as.int_val : -1;
  fd_set rfds;
  FD_ZERO(&rfds);
  int nfds = 0;
  int count = (int)handles->as.array_val.count;
  for (int i = 0; i < count; i++) {
    StolaValue *h = handles->as.array_val.items[i];
    if (h && h->type == STOLA_INT) {
      int fd = (int)h->as.int_val;
      FD_SET(fd, &rfds);
      if (fd + 1 > nfds) nfds = fd + 1;
    }
  }
  struct timeval tv, *tvp = NULL;
  if (timeout_ms >= 0) {
    tv.tv_sec  = (long)(timeout_ms / 1000);
    tv.tv_usec = (long)((timeout_ms % 1000) * 1000);
    tvp = &tv;
  }
  int r = select(nfds, &rfds, NULL, NULL, tvp);
  if (r <= 0) return result;
  for (int i = 0; i < count; i++) {
    StolaValue *h = handles->as.array_val.items[i];
    if (h && h->type == STOLA_INT) {
      int fd = (int)h->as.int_val;
      if (FD_ISSET(fd, &rfds))
        stola_push(result, stola_new_int((int64_t)fd));
    }
  }
  return result;
}

#endif
