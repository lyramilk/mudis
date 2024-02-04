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
		lyramilk::netio::aioproxysession_connector* endpoint;
		bool is_ssdb;
	  public:
		order(redis_upstream_server* ri,lyramilk::netio::aioproxysession_connector* endpoint,bool is_ssdb)
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
			if(proxy->async_redirect_to(endpoint)){
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


	class order_master:public redis_strategy
	{
		lyramilk::threading::mutex_rw lock;
		std::vector<redis_upstream> upstreams;
		std::vector<redis_upstream*> activelist;
		std::vector<redis_upstream*> backuplist;
	  public:
		order_master()
		{
		}

		virtual ~order_master()
		{
		}

		static redis_strategy* ctr(void*)
		{
			return new order_master;
		}

		static void dtr(redis_strategy* p)
		{
			delete (order_master*)p;
		}

		virtual bool load_config(const lyramilk::data::string& groupname,const lyramilk::data::array& upstreams)
		{
			for(lyramilk::data::array::const_iterator it = upstreams.begin();it != upstreams.end();++it){
				if(it->type() != lyramilk::data::var::t_map) continue;
				lyramilk::data::map m = *it;
				lyramilk::data::string host = m["host"].str();
				unsigned short port = m["port"];
				lyramilk::data::string password = m["password"].str();

				redis_upstream_server* srv = redis_strategy_master::instance()->add_redis_server(host,port,password);
				if(srv){
					this->upstreams.push_back(redis_upstream(srv,m["weight"].conv(1)));
					srv->group.push_back(this);
				}
			}
			return true;
		}

		virtual void onlistchange()
		{
			std::vector<redis_upstream*> tmpactivelist;
			std::vector<redis_upstream*> tmpbackuplist;
			for(unsigned int idx=0;idx<upstreams.size();++idx){
				if(upstreams[idx].srv->online){
					if(upstreams[idx].weight==0){
						tmpbackuplist.push_back(&upstreams[idx]);
					}else{
						tmpactivelist.push_back(&upstreams[idx]);
					}
				}
			}

			if(tmpactivelist.empty() && tmpbackuplist.empty()){
				if(activelist.empty() && backuplist.empty()){
				}else{
					lyramilk::klog(lyramilk::log::error,"mudis.order.onlistchange") << lyramilk::kdict("组[%s]己经没有活动的链接",name().c_str()) << std::endl;
					lyramilk::threading::mutex_sync _(lock.w());
					activelist.swap(tmpactivelist);
					backuplist.swap(tmpbackuplist);
				}
			}else{
				lyramilk::threading::mutex_sync _(lock.w());
				activelist.swap(tmpactivelist);
				backuplist.swap(tmpbackuplist);
			}
		}

		virtual redis_proxy_strategy* create(bool is_ssdb,redis_proxy* proxy)
		{
			lyramilk::threading::mutex_sync _(lock.r());


			for(unsigned int i=0;i<activelist.size();++i){
				unsigned int idx = i % activelist.size();
				redis_upstream* pupstream = activelist[idx];
				redis_upstream_server* pserver = pupstream->srv;
				if(!pserver->online) continue;

				redis_upstream_connector* endpoint = lyramilk::netio::aiosession::__tbuilder<redis_upstream_connector>();
				if(connect_upstream(is_ssdb,endpoint,pserver,proxy)){
					return new order(pserver,endpoint,is_ssdb);
				}
				//尝试链接失败
				endpoint->dtr(endpoint);
				pserver->enable(false);
			}

			if(!backuplist.empty()){
				int loopoffset = rand() % backuplist.size();
				for(unsigned int i=loopoffset;i < backuplist.size() + loopoffset;++i){
					int idx = loopoffset % backuplist.size();
					redis_upstream* pupstream = backuplist[idx];
					redis_upstream_server* pserver = pupstream->srv;
					if(!pserver->online) continue;

					redis_upstream_connector* endpoint = lyramilk::netio::aiosession::__tbuilder<redis_upstream_connector>();
					if(connect_upstream(is_ssdb,endpoint,pserver,proxy)){
						return new order(pserver,endpoint,is_ssdb);
					}

					//尝试链接失败
					endpoint->dtr(endpoint);
					pserver->enable(false);
				}
			}
			// 如果都失败了就尝试不检查在线状态
			if(!upstreams.empty()){
				int loopoffset = rand() % upstreams.size();
				for(unsigned int i=loopoffset;i < upstreams.size() + loopoffset;++i){
					int idx = loopoffset % upstreams.size();
					redis_upstream* pupstream = &upstreams[idx];
					redis_upstream_server* pserver = pupstream->srv;

					redis_upstream_connector* endpoint = lyramilk::netio::aiosession::__tbuilder<redis_upstream_connector>();
					if(connect_upstream(is_ssdb,endpoint,pserver,proxy)){
						return new order(pserver,endpoint,is_ssdb);
					}

					//尝试链接失败
					endpoint->dtr(endpoint);
					pserver->enable(false);
				}
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
