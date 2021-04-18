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
    int listenfd, connfd; //������, ������� ���� 
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    //���ڷ� ���� port�� listenfd ����
    listenfd = Open_listenfd(argv[1]);
    while(1){
        clientlen = sizeof(clientaddr);
        //lisetenfd�� �����û�� client�� �ּҸ� sockaddr_stoage�� ������
        //client�� �ּ�, ũ�⸦ �޾Ƽ� ������ ���� �����͸� ���ڷ� ���� 
        //accept�� ����° ���ڴ� �ϴ� addr�� ũ�⸦ �����ϰ�(input) ������ �Ϸ�Ǹ� ������ addr�� ������ ������ client�� �ּ� ������ ũ�⸦ ������ 
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        //������ ���ڿ� flag �ȵ��� 0���� �ִ� ��
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
    //request line, header �б� 
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
        //���� �������� Ȯ���ϴ� ���ǹ� -> ������������, �б� ������ ���� �ִ��� Ȯ��
        //S_ISREG -> isregular - �Ϲ��������� üũ�ϴ� macro
        //st_mode�� ������ ���������� ���� bit& �������� ���θ� Ȯ�� ������ 
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

    //HTTP response body �����
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    //HTTP response ����ϱ�
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    //�� ����� \r\n\r\n �ι�? 
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}


//��û ����� �а� ����(Tiny������ ��û ��� ���� � ������ ������� ����) 
//������ �̹� ��û ������ ����
void read_requesthdrs(rio_t *rp)
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    //\r\n ���ö����� readlineb ����?
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
        strcpy(cgiargs, ""); //cgi���� �����
        strcpy(filename, "."); //�Ʒ� �ٰ� ���Ҿ� ��� ������ ����̸����� ��ȯ(./index.html�� ���� )-> �����ϱ�
        strcat(filename, uri);
        if (uri[strlen(uri)-1] == '/') //uri�� / ���ڷ� �����ٸ� �⺻ ���� �̸� �߰�
            strcat(filename, "home.html");
        return 1;
    }
    else { //dynamic content
        ptr = index(uri, '?');
        if (ptr) { //cgi ���� ����
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

    //client���� response header ������
    get_filetype(filename, filetype);
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    Rio_writen(fd, buf, strlen(buf));
    printf("Response headers:\n");
    printf("%s", buf);

    //O_RDONLY -> ������ �б� �������� ���� <-> O_WRONLY, �� ��ġ�� O_RDWR
    if (!strcasecmp(method, "GET")){
    srcfd = Open(filename, O_RDONLY, 0);
    srcp = (char *)malloc(filesize);
    Rio_readn(srcfd, srcp, filesize);
    close(srcfd);
    Rio_writen(fd, srcp, filesize);
    free(srcp);
    }

    //mmap�� ��û�� ������ ����޸� �������� ������
    // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); //srcfd�� ù ��° filesize ����Ʈ�� �ּ� srcp���� �����ϴ� ���� �б�-��� ����޸� �������� ������ 
    // Close(srcfd);
    // Rio_writen(fd, srcp, filesize); //������ Ŭ���̾�Ʈ���� ���� -> �ּ� srcp���� �����ϴ� filesize ����Ʈ�� Ŭ���̾�Ʈ�� ���� �ĺ��ڷ� ������.
    // Munmap(srcp, filesize);//���ε� ����޸� �ּҸ� ��ȯ
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
        Dup2(fd, STDOUT_FILENO); //redirect stdout to client -> ���μ����� �ε�Ǳ� ���� ǥ�� ����� Ŭ���̾�Ʈ�� ������ ����ĺ��ڷ� ��������
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