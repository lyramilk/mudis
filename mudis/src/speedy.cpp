#include "strategy.h"
#include "redis_proxy.h"
#include <libmilk/dict.h>

/*
#include <sys/epoll.h>
#include <sys/poll.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <cassert>
#include <netdb.h>
#include <sys/ioctl.h>
*/
namespace lyramilk{ namespace mudis { namespace strategy
{
	class speedy:public redis_proxy_strategy
	{
		redis_upstream_server* rinfo;
		lyramilk::netio::aioproxysession_speedy* endpoint;
		bool is_ssdb;
	  public:
		speedy(redis_upstream_server* ri,lyramilk::netio::aioproxysession_speedy* endpoint,bool is_ssdb)
		{
			rinfo = ri;
			rinfo->add_ref();
			this->endpoint = endpoint;
			this->is_ssdb = is_ssdb;
		}

		virtual ~speedy()
		{
			rinfo->release();
		}

		virtual bool onauth(lyramilk::data::ostream& os,redis_proxy* proxy)
		{
			if(proxy->combine(endpoint)){
				if(is_ssdb){
					os << "2\nok\n1\n1\n\n";
				}else{
					os << "+OK\r\n";
				}
				return true;
			}
			return false;
		}
		virtual bool onrequest(const char* cache,int size,int* bytesused,lyramilk::data::ostream& os)
		{
			TODO();
			return false;
		}
	};


	class speedy_master:public redis_proxy_group
	{
		std::vector<redis_upstream_server*> upstreams;	//	redishash->redisinfo
	  public:
		speedy_master()
		{
		}

		virtual ~speedy_master()
		{
		}

		static redis_proxy_group* ctr(void*)
		{
			return new speedy_master;
		}

		static void dtr(redis_proxy_group* p)
		{
			delete (speedy_master*)p;
		}

		virtual bool load_config(const lyramilk::data::map& cfg,const lyramilk::data::map& gcfg)
		{
			lyramilk::data::map conf = gcfg;
			lyramilk::data::array& upstreams = conf["upstream"];
			for(lyramilk::data::array::iterator it = upstreams.begin();it != upstreams.end();++it){
				lyramilk::data::string host = it->operator[]("host").str();
				unsigned short port = it->operator[]("port");
				lyramilk::data::string password = it->operator[]("password").str();

				redis_upstream_server* srv = redis_strategy_master::instance()->add_redis_server(host,port,password);
				if(srv){
					this->upstreams.push_back(srv);
				}
			}
			return true;
		}

		virtual redis_proxy_strategy* create(bool is_ssdb)
		{
			for(int i=0;i<10;++i){
				int idx = rand() % upstreams.size();

				if(upstreams[idx]->online){
					lyramilk::netio::aioproxysession_speedy* endpoint = lyramilk::netio::aiosession::__tbuilder<lyramilk::netio::aioproxysession_speedy>();
					if(endpoint->open(upstreams[idx]->saddr,200)){
						redis_upstream_server* rinfo = upstreams[idx];
						if(rinfo->password.empty()){
							//不登录的话也先ping一下，防假死
							lyramilk::data::array cmd;
							cmd.push_back("ping");
							bool err = false;

							if(is_ssdb){
								lyramilk::netio::client c;
								c.fd(endpoint->fd());
								lyramilk::data::strings r = redis_session::exec_ssdb(c,cmd,&err);
								c.fd(-1);

								if(r.size() > 0 && r[0] == "ok"){
									return new speedy(upstreams[idx],endpoint,is_ssdb);
								}
							}else{
								lyramilk::netio::client c;
								c.fd(endpoint->fd());
								lyramilk::data::var r = redis_session::exec_redis(c,cmd,&err);
								c.fd(-1);
								if(r == "PONG"){
									return new speedy(upstreams[idx],endpoint,is_ssdb);
								}
							}

						}else{
							lyramilk::data::array cmd;
							cmd.push_back("auth");
							cmd.push_back(rinfo->password);
							bool err = false;

							if(is_ssdb){
								lyramilk::netio::client c;
								c.fd(endpoint->fd());
								lyramilk::data::strings r = redis_session::exec_ssdb(c,cmd,&err);
								c.fd(-1);
								if(r.size() > 0 && r[0] == "ok"){
									return new speedy(upstreams[idx],endpoint,is_ssdb);
								}
							}else{
								lyramilk::netio::client c;
								c.fd(endpoint->fd());
								lyramilk::data::var r = redis_session::exec_redis(c,cmd,&err);
								c.fd(-1);
								if(r == "OK"){
									return new speedy(upstreams[idx],endpoint,is_ssdb);
								}
							}


						}
					}
					//尝试链接失败
					endpoint->dtr(endpoint);
					upstreams[idx]->online = false;
				}
			}
			/*
			for(std::size_t idx=0;idx<upstreams.size();++idx){
				if(upstreams[idx]->online){
					lyramilk::netio::aioproxysession_speedy* endpoint = lyramilk::netio::aiosession::__tbuilder<lyramilk::netio::aioproxysession_speedy>();
					if(endpoint->open(upstreams[idx]->saddr,200)){
						redis_upstream_server* rinfo = upstreams[idx];

						if(rinfo->password.empty()){
							//不登录的话也先ping一下，防假死
							lyramilk::data::array cmd;
							cmd.push_back("ping");
							bool err = false;

							lyramilk::netio::client c;
							c.fd(endpoint->fd());
							lyramilk::data::var r = is_ssdb?redis_session::exec_ssdb(c,cmd,&err):redis_session::exec_redis(c,cmd,&err);
							c.fd(-1);

							if(r == "PONG"){
								return new speedy(upstreams[idx],endpoint);
							}
						}else{
							lyramilk::data::array cmd;
							cmd.push_back("auth");
							cmd.push_back(rinfo->password);
							bool err = false;

							lyramilk::netio::client c;
							c.fd(endpoint->fd());
							lyramilk::data::var r = is_ssdb?redis_session::exec_ssdb(c,cmd,&err):redis_session::exec_redis(c,cmd,&err);
							c.fd(-1);

							if(r == "OK"){
								return new speedy(upstreams[idx],endpoint);
							}
						}

					}
					//尝试链接失败
					endpoint->dtr(endpoint);
					upstreams[idx]->online = false;
				}
			}*/
			return nullptr;
		}

		virtual void destory(redis_proxy_strategy* p)
		{
			if(p){
				speedy* sp = (speedy*)p;
				delete sp;
			}
		}

	};

	static __attribute__ ((constructor)) void __init()
	{
		redis_strategy_master::instance()->define("speedy",speedy_master::ctr,speedy_master::dtr);
	}


}}}
