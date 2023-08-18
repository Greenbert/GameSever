#include <curses.h>
#include <pthread.h>
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
 
#define MAXSIZE     1024
#define IPADDRESS   "127.0.0.1"
#define SERV_PORT   8787
#define FDSIZE        1024
#define EPOLLEVENTS 20

#define PCSTR "@"
#define BLANK " "
#define MINE "*"

int scfd;
int myserFd;

void *startClient();
void start();
void showRoles(char *name,int x,int y);
void command_Handle(char *cmd);
void *listensock(void *fd);

void handle_sendNpcMove(int x,int y,char *name);
void handle_sendNpcBlank(int x,int y,char *name);

void handle_room(int sockfd,int type);
void handle_sendMove(int sockfd,int x,int y);
void handle_sendBlank(int sockfd,int x,int y);
void handle_sendMine(int sockfd,int x,int y);
static void handle_connection(int sockfd);
static void
handle_events(int epollfd,struct epoll_event *events,int num,int sockfd,char *buf);
static void do_read(int epollfd,int fd,int sockfd,char *buf);
static void do_read(int epollfd,int fd,int sockfd,char *buf);
static void do_write(int epollfd,int fd,int sockfd,char *buf);
static void add_event(int epollfd,int fd,int state);
static void delete_event(int epollfd,int fd,int state);
static void modify_event(int epollfd,int fd,int state);

struct character{
	int isalive;
	int x;
	int y;
};

struct mine{
	char belong[32];
	int isalive;
	int x;
	int y;
};

struct character pc;

// struct mine mineListPtr[];

void addColor(int x,int y,int colorFront,int colorBack,char *str,int pair){
	initscr();
    start_color();

    init_pair(pair, colorFront, colorBack);


    attron(COLOR_PAIR(pair));
    mvaddstr(x,y,str);
    attroff(COLOR_PAIR(pair));
}

void exitfun(){
	char input;
	move(LINES/2, COLS/3);
	addstr("GAME OVER! Press Q to quit");
	refresh();

	while(input=getchar()){
		if(input=='Q') break;
	}

	endwin();
}


void addMine(int x,int y){
	struct mine mn;

	mn.x = x;
	mn.y = y;
	mn.isalive = 1;

	addColor(mn.x+1,mn.y,COLOR_RED,COLOR_BLACK,MINE,2);	

}

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
			// printf("%d\n",sizeof(newbuffer));
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
			command_Handle(dest);
			// printf("scfd: %d, result is \"%s\"\n", fd,result);
			result = strtok(NULL, delims);
			
		}
}

void *npcfun(void *npcstr){
	struct character npc;
	npc.x=*((char *)npcstr) % (LINES-1);
	npc.y=*((char *)npcstr) % (COLS-1);
	npc.isalive=1;

	move(npc.x, npc.y);
	addstr( (char *)npcstr );	
	move(LINES-1, COLS-1);
	refresh();

	while(pc.isalive && npc.isalive){
		move(npc.x, npc.y);
		addstr( BLANK );

		handle_sendNpcBlank(npc.x,npc.y,(char *)npcstr);
			

		if (pc.x>npc.x) {npc.x++;}			
		if (pc.x<npc.x) {npc.x--;}			
		if (pc.y>npc.y) {npc.y++;}			
		if (pc.y<npc.y) {npc.y--;}			

		move(npc.x, npc.y);
		addstr( (char *)npcstr );

		handle_sendNpcMove(npc.x,npc.y,(char *)npcstr);

		move(LINES-1, COLS-1);
		refresh();

		if(pc.x==npc.x && pc.y==npc.y){
			pc.isalive=0;
			exitfun();
		}	

		usleep(*((char *)npcstr)*3000);
	}	
}

void createGameHall(){
	char input;

	initscr();
	noecho();
	crmode();
	clear();

	char buffer[64];
  
    int iret;
    memset(buffer,0,sizeof(buffer));
	sprintf(buffer,"socketfd: %d (Landlord)\n",scfd);

	move(LINES+1, COLS+1);
	addstr( buffer );	
	
	move(LINES-1, COLS-30);
	addstr( "Press 1 to start" );
	refresh();

	while(input=getchar()){
		if(input == '1'){
			clear();
			refresh();
			start();
		}
		//when user input
		if(input == 'Q'){
			if(has_colors() == FALSE)
			{ 
				endwin();
				printf("You terminal does not support color\n");
				exit(1);
			}
    		
			break;
		} 
	}
	endwin();
}

