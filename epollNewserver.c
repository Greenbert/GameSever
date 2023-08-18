#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#define MAXEVENTS 100
#define PORT 8787

// 把socket设置为非阻塞的方式。
int setnonblocking(int sockfd);

// 初始化服务端的监听端口。
int initserver(int port);

int scfds[100] = {0};
int mines[2][100] = {0};
int minesIndex = 0;
int players[2][100] = {0};

void putfd(int fd);
void putMine(int x,int y);
void setplayer(int fd,int x,int y);
void handle_send(char *buffer,int fd);
void command_Handle(char *cmd,int fd);

int checkInfo(char *tmp)
{
	if (tmp[0] == '<' && tmp[strlen(tmp) - 1] == '>')
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

char lastbuffer[2048] = {0};
void recive_request(char *buf,int fd){
    char *tbuffer;
	tbuffer = buf;

		if (strlen(lastbuffer) != 0) //前面一次尾部的不完整命令与后一次buffer组合
		{
			char newbuffer[2048];
			strcat(lastbuffer, buf);
			memset(newbuffer, 0, sizeof(newbuffer));
			printf("%d\n",sizeof(newbuffer));
			memcpy(newbuffer, lastbuffer, sizeof(lastbuffer));
			memset(lastbuffer, 0, sizeof(lastbuffer));
			tbuffer = newbuffer;
		}

    char delims[] = ":\n";
	char *result = NULL;
	result = strtok(tbuffer, delims);
    while (result != NULL)
		{
            if (checkInfo(result) == 0)
			{
				// printf("%s---cut!-%d\n",tmp,strlen(result));
				strcat(lastbuffer, result);
				break;
			}
    
      int len = strlen(result);
      char dest[len-1];
      dest[len-2] = '\0';
      strncpy(dest, result+1, len-2);
      
			printf("scfd: %d, result is \"%s\"\n", fd,dest);
      command_Handle(dest,fd);
			result = strtok(NULL, delims);
			
		}
}

int main(int argc,char *argv[])
{

  // 初始化服务端用于监听的socket。
  int listensock = initserver(PORT);
  printf("listensock=%d\n",listensock);

  if (listensock < 0)
  {
    printf("initserver() failed.\n"); return -1;
  }

  int epollfd;

  char buffer[1024];
  memset(buffer,0,sizeof(buffer));

  // 创建一个描述符
  epollfd = epoll_create(1);

  // 添加监听描述符事件
  struct epoll_event ev;
  ev.data.fd = listensock;
  ev.events = EPOLLIN;
  epoll_ctl(epollfd,EPOLL_CTL_ADD,listensock,&ev);

  while (1)
  {
    struct epoll_event events[MAXEVENTS]; // 存放有事件发生的结构数组。

    // 等待监视的socket有事件发生。
    int infds = epoll_wait(epollfd,events,MAXEVENTS,-1);
    // printf("epoll_wait infds=%d\n",infds);

    // 返回失败。
    if (infds < 0)
    {
      printf("epoll_wait() failed.\n"); perror("epoll_wait()"); break;
    }

    // 超时。
    if (infds == 0)
    {
      printf("epoll_wait() timeout.\n"); continue;
    }

    int ii=0;
    // 遍历有事件发生的结构数组。
    for (ii;ii<infds;ii++)
    {
      if ((events[ii].data.fd == listensock) &&(events[ii].events & EPOLLIN))
      {
        // 如果发生事件的是listensock，表示有新的客户端连上来。
        struct sockaddr_in client;
        socklen_t len = sizeof(client);
        int clientsock = accept(listensock,(struct sockaddr*)&client,&len);
        if (clientsock < 0)
        {
          printf("accept() failed.\n"); continue;
        }

        // 把新的客户端添加到epoll中。
        memset(&ev,0,sizeof(struct epoll_event));
        ev.data.fd = clientsock;
        ev.events = EPOLLIN;
        epoll_ctl(epollfd,EPOLL_CTL_ADD,clientsock,&ev);

        printf ("client(socket=%d) connected ok.\n",clientsock);

        putfd(clientsock);
        char buffer[1024];
        int iret;
        memset(buffer,0,sizeof(buffer));
        sprintf(buffer,"<Accept %d>:\n",clientsock);
        if ( (iret=send(clientsock,buffer,strlen(buffer),0))<=0) // 发送报文。
                { perror("send"); }
        // handle_send(buffer,clientsock);

        continue;
      }
      else if (events[ii].events & EPOLLIN)
      {
        // 客户端有数据过来或客户端的socket连接被断开。
        char buffer[1024];
        memset(buffer,0,sizeof(buffer));

        // 读取客户端的数据。
        ssize_t isize=read(events[ii].data.fd,buffer,sizeof(buffer));

        // 发生了错误或socket被对方关闭。
        if (isize <=0)
        {
          printf("client(eventfd=%d) disconnected.\n",events[ii].data.fd);

          // 把已断开的客户端从epoll中删除。
          memset(&ev,0,sizeof(struct epoll_event));
          ev.events = EPOLLIN;
          ev.data.fd = events[ii].data.fd;
          epoll_ctl(epollfd,EPOLL_CTL_DEL,events[ii].data.fd,&ev);
          close(events[ii].data.fd);
          continue;
        }

        recive_request(buffer,events[ii].data.fd);
        // printf("recv(eventfd=%d,size=%d):%s\n",events[ii].data.fd,isize,buffer);

        // 把收到的报文发回给客户端。
        // write(events[ii].data.fd,buffer,strlen(buffer));
      }
    }
  }

  close(epollfd);

  return 0;
}

// 初始化服务端的监听端口。
int initserver(int port)
{
  int sock = socket(AF_INET,SOCK_STREAM,0);
  if (sock < 0)
  {
    printf("socket() failed.\n"); return -1;
  }

  // Linux如下
  int opt = 1; unsigned int len = sizeof(opt);
  setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&opt,len);
  setsockopt(sock,SOL_SOCKET,SO_KEEPALIVE,&opt,len);

  struct sockaddr_in servaddr;
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servaddr.sin_port = htons(port);

  if (bind(sock,(struct sockaddr *)&servaddr,sizeof(servaddr)) < 0 )
  {
    printf("bind() failed.\n"); close(sock); return -1;
  }

  if (listen(sock,5) != 0 )
  {
    printf("listen() failed.\n"); close(sock); return -1;
  }

  return sock;
}

