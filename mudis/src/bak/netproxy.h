#ifndef _lyramilk_netio_netproxy_h_
#define _lyramilk_netio_netproxy_h_

#include <libmilk/netaio.h>
#include <libmilk/netio.h>
#include <libmilk/aio.h>

/**
	@namespace lyramilk::netio
*/

namespace lyramilk{namespace proxy
{

	class aioproxysession_upstream;

	class aioproxysession_supper : public lyramilk::netio::aiosession
	{
		lyramilk::data::stringstream scache;
		aioproxysession_supper* endpoint;
		friend class aioproxysession;
		friend class aioproxysession_upstream;
	  public:
		aioproxysession_supper();
	  	virtual ~aioproxysession_supper();
		virtual bool init();
	  protected:
		virtual bool notify_in();
		virtual bool notify_out();
		virtual bool onsenddata(const char* cache, int size,int* size_used, std::ostream& os) = 0;
		virtual bool notify_hup();
		virtual bool notify_err();
		virtual bool notify_pri();
		virtual bool oninit(const char* cache, int size,int* size_used, std::ostream& os,aioproxysession_supper** endpoint) = 0;
		virtual bool onupstream(const char* cache, int size,int* size_used, std::ostream& os) = 0;
	};


	/**
		@brief 异步代理会话
		@details 用作中间人转发上下流数据。
	*/
	class _lyramilk_api_ aioproxysession : public aioproxysession_supper
	{
		friend class aioproxysession_upstream;
	  public:
		aioproxysession();
		virtual ~aioproxysession();
	  protected:
		virtual bool notify_in();
		virtual bool oninit(const char* cache, int size,int* size_used, std::ostream& os,aioproxysession_supper** endpoint);
		virtual bool onsenddata(const char* cache, int size,int* size_used, std::ostream& os);
		virtual bool onupstream(const char* cache, int size,int* size_used, std::ostream& os);
		virtual bool ondownstream(const char* cache, int size,int* size_used, std::ostream& os);

		static aioproxysession_upstream* create_upstream();
	};





	/**
		@brief 由 aioproxysession 支配
	*/
	class aioproxysession_upstream : public aioproxysession_supper
	{
	  public:
		aioproxysession_upstream();
	  	virtual ~aioproxysession_upstream();
		virtual bool open(lyramilk::data::string host,lyramilk::data::uint16 port);
	  protected:
		virtual bool oninit(const char* cache, int size,int* size_used, std::ostream& os,aioproxysession_supper** endpoint);
		virtual bool onsenddata(const char* cache, int size,int* size_used, std::ostream& os);
		virtual bool onupstream(const char* cache, int size,int* size_used, std::ostream& os);
	};
}}

#endif
