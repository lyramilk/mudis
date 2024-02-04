#include <string>
#include <unistd.h>
#include <sys/shm.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <iostream>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <netdb.h>

std::string	remote_host	= "192.168.220.93";
unsigned short remote_port = 8552;
unsigned short local_port =	8311;


const int thread_limit = 2;

bool leave = false;
long long epoolsize	= 0;
in_addr	inaddr;

void mudis_sig_leave(int sig)
{
		leave =	true;
}


union epsock{
		struct
		{
				int	a;
				int	b;
		}u;
		unsigned long long c;
};



void svc(int epfd)
{
		epoll_event	ees[3];
		int	ee_count = epoll_wait(epfd,	ees,3, 100);
		for(int	i=0;i<ee_count;++i){
				epoll_event	&ee	= ees[i];
				epsock info;
				info.c = ee.data.u64;
				if(info.u.b	== info.u.a){
						// 识别listener
						sockaddr_in	addr;
						socklen_t addr_size	= sizeof(addr);
						int	acceptfd = ::accept(info.u.a,(sockaddr*)&addr,&addr_size);
						if(acceptfd	>= 0){
								// 客户端连接
								int	server_fd =	::socket(AF_INET,SOCK_STREAM, IPPROTO_IP);
								if(server_fd < 0){
										close(acceptfd);
										continue;
								}

								sockaddr_in	server_addr	= {0};
								server_addr.sin_addr.s_addr	= inaddr.s_addr;
								server_addr.sin_family = AF_INET;
								server_addr.sin_port = htons(remote_port);

								if(-1 == ::connect(server_fd,(const	sockaddr*)&server_addr,sizeof(server_addr))){
										close(server_fd);
										close(acceptfd);
										continue;
								}

								epsock newsessioninfo1;
								newsessioninfo1.u.a	= acceptfd;
								newsessioninfo1.u.b	= server_fd;


								epoll_event	newee1;
								newee1.data.u64	= newsessioninfo1.c;
								newee1.events =	EPOLLIN	| EPOLLERR | EPOLLHUP |	EPOLLRDHUP | EPOLLONESHOT;

								epsock newsessioninfo2;
								newsessioninfo2.u.a	= server_fd;
								newsessioninfo2.u.b	= acceptfd;

								epoll_event	newee2;
								newee2.data.u64	= newsessioninfo2.c;
								newee2.events =	EPOLLIN	| EPOLLERR | EPOLLHUP |	EPOLLRDHUP | EPOLLONESHOT;

								if(epoll_ctl(epfd, EPOLL_CTL_ADD, acceptfd,	&newee1) ==	-1){
										close(server_fd);
										close(acceptfd);
										continue;
								}

								if(epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &newee2) == -1){
										epoll_ctl(epfd,	EPOLL_CTL_DEL, acceptfd, &newee1);
										close(server_fd);
										close(acceptfd);
										continue;
								}
								__sync_add_and_fetch(&epoolsize,2);
						}
				}else if(ee.events & EPOLLIN){
						char buff[4096];
						int	rt = recv(info.u.a,buff,sizeof(buff),MSG_PEEK);
						if(rt >	0){
								int	rt2	= send(info.u.b,buff,rt,0);
								if(rt2 < rt){
										if(rt2 < 1 && errno	!= EAGAIN){
												// 写出端关闭
												epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.b, NULL);
												epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.a, NULL);
												close(info.u.b);
												close(info.u.a);
												__sync_sub_and_fetch(&epoolsize,2);
												continue;
										}
										// 写出溢出处理
										if(rt2 > 0){
												recv(info.u.a,buff,rt2,0);
										}

										epoll_event	newee2;
										newee2.data.u64	= info.c;
										newee2.events =	EPOLLIN	| EPOLLOUT | EPOLLERR |	EPOLLHUP | EPOLLRDHUP |	EPOLLONESHOT;
										if(epoll_ctl(epfd, EPOLL_CTL_MOD, info.u.a,	&newee2) ==	-1){
												// 重置事件出错
												epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.b, NULL);
												epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.a, NULL);
												close(info.u.b);
												close(info.u.a);
												__sync_sub_and_fetch(&epoolsize,2);
												continue;
										}else{
												epoll_event	resetee;
												resetee.data.u64 = info.c;
												resetee.events = EPOLLIN | EPOLLERR	| EPOLLHUP | EPOLLRDHUP	| EPOLLONESHOT;

												if(epoll_ctl(epfd, EPOLL_CTL_MOD, info.u.a,	&resetee) == -1){
														// 重置事件
														epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.b, NULL);
														epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.a, NULL);
														close(info.u.b);
														close(info.u.a);
														__sync_sub_and_fetch(&epoolsize,2);
														continue;
												}
										}
								}else{
										// 写出正常。
										recv(info.u.a,buff,rt2,0);


										epoll_event	resetee;
										resetee.data.u64 = info.c;
										resetee.events = EPOLLIN | EPOLLERR	| EPOLLHUP | EPOLLRDHUP	| EPOLLONESHOT;

										if(epoll_ctl(epfd, EPOLL_CTL_MOD, info.u.a,	&resetee) == -1){
												// 重置事件
												epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.b, NULL);
												epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.a, NULL);
												close(info.u.b);
												close(info.u.a);
												__sync_sub_and_fetch(&epoolsize,2);
												continue;
										}
								}
						}else{
								// 读取出错
								epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.b, NULL);
								epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.a, NULL);
								close(info.u.b);
								close(info.u.a);
								__sync_sub_and_fetch(&epoolsize,2);
								continue;
						}
				}else if(ee.events & EPOLLOUT){
						char buff[4096];
						int	rt = recv(info.u.b,buff,sizeof(buff),MSG_PEEK);
						if(rt >	0){
								int	rt2	= send(info.u.a,buff,rt,0);
								if(rt2 < rt){
										if(rt2 < 1){
												// 告诉我可写却没写成功
												epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.b, NULL);
												epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.a, NULL);
												close(info.u.b);
												close(info.u.a);
												__sync_sub_and_fetch(&epoolsize,2);
												continue;
										}
										// 写出溢出处理
										recv(info.u.b,buff,rt2,0);

										// 没写完，继续等
										epoll_event	resetee;
										resetee.data.u64 = info.c;
										resetee.events = EPOLLIN | EPOLLOUT	| EPOLLERR | EPOLLHUP |	EPOLLRDHUP | EPOLLONESHOT;

										if(epoll_ctl(epfd, EPOLL_CTL_MOD, info.u.a,	&resetee) == -1){
												// 重置事件
												epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.b, NULL);
												epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.a, NULL);
												close(info.u.b);
												close(info.u.a);
												__sync_sub_and_fetch(&epoolsize,2);
												continue;
										}
								}else{
										// 读写正常，重置为读状态。
										recv(info.u.b,buff,rt2,0);

										epoll_event	resetee;
										resetee.data.u64 = info.c;
										resetee.events = EPOLLIN | EPOLLERR	| EPOLLHUP | EPOLLRDHUP	| EPOLLONESHOT;

										if(epoll_ctl(epfd, EPOLL_CTL_MOD, info.u.a,	&resetee) == -1){
												// 重置事件
												epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.b, NULL);
												epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.a, NULL);
												close(info.u.b);
												close(info.u.a);
												__sync_sub_and_fetch(&epoolsize,2);
												continue;
										}
								}
						}else{
								// 告诉我可写时却没有从另外一端读到数据（这是不应该发生的逻辑）
								epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.b, NULL);
								epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.a, NULL);
								close(info.u.b);
								close(info.u.a);
								__sync_sub_and_fetch(&epoolsize,2);
								continue;
						}
				}else{
						// 套接字出错
						epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.b, NULL);
						epoll_ctl(epfd,	EPOLL_CTL_DEL, info.u.a, NULL);
						close(info.u.b);
						close(info.u.a);
						__sync_sub_and_fetch(&epoolsize,2);
						continue;
				}
		}
}




