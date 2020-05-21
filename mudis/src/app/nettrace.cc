#include <iostream>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/poll.h>
#include <unistd.h>
#include <signal.h>
#include <sys/shm.h>
#include <netinet/tcp.h>

std::string remote_host;
unsigned short remote_port = -1;
unsigned short local_port = -1;

int level = 0;
int leave = false;
int threadcount = 1;

class ref_thread
{
  public:
	ref_thread(){
		__sync_add_and_fetch(&threadcount,1);
	}
	~ref_thread(){
		__sync_sub_and_fetch(&threadcount,1);
	}

};

void* thread_task(void* p)
{
	ref_thread holder;


	int client_fd = (int)(long long)p;

	{
		int val = 1;
		int disableval = 1;
		int interval = 20;
		int cnt = 3;
		setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
		setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &disableval, sizeof(disableval));
		setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPIDLE, &interval, sizeof(interval));
		setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
		setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
	}

	sockaddr_in client_addr = {0};
	socklen_t client_addr_len = sizeof(client_addr);
	getpeername(client_fd, (sockaddr*)&client_addr,&client_addr_len );


	hostent* h = gethostbyname(remote_host.c_str());
	if(h == NULL){
		printf("error:%s\n",strerror(errno));
		return NULL;
	}

	in_addr* inaddr = (in_addr*)h->h_addr;
	if(inaddr == NULL){
		printf("error:%s\n",strerror(errno));
		return NULL;
	}

	int server_fd = ::socket(AF_INET,SOCK_STREAM, IPPROTO_IP);
	if(server_fd < 0){
		printf("error:%s\n",strerror(errno));
		return NULL;
	}

	sockaddr_in server_addr = {0};
	server_addr.sin_addr.s_addr = inaddr->s_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(remote_port);




	if(0 == ::connect(server_fd,(const sockaddr*)&server_addr,sizeof(server_addr))){
		std::string server_saddr = inet_ntoa(server_addr.sin_addr);
		std::string client_saddr = inet_ntoa(client_addr.sin_addr);

		{
			char buff[128];
			int r;
			r = snprintf(buff,sizeof(buff),"%s:%d",server_saddr.c_str(),ntohs(server_addr.sin_port));
			server_saddr.assign(buff,r);

			r = snprintf(buff,sizeof(buff),"%s:%d",client_saddr.c_str(),ntohs(client_addr.sin_port));
			client_saddr.assign(buff,r);
		}

		pollfd pfd[2];
		pfd[0].fd = server_fd;
		pfd[0].events = POLLIN;
		pfd[0].revents = 0;
		pfd[1].fd = client_fd;
		pfd[1].events = POLLIN;
		pfd[1].revents = 0;


		while(true){
			int ret = ::poll(pfd,2,1000);
			if(ret > 0){
				if(pfd[1].revents&POLLIN){
					char buff[4096];
					int r = recv(pfd[1].fd,buff,4096,0);
					if(r > 0){
						if(level == 1) printf("\x1b[32m[send]sesion %s --> %s %u bytes\x1b[0m\n",client_saddr.c_str(),server_saddr.c_str(),r);
						if(level == 2) printf("\x1b[32m[send]sesion %s --> %s %u bytes\x1b[0m\n%.*s\n",client_saddr.c_str(),server_saddr.c_str(),r,r,buff);
						if(send(pfd[0].fd,buff,r,0) != r ) break;
					}else{
						break;
					}
				}else if(pfd[0].revents&POLLIN){
					char buff[4096];
					int r = recv(pfd[0].fd,buff,4096,0);
					if(r > 0){
						if(level == 1) printf("\x1b[33m[recv]sesion %s <-- %s %u bytes\x1b[0m\n",client_saddr.c_str(),server_saddr.c_str(),r);
						if(level == 2) printf("\x1b[33m[recv]sesion %s <-- %s %u bytes\x1b[0m\n%.*s\n",client_saddr.c_str(),server_saddr.c_str(),r,r,buff);
						if(send(pfd[1].fd,buff,r,0) != r ) break;
					}else{
						break;
					}
				}else{
					break;
				}
			}
		}
	}
	close(server_fd);
	close(client_fd);
	return NULL;
}

void useage(std::string selfname)
{
	std::cout << "useage:" << selfname << " [optional] <file>" << std::endl;
	std::cout << "version: 1.0.0" << std::endl;
	std::cout << "\t-h <host>\t" << "remote host" << std::endl;
	std::cout << "\t-p <port>\t" << "remote port" << std::endl;
	std::cout << "\t-l <port>\t" << "listen port" << std::endl;
	std::cout << "\t-e <level>\t" << "0,1,2" << std::endl;
	std::cout << "\t-d \t" << "daemon" << std::endl;
}

void mudis_sig_leave(int sig)
{
	leave = true;
}

int main(int argc,char* argv[])
{
	bool isdaemon = false;
	std::string selfname = argv[0];
	{
		int oc;
		while((oc = getopt(argc, argv, "s:h:p:l:e:d?")) != -1){
			switch(oc)
			{
			  case 'h':
				remote_host = optarg;
				break;
			  case 'p':
				remote_port = atoi(optarg);
				break;
			  case 'l':
				local_port = atoi(optarg);
				break;
			  case 'e':
				level = atoi(optarg);
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

	if(remote_host.empty() || remote_port == -1 || local_port == -1){
		useage(selfname);
		return 0;
	}

	if(isdaemon){
		::daemon(1,0);
		level = 0;
	}

	/* 己经正式决定要执行下去了。 */
	signal(SIGUSR1, mudis_sig_leave);
	signal(SIGPIPE, SIG_IGN);



	int listener = ::socket(AF_INET,SOCK_STREAM, IPPROTO_IP);
	sockaddr_in addr = {0};
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(local_port);
	int opt = 1;
	setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
	if(::bind(listener,(const sockaddr*)&addr,sizeof(addr))){
		::close(listener);
		printf("error:%s\n",strerror(errno));
		return 1;
	}


	pollfd pfd;
	pfd.fd = listener;
	pfd.events = POLLIN;
	listen(listener,5);

	while(threadcount > 0){
		if(leave){
			if(listener >= 0){
				__sync_sub_and_fetch(&threadcount,1);
				::close(listener);
				listener = -1;
			}
		}

		if(listener == -1){
			usleep(100000);
			continue;
		}

		int ret = ::poll(&pfd,1,100);
		if(ret > 0 && pfd.revents&POLLIN){
			sockaddr_in addr;
			socklen_t addr_size = sizeof(addr);
			int acceptfd = ::accept(listener,(sockaddr*)&addr,&addr_size);
			if(acceptfd >= 0){
				pthread_t thread;
				if(pthread_create(&thread,NULL,(void* (*)(void*))thread_task,(void*)acceptfd) == 0){
					pthread_detach(thread);
				}else{
					::close(acceptfd);
				}
			}
		}else if(ret < 0){
			usleep(100000);
		}
	}
	if(listener >= 0){
		::close(listener);
	}
	return 0;
}
