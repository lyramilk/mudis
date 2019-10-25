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
				if(it->type() != lyramilk::data::var::t_map) continue;
				lyramilk::data::map& m = *it;
				lyramilk::data::string host = m["host"].str();
				unsigned short port = m["port"];
				lyramilk::data::string password = m["password"].str();

				redis_upstream_server* srv = redis_strategy_master::instance()->add_redis_server(host,port,password);
				if(srv){
					srv->weight = m["weight"].conv(1);
					if(srv->weight > 1000) srv->weight = 1000;
					if(srv->weight < 0) srv->weight = 0;
					this->upstreams.push_back(srv);
				}
			}
			return true;
		}

		virtual redis_proxy_strategy* create(bool is_ssdb)
		{
			for(unsigned int i=0;i<3;++i){
				int weight_total = 0;
				for(unsigned int idx=0;idx<upstreams.size();++idx){
					if(upstreams[idx]->online){
						weight_total += upstreams[idx]->weight;
					}
				}

				int magic = rand() % weight_total;
				for(unsigned int idx=0;idx<upstreams.size();++idx){
					if(magic < upstreams[idx]->weight){
						lyramilk::netio::aioproxysession_speedy* endpoint = lyramilk::netio::aiosession::__tbuilder<lyramilk::netio::aioproxysession_speedy>();
						if(connect_upstream(is_ssdb,endpoint,upstreams[idx])){
							return new speedy(upstreams[idx],endpoint,is_ssdb);
						}

						//尝试链接失败
						endpoint->dtr(endpoint);
						upstreams[idx]->online = false;
					}else{
						magic -= upstreams[idx]->weight;
					}
				}
			}

			for(unsigned int idx=0;idx<upstreams.size();++idx){
				lyramilk::netio::aioproxysession_speedy* endpoint = lyramilk::netio::aiosession::__tbuilder<lyramilk::netio::aioproxysession_speedy>();
				if(connect_upstream(is_ssdb,endpoint,upstreams[idx])){
					return new speedy(upstreams[idx],endpoint,is_ssdb);
				}
				//尝试链接失败
				endpoint->dtr(endpoint);
				upstreams[idx]->online = false;
			}
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
