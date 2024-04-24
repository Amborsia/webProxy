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

/* request 헤더 읽기 함수 선언 */
void p_read_requesthdrs(rio_t *rp, char *hostname, char *port, char *request_header);

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
    char cache_obj[MAX_OBJECT_SIZE];
    char cache_url[MAXLINE];
    int LRU;        // least recently used (LRU) priority
    int isEmpty;    // Indicates if this block is empty

    int readCnt;    // count of readers
    sem_t wmutex;   // protects accesses to cache
    sem_t rdcntmutex; // protects accesses to readcnt
} cache_block;

/* 캐시 구조체 정의 */
typedef struct
{
    cache_block cacheobjs[CACHE_OBJS_COUNT]; // ten cache blocks
    int cache_num;                           // Cache number (unused)
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
    int clientfd = *((int *)vargp);
    Pthread_detach((pthread_self()));
    Free(vargp);
    p_doit(clientfd);
    Close(clientfd);
    return NULL;
}

/* 클라이언트 요청 처리 함수 */
void p_doit(int clientfd)
{
    int serverfd;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], request_header[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    char ip[MAXLINE], port[MAXLINE], hostname[MAXLINE], path[MAXLINE], header[MAXLINE];
    rio_t rio, server_rio;
    int len;

    /* 요청 라인 및 헤더 읽기 */
    Rio_readinitb(&rio, clientfd);
    Rio_readlineb(&rio, request_header, MAXLINE);
    printf("1. Request headers:\n");
    printf("%s", request_header);
    sscanf(request_header, "%s %s", method, uri);

    char url_store[MAXLINE];
    strcpy(url_store, uri); // 연결 소켓이 들고 있는 URI를 복사합니다.

    /* 캐시에서 URI 찾기 */
    int cache_index;
    if ((cache_index = cache_find(url_store)) != -1)
    {
        readerPre(cache_index); // 캐시 읽기 뮤텍스 락
        Rio_writen(clientfd, cache.cacheobjs[cache_index].cache_obj, strlen(cache.cacheobjs[cache_index].cache_obj));
        readerAfter(cache_index); // 캐시 읽기 뮤텍스 언락
        return;
    }

    p_parse_uri(uri, hostname, path, port);

    sprintf(request_header, "%s /%s %s\r\n", method, path, "HTTP/1.0");
    sprintf(request_header, "%sConnection: close\r\n", request_header);
    sprintf(request_header, "%sProxy-Connection: close\r\n", request_header);
    sprintf(request_header, "%s%s\r\n", request_header, user_agent_hdr);

    printf("\n%s %s %s\n", hostname, port, path);
    printf("%s", request_header);

    serverfd = Open_clientfd(hostname, port);
    Rio_readinitb(&server_rio, serverfd);
    Rio_writen(serverfd, request_header, strlen(request_header));

    char cachebuf[MAX_OBJECT_SIZE];
    int sizebuf = 0;
    size_t n;

    /* 서버 응답 읽고 클라이언트에 전송 */
    while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0)
    {
        sizebuf += n;
        if (sizebuf < MAX_OBJECT_SIZE)
            strcat(cachebuf, buf);
        Rio_writen(clientfd, buf, n);
    }

    Close(serverfd);

    /* 캐시에 저장 */
    if (sizebuf < MAX_OBJECT_SIZE)
    {
        cache_uri(url_store, cachebuf);
    }
}

/* URI 파싱 함수 */
void p_parse_uri(char *uri, char *hostname, char *path, char *port)
{
    char *ptr = strstr(uri, "//");
    ptr = ptr != NULL ? ptr + 2 : uri;

    char *host_end = strchr(ptr, ':');
    if (host_end == NULL)
    {
        host_end = strchr(ptr, '/');
    }

    int len = host_end - ptr;
    strncpy(hostname, ptr, len);
    hostname[len] = '\0';

    if (*host_end == ':')
    {
        char *port_start = host_end + 1;
        char *path_start = strchr(port_start, '/');
        if (path_start == NULL)
        {
            printf("Invalid URI\n");
            exit(1);
        }
        len = path_start - port_start;
        strncpy(port, port_start, len);
        port[len] = '\0';
    }

    char *path_start = strchr(ptr, '/');
    if (path_start != NULL)
    {
        strcpy(path, path_start);
    }
    else
    {
        strcpy(path, "/");
    }
}

