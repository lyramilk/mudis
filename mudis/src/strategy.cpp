#include "strategy.h"
#include "redis_proxy.h"
#include <libmilk/dict.h>
#include <libmilk/testing.h>

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
		nocheck = false;
	}

	redis_upstream_server::~redis_upstream_server()
	{
	}

	void redis_upstream_server::enable(bool isenable)
	{
		if(isenable != online){
			online = isenable;
			for(std::vector<redis_proxy_group*>::iterator it = group.begin();it!=group.end();++it){
				redis_proxy_group* p = *it;
				p->update();
			}
		}
	}

	void redis_upstream_server::add_ref()
	{
		__sync_fetch_and_add(&payload,1);
	}

	void redis_upstream_server::release()
	{
		__sync_fetch_and_sub(&payload,1);
	}

	bool redis_upstream_server::check()
	{
		lyramilk::netio::client c;
		if(!host.empty() && c.open(host,port,80)){
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

	// redis_upstream
	redis_upstream::redis_upstream(redis_upstream_server* p,int w){
		srv = p;
		weight = w;
	}

	redis_upstream::~redis_upstream()
	{
	}


	// redis_proxy_strategy
	redis_proxy_strategy::redis_proxy_strategy()
	{
	}

	redis_proxy_strategy::~redis_proxy_strategy()
	{
		/*
		if(!ri.client_host.empty()){
			redis_strategy_master::instance()->queue.push(ri);
		}*/
	}

	// redis_proxy_group
	redis_proxy_group::redis_proxy_group()
	{
		changed = true;
	}

	redis_proxy_group::~redis_proxy_group()
	{
	}


	void redis_proxy_group::check()
	{
	}

	void redis_proxy_group::update()
	{
		changed = true;
	}

	void redis_proxy_group::reflush()
	{
		if(changed){
			onlistchange();
			changed = false;
		}
	}

	lyramilk::data::string redis_proxy_group::name()
	{
		return groupname;
	}

	bool redis_proxy_group::connect_upstream(bool is_ssdb,lyramilk::netio::aioproxysession_speedy* endpoint,redis_upstream_server* upstream)
	{
		if(endpoint->open(upstream->saddr,200)){
			redis_upstream_server* rinfo = upstream;
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
						return true;
					}
				}else{
					lyramilk::netio::client c;
					c.fd(endpoint->fd());
					lyramilk::data::var r = redis_session::exec_redis(c,cmd,&err);
					c.fd(-1);
					if(r == "PONG"){
						return true;
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
						return true;
					}
				}else{
					lyramilk::netio::client c;
					c.fd(endpoint->fd());
					lyramilk::data::var r = redis_session::exec_redis(c,cmd,&err);
					c.fd(-1);
					if(r == "OK"){
						return true;
					}
				}
			}
		}

		return false;
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

	bool redis_strategy_master::load_group_config(const lyramilk::data::string& groupname,const lyramilk::data::string& strategy,const lyramilk::data::array& cfg)
	{
		redis_proxy_group* g = create(strategy);
		if(g){
			g->groupname = groupname;
			if(!g->load_config(groupname,cfg)){
				lyramilk::klog(lyramilk::log::error,"mudis.load_config") << D("加载配置组%s(%s)失败",groupname.c_str(),strategy.c_str()) << std::endl;
				return false;
			}
			lyramilk::klog(lyramilk::log::trace,"mudis.load_config") << D("加载配置组%s(%s)完成",groupname.c_str(),strategy.c_str()) << std::endl;
			glist[groupname] = g;
			return true;
		}
		lyramilk::klog(lyramilk::log::warning,"mudis.load_config") << D("为%s加载策略失败：%s",groupname.c_str(),strategy.c_str()) << std::endl;
		return false;
	}

	redis_upstream_server* redis_strategy_master::add_redis_server(const lyramilk::data::string& host,unsigned short port,const lyramilk::data::string& password)
	{
		lyramilk::data::string key = hash(host,port,password);

		std::map<lyramilk::data::string,redis_upstream_server*>::iterator it = rlist.find(key);

		redis_upstream_server*& s = rlist[key];
		if(s == nullptr){
			s = new redis_upstream_server;
		}

		s->host = host;
		s->port = port;
		s->password = password;


		hostent* h = gethostbyname(host.c_str());
		if(h == nullptr){
			lyramilk::klog(lyramilk::log::error,"mudis.redis_strategy_master.add_redis_server") << lyramilk::kdict("获取%s的IP地址失败：%p,%s",host.c_str(),h,strerror(errno)) << std::endl;
			return nullptr;
		}

		in_addr* inaddr = (in_addr*)h->h_addr;
		if(inaddr == nullptr){
			lyramilk::klog(lyramilk::log::error,"mudis.redis_strategy_master.add_redis_server") << lyramilk::kdict("获取%s的IP地址失败：%p,%s",host.c_str(),inaddr,strerror(errno)) << std::endl;
			return nullptr;
		}

		memset(&s->saddr,0,sizeof(s->saddr));
		s->saddr.sin_addr.s_addr = inaddr->s_addr;
		s->saddr.sin_family = AF_INET;
		s->saddr.sin_port = htons(port);

		return s;
	}

	redis_upstream_server* redis_strategy_master::add_redis_server(const lyramilk::data::string& groupname)
	{
		lyramilk::data::string key = hash(groupname,0,"<noname>");

		redis_upstream_server*& s = rlist[key];
		if(s == nullptr){
			s = new redis_upstream_server;
		}

		s->host = "";
		s->online = false;
		return s;
	}



	//	redis_strategy_master
	bool redis_strategy_master::check_upstreams()
	{
		for(std::map<lyramilk::data::string,redis_upstream_server*>::iterator it =  rlist.begin();it!=rlist.end() && !leave;++it){
			if(!it->second->nocheck){
				bool online = it->second->check();
				it->second->enable(online);
				if(online){
					++ it->second->alive;
				}else{
					it->second->alive = 0;
				}
			}
		}
		return true;
	}

	bool redis_strategy_master::check_groups()
	{
		for(std::map<lyramilk::data::string,redis_proxy_group*>::iterator it = glist.begin();it!=glist.end();++it){
			redis_proxy_group* g = it->second;
			g->check();
		}
		return true;
	}

	bool redis_strategy_master::check_groups_changes()
	{
		for(std::map<lyramilk::data::string,redis_proxy_group*>::iterator it = glist.begin();it!=glist.end();++it){
			redis_proxy_group* g = it->second;
			g->reflush();
		}
		return true;
	}

/*
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
	}*/
}}
