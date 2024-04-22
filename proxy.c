#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define N 4

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void p_doit(int fd);
void p_read_requesthdrs(rio_t *rp, char *hostname, char *port, char *request_header);
void p_parse_uri(char *uri, char *hostname, char *path, char *port);
void *thread(void *vargp);
int main(int argc, char **argv)
{
  int listenfd, *clientfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;
  // printf("%s", user_agent_hdr);
  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    clientfd = (int *)Malloc(sizeof(int));
    *clientfd = Accept(listenfd, (SA *)&clientaddr,
                       &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, clientfd);
  }
}

void *thread(void *vargp)
{
  int clientfd = *((int *)vargp);
  Pthread_detach((pthread_self()));
  Free(vargp);
  p_doit(clientfd);
  Close(clientfd);
  return NULL;
}

void p_doit(int clientfd)
{
  int serverfd;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], request_header[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  char ip[MAXLINE], port[MAXLINE], hostname[MAXLINE], path[MAXLINE], header[MAXLINE];
  rio_t rio;
  int len;
  /* Read request line and headers*/
  Rio_readinitb(&rio, clientfd);
  Rio_readlineb(&rio, request_header, MAXLINE);
  printf("1.Request headers:\n");
  printf("%s", request_header);
  sscanf(request_header, "%s %s", method, uri);
  // strcpy(version, "HTTP/1.0");
  p_parse_uri(uri, hostname, path, port);
  // sprintf(request_header, "%s %s %s\r\n", method, path, version);
  //  strcat(request_header, buf);

  // p_read_requesthdrs(&rio, hostname, port, request_header);
  sprintf(request_header, "%s /%s %s\r\n", method, path, "HTTP/1.0");
  sprintf(request_header, "%sConnection: close\r\n", request_header);
  sprintf(request_header, "%sProxy-Connection: close\r\n", request_header);
  sprintf(request_header, "%s%s\r\n", request_header, user_agent_hdr);

  // if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD"))
  // {
  //   clienterror(clientfd, method, "501", "Not Implemented", "Tiny does not implement this method");
  //   return;
  // }
  printf("\n\n\n%s %s %s\n\n\n", hostname, port, path);
  printf("%s", request_header);
  serverfd = Open_clientfd(hostname, port);
  Rio_writen(serverfd, request_header, strlen(request_header));
  // sprintf(request_header, "HTTP/1.0 200 OK\r\n");
  // sprintf(request_header, "%sServer: ProxyServer\r\n", buf);
  // sprintf(request_header, "%sConnection: close\r\n", buf);
  // sprintf(request_header, "%sContent-length: %d\r\n", buf, strlen(request_header));
  ssize_t n;

  n = Rio_readn(serverfd, buf, MAX_OBJECT_SIZE);
  Rio_writen(clientfd, buf, n);
  // printf("%d\n\n", len);
  Close(serverfd);
}

void p_parse_uri(char *uri, char *hostname, char *path, char *port)
{
  char *ptr = strstr(uri, "//");
  ptr = ptr != NULL ? ptr + 2 : uri; // "//" 다음의 위치로 이동

  // 호스트 이름 추출
  char *host_end = strchr(ptr, ':');
  if (host_end == NULL) // 포트 번호가 없는 경우
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

  // 경로 추출
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

void p_read_requesthdrs(rio_t *rp, char *hostname, char *port, char *request_header)
{
  char buf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE);

  while (strcmp(buf, "\r\n"))
  {
    // if (strstr(buf, "Host: ") == buf)
    // {
    //   printf("*********%s", buf);
    //   char *ptr = buf + strlen("Host: ");
    //   char *port_ptr = strchr(ptr, ':');
    //   if (port_ptr != NULL)
    //   {
    //     // ':'가 발견되었으면 ':'를 기준으로 문자열을 나누고 호스트네임과 포트에 저장
    //     *port_ptr = '\0'; // ':'를 NULL 문자로 변경하여 호스트네임을 종료
    //     strncpy(hostname, ptr, MAXLINE);

    //     strncpy(port, port_ptr + 1, MAXLINE);
    //     port[strcspn(port, "\r\n")] = '\0';
    //   }
    //   else
    //   {
    //     // ':'가 발견되지 않았으면 포트를 지정하지 않은 것으로 간주하고 호스트네임에 전체 문자열을 저장
    //     strncpy(hostname, ptr, MAXLINE);

    //     *port = '\0'; // 포트를 지정하지 않음을 나타내는 NULL 문자
    //     port_ptr[strcspn(port_ptr, "\r\n")] = '\0';
    //   }
    //   // printf("8-----------%s %s\n", hostname, port);
    // }

    if (strstr(buf, "User-Agent:") == buf)
    {
      // printf("9-----------%s\n", buf);
      char user_agent_buf[MAXLINE];
      snprintf(user_agent_buf, MAXLINE, user_agent_hdr);
      strcpy(request_header, user_agent_buf);
      // printf("9-----------%s\n", buf);
    }
    Rio_readlineb(rp, buf, MAXLINE);
    // printf("-----------%s\n", buf);

    strcat(request_header, buf);
    printf("-----------%s\n", request_header);
    // strcat(request_header, "\r\n");
  }
  return;
}