//开始游戏
void start(){
	char input;
	pthread_t t;

	initscr();
	noecho();
	crmode();
	clear();

	pc.x=LINES/2;
	pc.y=COLS/2;
	pc.isalive=1;
	move(pc.x, pc.y);
	addstr( PCSTR );
	addColor(pc.x,pc.y,COLOR_YELLOW,COLOR_MAGENTA,PCSTR,1);	
	move(LINES-1, COLS-1);

	handle_sendMove(scfd,pc.x,pc.y);
	refresh();


	pthread_create(&t, NULL, npcfun, "A"); 	
	pthread_create(&t, NULL, npcfun, "M"); 	
	// pthread_create(&t, NULL, npcfun, "T"); 	
	// pthread_create(&t, NULL, npcfun, "Z");
	// pthread_create(&t, NULL, npcfun, "U"); 	

	while(input=getchar()){
		//when user input
		if(!pc.isalive) break;
 
		move(pc.x, pc.y);
		addstr( BLANK );
		handle_sendBlank(scfd,pc.x,pc.y);	

		if (input == 'w' && pc.x>0)      pc.x--;
		else if (input == 'a' && pc.y>0) pc.y--;
		else if (input == 's' && pc.x<LINES-1) pc.x++;
		else if (input == 'd' && pc.y<COLS-1) pc.y++;
		else if (input == ' '){
			handle_sendMine(scfd,pc.x+1,pc.y);
			addMine(pc.x,pc.y);
			// addstr(MINE);
		}
		handle_sendMove(scfd,pc.x,pc.y);

		move(pc.x, pc.y);
		addstr( PCSTR );
		addColor(pc.x,pc.y,COLOR_YELLOW,COLOR_MAGENTA,PCSTR,1);	
		move(LINES-1, COLS-1);
		refresh();
	}

	exitfun();
}

main(){
	char input;
	pthread_t t;
	pthread_create(&t, NULL, startClient,NULL); 	

	initscr();
	noecho();
	crmode();
	clear();

	pc.x=LINES/2;
	pc.y=COLS/2;
	pc.isalive=1;
	move(LINES/2-1, COLS/2-5);
	addstr( "Don't Step on the Mines!" );	
	move(LINES/2, COLS/2-1);
	addstr( "1,Start game" );
	// move(LINES/2+1, COLS/2-1);
	// addstr( "2,Join the room" );
	move(LINES/2+2, COLS/2-1);
	addstr( "Press Q to quit" );
	refresh();

	// pthread_create(&t, NULL, npcfun, "A"); 	
	// pthread_create(&t, NULL, npcfun, "M"); 	
	// pthread_create(&t, NULL, npcfun, "m"); 	
	// pthread_create(&t, NULL, npcfun, "Z"); 	

	while(input=getchar()){
		if(input == '1'){
			clear();
			refresh();
			start();
			// createGameHall();
		}
		//when user input
		if(input == 'Q'){
			if(has_colors() == FALSE)
			{ 
				endwin();
				printf("You terminal does not support color\n");
				exit(1);
			}
    		
			break;
		} 
	}
	endwin();

}

//客户端epoll发送接收
void handle_sendMine(int sockfd,int x,int y){
	char buffer[1024];
  // 与服务端通信，发送一个报文后等待回复，然后再发下一个报文。
  
    int iret;
    memset(buffer,0,sizeof(buffer));
	sprintf(buffer,"<Put %d %d>:\n",x,y);
    if ( (iret=send(sockfd,buffer,strlen(buffer),0))<=0) // 向服务端发送请求报文。
    { perror("send"); }
}

void handle_sendMove(int sockfd,int x,int y){
	char buffer[1024];
    // 与服务端通信，发送一个报文后等待回复，然后再发下一个报文。
  
    int iret;
    memset(buffer,0,sizeof(buffer));
	sprintf(buffer,"<Move %d %d>:\n",x,y);
    if ( (iret=send(sockfd,buffer,strlen(buffer),0))<=0) // 向服务端发送请求报文。
    { perror("send"); }
}

void handle_sendBlank(int sockfd,int x,int y){
	char buffer[1024];
  // 与服务端通信，发送一个报文后等待回复，然后再发下一个报文。
  
    int iret;
    memset(buffer,0,sizeof(buffer));
	sprintf(buffer,"<Blank %d %d>:\n",x,y);
    if ( (iret=send(sockfd,buffer,strlen(buffer),0))<=0) // 向服务端发送请求报文。
    { perror("send"); }
}

