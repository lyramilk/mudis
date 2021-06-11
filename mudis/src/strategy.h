#ifndef _lyramilk_rproxy_strategy_h_
#define _lyramilk_rproxy_strategy_h_

#include <libmilk/netaio.h>
#include <libmilk/factory.h>
#include <libmilk/log.h>
#include <libmilk/netproxy.h>
#include <stdlib.h>
#include <arpa/inet.h>


namespace lyramilk{ namespace mudis
{
	namespace strategy{
		class admin;
	};

	class redis_proxy;
	class redis_strategy;

	class redis_upstream_server
	{
	  public:
		lyramilk::data::string host;
		lyramilk::data::string password;
		unsigned short port;
		bool online;
		int alive;
		int payload;
		bool nocheck;

		std::vector<redis_strategy*> group;
		sockaddr_in saddr;

		redis_upstream_server();
		virtual ~redis_upstream_server();

		void enable(bool isenable);
		void add_ref();
		void release();
		virtual bool check();
	};

	struct redis_sentinel_info
	{
		lyramilk::data::string host;
		lyramilk::data::string password;
		unsigned short port;
		lyramilk::data::string name;
		lyramilk::data::string auth;
	};

	class redis_upstream
	{
	  public:
		redis_upstream_server* srv;
		int weight;

		redis_upstream(redis_upstream_server* p,int w);
		virtual ~redis_upstream();
	};

	struct  redis_session_info
	{
		lyramilk::data::string client_host;
		unsigned short client_port;
		bool operator < (const redis_session_info& o) const
		{
			if(client_host < o.client_host) return true;
			if(client_host > o.client_host) return false;
			return client_port < o.client_port;
		}
		bool operator == (const redis_session_info& o) const
		{
			return client_host == o.client_host && client_port == o.client_port;
		}
	};

	enum redis_session_cmd_type
	{
		rct_add,
		rct_del,
	};

	struct  redis_session_cmd : public redis_session_info
	{
		lyramilk::data::string group;
		redis_session_cmd_type cmdtype;
	};

	class redis_proxy_strategy
	{
		friend class redis_strategy_master;
		friend class redis_proxy;
		redis_session_cmd ri;
	  public:
		redis_proxy_strategy();
	  	virtual ~redis_proxy_strategy();

	  	virtual bool onauth(lyramilk::data::ostream& os,redis_proxy* proxy) = 0;
	  	virtual bool onrequest(const char* cache,int size,int* bytesused,lyramilk::data::ostream& os) = 0;
	};


	class redis_upstream_connector:public lyramilk::netio::aioproxysession_speedy_async
	{
		virtual bool oninit(lyramilk::data::ostream& os);
		virtual void onfinally(lyramilk::data::ostream& os);
		virtual bool onrequest(const char* cache, int size, int* sizeused, lyramilk::data::ostream& os);
		virtual bool onclientauthok();
	  public:
		bool is_ssdb;
		redis_upstream_server* upstream;
		redis_proxy* proxy;

		bool pend_redis_or_ssdb_cmd(lyramilk::data::ostream& ss,const lyramilk::data::array& cmd);


		redis_upstream_connector();
	  	virtual ~redis_upstream_connector();

	};

	class redis_strategy
	{
		friend class redis_strategy_master;
		bool changed;
		lyramilk::data::string groupname;
	  protected:
		virtual void onlistchange() = 0;
		virtual bool load_config(const lyramilk::data::string& groupname,const lyramilk::data::array& cfg) = 0;
	  public:
		redis_strategy();
	  	virtual ~redis_strategy();

		virtual redis_proxy_strategy* create(bool is_ssdb,redis_proxy* proxy) = 0;
		virtual void destory(redis_proxy_strategy* p) = 0;

		virtual void check();
		virtual void update();
		virtual void reflush();
		virtual lyramilk::data::string name();

		static bool connect_upstream(bool is_ssdb,redis_upstream_connector* endpoint,redis_upstream_server* upstream,redis_proxy* proxy);
	};


	class redis_strategy_master:public lyramilk::util::factory<redis_strategy>
	{
		friend class strategy::admin;
		friend class redis_proxy;
		std::map<lyramilk::data::string,redis_strategy*> glist;	//	groupname->group
		std::map<lyramilk::data::string,redis_upstream_server*> rlist;	//	redishash->redisinfo
	  public:
		bool leave;

		redis_strategy_master();
	  	virtual ~redis_strategy_master();
		static redis_strategy_master* instance();

	  	static lyramilk::data::string hash(const lyramilk::data::string& host,unsigned short port,const lyramilk::data::string& password);

		virtual redis_strategy* get_by_groupname(const lyramilk::data::string& groupname);
		virtual bool load_group_config(const lyramilk::data::string& groupname,const lyramilk::data::string& strategy,const lyramilk::data::array& cfg);
		virtual redis_upstream_server* add_redis_server(const lyramilk::data::string& host,unsigned short port,const lyramilk::data::string& password);
		virtual redis_upstream_server* add_redis_server(const lyramilk::data::string& groupname);
		//virtual redis_upstream_server_with_redis_sentinel* add_redis_server(const lyramilk::data::string& groupname,const std::vector<redis_sentinel_info>& redis_sentinel_list);
		virtual bool check_upstreams();
		virtual bool check_groups();
		virtual bool check_groups_changes();
	};
}}
#endif
