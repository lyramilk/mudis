#include <iostream>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <unistd.h>
#include <signal.h>
#include <sys/shm.h>
#include <netinet/tcp.h>

std::string	remote_host;
unsigned short remote_port = -1;
unsigned short local_port =	-1;

int	level =	0;
int	leave =	false;
int	threadcount	= 1;
long long epoolsize	= 0;
int g_listenfd = -1;

in_addr	inaddr;

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
								if(level == 1) printf("\x1b[32m[send]sesion %s --> %s %u bytes\x1b[0m\n",client_saddr.c_str(),server_saddr.c_str(),rt);
								if(level == 2) printf("\x1b[32m[send]sesion %s --> %s %u bytes\x1b[0m\n%.*s\n",client_saddr.c_str(),server_saddr.c_str(),r,rt,buff);


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


void useage(std::string	selfname)
{
	std::cout << "useage:" << selfname << "	[optional] <file>" << std::endl;
	std::cout << "version: 1.0.0" << std::endl;
	std::cout << "\t-h <host>\t" <<	"remote	host" << std::endl;
	std::cout << "\t-p <port>\t" <<	"remote	port" << std::endl;
	std::cout << "\t-l <port>\t" <<	"listen	port" << std::endl;
	std::cout << "\t-e <level>\t" << "0,1,2" <<	std::endl;
	std::cout << "\t-d \t" << "daemon" << std::endl;
}

void mudis_sig_leave(int sig)
{
	leave =	true;
}

void* thread_task(void*	p)
{
		int	epfd = (int)(unsigned long long)p;
		while(true){
				svc(epfd);
		}
		return NULL;
}

void* thread_listen_task(void*	p)
{
		int	epfd = (int)(unsigned long long)p;
		while(true){

			sockaddr_in	addr;
			socklen_t addr_size	= sizeof(addr);

			int	acceptfd = ::accept(g_listenfd,(sockaddr*)&addr,&addr_size);
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
		}
		return NULL;
}

int	main(int argc,char*	argv[])
{
	bool isdaemon =	false;
	std::string	selfname = argv[0];
	{
		int	oc;
		while((oc =	getopt(argc, argv, "s:h:p:l:e:d?"))	!= -1){
			switch(oc)
			{
			  case 'h':
				remote_host	= optarg;
				break;
			  case 'p':
				remote_port	= atoi(optarg);
				break;
			  case 'l':
				local_port = atoi(optarg);
				break;
			  case 'e':
				level =	atoi(optarg);
				break;
			  case 'd':
				isdaemon = true;
				break;
			  case '?':
			  default:
				useage(selfname);
				return 0;
			}
		}
	}

	if(remote_host.empty() || remote_port == -1	|| local_port == -1){
		useage(selfname);
		return 0;
	}

	if(isdaemon){
		::daemon(1,0);
		level =	0;
	}

	/* 己经正式决定要执行下去了。 */
	signal(SIGUSR1,	mudis_sig_leave);
	signal(SIGPIPE,	SIG_IGN);



	hostent* h = gethostbyname(remote_host.c_str());
	if(h == NULL){
		printf("error:%s\n",strerror(errno));
		return 1;
	}

	in_addr* _inaddr = (in_addr*)h->h_addr;
	if(_inaddr == NULL){
		printf("error:%s\n",strerror(errno));
		return 1;
	}

	inaddr = *_inaddr;

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

	listen(listener,5000);
	g_listenfd = listener;

	int	epfd = epoll_create(100000);
/*
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
*/

/*
	while(threadcount >	0){
		if(leave){
			if(listener	>= 0){
				__sync_sub_and_fetch(&threadcount,1);
				::close(listener);
				listener = -1;
			}
		}

		if(listener	== -1){
			usleep(100000);
			continue;
		}

		int	ret	= ::poll(&pfd,1,100);
		if(ret > 0 && pfd.revents&POLLIN){
			sockaddr_in	addr;
			socklen_t addr_size	= sizeof(addr);
			int	acceptfd = ::accept(listener,(sockaddr*)&addr,&addr_size);
			if(acceptfd	>= 0){
				pthread_t thread;
				if(pthread_create(&thread,NULL,(void* (*)(void*))thread_task,(void*)acceptfd) == 0){
					pthread_detach(thread);
				}else{
					::close(acceptfd);
				}
				pthread_t thread_listen;
				if(pthread_create(&thread_listen,NULL,(void* (*)(void*))thread_listen_task,(void*)acceptfd) == 0){
					pthread_detach(thread_listen);
				}else{
					::close(acceptfd);
				}
			}
		}else if(ret < 0){
			usleep(100000);
		}
	}
	if(listener	>= 0){
		::close(listener);
	}*/


	pthread_t thread;
	if(pthread_create(&thread,NULL,(void* (*)(void*))thread_listen_task,(void*)epfd) == 0){
		pthread_detach(thread);
	}
	thread_task((void*)epfd);
	return 0;
}
