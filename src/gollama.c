// C64 GrooveGoat - a local language model (Ollama) as the arranger, see gollama.h
//
// A minimal HTTP/1.1 client (no TLS: only for the local Ollama server).
// The request runs in a thread so the program keeps playing and drawing.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <SDL.h>

#ifdef __WIN32__
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET SOCK;
#define BADSOCK INVALID_SOCKET
#define closesock closesocket
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
typedef int SOCK;
#define BADSOCK (-1)
#define closesock close
#endif

#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include "gollama.h"
#include "gsg.h"
#include "goattrk2.h"

// ollama.log in the configuration folder: what happened on every request
// (for help when the arranger does not work)
static void olog(const char *fmt, ...)
{
  static int first = 1;
  char path[MAX_PATHNAME];
  FILE *f;
  va_list ap;
  configpath(path, "ollama.log");
  f = fopen(path, first ? "wt" : "at");
  if (!f) return;
  first = 0;
  {
    time_t t = time(NULL);
    char ts[32];
    strftime(ts, sizeof ts, "%H:%M:%S", localtime(&t));
    fprintf(f, "%s ", ts);
  }
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fprintf(f, "\n");
  fclose(f);
}

static int lasterr(void)
{
#ifdef __WIN32__
  return WSAGetLastError();
#else
  return errno;
#endif
}

static int istimeout(int e)
{
#ifdef __WIN32__
  return e == WSAETIMEDOUT;
#else
  return (e == EAGAIN) || (e == EWOULDBLOCK) || (e == EINTR);
#endif
}

char ollamahost[64] = "127.0.0.1:11434";

static void netinit(void)
{
#ifdef __WIN32__
  static int done = 0;
  if (!done)
  {
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    done = 1;
  }
#endif
}

static volatile int cancelled = 0;

static SOCK connectto(const char *hostport, int timeout_ms, char *err, int errsize)
{
  char host[64], port[8] = "11434";
  const char *colon = strrchr(hostport, ':');
  struct addrinfo hints, *res = NULL, *ai;
  SOCK s = BADSOCK;
  netinit();
  snprintf(host, sizeof host, "%.*s", colon ? (int)(colon - hostport) : (int)strlen(hostport), hostport);
  if (colon) snprintf(port, sizeof port, "%s", colon + 1);
  if (!strcmp(host, "localhost")) snprintf(host, sizeof host, "127.0.0.1");
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host, port, &hints, &res) || (!res))
  {
    snprintf(err, errsize, "Unknown host %s", host);
    return BADSOCK;
  }
  for (ai = res; ai; ai = ai->ai_next)
  {
    s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s == BADSOCK) continue;
    {
#ifdef __WIN32__
      DWORD tv = timeout_ms;
      setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
      setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof tv);
#else
      struct timeval tv;
      tv.tv_sec = timeout_ms / 1000;
      tv.tv_usec = (timeout_ms % 1000) * 1000;
      setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
      setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#endif
    }
    if (connect(s, ai->ai_addr, (int)ai->ai_addrlen) == 0) break;
    closesock(s);
    s = BADSOCK;
  }
  freeaddrinfo(res);
  if (s == BADSOCK)
  {
    snprintf(err, errsize, "Ollama is not running on %s", hostport);
    olog("connect %s:%s failed (error %d)", host, port, lasterr());
  }
  return s;
}

