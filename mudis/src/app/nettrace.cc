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

std::string remote_host = "192.168.220.93";
unsigned short remote_port = 8552;

int level = 0;

void* thread_task(void* p)
{
	int client_fd = (int)(long long)p;

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

int main(int argc,char* argv[])
{
	unsigned short port = 8311;

	std::string selfname = argv[0];
	{
		int oc;
		while((oc = getopt(argc, argv, "h:p:l:e:d?")) != -1){
			switch(oc)
			{
			  case 'h':
				remote_host = optarg;
				break;
			  case 'p':
				remote_port = atoi(optarg);
				break;
			  case 'l':
				port = atoi(optarg);
				break;
			  case 'e':
				level = atoi(optarg);
				break;
			  case 'd':
				daemon(1,0);
				break;
			  case '?':
			  default:
				useage(selfname);
				return 0;
			}
		}
	}
	printf("[process] 0.0.0.0:%d    ---->    %s:%d	with log level %d\n",port,remote_host.c_str(),remote_port,level);

	signal(SIGPIPE, SIG_IGN);

	int listener = ::socket(AF_INET,SOCK_STREAM, IPPROTO_IP);
	sockaddr_in addr = {0};
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	int opt = 1;
	setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
	if(::bind(listener,(const sockaddr*)&addr,sizeof(addr))){
		::close(listener);
		printf("error:%s\n",strerror(errno));
		return 1;
	}
	listen(listener,5);

	while(true){
		sockaddr_in addr;
		socklen_t addr_size = sizeof(addr);
		int acceptfd = ::accept(listener,(sockaddr*)&addr,&addr_size);
		if(acceptfd >= 0){
			pthread_t thread;
			if(pthread_create(&thread,NULL,(void* (*)(void*))thread_task,(void*)acceptfd) == 0){
				pthread_detach(thread);
			}
		}
	}
	return 0;
}