// 把socket设置为非阻塞的方式。
int setnonblocking(int sockfd)
{  
  if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFD, 0)|O_NONBLOCK) == -1)  return -1;

  return 0;  
}

void putfd(int fd){
    int i = 0;
    for(i;i<10;i++){
        printf("%d\n",scfds[i]);
        if(scfds[i] == 0){
            break;
        }
    }
    scfds[i] = fd;
    printf("-%d\n",i);
}

void putMine(int x,int y){
    mines[0][minesIndex] = x;
    mines[1][minesIndex] = y;
    minesIndex++;
}

void removeMine(int index){
    mines[0][index] = 0;
    mines[1][index] = 0;
}

int checkMine(int x,int y){
    int i = 0;
    for(i;i<minesIndex;i++){
      if(mines[0][i] == x && mines[1][i] == y){
        return i;
      }
    }
    return -1;
}

void setplayer(int fd,int x,int y){
    players[0][fd] = x;
    players[1][fd] = y;
}

//群发
void handle_send(char *buffer,int fd){
    int i = 0;
    int iret;
    for(i;i<10;i++){
        if(scfds[i] == 0){
            break;
        }
        else if(scfds[i] != fd){
            printf("send %d: %s\n",scfds[i],buffer);
            if ( (iret=send(scfds[i],buffer,strlen(buffer),0))<=0) // 发送报文。
                { perror("send"); }
        }
    }
}

void command_Handle(char *cmd,int fd){
	char type[10];
  char buffer[1024];
  memset(buffer,0,sizeof(buffer));

	memset(type,0,sizeof(type));

	sscanf (cmd,"%s",type);
  // printf("%s\n",type);
	if (strcmp(type, "Move")==0 || strcmp(type, "Blank")==0){
    
		int x;
    int y;
    sscanf (cmd,"%s %d %d",type,&x,&y);

    if(strcmp(type, "Move")==0){
      int res = checkMine(x,y); //踩雷
      if(res != -1){
        printf("kill %d x,y: %d %d\n",fd,x,y);
        sprintf(buffer,"<Kill %d>:\n",fd);
        int iret;
        if ( (iret=send(fd,buffer,strlen(buffer),0))<=0) // 发送报文。
                { perror("send"); }
        removeMine(res);
      }else{
        sprintf(buffer,"<%s %d %d %d>:\n",type,fd,x,y);

        setplayer(fd,x,y);

        handle_send(buffer,fd);
      }
    }else{
      sprintf(buffer,"<%s %d %d %d>:\n",type,fd,x,y);

      handle_send(buffer,fd);
    }
		
	}else if (strcmp(type, "Put")==0){
    int x;
    int y;
    sscanf (cmd,"%s %d %d",type,&x,&y);
    // printf("put %d,%d\n",x,y);
    putMine(x,y);
  }else if(strcmp(type, "NpcM")==0 || strcmp(type, "NpcB")==0){
    int x;
    int y;
    char name[10];
    memset(name,0,sizeof(name));
    sscanf (cmd,"%s %s %d %d",type,name,&x,&y);
    sprintf(buffer,"<%s %s %d %d>:\n",type,name,x,y);
    handle_send(buffer,fd);
  }
}