// one request; the body of the answer (malloc) or NULL. maxwait: seconds
static char *http1(const char *method, const char *path, const char *body, int maxwait, char *err, int errsize, int *retry)
{
  SOCK s = connectto(ollamahost, 2000, err, errsize);
  char *req, *resp = NULL, *b;
  int blen = body ? (int)strlen(body) : 0, rlen = 0, cap = 0, waited = 0, sent = 0, reqlen, rerr = 0;
  *retry = 0;
  if (s == BADSOCK) return NULL;
  req = (char *)malloc(blen + 512);
  reqlen = sprintf(req, "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: GrooveGoat\r\nAccept: application/json\r\n"
                   "Content-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", method, path,
                   ollamahost, blen);
  if (blen) memcpy(req + reqlen, body, blen);
  reqlen += blen;
  while (sent < reqlen)
  {
    int n = send(s, req + sent, reqlen - sent, 0);
    if (n <= 0)
    {
      if ((n < 0) && (istimeout(lasterr())) && (++waited < 10)) continue;
      break;
    }
    sent += n;
  }
  free(req);
  waited = 0;
  if (sent < reqlen)
  {
    snprintf(err, errsize, "Could not send the request to Ollama (error %d)", lasterr());
    olog("%s %s: sent %d of %d bytes (error %d)", method, path, sent, reqlen, lasterr());
    closesock(s);
    *retry = 1;
    return NULL;
  }
  for (;;)
  {
    int n;
    if (cancelled) break;
    if (cap - rlen < 4096)
    {
      cap = cap ? cap * 2 : 16384;
      resp = (char *)realloc(resp, cap + 1);
    }
    n = recv(s, resp + rlen, cap - rlen, 0);
    if (n > 0) { rlen += n; continue; }
    if (n == 0) break;
    rerr = lasterr();
    // timeout of one second: wait on (a big model needs its time)
    if (!istimeout(rerr)) break;
    rerr = 0;
    if (++waited > maxwait) break;
  }
  closesock(s);
  if (cancelled)
  {
    snprintf(err, errsize, "Cancelled");
    free(resp);
    return NULL;
  }
  if (resp) resp[rlen] = 0;
  olog("%s %s: %d bytes in %d s%s%.*s", method, path, rlen, waited, rlen ? ": " : "", rlen > 300 ? 300 : rlen,
       rlen ? resp : "");
  if (!rlen)
  {
    free(resp);
    if (rerr) olog("  receive error %d", rerr);
    if (waited > maxwait) snprintf(err, errsize, "Ollama did not answer within %d s", maxwait);
    else
    {
      snprintf(err, errsize, "Ollama closed the connection without an answer%s - see ollama.log",
               rerr ? " (connection error)" : "");
      *retry = 1;
    }
    return NULL;
  }
  b = strstr(resp, "\r\n\r\n");
  if ((strncmp(resp, "HTTP/1.", 7)) || (!b))
  {
    snprintf(err, errsize, "Something on %s answers, but it is not Ollama - see ollama.log", ollamahost);
    free(resp);
    return NULL;
  }
  {
    int status = atoi(resp + 9);
    int chunked = 0;
    char *h;
    *b = 0;
    for (h = resp; *h; h++) *h = (char)tolower((unsigned char)*h);
    chunked = strstr(resp, "transfer-encoding: chunked") != NULL;
    b += 4;
    if (chunked)
    {
      // join the chunks
      char *o = b, *q = b;
      for (;;)
      {
        long n = strtol(q, &h, 16);
        if ((h == q) || (n <= 0)) break;
        q = strstr(h, "\r\n");
        if (!q) break;
        q += 2;
        memmove(o, q, n);
        o += n;
        q += n + 2;
      }
      *o = 0;
    }
    if (status != 200)
    {
      JV *j = jv_parse(b);
      snprintf(err, errsize, "Ollama: %s (HTTP %d)", jv_str(jv_get(j, "error"), "error"), status);
      jv_free(j);
      free(resp);
      return NULL;
    }
    memmove(resp, b, strlen(b) + 1);
  }
  return resp;
}

// one request; the body of the answer (malloc) or NULL. maxwait: seconds.
// A connection closed without an answer is tried once more.
static char *http(const char *method, const char *path, const char *body, int maxwait, char *err, int errsize)
{
  int retry = 0;
  char *r = http1(method, path, body, maxwait, err, errsize, &retry);
  if ((!r) && (retry) && (!cancelled))
  {
    olog("  trying again");
    SDL_Delay(500);
    r = http1(method, path, body, maxwait, err, errsize, &retry);
  }
  return r;
}

