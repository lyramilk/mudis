#include "netproxy.h"
#include <libmilk/dict.h>
#include <libmilk/ansi_3_64.h>
#include <libmilk/log.h>

#include <sys/epoll.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <cassert>
#include <netdb.h>
#include <sys/ioctl.h>


namespace lyramilk{namespace proxy
{

	// aioproxysession_supper

	const unsigned int flag_default = EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLONESHOT;
	
	aioproxysession_supper::aioproxysession_supper()
	{
		endpoint = nullptr;
		flag = EPOLLIN | flag_default;
COUT << "构造" << this << std::endl;
	}
	aioproxysession_supper::~aioproxysession_supper()
	{
		if(endpoint){
			endpoint->endpoint = nullptr;
			//pool->remove(endpoint);
		}
COUT << "析构" << this << std::endl;
	}
	bool aioproxysession_supper::init()
	{
		return true;
	}

	bool aioproxysession_supper::notify_in()
	{
COUT << std::endl;
		if(endpoint == nullptr) return false;

		return pool->reset(endpoint,EPOLLOUT| flag_default) && pool->reset(this,flag_default);
	}


	bool aioproxysession_supper::notify_out()
	{
COUT << std::endl;
		if(endpoint == nullptr) return false;

		char buff[4096];

		while(scache){
			std::size_t pos_start = scache.tellg();
			scache.read(buff,sizeof(buff));
			int gcount = scache.gcount();
			if(gcount == 0) break;
			int r = ::send(fd(),buff,gcount,0);
			if(r == 0){
				return false;
			}
			if(r == -1 && errno != EAGAIN){
				return false;
			}

			if(r < gcount){
				std::size_t p = 0;
				if(r > 0) p += r;
				lyramilk::data::string str = scache.str().substr(pos_start + p);
				scache.clear();
				scache.str(str);
				return pool->reset(this,EPOLLOUT | flag_default);
			}
		}
		scache.clear();
		scache.str("");

		int i = 0;
		do{
			i = ::recv(endpoint->fd(),buff,sizeof(buff),MSG_PEEK);
COUT << i << std::endl;

			if(i == 0) return false;
			if(i == -1){
				if(errno == EAGAIN ){
					return pool->reset(this,EPOLLIN | flag_default);
				}
				if(errno == EINTR) continue;
				return false;
			}
			assert(i > 0);

			errno = 0;
			int bytesused = 0;
			if(!onsenddata(buff,i,&bytesused,scache)){
				return false;
			}
COUT << bytesused << "," << i << std::endl;

			if(bytesused > 0){
				::recv(endpoint->fd(),buff,bytesused,0);
			}

			while(scache){
				std::size_t pos_start = scache.tellg();
				scache.read(buff,sizeof(buff));
				int gcount = scache.gcount();
				if(gcount == 0) break;
				int r = ::send(fd(),buff,gcount,0);
				if(r == 0){
					return false;
				}
				if(r == -1 && errno != EAGAIN){
					return false;
				}

				if(r < gcount){
					std::size_t p = 0;
					if(r > 0) p += r;
					lyramilk::data::string str = scache.str().substr(pos_start + p);
					scache.clear();
					scache.str(str);
					return pool->reset(this,EPOLLOUT | flag_default);
				}
			}
			scache.clear();
			scache.str("");
		}while(i == sizeof(buff));
		return pool->reset(endpoint,EPOLLIN | flag_default) && pool->reset(this,EPOLLIN | flag_default);
	}

	bool aioproxysession_supper::notify_hup()
	{
		lyramilk::klog(lyramilk::log::debug,"lyramilk.netio.aioproxysession_supper.notify_hup") << lyramilk::kdict("发生了HUP事件%s",strerror(errno)) << std::endl;
		return false;
	}

	bool aioproxysession_supper::notify_err()
	{
		lyramilk::klog(lyramilk::log::debug,"lyramilk.netio.aioproxysession_supper.notify_err") << lyramilk::kdict("发生了ERR事件%s",strerror(errno)) << std::endl;
		return false;
	}

	bool aioproxysession_supper::notify_pri()
	{
		lyramilk::klog(lyramilk::log::debug,"lyramilk.netio.aioproxysession_supper.notify_pri") << lyramilk::kdict("发生了PRI事件%s",strerror(errno)) << std::endl;
		return false;
	}


	// aioproxysession
	aioproxysession::aioproxysession()
	{
	}

	aioproxysession::~aioproxysession()
	{
	}


