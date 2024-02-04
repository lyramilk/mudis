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
			for(std::vector<redis_strategy*>::iterator it = group.begin();it!=group.end();++it){
				redis_strategy* p = *it;
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
		lyramilk::debug::nsecdiff nd;
		nd.mark();

		lyramilk::netio::client c;
		if(!host.empty() && c.open(host,port,2000)){
			if(password.empty()){
				//不登录的话也先ping一下，防假死
				lyramilk::data::array cmd;
				cmd.push_back("ping");
				bool err = false;

				lyramilk::data::var r = redis_session::exec_redis(c,cmd,&err);

				if(r == "PONG"){
					return true;
				}
				lyramilk::klog(lyramilk::log::error,"mudis.check_redis") << lyramilk::kdict("检查%s:%u失败：%s 耗时%.3f",host.c_str(),port,r.str().c_str(),double(nd.diff())/1000000.0f) << std::endl;
				return false;
			}else{
				lyramilk::data::array cmd;
				cmd.push_back("auth");
				cmd.push_back(password);
				bool err = false;

				lyramilk::data::var r = redis_session::exec_redis(c,cmd,&err);

				if(r == "OK"){
					return true;
				}
				lyramilk::klog(lyramilk::log::error,"mudis.check_redis") << lyramilk::kdict("检查%s:%u失败：%s 耗时%.3f",host.c_str(),port,r.str().c_str(),double(nd.diff())/1000000.0f) << std::endl;
				return false;
			}
		}

		lyramilk::klog(lyramilk::log::error,"mudis.check_redis") << lyramilk::kdict("检查%s:%u失败：%s 耗时%.3f",host.c_str(),port,strerror(errno),double(nd.diff())/1000000.0f) << std::endl;
		return false;
	}

	// redis_upstream_connector
	redis_upstream_connector::redis_upstream_connector()
	{
		upstream = nullptr;
		is_ssdb = false;
	}

	redis_upstream_connector::~redis_upstream_connector()
	{
	}

	bool redis_upstream_connector::pend_redis_or_ssdb_cmd(lyramilk::data::ostream& ss,const lyramilk::data::array& cmd)
	{
		if(is_ssdb){
			lyramilk::data::array::const_iterator it = cmd.begin();
			for(;it!=cmd.end();++it){
				lyramilk::data::string str = it->str();
				ss << str.size() << "\n";
				ss << str << "\n";
			}
			ss << "\n";
		}else{
			lyramilk::data::array::const_iterator it = cmd.begin();
			ss << "*" << cmd.size() << "\r\n";
			for(;it!=cmd.end();++it){
				lyramilk::data::string str = it->str();
				ss << "$" << str.size() << "\r\n";
				ss << str << "\r\n";
			}
		}
		return true;
	}

	bool redis_upstream_connector::oninit(lyramilk::data::ostream& os)
	{
		redis_upstream_server* rinfo = upstream;
		if(rinfo->password.empty()){
			//不登录的话也先ping一下，防假死
			lyramilk::data::array cmd;
			cmd.push_back("ping");
			pend_redis_or_ssdb_cmd(os,cmd);
		}else{
			lyramilk::data::array cmd;
			cmd.push_back("auth");
			cmd.push_back(rinfo->password);
			pend_redis_or_ssdb_cmd(os,cmd);
		}

		return true;
	}

	void redis_upstream_connector::onfinally(lyramilk::data::ostream& os)
	{
	}


	bool redis_upstream_connector::onclientauthok()
	{
		if(proxy->start_async_redirect(true)){
			if(is_ssdb){
				proxy->write("2\nok\n1\n1\n\n",10);
			}else{
				proxy->write("+OK\r\n",5);
			}
			return true;
		}
		return false;
	}

	bool redis_upstream_connector::onrequest(const char* cache, int size, int* sizeused, lyramilk::data::ostream& os)
	{
		redis_upstream_server* rinfo = upstream;
		if(rinfo->password.empty()){
			if(is_ssdb){
			}else{
				if(memcmp(cache,"+PONG\r\n",size) == 0){
					enable_async_redirect(true);
					*sizeused = size;
					return onclientauthok();
				}
				return false;
			}
		}else{
			if(is_ssdb){
				if(memcmp(cache,"2\nok\n1\n1\n\n",size) == 0){
					enable_async_redirect(true);
					*sizeused = size;
					return onclientauthok();
				}
				return false;
			}else{
				if(memcmp(cache,"+OK\r\n",size) == 0){
					enable_async_redirect(true);
					*sizeused = size;
					return onclientauthok();
				}
				return false;
			}
		}

		return true;
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

	bool redis_proxy_strategy::is_async_auth()
	{
		return true;
	}

	// redis_strategy
	redis_strategy::redis_strategy()
	{
		changed = true;
	}

	redis_strategy::~redis_strategy()
	{
	}


	void redis_strategy::check()
	{
	}

	void redis_strategy::update()
	{
		changed = true;
	}

	void redis_strategy::reflush()
	{
		if(changed){
			onlistchange();
			changed = false;
		}
	}

	lyramilk::data::string redis_strategy::name()
	{
		return groupname;
	}

	bool redis_strategy::connect_upstream(bool is_ssdb,redis_upstream_connector* endpoint,redis_upstream_server* upstream,redis_proxy* proxy)
	{
		endpoint->upstream = upstream;
		endpoint->is_ssdb = is_ssdb;
		endpoint->proxy = proxy;
		if(endpoint->async_open(upstream->saddr)){
			return proxy->async_redirect_connect(endpoint);
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

	redis_strategy* redis_strategy_master::get_by_groupname(const lyramilk::data::string& groupname)
	{
		std::map<lyramilk::data::string,redis_strategy*>::const_iterator it = glist.find(groupname);
		if(it!=glist.end()){
			return it->second;
		}
		return nullptr;
	}

	bool redis_strategy_master::load_group_config(const lyramilk::data::string& groupname,const lyramilk::data::string& strategy,const lyramilk::data::array& cfg)
	{
		redis_strategy* g = create(strategy);
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

		//std::map<lyramilk::data::string,redis_upstream_server*>::iterator it = rlist.find(key);

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
		for(std::map<lyramilk::data::string,redis_strategy*>::iterator it = glist.begin();it!=glist.end();++it){
			redis_strategy* g = it->second;
			g->check();
		}
		return true;
	}

	bool redis_strategy_master::check_groups_changes()
	{
		for(std::map<lyramilk::data::string,redis_strategy*>::iterator it = glist.begin();it!=glist.end();++it){
			redis_strategy* g = it->second;
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