/* 요청 헤더 읽기 함수 */
void p_read_requesthdrs(rio_t *rp, char *hostname, char *port, char *request_header)
{
    char buf[MAXLINE];
    Rio_readlineb(rp, buf, MAXLINE);

    while (strcmp(buf, "\r\n"))
    {
        if (strstr(buf, "User-Agent:") == buf)
        {
            char user_agent_buf[MAXLINE];
            snprintf(user_agent_buf, MAXLINE, user_agent_hdr);
            strcpy(request_header, user_agent_buf);
        }
        Rio_readlineb(rp, buf, MAXLINE);
        strcat(request_header, buf);
    }
    return;
}

/* 캐시 초기화 함수 */
void cache_init()
{
    cache.cache_num = 0;
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
    P(&cache.cacheobjs[i].rdcntmutex);
    cache.cacheobjs[i].readCnt++;
    if (cache.cacheobjs[i].readCnt == 1)
        P(&cache.cacheobjs[i].wmutex);
    V(&cache.cacheobjs[i].rdcntmutex);
}

/* Reader After 함수 */
void readerAfter(int i)
{
    P(&cache.cacheobjs[i].rdcntmutex);
    cache.cacheobjs[i].readCnt--;
    if (cache.cacheobjs[i].readCnt == 0)
        V(&cache.cacheobjs[i].wmutex);
    V(&cache.cacheobjs[i].rdcntmutex);
}

/* 캐시 찾기 함수 */
int cache_find(char *url)
{
    int i;
    for (i = 0; i < CACHE_OBJS_COUNT; i++)
    {
        readerPre(i);
        if (cache.cacheobjs[i].isEmpty == 0 && strcmp(url, cache.cacheobjs[i].cache_url) == 0)
        {
            readerAfter(i);
            return i;
        }
        readerAfter(i);
    }
    return -1;
}

/* 캐시 쫓아내기 함수 */
int cache_eviction()
{
    int min = LRU_MAGIC_NUMBER;
    int minindex = 0;
    int i;
    for (i = 0; i < CACHE_OBJS_COUNT; i++)
    {
        readerPre(i);
        if (cache.cacheobjs[i].isEmpty == 1)
        {
            minindex = i;
            readerAfter(i);
            break;
        }
        if (cache.cacheobjs[i].LRU < min)
        {
            minindex = i;
            min = cache.cacheobjs[i].LRU;
            readerAfter(i);
            continue;
        }
        readerAfter(i);
    }
    return minindex;
}

/* Writer Pre 함수 */
void writePre(int i)
{
    P(&cache.cacheobjs[i].wmutex);
}

/* Writer After 함수 */
void writeAfter(int i)
{
    V(&cache.cacheobjs[i].wmutex);
}

/* LRU 업데이트 함수 */
void cache_LRU(int index)
{
    int i;
    for (i = 0; i < CACHE_OBJS_COUNT; i++)
    {
        if (i == index)
        {
            continue;
        }
        writePre(i);
        if (cache.cacheobjs[i].isEmpty == 0)
        {
            cache.cacheobjs[i].LRU--;
        }
        writeAfter(i);
    }
}

/* URI 및 내용 캐싱 함수 */
void cache_uri(char *uri, char *buf)
{
    int i = cache_eviction();

    writePre(i);

    strcpy(cache.cacheobjs[i].cache_obj, buf);
    strcpy(cache.cacheobjs[i].cache_url, uri);
    cache.cacheobjs[i].isEmpty = 0;
    cache.cacheobjs[i].LRU = LRU_MAGIC_NUMBER;
    cache_LRU(i);

    writeAfter(i);
}
