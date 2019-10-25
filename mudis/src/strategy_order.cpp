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
	class order:public redis_proxy_strategy
	{
		redis_upstream_server* rinfo;
		lyramilk::netio::aioproxysession_speedy* endpoint;
		bool is_ssdb;
	  public:
		order(redis_upstream_server* ri,lyramilk::netio::aioproxysession_speedy* endpoint,bool is_ssdb)
		{
			rinfo = ri;
			rinfo->add_ref();
			this->endpoint = endpoint;
			this->is_ssdb = is_ssdb;
		}

		virtual ~order()
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


	class order_master:public redis_proxy_group
	{
		std::vector<redis_upstream_server*> upstreams;	//	redishash->redisinfo
	  public:
		order_master()
		{
		}

		virtual ~order_master()
		{
		}

		static redis_proxy_group* ctr(void*)
		{
			return new order_master;
		}

		static void dtr(redis_proxy_group* p)
		{
			delete (order_master*)p;
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
					this->upstreams.push_back(srv);
				}
			}
			return true;
		}

		virtual redis_proxy_strategy* create(bool is_ssdb)
		{
			for(unsigned int i=0;i<upstreams.size();++i){
				unsigned int idx = i % upstreams.size();
				if(upstreams[idx]->online){
					lyramilk::netio::aioproxysession_speedy* endpoint = lyramilk::netio::aiosession::__tbuilder<lyramilk::netio::aioproxysession_speedy>();
					if(connect_upstream(is_ssdb,endpoint,upstreams[idx])){
						return new order(upstreams[idx],endpoint,is_ssdb);
					}
					//尝试链接失败
					endpoint->dtr(endpoint);
					upstreams[idx]->online = false;
				}
			}
			for(unsigned int idx=0;idx<upstreams.size();++idx){
				lyramilk::netio::aioproxysession_speedy* endpoint = lyramilk::netio::aiosession::__tbuilder<lyramilk::netio::aioproxysession_speedy>();
				if(connect_upstream(is_ssdb,endpoint,upstreams[idx])){
					return new order(upstreams[idx],endpoint,is_ssdb);
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
				order* sp = (order*)p;
				delete sp;
			}
		}

	};

	static __attribute__ ((constructor)) void __init()
	{
		redis_strategy_master::instance()->define("order",order_master::ctr,order_master::dtr);
	}


}}}