	bool aioproxysession::notify_in()
	{
COUT << std::endl;
		if(endpoint){
			return pool->reset(endpoint,EPOLLOUT| flag_default) && pool->reset(this,flag_default);
		}

		char buff[4096];
		int i = 0;
		do{
			i = ::recv(fd(),buff,sizeof(buff),MSG_PEEK);
			if(i == 0) return false;
			if(i < 0){
				if(errno == EAGAIN ){
					return pool->reset(this,EPOLLIN | flag_default);
				}
				if(errno == EINTR) continue;
				return false;
			}
			assert(i > 0);


			int bytesused = 0;

			if(!oninit(buff,i,&bytesused,scache,&endpoint)){
				return false;
			}

			if(bytesused > i){
				return false;
			}

			if(bytesused > 0){
				::recv(fd(),buff,bytesused,0);
			}

			while(scache){
				std::size_t pos_start = scache.tellg();
				scache.read(buff,sizeof(buff));
				int gcount = scache.gcount();
				if(gcount == 0) break;
				int r = ::send(fd(),buff,gcount,0);
				if(r == 0){
					return false;
				}
				if(r == -1 && errno != EAGAIN){
					return false;
				}

				if(r < gcount){
					std::size_t p = 0;
					if(r > 0) p += r;
					lyramilk::data::string str = scache.str().substr(pos_start + p);
					scache.clear();
					scache.str(str);
					return pool->reset(this,EPOLLOUT | flag_default);
				}
			}
			scache.clear();
			scache.str("");


COUT.write(buff,i) << std::endl;


			if(endpoint){
				endpoint->endpoint = this;
				lyramilk::io::aiopoll_safe* pool = (lyramilk::io::aiopoll_safe*)this->pool;
				pool->add(endpoint,EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLONESHOT,true);

				if(bytesused < i){
					return pool->reset(endpoint,EPOLLOUT | flag_default) && pool->reset(this,EPOLLIN | flag_default);
				}else{
					return pool->reset(endpoint,EPOLLIN | flag_default) && pool->reset(this,EPOLLIN | flag_default);
				}
			}
		}while(i == sizeof(buff));

		return pool->reset(this,EPOLLIN | flag_default);
	}

	bool aioproxysession::oninit(const char* cache, int size,int* size_used, std::ostream& os,aioproxysession_supper** endpoint)
	{
		/*
		lyramilk::netio::netaddress addr("127.0.0.1",80);
		aioproxysession_upstream* ups = create_upstream();
		if(!ups->open(addr.ip_str().c_str(),addr.port)){
			lyramilk::netio::aiosession::__tdestoryer<aioproxysession_upstream>(ups);
			return false;
		}
		if(!ups->init()){
			lyramilk::netio::aiosession::__tdestoryer<aioproxysession_upstream>(ups);
			return false;
		}
		*endpoint = ups;
		return true;
		*/
		return false;
	}

	bool aioproxysession::onsenddata(const char* cache, int size,int* size_used, std::ostream& os)
	{
		return ondownstream(cache,size,size_used,os);
	}

	bool aioproxysession::onupstream(const char* cache, int size,int* size_used, std::ostream& os)
	{
		os.write(cache,size);
		*size_used = size;
		return true;
	}
	bool aioproxysession::ondownstream(const char* cache, int size,int* size_used, std::ostream& os)
	{
		os.write(cache,size);
		*size_used = size;
		return true;
	}

	aioproxysession_upstream* aioproxysession::create_upstream()
	{
		return lyramilk::netio::aiosession::__tbuilder<aioproxysession_upstream>();
	}



	// aioproxysession_upstream
	aioproxysession_upstream::aioproxysession_upstream()
	{
	}

	aioproxysession_upstream::~aioproxysession_upstream()
	{
	}



	bool aioproxysession_upstream::open(lyramilk::data::string host,lyramilk::data::uint16 port)
	{
		if(fd() >= 0){
			lyramilk::klog(lyramilk::log::error,"lyramilk.netio.client.open") << lyramilk::kdict("打开监听套件字失败，因为该套接字己打开。") << std::endl;
			return false;
		}
		hostent* h = gethostbyname(host.c_str());
		if(h == nullptr){
			lyramilk::klog(lyramilk::log::error,"lyramilk.netio.client.open") << lyramilk::kdict("获取IP地址失败：%s",strerror(errno)) << std::endl;
			return false;
		}

		in_addr* inaddr = (in_addr*)h->h_addr;
		if(inaddr == nullptr){
			lyramilk::klog(lyramilk::log::error,"lyramilk.netio.client.open") << lyramilk::kdict("获取IP地址失败：%s",strerror(errno)) << std::endl;
			return false;
		}

		lyramilk::netio::native_socket_type tmpsock = ::socket(AF_INET,SOCK_STREAM, IPPROTO_IP);
		if(tmpsock < 0){
			lyramilk::klog(lyramilk::log::error,"lyramilk.netio.client.open") << lyramilk::kdict("打开监听套件字失败：%s",strerror(errno)) << std::endl;
			return false;
		}

		sockaddr_in addr = {0};
		addr.sin_addr.s_addr = inaddr->s_addr;
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);


		if(0 == ::connect(tmpsock,(const sockaddr*)&addr,sizeof(addr))){
			unsigned int argp = 1;
			//ioctlsocket(tmpsock,FIONBIO,&argp);
			ioctl(tmpsock,FIONBIO,&argp);
			this->fd(tmpsock);
			return true;
		}
		lyramilk::klog(lyramilk::log::error,"lyramilk.netio.client.open") << lyramilk::kdict("打开监听套件字失败：%s",strerror(errno)) << std::endl;
		::close(tmpsock);
		return false;
	}

	bool aioproxysession_upstream::oninit(const char* cache, int size,int* size_used, std::ostream& os,aioproxysession_supper** endpoint)
	{
		assert(endpoint);
		return true;
	}

	bool aioproxysession_upstream::onsenddata(const char* cache, int size,int* size_used, std::ostream& os)
	{
		return endpoint->onupstream(cache,size,size_used,os);
	}


	bool aioproxysession_upstream::onupstream(const char* cache, int size,int* size_used, std::ostream& os)
	{
		return endpoint->onupstream(cache,size,size_used,os);
	}

}}
