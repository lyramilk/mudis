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
			bool is_ssdb;
		  public:
			admin(bool is_ssdb)
			{
				this->is_ssdb = is_ssdb;
			}

			virtual ~admin()
			{
			}

			virtual bool is_async_auth()
			{
				return false;
			}

			virtual bool onauth(lyramilk::data::ostream& os,redis_proxy* proxy)
			{
				if(is_ssdb){
					os << "2\nok\n1\n1\n\n";
				}else{
					os << "+OK\r\n";
				}
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

					if(is_ssdb){
						os << "2\nok\n";

						std::map<lyramilk::data::string,redis_upstream_server*>::iterator it = redis_strategy_master::instance()->rlist.begin();
						for(;it!=redis_strategy_master::instance()->rlist.end();++it){

							lyramilk::data::ostringstream oss;
							oss << it->second->host << ":" << it->second->port << " ";
							if(it->second->online){
								oss << "online";
								oss << " " << it->second->alive;
							}else{
								oss << "down -1";
							}


							lyramilk::data::string response = oss.str();
							os << response.size() << "\n";
							os << response << "\n";
						}
						os << "\n";

					}else{
						std::size_t redis_count = redis_strategy_master::instance()->rlist.size();
						os << "*" << redis_count << "\r\n";

						std::map<lyramilk::data::string,redis_upstream_server*>::iterator it = redis_strategy_master::instance()->rlist.begin();
						for(;it!=redis_strategy_master::instance()->rlist.end();++it){

							lyramilk::data::ostringstream oss;
							oss << it->second->host << ":" << it->second->port << " ";
							if(it->second->online){
								oss << "online";
								oss << " " << it->second->alive;
							}else{
								oss << "down -1";
							}


							lyramilk::data::string response = oss.str();
							os << "$" << response.size() << "\r\n";
							os << response << "\r\n";
						}
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
						sinfo.push_back("  " "mudis: " MUDIS_VERSION);

					}

					if(module == "status" || module == ""){
						sinfo.push_back("# Status");
						std::map<lyramilk::data::string,redis_upstream_server*>::const_iterator it = redis_strategy_master::instance()->rlist.begin();
						for(;it!=redis_strategy_master::instance()->rlist.end();++it){

							lyramilk::data::ostringstream oss;
							oss << "  " << it->second->host << ":" << it->second->port << " ";
							if(it->second->online){
								oss << "up ";
								oss << it->second->alive;
							}else{
								oss << "down -1";
							}


							sinfo.push_back(oss.str());
						}
					}
/*
					if(module == "groups" || module == ""){
						sinfo.push_back("# Groups");
						std::map<lyramilk::data::string,std::set<redis_session_info> >::const_iterator it = redis_strategy_master::instance()->clients.begin();
						for(;it!=redis_strategy_master::instance()->clients.end();++it){
							lyramilk::data::ostringstream oss;
							oss << "  " << it->first << "(" << it->second.size() << " clients)";
							sinfo.push_back(oss.str());
						}
					}

					if(module == "clients" || module == ""){
						sinfo.push_back("# Clients");
						std::map<lyramilk::data::string,std::set<redis_session_info> >::const_iterator it = redis_strategy_master::instance()->clients.begin();
						for(;it!=redis_strategy_master::instance()->clients.end();++it){
							if(it->second.size() > 0){
								lyramilk::data::ostringstream oss;
								oss << "  " << it->first << "(" << it->second.size() << " clients)";
								sinfo.push_back(oss.str());
								{
									std::set<redis_session_info>::const_iterator sit = it->second.begin();
									for(;sit!=it->second.end();++sit){
										lyramilk::data::ostringstream oss;
										oss << "    " << sit->client_host << ":" << sit->client_port;
										sinfo.push_back(oss.str());
									}
								}
							}
						}
					}*/

					sinfo.push_back("");

					if(is_ssdb){
						os << "2\nok\n";
						os << "0\n\n";
						//os << "6\nserver\n";
						lyramilk::data::ostringstream oss;
						lyramilk::data::strings::const_iterator it = sinfo.begin();
						for(;it!=sinfo.end();++it){
							oss << *it << "\n";
						}
						lyramilk::data::string str = oss.str();
						os << str.size() << "\n";
						os << str << "\n";
						os << "\n";
					}else{
						os << "*" << sinfo.size() << "\r\n";
						lyramilk::data::strings::iterator it = sinfo.begin();
						for(;it!=sinfo.end();++it){
							os << "$" << it->size() << "\r\n";
							os << *it << "\r\n";
						}
					}
					return redis_proxy::rs_ok;

				}else if(scmd == "shutdown"){
					lyramilk::mudis::redis_strategy_master::instance()->leave = true;
					if(is_ssdb){
						os << "2\nok\n1\n1\n\n";
					}else{
						os << "+OK\r\n";
					}
					return redis_proxy::rs_ok;
				}else if(scmd == "bye"){
					if(is_ssdb){
						os << "2\nok\n1\n1\n\n";
					}else{
						os << "+OK\r\n";
					}
					return redis_proxy::rs_error;
				}

				if(is_ssdb){
					lyramilk::data::string err = "error";
					lyramilk::data::string msg = "unknown command";
					os << err.size() << "\n" << err << "\n" << msg.size() << "\n" << msg << "\n\n";
				}else{
					os << "-ERR unknown command '" << scmd << "'\r\n";
				}
				return redis_proxy::rs_ok;
			}



		};


		class admin_master:public redis_strategy
		{
		  public:
			admin_master()
			{
			}

			virtual ~admin_master()
			{
			}
	
			static redis_strategy* ctr(void*)
			{
				return new admin_master;
			}
	
			static void dtr(redis_strategy* p)
			{
				delete (admin_master*)p;
			}

			virtual bool load_config(const lyramilk::data::string& groupname,const lyramilk::data::array& gcfg)
			{
				return true;
			}


			virtual void onlistchange()
			{
				
			}

			virtual redis_proxy_strategy* create(bool is_ssdb,redis_proxy* proxy)
			{
				return new admin(is_ssdb);
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