void handle_sendNpcMove(int x,int y,char *name){
	char buffer[1024];
    // 与服务端通信，发送一个报文后等待回复，然后再发下一个报文。
  
    int iret;
    memset(buffer,0,sizeof(buffer));
	sprintf(buffer,"<NpcM %s%d %d %d>:\n",name,myserFd,x,y);
    if ( (iret=send(scfd,buffer,strlen(buffer),0))<=0) // 向服务端发送请求报文。
    { perror("send"); }
}

void handle_sendNpcBlank(int x,int y,char *name){
	char buffer[1024];
  // 与服务端通信，发送一个报文后等待回复，然后再发下一个报文。
  
    int iret;
    memset(buffer,0,sizeof(buffer));
	sprintf(buffer,"<NpcB %s%d %d %d>:\n",name,myserFd,x,y);
    if ( (iret=send(scfd,buffer,strlen(buffer),0))<=0) // 向服务端发送请求报文。
    { perror("send"); }
}

void handle_room(int sockfd,int type){
	char buffer[1024];
 
    int iret;
    memset(buffer,0,sizeof(buffer));
	if(type == 0){
		//创建房间
		sprintf(buffer,"<RoomC %d>:\n",sockfd);
	}else if(type == 1){
		//加入房间
		sprintf(buffer,"<RoomI %d>:\n",sockfd);
	}else if(type == -1){
		//退出房间
		sprintf(buffer,"<RoomO %d>:\n",sockfd);
	}
    if ( (iret=send(sockfd,buffer,strlen(buffer),0))<=0) // 向服务端发送请求报文。
    { perror("send"); }
}

void *startClient()
{
    int                 sockfd;
    struct sockaddr_in  servaddr;
    sockfd = socket(AF_INET,SOCK_STREAM,0);
    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERV_PORT);
    inet_pton(AF_INET,IPADDRESS,&servaddr.sin_addr);
    connect(sockfd,(struct sockaddr*)&servaddr,sizeof(servaddr));
    //处理连接
    handle_connection(sockfd);
    close(sockfd);
    return 0;
}
 
 
static void handle_connection(int sockfd)
{
    int epollfd;
    struct epoll_event events[EPOLLEVENTS];
    char buf[MAXSIZE];
    int ret;
    epollfd = epoll_create(FDSIZE);
    add_event(epollfd,STDIN_FILENO,EPOLLIN);
    scfd = sockfd;

	int *tmpbuf;
	tmpbuf=malloc(sizeof(int));
	*tmpbuf=sockfd;

	pthread_t t;
	pthread_create(&t, NULL, listensock,tmpbuf);
	

    for ( ; ; )
    {
        // printf("in\n");
        // handle_send(sockfd);
        ret = epoll_wait(epollfd,events,EPOLLEVENTS,-1);
        //  printf("out\n");
        handle_events(epollfd,events,ret,sockfd,buf);
    }
    close(epollfd);
}
 
static void
handle_events(int epollfd,struct epoll_event *events,int num,int sockfd,char *buf)
{
    int fd;
    int i;
    for (i = 0;i < num;i++)
    {
        fd = events[i].data.fd;
        if (events[i].events & EPOLLIN)
            do_read(epollfd,fd,sockfd,buf);
        else if (events[i].events & EPOLLOUT)
            do_write(epollfd,fd,sockfd,buf);
    }
}
 
static void do_read(int epollfd,int fd,int sockfd,char *buf)
{
    int nread;
    nread = read(fd,buf,MAXSIZE);
    if (nread == -1)
    {
        perror("read error:");
        close(fd);
    }
    else if (nread == 0)
    {
        fprintf(stderr,"server close.\n");
        close(fd);
    }
    else
    {
        if (fd == STDIN_FILENO){
			//打印读取消息
			recive_request(buf,fd);
            add_event(epollfd,sockfd,EPOLLOUT);
		}	
        else
        {
            delete_event(epollfd,sockfd,EPOLLIN);
            add_event(epollfd,STDOUT_FILENO,EPOLLOUT);
        }
    }
}
 
