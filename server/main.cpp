#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include "gotoc.pb.h"

#define ERROR_ARGS 1
#define ERROR_PATH 2
#define ERROR_SOCKET 3
#define ERROR_SIGNAL 4

int child(int fd);
ssize_t onProcess(int fd, uint8_t *pdata, ssize_t size);
void onMsg(int fd, gotocpb::MsgBody *msg);
void onAdd(int fd, gotocpb::MsgBody *msg);

void sigchld(int signo) {
  while (waitpid(-1, NULL, WNOHANG) > 0) {
  }
}

int setupsigchld() {
  struct sigaction act;
  act.sa_handler = sigchld;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  act.sa_flags |= SA_RESTART;
  return sigaction(SIGCHLD, &act, NULL);
}

int unix_listen(const char *pathname) {
  sockaddr_un addr;
  if (strlen(pathname) >= sizeof(addr.sun_path)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  int listenfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listenfd < 0) {
    return -2;
  }
  unlink(pathname);
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, pathname);
  if (0 != bind(listenfd, (const sockaddr *)&addr,
                offsetof(sockaddr_un, sun_path) + strlen(addr.sun_path))) {
    int tmp = errno;
    close(listenfd);
    errno = tmp;
    return -3;
  }
  if (0 != listen(listenfd, 0)) {
    int tmp = errno;
    close(listenfd);
    errno = tmp;
    return -4;
  }
  return listenfd;
}

int net_listen(const char *host, int port) { return 0; }

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "%s path\n%s host port\n", argv[0], argv[0]);
    return ERROR_ARGS;
  }
  int isetup = setupsigchld();
  if (isetup != 0) {
    fprintf(stderr, "%s:%d %d %s\n", __FILE__, __LINE__, isetup,
            strerror(errno));
    return ERROR_SIGNAL;
  }
  int listenfd;
  if (argc < 3) {
    listenfd = unix_listen(argv[1]);
  } else {
    listenfd = net_listen(argv[1], atoi(argv[2]));
  }
  if (listenfd < 0) {
    fprintf(stderr, "%s:%d %d %s\n", __FILE__, __LINE__, listenfd,
            strerror(errno));
    return ERROR_SOCKET;
  }
  while (true) {
    int clientfd = accept(listenfd, NULL, NULL);
    if (clientfd < 0) {
      fprintf(stderr, "%s:%d %d %s\n", __FILE__, __LINE__, clientfd,
              strerror(errno));
      continue;
    }
    pid_t pid = fork();
    if (pid < 0) {
      fprintf(stderr, "%s:%d %ld %s\n", __FILE__, __LINE__, (long)pid,
              strerror(errno));
      close(clientfd);
      continue;
    }
    if (pid == 0) {
      close(listenfd);
      exit(child(clientfd));
    }
    close(clientfd);
  }
  return 0;
}

uint8_t readbuf[1024 * 1024 * 10];
uint8_t writebuf[1024 * 1024 * 10];

int child(int fd) {
  ssize_t readSize = 0;
  while (true) {
    ssize_t n = recv(fd, readbuf + readSize, sizeof(readbuf) - readSize, 0);
    if (n <= 0) {
      return 0;
    }
    readSize += n;
    ssize_t procTotal = 0;
    while (true) {
      ssize_t proc = onProcess(fd, readbuf + procTotal, readSize - procTotal);
      if (proc == 0) {
        break;
      }
      procTotal += proc;
    }
    if (procTotal > 0) {
      memcpy(readbuf, readbuf + procTotal, readSize - procTotal);
      readSize -= procTotal;
    }
  }
}

void sendmessage(int fd, gotocpb::CmdID cmd, google::protobuf::MessageLite *msg) {
  gotocpb::MsgBody body;
  body.set_id(cmd);
  if (msg != nullptr) {
    body.set_data(msg->SerializeAsString());
  }
  size_t total = body.ByteSizeLong() + 4;
  if (total > sizeof(writebuf)) {
    fprintf(stderr, "%s:%d %u\n", __FILE__, __LINE__, (unsigned)total);
    exit(0);
  }
  if (body.SerializeToArray(writebuf + 4, int(sizeof(writebuf) - 4)) == false) {
    fprintf(stderr, "%s:%d %d\n", __FILE__, __LINE__, (int)cmd);
    exit(0);
  }
  uint32_t *phead = (uint32_t *)writebuf;
  *phead = htonl(uint32_t(total));
  ssize_t iwrite = send(fd, writebuf, total, 0);
  if (iwrite < 0) {
    fprintf(stderr, "%s:%d %d %s\n", __FILE__,__LINE__,(int)iwrite, strerror(errno));
    exit(0);
  }
  if((size_t)iwrite != total){
    fprintf(stderr, "%s:%d %u\n", __FILE__,__LINE__,(unsigned int)iwrite);
    exit(0);
  }
}

ssize_t onProcess(int fd, uint8_t *pdata, ssize_t size) {
  if (size < 4) {
    return 0;
  }
  uint32_t *phead = (uint32_t *)pdata;
  uint32_t length = ntohl(*phead);
  if (length > uint32_t(size)) {
    if ((ssize_t)length > sizeof(readbuf)) {
      fprintf(stderr, "%s:%d %u\n", __FILE__, __LINE__, (unsigned int)length);
      exit(0);
    }
    return 0;
  }
  if (length <= 4) {
    fprintf(stderr, "%s:%d %u\n", __FILE__, __LINE__, (unsigned int)length);
    exit(0);
  }
  gotocpb::MsgBody msg;
  if (msg.ParseFromArray(pdata + 4, length - 4) == false) {
    fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
    exit(0);
  }
  onMsg(fd, &msg);
  return length;
}

void onMsg(int fd, gotocpb::MsgBody *msg) {
    switch (msg->id()) {
      case gotocpb::ADD_REQ:
      onAdd(fd, msg);
      break;
    default:
      fprintf(stderr, "%s:%d %d\n", __FILE__, __LINE__, int(msg->id()));
      exit(0);
  }
}

void onAdd(int fd, gotocpb::MsgBody *msg){
  gotocpb::AddReq req;
  if(!req.ParseFromString(msg->data())){
    fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
    exit(0);
  }
  int sum = req.num1()+ req.num2();
  gotocpb::AddRsp rsp;
  rsp.set_sum(sum);
  sendmessage(fd, gotocpb::ADD_RSP, &rsp);
}