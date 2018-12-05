#include "config.h"
#include "strategy.h"
#include "redis_proxy.h"
#include <libmilk/dict.h>
#include <libmilk/stringutil.h>


#include <sys/epoll.h>
#include <sys/poll.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <cassert>
#include <netdb.h>
#include <sys/ioctl.h>

namespace lyramilk{ namespace mudis { namespace strategy
{

		class admin:public redis_proxy_strategy,public redis_session
		{
		  public:
			admin()
			{
			}

			virtual ~admin()
			{
			}

			virtual bool onauth(lyramilk::data::ostream& os,redis_proxy* proxy)
			{
				os << "+OK\r\n";
				return true;
			}
			virtual bool onrequest(const char* cache,int size,int* bytesused,lyramilk::data::ostream& os)
			{
				result_status r = parsing(cache,size,bytesused,&os);
				if(r == rs_error || r == rs_parse_error){
					return false;
				}
				return true;
			}

			virtual redis_session::result_status notify_cmd(const lyramilk::data::var::array& cmd, void* userdata)
			{
				std::ostream& os = *(std::ostream*)userdata;

				lyramilk::data::string scmd = lyramilk::data::lower_case(cmd[0].str());

				if(scmd == "list"){
					std::size_t redis_count = redis_strategy_master::instance()->rlist.size();
					os << "*" << redis_count << "\r\n";

					std::map<lyramilk::data::string,redis_upstream_server>::iterator it = redis_strategy_master::instance()->rlist.begin();
					for(;it!=redis_strategy_master::instance()->rlist.end();++it){

						lyramilk::data::ostringstream oss;
						oss << it->second.host << ":" << it->second.port << " ";
						if(it->second.online){
							oss << "online";
							oss << " " << it->second.alive;
						}else{
							oss << "down -1";
						}


						lyramilk::data::string response = oss.str();
						os << "$" << response.size() << "\r\n";
						os << response << "\r\n";
					}
					return redis_proxy::rs_ok;
				}else if(scmd == "info"){
					lyramilk::data::string module;
					if(cmd.size() > 1){
						module = lyramilk::data::lower_case(cmd[1].str());
					}
					lyramilk::data::strings sinfo;

					if(module == "server" || module == ""){
						sinfo.push_back("# Server");
						sinfo.push_back("mudis:" MUDIS_VERSION);

					}
					sinfo.push_back("");

					os << "*" << sinfo.size() << "\r\n";
					lyramilk::data::strings::iterator it = sinfo.begin();
					for(;it!=sinfo.end();++it){
						os << "$" << it->size() << "\r\n";
						os << *it << "\r\n";
					}
					return redis_proxy::rs_ok;
				}
				os << "-ERR unknown command '" << scmd << "'\r\n";
				return redis_proxy::rs_ok;
			}



		};


		class admin_master:public redis_proxy_group
		{
		  public:
			admin_master()
			{
			}

			virtual ~admin_master()
			{
			}
	
			static redis_proxy_group* ctr(void*)
			{
				return new admin_master;
			}
	
			static void dtr(redis_proxy_group* p)
			{
				delete (admin_master*)p;
			}

			virtual bool load_config(const lyramilk::data::map& cfg,const lyramilk::data::map& gcfg)
			{
				return true;
			}

			virtual redis_proxy_strategy* create()
			{
				return new admin;
			}

			virtual void destory(redis_proxy_strategy* p)
			{
				if(p){
					admin* sp = (admin*)p;
					delete sp;
				}
			}

		};
	
		static __attribute__ ((constructor)) void __init()
		{
			redis_strategy_master::instance()->define("admin",admin_master::ctr,admin_master::dtr);
		}


}}}
