#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define LRU_MAGIC_NUMBER 9999
#define N 4
#define CACHE_OBJS_COUNT 10

/* User-Agent 헤더 */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

/* doit 함수 선언 */
void p_doit(int fd);

/* URI 파싱 함수 선언 */
void p_parse_uri(char *uri, char *hostname, char *path, char *port);

/* 쓰레드 함수 선언 */
void *thread(void *vargp);

/* 캐시 초기화 함수 */
void cache_init();

/* URL에 대한 캐시 찾기 함수 */
int cache_find(char *url);

/* URI를 캐시에 저장하는 함수 */
void cache_uri(char *uri, char *buf);

/* Reader Pre 함수 선언 */
void readerPre(int i);

/* Reader After 함수 선언 */
void readerAfter(int i);

/* 캐시 블록 구조체 정의 */
typedef struct
{
  char cache_obj[MAX_OBJECT_SIZE]; // 캐시된 객체
  char cache_url[MAXLINE];         // 캐시된 URL
  int LRU;                         // LRU 우선순위
  int isEmpty;                     // 캐시 블록이 비어 있는지 여부

  int readCnt;      // 리더 카운트
  sem_t wmutex;     // 캐시 접근 보호 뮤텍스
  sem_t rdcntmutex; // readCnt 접근 보호 뮤텍스
} cache_block;

/* 캐시 구조체 정의 */
typedef struct
{
  cache_block cacheobjs[CACHE_OBJS_COUNT]; // 캐시 블록 배열
  int cache_num;                           // 캐시 번호 (사용되지 않음)
} Cache;

/* 전역 캐시 변수 선언 */
Cache cache;

/* 메인 함수 */
int main(int argc, char **argv)
{
  int listenfd, *clientfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  /* 캐시 초기화 */
  cache_init();

  /* 명령행 인자 확인 */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  /* SIGPIPE 시그널 무시 */
  Signal(SIGPIPE, SIG_IGN);

  /* 포트로 대기 소켓 생성 */
  listenfd = Open_listenfd(argv[1]);

  /* 클라이언트 연결 대기 */
  while (1)
  {
    clientlen = sizeof(clientaddr);
    clientfd = (int *)Malloc(sizeof(int));
    *clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, clientfd);
  }
}

/* 쓰레드 함수 */
void *thread(void *vargp)
{
  // 클라이언트 소켓 파일 디스크립터를 int 형식으로 형변환하여 가져옵니다.
  int clientfd = *((int *)vargp);
  // 쓰레드 분리를 위해 pthread_detach 함수를 호출합니다.
  Pthread_detach((pthread_self()));
  // 클라이언트 소켓 파일 디스크립터를 동적으로 할당된 메모리에서 해제합니다.
  Free(vargp);
  // p_doit 함수를 호출하여 클라이언트와의 통신을 수행합니다.
  p_doit(clientfd);
  // 클라이언트 소켓을 닫습니다.
  Close(clientfd);
  // 스레드 함수 종료를 나타내는 NULL 포인터를 반환합니다.
  return NULL;
}

/* 클라이언트 요청 처리 함수 */
/* 클라이언트 요청 처리 함수 */
void p_doit(int clientfd)
{
  // 서버 소켓 파일 디스크립터를 선언합니다.
  int serverfd;
  // 버퍼와 문자열 변수를 선언합니다.
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], request_header[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  char ip[MAXLINE], port[MAXLINE], hostname[MAXLINE], path[MAXLINE], header[MAXLINE];
  // rio_t 구조체 변수를 선언합니다.
  rio_t rio, server_rio;
  // 변수를 초기화합니다.
  int len;

  /* 요청 라인 및 헤더 읽기 */
  // 클라이언트 소켓을 읽기 위해 초기화합니다.
  Rio_readinitb(&rio, clientfd);
  // 요청 헤더를 읽고 저장합니다.
  Rio_readlineb(&rio, request_header, MAXLINE);
  // 요청 헤더를 출력합니다.
  printf("1. Request headers:\n");
  printf("%s", request_header);
  // 요청 메서드와 URI를 추출합니다.
  sscanf(request_header, "%s %s", method, uri);

  // 캐시에 저장될 URI를 복사합니다.
  char url_store[MAXLINE];
  strcpy(url_store, uri);

  /* 캐시에서 URI 찾기 */
  int cache_index;
  if ((cache_index = cache_find(url_store)) != -1)
  {
    // 캐시를 찾았을 경우, 읽기 뮤텍스를 락합니다.
    readerPre(cache_index);
    // 클라이언트에 캐시된 객체를 쓰기 위해 캐시의 내용을 전송합니다.
    Rio_writen(clientfd, cache.cacheobjs[cache_index].cache_obj, strlen(cache.cacheobjs[cache_index].cache_obj));
    // 읽기 뮤텍스를 언락합니다.
    readerAfter(cache_index);
    return;
  }

  // URI를 파싱하여 호스트명, 경로, 포트를 추출합니다.
  p_parse_uri(uri, hostname, path, port);

  // 요청 헤더를 구성합니다.
  sprintf(request_header, "%s /%s %s\r\n", method, path, "HTTP/1.0");
  sprintf(request_header, "%sConnection: close\r\n", request_header);
  sprintf(request_header, "%sProxy-Connection: close\r\n", request_header);
  sprintf(request_header, "%s%s\r\n", request_header, user_agent_hdr);

  printf("\n%s %s %s\n", hostname, port, path);
  printf("%s", request_header);

  // 서버에 연결합니다.
  serverfd = Open_clientfd(hostname, port);
  // 서버 소켓을 읽기 위해 초기화합니다.
  Rio_readinitb(&server_rio, serverfd);
  // 요청 헤더를 서버에 전송합니다.
  Rio_writen(serverfd, request_header, strlen(request_header));

  // 캐시에 저장될 버퍼와 객체 크기를 초기화합니다.
  char cachebuf[MAX_OBJECT_SIZE];
  int sizebuf = 0;
  size_t n;

  /* 서버 응답 읽고 클라이언트에 전송 */
  while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0)
  {
    sizebuf += n;
    // 캐시 버퍼에 서버 응답을 복사합니다.
    if (sizebuf < MAX_OBJECT_SIZE)
      strcat(cachebuf, buf);
    // 클라이언트에 서버 응답을 전송합니다.
    Rio_writen(clientfd, buf, n);
  }

  // 서버 소켓을 닫습니다.
  Close(serverfd);

  /* 캐시에 저장 */
  if (sizebuf < MAX_OBJECT_SIZE)
  {
    // 캐시에 URI와 응답을 저장합니다.
    cache_uri(url_store, cachebuf);
  }
}

