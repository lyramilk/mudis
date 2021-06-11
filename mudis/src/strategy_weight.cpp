#include "strategy.h"
#include "redis_proxy.h"
#include <libmilk/dict.h>

namespace lyramilk{ namespace mudis { namespace strategy
{
	class weight:public redis_proxy_strategy
	{
		redis_upstream_server* rinfo;
		lyramilk::netio::aioproxysession_speedy* endpoint;
		bool is_ssdb;
	  public:
		weight(redis_upstream_server* ri,lyramilk::netio::aioproxysession_speedy* endpoint,bool is_ssdb)
		{
			rinfo = ri;
			rinfo->add_ref();
			this->endpoint = endpoint;
			this->is_ssdb = is_ssdb;
		}

		virtual ~weight()
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


	class weight_master:public redis_strategy
	{
		lyramilk::threading::mutex_rw lock;
		std::vector<redis_upstream> upstreams;
		std::vector<redis_upstream*> activelist;
		std::vector<redis_upstream*> backuplist;
	  public:
		weight_master()
		{
		}

		virtual ~weight_master()
		{
		}

		static redis_strategy* ctr(void*)
		{
			return new weight_master;
		}

		static void dtr(redis_strategy* p)
		{
			delete (weight_master*)p;
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
					int weight = m["weight"].conv(1);
					if(weight > 100) weight = 100;
					if(weight < 0) weight = 0;
					this->upstreams.push_back(redis_upstream(srv,weight));
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
					lyramilk::klog(lyramilk::log::error,"mudis.weight.onlistchange") << lyramilk::kdict("组[%s]己经没有活动的链接",name().c_str()) << std::endl;
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
			for(unsigned int i=0;i<3;++i){
				int weight_total = 0;
				for(unsigned int idx=0;idx<activelist.size();++idx){
					weight_total += activelist[idx]->weight;
				}


				if(weight_total > 0){
					int magic = rand() % weight_total;
					for(unsigned int idx=0;idx<activelist.size();++idx){
						redis_upstream* pupstream = activelist[idx];
						redis_upstream_server* pserver = pupstream->srv;
						if(!pserver->online) continue;

						if(magic < pupstream->weight){
							redis_upstream_connector* endpoint = lyramilk::netio::aiosession::__tbuilder<redis_upstream_connector>();
							if(connect_upstream(is_ssdb,endpoint,pserver,proxy)){
								return new weight(pserver,endpoint,is_ssdb);
							}

							//尝试链接失败
							endpoint->dtr(endpoint);
							pserver->enable(false);
						}else{
							magic -= pupstream->weight;
						}
					}
				}
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
						return new weight(pserver,endpoint,is_ssdb);
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
				weight* sp = (weight*)p;
				delete sp;
			}
		}

	};

	static __attribute__ ((constructor)) void __init()
	{
		redis_strategy_master::instance()->define("weight",weight_master::ctr,weight_master::dtr);
	}


}}}