void* thread_task(void*	p)
{
		int	epfd = (int)(unsigned long long)p;
		while(epoolsize	> 0){
				svc(epfd);
		}
		return NULL;
}

int	main(int argc,const	char* argv[])
{
		bool isdaemon =	false;
		std::string	operate	= "start";

		{
				int	oc;
				while((oc =	getopt(argc, (char**)argv, "c:t:dDp:s:k:?")) !=	-1){
						switch(oc)
						{
						  case 's':
								operate	= optarg;
								break;
						  case 'd':
								isdaemon = true;
								break;
						}
				}
		}

		int	shmid =	shmget(key_t(1554538485), sizeof(pid_t), 0666|IPC_CREAT);

		pid_t* shm = (pid_t*)shmat(shmid, 0, 0);
		if(shm == (pid_t*)-1){
				std::cerr << "获取共享内存失败"	<< std::endl;
		}

		pid_t pid =	*shm;
		/* reload功能	*/
		std::cout << "试图重新启动" << pid <<	std::endl;

		if(operate == "reload"){
				if(pid != 0){
						kill(pid,SIGUSR1);

						for(int	i=0;i<30;++i){
								if(*shm	!= 0){
										usleep(100000);
								}
						}


				}
		}else if(operate ==	"start"){
		}else if(operate ==	"trystart"){
		}


		if(isdaemon){
				::daemon(1,0);
		}

		/* 己经正式决定要执行下去了。 */
		signal(SIGUSR1,	mudis_sig_leave);
		signal(SIGPIPE,	SIG_IGN);

		int	listener = ::socket(AF_INET,SOCK_STREAM, IPPROTO_IP);
		sockaddr_in	addr = {0};
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_family	= AF_INET;
		addr.sin_port =	htons(local_port);
		int	opt	= 1;
		setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
		if(::bind(listener,(const sockaddr*)&addr,sizeof(addr))){
				::close(listener);
				printf("error:%s\n",strerror(errno));
				return 1;
		}
		listen(listener,5);


		int	epfd = epoll_create(100000);

		epsock listen_info;

		listen_info.u.a	= listener;
		listen_info.u.b	= listener;

		epoll_event	ee;
		ee.data.u64	= listen_info.c;
		ee.events =	EPOLLIN;

		if (epoll_ctl(epfd,	EPOLL_CTL_ADD, listener, &ee) == -1) {
				printf("error:%s\n",strerror(errno));
				return -1;
		}


		{
				hostent* h = gethostbyname(remote_host.c_str());
				if(h ==	NULL){
						printf("error:%s\n",strerror(errno));
						return -1;
				}

				in_addr* tmpinaddr = (in_addr*)h->h_addr;
				if(tmpinaddr ==	NULL){
						printf("error:%s\n",strerror(errno));
						return -1;
				}
				inaddr =*tmpinaddr;
		}


		__sync_add_and_fetch(&epoolsize,1);

		for(int	i=0;i<thread_limit;++i){
				pthread_t thread;
				if(pthread_create(&thread,NULL,(void* (*)(void*))thread_task,(void*)epfd) == 0){
						pthread_detach(thread);
				}
		}

		*shm = getpid();

		while(epoolsize	> 0){
				if(leave){
						if(listener	>= 0){
								epoll_ctl(epfd,	EPOLL_CTL_DEL, listener, &ee);
								__sync_sub_and_fetch(&epoolsize,1);
								::close(listener);
								listener = -1;
						}
				}
				usleep(100000);
		}
		if(listener	>= 0){
				::close(listener);
		}
		if(epfd	>= 0){
				::close(epfd);
		}
		*shm = 0;
		return 0;
}