/* URI 파싱 함수 */
void p_parse_uri(char *uri, char *hostname, char *path, char *port)
{
  // URI에서 "//"을 찾아 포인터를 설정합니다.
  char *ptr = strstr(uri, "//");
  // 만약 "//"을 찾았다면 ptr을 이동시킵니다.
  ptr = ptr != NULL ? ptr + 2 : uri;

  // 호스트의 끝을 찾습니다.
  char *host_end = strchr(ptr, ':');
  // 만약 ":"이 없다면 경로의 끝을 찾습니다.
  if (host_end == NULL)
  {
    host_end = strchr(ptr, '/');
  }

  // 호스트명의 길이를 계산하고 호스트명을 복사합니다.
  int len = host_end - ptr;
  strncpy(hostname, ptr, len);
  hostname[len] = '\0';

  // 만약 ":"이 있다면 포트를 추출합니다.
  if (*host_end == ':')
  {
    char *port_start = host_end + 1;
    char *path_start = strchr(port_start, '/');
    // 만약 "/"이 없다면 잘못된 URI입니다.
    if (path_start == NULL)
    {
      printf("Invalid URI\n");
      exit(1);
    }
    // 포트의 길이를 계산하고 포트를 복사합니다.
    len = path_start - port_start;
    strncpy(port, port_start, len);
    port[len] = '\0';
  }

  // 경로의 시작을 찾습니다.
  char *path_start = strchr(ptr, '/');
  // 만약 경로가 존재한다면 경로를 복사합니다.
  if (path_start != NULL)
  {
    strcpy(path, path_start);
  }
  else
  {
    // 경로가 없다면 "/"를 복사합니다.
    strcpy(path, "/");
  }
}

/* 캐시 초기화 함수 */
void cache_init()
{
  // 캐시 번호를 초기화합니다.
  cache.cache_num = 0;
  // 캐시 객체들을 초기화합니다.
  int i;
  for (i = 0; i < CACHE_OBJS_COUNT; i++)
  {
    cache.cacheobjs[i].LRU = 0;
    cache.cacheobjs[i].isEmpty = 1;
    Sem_init(&cache.cacheobjs[i].wmutex, 0, 1);
    Sem_init(&cache.cacheobjs[i].rdcntmutex, 0, 1);
    cache.cacheobjs[i].readCnt = 0;
  }
}

/* Reader Pre 함수 */
void readerPre(int i)
{
  // 읽기 카운트 뮤텍스를 잠금합니다.
  P(&cache.cacheobjs[i].rdcntmutex);
  // 읽기 카운트를 증가시킵니다.
  cache.cacheobjs[i].readCnt++;
  // 첫 번째 리더라면 쓰기 뮤텍스를 잠급니다.
  if (cache.cacheobjs[i].readCnt == 1)
    P(&cache.cacheobjs[i].wmutex);
  // 읽기 카운트 뮤텍스를 잠금해제합니다.
  V(&cache.cacheobjs[i].rdcntmutex);
}

