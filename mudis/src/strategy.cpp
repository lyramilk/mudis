#include "strategy.h"
#include "redis_proxy.h"
#include <libmilk/dict.h>

#include <netdb.h>
#include <errno.h>
#include <string.h>

/*

#include <arpa/inet.h>
#include <sys/socket.h>

#include <sys/epoll.h>
#include <sys/poll.h>
#include <cassert>
#include <sys/ioctl.h>

*/
namespace lyramilk{ namespace mudis
{

	redis_upstream_server::redis_upstream_server()
	{
		port = 6379;
		online = false;
		alive = 0;
		payload = 0;
	}

	redis_upstream_server::~redis_upstream_server()
	{
	}

	void redis_upstream_server::add_ref()
	{
		__sync_fetch_and_add(&payload,1);
	}

	void redis_upstream_server::release()
	{
		__sync_fetch_and_sub(&payload,1);
	}

	// redis_proxy_strategy
	redis_proxy_strategy::redis_proxy_strategy()
	{
	}

	redis_proxy_strategy::~redis_proxy_strategy()
	{
		if(!ri.client_host.empty()){
			redis_strategy_master::instance()->queue.push(ri);
		}
	}

	// redis_proxy_group
	redis_proxy_group::redis_proxy_group()
	{
	}

	redis_proxy_group::~redis_proxy_group()
	{
	}

	// redis_strategy_master

	redis_strategy_master::redis_strategy_master()
	{
		leave = false;
	}

	redis_strategy_master::~redis_strategy_master()
	{
	}

	redis_strategy_master* redis_strategy_master::instance()
	{
		static redis_strategy_master _mm;
		return &_mm;
	}

	lyramilk::data::string redis_strategy_master::hash(const lyramilk::data::string& host,unsigned short port,const lyramilk::data::string& password)
	{
		lyramilk::data::ostringstream oss;
		oss << "r:" << host << ":" << port << ":" << password;
		return oss.str();
	}

	redis_proxy_group* redis_strategy_master::get_by_groupname(const lyramilk::data::string& groupname)
	{
		std::map<lyramilk::data::string,redis_proxy_group*>::const_iterator it = glist.find(groupname);
		if(it!=glist.end()){
			return it->second;
		}
		return nullptr;
	}

	bool redis_strategy_master::load_config(const lyramilk::data::map& cfg)
	{
		lyramilk::data::map conf = cfg;
		lyramilk::data::map& proxys = conf["proxy"];


		lyramilk::data::map::iterator it = proxys.begin();
		for(;it!=proxys.end();++it){
			lyramilk::data::string strategy = it->second["strategy"];
			lyramilk::data::string groupname = it->first;

			redis_proxy_group* g = create(strategy);
			if(g){
				if(!g->load_config(cfg,it->second)){
					lyramilk::klog(lyramilk::log::error,"mudis.load_config") << D("加载配置组%s(%s)失败",groupname.c_str(),strategy.c_str()) << std::endl;
					return false;
				}

				lyramilk::klog(lyramilk::log::trace,"mudis.load_config") << D("加载配置组%s(%s)完成",groupname.c_str(),strategy.c_str()) << std::endl;

				glist[groupname] = g;
			}else{
				lyramilk::klog(lyramilk::log::warning,"mudis.load_config") << D("为%s加载策略失败：%s",groupname.c_str(),strategy.c_str()) << std::endl;
			}
		}
		return true;
	}

	redis_upstream_server* redis_strategy_master::add_redis_server(const lyramilk::data::string& host,unsigned short port,const lyramilk::data::string& password)
	{
		lyramilk::data::string key = hash(host,port,password);
		redis_upstream_server &s = rlist[key];

		s.host = host;
		s.port = port;
		s.password = password;


		hostent* h = gethostbyname(host.c_str());
		if(h == nullptr){
			lyramilk::klog(lyramilk::log::error,"mudis.redis_strategy_master.add_redis_server") << lyramilk::kdict("获取%s的IP地址失败：%p,%s",host.c_str(),h,strerror(errno)) << std::endl;
			return nullptr;
		}

		in_addr* inaddr = (in_addr*)h->h_addr;
		if(inaddr == nullptr){
			lyramilk::klog(lyramilk::log::error,"mudis.redis_strategy_master.add_redis_server") << lyramilk::kdict("获取%s的IP地址失败：%p,%s",host.c_str(),inaddr,strerror(errno)) << std::endl;
			return false;
		}

		memset(&s.saddr,0,sizeof(s.saddr));
		s.saddr.sin_addr.s_addr = inaddr->s_addr;
		s.saddr.sin_family = AF_INET;
		s.saddr.sin_port = htons(port);

		return &s;
	}




	//	redis_strategy_master
	bool redis_strategy_master::check_redis(const lyramilk::data::string& host,unsigned short port,const lyramilk::data::string& password)
	{
		lyramilk::netio::client c;
		if(c.open(host,port)){
			if(password.empty()){
				//不登录的话也先ping一下，防假死
				lyramilk::data::array cmd;
				cmd.push_back("ping");
				bool err = false;

				lyramilk::data::var r = redis_session::exec_redis(c,cmd,&err);

				if(r == "PONG"){
					return true;
				}
				lyramilk::klog(lyramilk::log::error,"mudis.check_redis") << lyramilk::kdict("检查%s:%u失败：%s",host.c_str(),port,r.str().c_str()) << std::endl;
			}else{
				lyramilk::data::array cmd;
				cmd.push_back("auth");
				cmd.push_back(password);
				bool err = false;

				lyramilk::data::var r = redis_session::exec_redis(c,cmd,&err);

				if(r == "OK"){
					return true;
				}
				lyramilk::klog(lyramilk::log::error,"mudis.check_redis") << lyramilk::kdict("检查%s:%u失败：%s",host.c_str(),port,r.str().c_str()) << std::endl;
		}
		}

		return false;
	}


	bool redis_strategy_master::check_upstreams()
	{
		std::map<lyramilk::data::string,redis_upstream_server>::iterator it =  rlist.begin();
		for(;it!=rlist.end() && !leave;++it){
			it->second.online = check_redis(it->second.host,it->second.port,it->second.password);
			if(it->second.online){
				++ it->second.alive;
			}else{
				it->second.alive = 0;
			}
		}
		return true;
	}

	bool redis_strategy_master::check_clients()
	{
		redis_session_cmd ri;
		while(queue.try_pop(&ri) && !leave){
			//统计
			if(ri.cmdtype == rct_add){
				clients[ri.group].insert(ri);
			}else if(ri.cmdtype == rct_del){
				clients[ri.group].erase(ri);
			}
		}

		return true;
	}
}}
