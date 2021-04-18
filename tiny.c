#include "csapp.h"

// #include <stdio.h>
// #include <stdlib.h>
// #include <unistd.h>
// #include <strings.h>
// #include <sys/types.h>
// #include <sys/socket.h>
// #include <netdb.h>

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void echo(int connfd);

int main(int argc, char **argv)
{   
    int listenfd, connfd; //듣기소켓, 연결소켓 생성 
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    //인자로 받은 port에 listenfd 생성
    listenfd = Open_listenfd(argv[1]);
    while(1){
        clientlen = sizeof(clientaddr);
        //lisetenfd에 연결요청한 client의 주소를 sockaddr_stoage에 저장함
        //client의 주소, 크기를 받아서 저장할 곳의 포인터를 인자로 받음 
        //accept의 세번째 인자는 일단 addr의 크기를 설정하고(input) 접속이 완료되면 실제로 addr에 설정된 접속한 client의 주소 정보의 크기를 저장함 
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        //마지막 인자에 flag 안들어가면 0으로 넣는 듯
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        // echo(connfd);
        doit(connfd);
        Close(connfd);
    }
}

void doit(int fd)
{
    int is_static;
    int is_header = 0;
    struct stat sbuf;

    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;
    //request line, header 읽기 
    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE);
    printf("Request headers:\n");
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);

    
    if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")){
        clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
        return;
    }

    read_requesthdrs(&rio);

    //parse URI from GET request
    is_static = parse_uri(uri, filename, cgiargs);
    if (stat(filename, &sbuf) < 0) {
        clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
        return;
    }

    if (is_static) {
        //실행 가능한지 확인하는 조건문 -> 보통파일인지, 읽기 권한을 갖고 있는지 확인
        //S_ISREG -> isregular - 일반파일인지 체크하는 macro
        //st_mode는 파일의 유형값으로 직접 bit& 연산으로 여부를 확인 가능함 
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
            clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
            return;
        }
        serve_static(fd, filename, sbuf.st_size, method);
    }
    else {
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
            clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
            return;
        }
        serve_dynamic(fd, filename, cgiargs, method);
    }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    //HTTP response body 만들기
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    //HTTP response 출력하기
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    //왜 여기는 \r\n\r\n 두번? 
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}


//요청 헤더를 읽고 무시(Tiny에서는 요청 헤더 내의 어떤 정보도 사용하지 않음) 
//위에서 이미 요청 라인은 읽음
void read_requesthdrs(rio_t *rp)
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    //\r\n 나올때까지 readlineb 실행?
    while(strcmp(buf, "\r\n")) {
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}

int parse_uri(char *uri, char *filename, char *cgiargs)
{
    char *ptr;

    if (!strstr(uri, "cgi-bin")) { //static content
        strcpy(cgiargs, ""); //cgi인자 지우기
        strcpy(filename, "."); //아래 줄과 더불어 상대 리눅스 경로이름으로 변환(./index.html과 같은 )-> 이해하기
        strcat(filename, uri);
        if (uri[strlen(uri)-1] == '/') //uri가 / 문자로 끝난다면 기본 파일 이름 추가
            strcat(filename, "home.html");
        return 1;
    }
    else { //dynamic content
        ptr = index(uri, '?');
        if (ptr) { //cgi 인자 추출
            strcpy(cgiargs, ptr+1);
            *ptr = '\0';
        }
        else {
            strcpy(cgiargs, "");
        }
        strcpy(filename, ".");
        strcat(filename, uri);
        return 0;
    }
}

void serve_static(int fd, char *filename, int filesize, char *method)
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    //client에게 response header 보내기
    get_filetype(filename, filetype);
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    Rio_writen(fd, buf, strlen(buf));
    printf("Response headers:\n");
    printf("%s", buf);

    //O_RDONLY -> 파일을 읽기 전용으로 열기 <-> O_WRONLY, 둘 합치면 O_RDWR
    if (!strcasecmp(method, "GET")){
    srcfd = Open(filename, O_RDONLY, 0);
    srcp = (char *)malloc(filesize);
    Rio_readn(srcfd, srcp, filesize);
    close(srcfd);
    Rio_writen(fd, srcp, filesize);
    free(srcp);
    }

    //mmap는 요청한 파일을 가상메모리 영역으로 매핑함
    // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); //srcfd의 첫 번째 filesize 바이트를 주소 srcp에서 시작하는 사적 읽기-허용 가상메모리 영역으로 매핑함 
    // Close(srcfd);
    // Rio_writen(fd, srcp, filesize); //파일을 클라이언트에게 전송 -> 주소 srcp에서 시작하는 filesize 바이트를 클라이언트의 연결 식별자로 복사함.
    // Munmap(srcp, filesize);//매핑된 가상메모리 주소를 반환
}

void get_filetype(char *filename, char *filetype)
{
    if (strstr(filename, ".html"))
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
        strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpeg");
    else if (strstr(filename, ".mp4"))
        strcpy(filetype, "video/mp4");
    else if (strstr(filename, ".mpeg"))
        strcpy(filetype, "video/mpeg");
    else
        strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method)
{
    char buf[MAXLINE], *emptylist[] = { NULL };

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));

    if (Fork() == 0) { 
        // Real server would set all CGI vars here
        setenv("QUERY_STRING", cgiargs, 1);
        setenv("METHOD", method, 1);
        Dup2(fd, STDOUT_FILENO); //redirect stdout to client -> 프로세스가 로드되기 전에 표준 출력을 클라이언트와 연관된 연결식별자로 재지정함
        Execve(filename, emptylist, environ); // Run CGI program
    }
    Wait(NULL); // Parent waits for and reaps child
}

void echo(int connfd)
{   
    rio_t rio;
    size_t n;
    char buf[MAXLINE];
    Rio_readinitb(&rio, connfd);
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
        printf("server received %d bytes\n", (int)n);
        // Rio_writen(connfd, "Hi\n", 3);
    }
}