static void do_write(int epollfd,int fd,int sockfd,char *buf)
{
    int nwrite;
   
    nwrite = write(fd,buf,strlen(buf));
    if (nwrite == -1)
    {
        perror("write error:");
        close(fd);
    }
    else
    {
        if (fd == STDOUT_FILENO)
            delete_event(epollfd,fd,EPOLLOUT);
        else
            modify_event(epollfd,fd,EPOLLIN);
    }
    memset(buf,0,MAXSIZE);
}
 
static void add_event(int epollfd,int fd,int state)
{
    struct epoll_event ev;
    ev.events = state;
    ev.data.fd = fd;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&ev);
}
 
static void delete_event(int epollfd,int fd,int state)
{
    struct epoll_event ev;
    ev.events = state;
    ev.data.fd = fd;
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,&ev);
}
 
static void modify_event(int epollfd,int fd,int state)
{
    struct epoll_event ev;
    ev.events = state;
    ev.data.fd = fd;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&ev);
}

void command_Handle(char *cmd){
	char type[10];

	memset(type,0,sizeof(type));

	sscanf (cmd,"%s",type);
	// printf("%s\n",type);
	if (strcmp(type, "Move")==0){
		// showRoles("kkk",10,11);
		char name[10];
		char tmp[10];
		memset(name,0,sizeof(name));
		int fd;
		int x;
		int y;
		sscanf (cmd,"%s %d %d %d",tmp,&fd,&x,&y);
		// printf("%d\n",fd);

		sprintf(name,"@%d",fd);
		showRoles(name,x,y);
		// showRoles(name,10,11);
	}else if (strcmp(type, "Blank")==0){
		// showRoles("kkk",10,11);
		char name[10];
		char tmp[10];
		memset(name,0,sizeof(name));
		int fd;
		int x;
		int y;
		sscanf (cmd,"%s %d %d %d",tmp,&fd,&x,&y);
		// printf("%s\n",type);
		char tname[10];
		memset(tname,0,sizeof(tname));
		sprintf(tname,"@%d",fd);
		char tblank[10];
		memset(tblank,0,sizeof(tblank));
		int i = 0;
		for(i;i<strlen(tname);i++){
			tblank[i] = ' ';
		}

		// sprintf(name,tblank,fd);
		showRoles(tblank,x,y);
	}else if(strcmp(type, "Kill")==0){
		char name[10];
		int fd;
		memset(name,0,sizeof(name));
		
		sscanf (cmd,"%s %d",name,&fd);
		if(myserFd == fd){
			pc.isalive=0;
			exitfun();
		}
	}else if(strcmp(type, "Accept")==0){
		char name[10];
		int fd;
		memset(name,0,sizeof(name));
		
		sscanf (cmd,"%s %d",name,&fd);
		
		myserFd = fd;
		// showRoles(name,2,2);
	}else if(strcmp(type, "NpcM")==0){
		// showRoles("kkk",10,11);
		// NpcM A5 x y
		char name[10];
		char tmp[10];
		memset(name,0,sizeof(name));
		int fd;
		int x;
		int y;
		sscanf (cmd,"%s %s %d %d",tmp,name,&x,&y);
		// printf("%d\n",fd);
		showRoles(name,x,y);
	}else if(strcmp(type, "NpcB")==0){
		// showRoles("kkk",10,11);
		char name[10];
		char tmp[10];
		memset(name,0,sizeof(name));
		int fd;
		int x;
		int y;
		sscanf (cmd,"%s %s %d %d",tmp,name,&x,&y);
		// printf("%s\n",type);

		char tblank[21];
		memset(tblank,0,sizeof(tblank));
		int i = 0;
		for(i;i<strlen(name);i++){
			tblank[i] = ' ';
		}

		// sprintf(name,"  ",fd);
		showRoles(tblank,x,y);
	}
}

void showRoles(char *name,int x,int y){
	move(x, y);
	addstr( name );
	move(LINES-1, COLS-1);
	refresh();
}

void coverRoles(int x,int y){
	move(x, y);
	addstr( "  " );
	refresh();
}

void *listensock(void *fd){
	int sockfd = *((int *)fd);
	char buf[1024];
	for (;;)
  {
		
    memset(buf,0,sizeof(buf));
    if (read(sockfd,buf,sizeof(buf)) <=0) 
    { 
      printf("read() failed.\n");  close(sockfd);  break;
    }
	
	recive_request(buf,sockfd);
    // printf("recv:%s\n",buf);
  }
}