int ollama_models(char names[][64], int max, char *err, int errsize)
{
  char *a;
  JV *j, *m;
  int k, n = 0;
  cancelled = 0;
  a = http("GET", "/api/tags", NULL, 3, err, errsize);
  if (!a) return -1;
  j = jv_parse(a);
  free(a);
  m = jv_get(j, "models");
  for (k = 0; (k < jv_len(m)) && (n < max); k++)
  {
    const char *nm = jv_str(jv_get(jv_at(m, k), "name"), "");
    if (nm[0]) snprintf(names[n++], 64, "%s", nm);
  }
  jv_free(j);
  return n;
}

// ---------------------------------------------------------------------------
// The request in the background
// ---------------------------------------------------------------------------

static SDL_Thread *th = NULL;
static SDL_mutex *mx = NULL;
static volatile int state = 2;       // 0 busy, 1 done, -1 error, 2 idle
static char *answer = NULL, *body = NULL;
static char aerr[200];
static Uint32 started = 0;

static int jsonesc(char *out, const char *s)
{
  int o = 0;
  for (; *s; s++)
  {
    unsigned char ch = *s;
    if ((ch == '"') || (ch == '\\')) { if (out) { out[o] = '\\'; out[o + 1] = ch; } o += 2; }
    else if (ch == '\n') { if (out) { out[o] = '\\'; out[o + 1] = 'n'; } o += 2; }
    else if (ch < 32) { if (out) out[o] = ' '; o++; }
    else { if (out) out[o] = ch; o++; }
  }
  return o;
}

static int worker(void *arg)
{
  char err[200] = "";
  char *a = http("POST", "/api/generate", body, 600, err, sizeof err);
  char *text = NULL;
  (void)arg;
  if (a)
  {
    JV *j = jv_parse(a);
    const char *r = jv_str(jv_get(j, "response"), NULL);
    if (r) text = strdup(r);
    else snprintf(err, sizeof err, "Ollama gave no answer");
    olog("  answer text: %d characters", r ? (int)strlen(r) : -1);
    jv_free(j);
    free(a);
  }
  SDL_LockMutex(mx);
  free(answer);
  answer = text;
  snprintf(aerr, sizeof aerr, "%s", err);
  state = text ? 1 : -1;
  SDL_UnlockMutex(mx);
  return 0;
}

int ollama_start(const char *model, const char *prompt)
{
  int n;
  if (!mx) mx = SDL_CreateMutex();
  if (state == 0) return 0;
  if (th)
  {
    SDL_WaitThread(th, NULL);
    th = NULL;
  }
  free(body);
  n = jsonesc(NULL, prompt) + jsonesc(NULL, model) + 256;
  body = (char *)malloc(n);
  {
    int o = sprintf(body, "{\"model\": \"");
    o += jsonesc(body + o, model);
    o += sprintf(body + o, "\", \"prompt\": \"");
    o += jsonesc(body + o, prompt);
    sprintf(body + o, "\", \"stream\": false, \"format\": \"json\", \"options\": {\"temperature\": 0.8}}");
  }
  cancelled = 0;
  state = 0;
  started = SDL_GetTicks();
  th = SDL_CreateThread(worker, "ollama", NULL);
  if (!th)
  {
    state = 2;
    return 0;
  }
  return 1;
}

int ollama_poll(char **ans, char *err, int errsize)
{
  int st;
  if (!mx) return 2;
  SDL_LockMutex(mx);
  st = state;
  if (st == 1)
  {
    *ans = answer;
    answer = NULL;
    state = 2;
  }
  else if (st == -1)
  {
    snprintf(err, errsize, "%s", aerr);
    state = 2;
  }
  SDL_UnlockMutex(mx);
  if (((st == 1) || (st == -1)) && (th))
  {
    SDL_WaitThread(th, NULL);
    th = NULL;
  }
  return st;
}

void ollama_cancel(void)
{
  cancelled = 1;
}

unsigned ollama_seconds(void)
{
  return (SDL_GetTicks() - started) / 1000;
}
