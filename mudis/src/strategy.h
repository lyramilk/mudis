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

		sockaddr_in saddr;

		redis_upstream_server();
		~redis_upstream_server();


		void add_ref();
		void release();
	};


	class redis_proxy_strategy
	{
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
	};

	class redis_strategy_master:public lyramilk::util::factory<redis_proxy_group>,public lyramilk::threading::threads
	{
		friend class strategy::admin;
		std::map<lyramilk::data::string,redis_proxy_group*> glist;	//	groupname->group
		std::map<lyramilk::data::string,redis_upstream_server> rlist;	//	redishash->redisinfo
		virtual int svc();
	  public:
		bool leave;

		redis_strategy_master();
	  	virtual ~redis_strategy_master();
		static redis_strategy_master* instance();

	  	static lyramilk::data::string hash(const lyramilk::data::string& host,unsigned short port,const lyramilk::data::string& password);
	  	static bool check_redis(const lyramilk::data::string& host,unsigned short port,const lyramilk::data::string& password);

		virtual redis_proxy_group* get_by_groupname(const lyramilk::data::string& groupname);
		virtual bool load_config(const lyramilk::data::map& cfg);
		virtual redis_upstream_server* add_redis_server(const lyramilk::data::string& host,unsigned short port,const lyramilk::data::string& password);
	};
}}
#endif
