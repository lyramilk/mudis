#include "strategy.h"
#include "redis_proxy.h"
#include <libmilk/dict.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>

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



SENTINEL get-master-addr-by-name feedvideo2_master
*/
namespace lyramilk{ namespace mudis { namespace strategy
{






	class redis_sentinel:public redis_proxy_strategy
	{
		redis_upstream_server* rinfo;
		lyramilk::netio::aioproxysession_connector* endpoint;
		bool is_ssdb;
	  public:
		redis_sentinel(redis_upstream_server* ri,lyramilk::netio::aioproxysession_connector* endpoint,bool is_ssdb)
		{
			rinfo = ri;
			rinfo->add_ref();
			this->endpoint = endpoint;
			this->is_ssdb = is_ssdb;
		}

		virtual ~redis_sentinel()
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


	class redis_sentinel_master:public redis_strategy
	{
		lyramilk::threading::mutex_rw lock;
		std::vector<redis_upstream> upstreams;
		std::vector<redis_upstream*> activelist;
		std::vector<redis_sentinel_info> redis_sentinel_list;
	  public:
		redis_sentinel_master()
		{
		}

		virtual ~redis_sentinel_master()
		{
		}

		static redis_strategy* ctr(void*)
		{
			return new redis_sentinel_master;
		}

		static void dtr(redis_strategy* p)
		{
			delete (redis_sentinel_master*)p;
		}

		virtual bool load_config(const lyramilk::data::string& groupname,const lyramilk::data::array& upstreams)
		{
			redis_sentinel_list.clear();

			for(lyramilk::data::array::const_iterator it = upstreams.begin();it != upstreams.end();++it){
				if(it->type() != lyramilk::data::var::t_map) return false;
				lyramilk::data::map sm = *it;
				redis_sentinel_info rsi;
				rsi.host = sm["host"].str();
				rsi.port = sm["port"];
				rsi.password = sm["password"].str();
				rsi.name = sm["name"].str();
				rsi.auth = sm["auth"].str();
				redis_sentinel_list.push_back(rsi);
			}
			redis_upstream_server* srv = redis_strategy_master::instance()->add_redis_server(groupname);
			if(srv){
				srv->nocheck = true;
				this->upstreams.push_back(redis_upstream(srv,1));
				srv->group.push_back(this);
			}
			return true;
		}



		virtual bool check_redis_sentinel(redis_upstream* ups)
		{
			for(std::vector<redis_sentinel_info>::iterator it = redis_sentinel_list.begin();it!=redis_sentinel_list.end();++it){
				lyramilk::netio::client c;
				if(c.open(it->host,it->port)){
					if(it->password.empty()){
					}else{
						lyramilk::data::array cmd;
						cmd.push_back("auth");
						cmd.push_back(it->password);
						bool err = false;

						lyramilk::data::var r = redis_session::exec_redis(c,cmd,&err);

						if(r != "OK"){
							lyramilk::klog(lyramilk::log::error,"mudis.redis_upstream_server_with_redis_sentinel.check") << lyramilk::kdict("登录哨兵失败：%p:%d:%s  %s",it->host.c_str(),it->port,it->password.c_str(),strerror(errno)) << std::endl;
							return false;
						}
					}

					lyramilk::data::array cmd;
					cmd.push_back("sentinel");
					cmd.push_back("get-master-addr-by-name");
					cmd.push_back(it->name);
					bool err = false;
					lyramilk::data::var r = redis_session::exec_redis(c,cmd,&err);
					if(r.type() == lyramilk::data::var::t_array){
						lyramilk::data::array& ar = r;
						if(ar.size() == 2){
							lyramilk::data::string host = ar[0].str();
							unsigned short port = ar[1];

							hostent* h = gethostbyname(host.c_str());
							if(h == nullptr){
								lyramilk::klog(lyramilk::log::error,"mudis.redis_upstream_server_with_redis_sentinel.check") << lyramilk::kdict("获取%s的IP地址失败：%p,%s",host.c_str(),h,strerror(errno)) << std::endl;
								return false;
							}

							in_addr* inaddr = (in_addr*)h->h_addr;
							if(inaddr == nullptr){
								lyramilk::klog(lyramilk::log::error,"mudis.redis_upstream_server_with_redis_sentinel.check") << lyramilk::kdict("获取%s的IP地址失败：%p,%s",host.c_str(),inaddr,strerror(errno)) << std::endl;
								return false;
							}

							lyramilk::threading::mutex_sync _(lock.w());
							memset(&ups->srv->saddr,0,sizeof(ups->srv->saddr));
							ups->srv->saddr.sin_addr.s_addr = inaddr->s_addr;
							ups->srv->saddr.sin_family = AF_INET;
							ups->srv->saddr.sin_port = htons(port);
							ups->srv->host = host;
							ups->srv->port = port;
							ups->srv->password = it->auth;
							ups->srv->online = true;
							return true;
						}
					}
				}

			}
			
			return false;

		}



		virtual void onlistchange()
		{
			std::vector<redis_upstream*> tmpactivelist;
			for(unsigned int idx=0;idx<upstreams.size();++idx){
				if(check_redis_sentinel(&upstreams[idx])){
					tmpactivelist.push_back(&upstreams[idx]);
					break;
				}
			}
			lyramilk::threading::mutex_sync _(lock.w());
			activelist.swap(tmpactivelist);
		}

		virtual void check()
		{
			onlistchange();
		}

		virtual redis_proxy_strategy* create(bool is_ssdb,redis_proxy* proxy)
		{
			lyramilk::threading::mutex_sync _(lock.r());
			if(!activelist.empty()){
				for(unsigned int i=0;i<activelist.size();++i){
					redis_upstream* pupstream = activelist[i];
					redis_upstream_server* pserver = pupstream->srv;
					if(!pserver->online) continue;

					redis_upstream_connector* endpoint = lyramilk::netio::aiosession::__tbuilder<redis_upstream_connector>();
					if(connect_upstream(false,endpoint,pserver,proxy)){
						return new redis_sentinel(pserver,endpoint,false);
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
				redis_sentinel* sp = (redis_sentinel*)p;
				delete sp;
			}
		}

	};

	static __attribute__ ((constructor)) void __init()
	{
		redis_strategy_master::instance()->define("redis_sentinel",redis_sentinel_master::ctr,redis_sentinel_master::dtr);
	}


}}}