/* Reader After 함수 */
void readerAfter(int i)
{
  // 읽기 카운트 뮤텍스를 잠금합니다.
  P(&cache.cacheobjs[i].rdcntmutex);
  // 읽기 카운트를 감소시킵니다.
  cache.cacheobjs[i].readCnt--;
  // 모든 리더가 빠져나갔다면 쓰기 뮤텍스를 잠금해제합니다.
  if (cache.cacheobjs[i].readCnt == 0)
    V(&cache.cacheobjs[i].wmutex);
  // 읽기 카운트 뮤텍스를 잠금해제합니다.
  V(&cache.cacheobjs[i].rdcntmutex);
}

/* 캐시 찾기 함수 */
int cache_find(char *url)
{
  // 캐시 객체들을 반복하며 주어진 URL과 일치하는 캐시를 찾습니다.
  int i;
  for (i = 0; i < CACHE_OBJS_COUNT; i++)
  {
    // 리더 프리 함수를 호출하여 읽기 뮤텍스를 잠급니다.
    readerPre(i);
    // 캐시가 비어있지 않고 URL이 일치하면 해당 인덱스를 반환합니다.
    if (cache.cacheobjs[i].isEmpty == 0 && strcmp(url, cache.cacheobjs[i].cache_url) == 0)
    {
      // 리더 애프터 함수를 호출하여 읽기 뮤텍스를 잠금해제합니다.
      readerAfter(i);
      return i;
    }
    // 리더 애프터 함수를 호출하여 읽기 뮤텍스를 잠금해제합니다.
    readerAfter(i);
  }
  // 일치하는 캐시가 없으면 -1을 반환합니다.
  return -1;
}

/* 캐시 쫓아내기 함수 */
int cache_eviction()
{
  // LRU 알고리즘에 따라 캐시를 쫓아냅니다.
  int min = LRU_MAGIC_NUMBER;
  int minindex = 0;
  int i;
  for (i = 0; i < CACHE_OBJS_COUNT; i++)
  {
    // 리더 프리 함수를 호출하여 읽기 뮤텍스를 잠급니다.
    readerPre(i);
    // 캐시가 비어있는 경우에는 해당 인덱스를 반환합니다.
    if (cache.cacheobjs[i].isEmpty == 1)
    {
      minindex = i;
      // 리더 애프터 함수를 호출하여 읽기 뮤텍스를 잠금해제합니다.
      readerAfter(i);
      break;
    }
    // LRU를 갱신하면서 최소 값을 찾습니다.
    if (cache.cacheobjs[i].LRU < min)
    {
      minindex = i;
      min = cache.cacheobjs[i].LRU;
      // 리더 애프터 함수를 호출하여 읽기 뮤텍스를 잠금해제합니다.
      readerAfter(i);
      continue;
    }
    // 리더 애프터 함수를 호출하여 읽기 뮤텍스를 잠금해제합니다.
    readerAfter(i);
  }
  // LRU를 적용하여 쫓아낼 캐시의 인덱스를 반환합니다.
  return minindex;
}

/* Writer Pre 함수 */
void writePre(int i)
{
  // 쓰기 뮤텍스를 잠급니다.
  P(&cache.cacheobjs[i].wmutex);
}

/* Writer After 함수 */
void writeAfter(int i)
{
  // 쓰기 뮤텍스를 잠금해제합니다.
  V(&cache.cacheobjs[i].wmutex);
}

/* LRU 업데이트 함수 */
void cache_LRU(int index)
{
  // LRU 값을 업데이트합니다.
  int i;
  for (i = 0; i < CACHE_OBJS_COUNT; i++)
  {
    // 현재 인덱스를 제외한 모든 캐시의 LRU 값을 감소시킵니다.
    if (i == index)
    {
      continue;
    }
    // 쓰기 프리 함수를 호출하여 쓰기 뮤텍스를 잠급니다.
    writePre(i);
    // 캐시가 비어있지 않다면 LRU 값을 감소시킵니다.
    if (cache.cacheobjs[i].isEmpty == 0)
    {
      cache.cacheobjs[i].LRU--;
    }
    // 쓰기 애프터 함수를 호출하여 쓰기 뮤텍스를 잠금해제합니다.
    writeAfter(i);
  }
}

/* URI 및 내용 캐싱 함수 */
void cache_uri(char *uri, char *buf)
{
  // 캐시 쫓아내기 함수를 호출하여 쫓아낼 캐시의 인덱스를 찾습니다.
  int i = cache_eviction();

  // 쓰기 프리 함수를 호출하여 쓰기 뮤텍스를 잠급니다.
  writePre(i);

  // 캐시에 URI와 응답을 저장합니다.
  strcpy(cache.cacheobjs[i].cache_obj, buf);
  strcpy(cache.cacheobjs[i].cache_url, uri);
  cache.cacheobjs[i].isEmpty = 0;
  cache.cacheobjs[i].LRU = LRU_MAGIC_NUMBER;
  // LRU 업데이트 함수를 호출하여 LRU 값을 갱신합니다.
  cache_LRU(i);

  // 쓰기 애프터 함수를 호출하여 쓰기 뮤텍스를 잠금해제합니다.
  writeAfter(i);
}