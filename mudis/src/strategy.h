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

	struct redis_upstream_server
	{
		lyramilk::data::string host;
		lyramilk::data::string password;
		unsigned short port;
		bool online;
		int alive;
		int payload;
		int weight;

		sockaddr_in saddr;

		redis_upstream_server();
		~redis_upstream_server();


		void add_ref();
		void release();
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


	class redis_proxy_group
	{
	  public:
		redis_proxy_group();
	  	virtual ~redis_proxy_group();

		virtual bool load_config(const lyramilk::data::map& cfg,const lyramilk::data::map& gcfg) = 0;
		virtual redis_proxy_strategy* create(bool is_ssdb) = 0;
		virtual void destory(redis_proxy_strategy* p) = 0;

		static bool connect_upstream(bool is_ssdb,lyramilk::netio::aioproxysession_speedy* endpoint,redis_upstream_server* upstream);
	};


	class redis_strategy_master:public lyramilk::util::factory<redis_proxy_group>
	{
		friend class strategy::admin;
		friend class redis_proxy;
		std::map<lyramilk::data::string,redis_proxy_group*> glist;	//	groupname->group
		std::map<lyramilk::data::string,redis_upstream_server> rlist;	//	redishash->redisinfo
	  public:
		lyramilk::threading::lockfreequeue<redis_session_cmd> queue;
		bool leave;

		std::map<lyramilk::data::string,std::set<redis_session_info> > clients;


		redis_strategy_master();
	  	virtual ~redis_strategy_master();
		static redis_strategy_master* instance();

	  	static lyramilk::data::string hash(const lyramilk::data::string& host,unsigned short port,const lyramilk::data::string& password);
	  	static bool check_redis(const lyramilk::data::string& host,unsigned short port,const lyramilk::data::string& password);

		virtual redis_proxy_group* get_by_groupname(const lyramilk::data::string& groupname);
		virtual bool load_config(const lyramilk::data::map& cfg);
		virtual redis_upstream_server* add_redis_server(const lyramilk::data::string& host,unsigned short port,const lyramilk::data::string& password);
		virtual bool check_upstreams();
		virtual bool check_clients();
	};
}}
#endif
