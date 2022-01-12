#include <fstream>
#include "redis_proxy.h"
#include "strategy.h"
#include "config.h"
#include "loadconfig.h"
#include <libmilk/var.h>
#include <libmilk/yaml1.h>
#include <libmilk/json.h>
#include <libmilk/log.h>
#include <libmilk/dict.h>
#include <libmilk/stringutil.h>
#include <libmilk/setproctitle.h>
#include <libmilk/exception.h>
#include <unistd.h>
#include <signal.h>
#include <libintl.h>
#include <sys/epoll.h>


#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <limits.h>
#include "pidfile.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>

bool enable_log_debug = true;
bool enable_log_trace = true;
bool enable_log_warning = true;
bool enable_log_error = true;


class simple_log_logfile:public lyramilk::log::logf
{
  public:
	simple_log_logfile(lyramilk::data::string logfilepathfmt):lyramilk::log::logf(logfilepathfmt)
	{
	}
	virtual ~simple_log_logfile()
	{
	}

	virtual void log(time_t ti,lyramilk::log::type ty,const lyramilk::data::string& usr,const lyramilk::data::string& app,const lyramilk::data::string& module,const lyramilk::data::string& str) const
	{
		switch(ty){
		  case lyramilk::log::debug:
			if(!enable_log_debug)return;
			break;
		  case lyramilk::log::trace:
			if(!enable_log_trace)return;
			break;
		  case lyramilk::log::warning:
			if(!enable_log_warning)return;
			break;
		  case lyramilk::log::error:
			if(!enable_log_error)return;
			break;
		}
		lyramilk::log::logf::log(ti,ty,usr,app,module,str);
	}
};

class redis_proxy_server:public lyramilk::netio::aioserver<lyramilk::mudis::redis_proxy>
{
};

void useage(lyramilk::data::string selfname)
{
	std::cout << "useage:" << selfname << " [" << D("选项") << "] <file>" << std::endl;
	std::cout << "version: " << MUDIS_VERSION << std::endl;
	std::cout << "\t-c <file>\t" << D("使用配置文件：<file>") << std::endl;
	std::cout << "\t-d       \t" << D("以守护进程方式启动") << std::endl;
	std::cout << "\t-D       \t" << D("以守护进程方式启动，但会在前台阻塞住，方便docker中使用。") << std::endl;
	std::cout << "\t-p <file>\t" << D("指定pid文件：<file>，同时忽略掉配置文件中指定的pid文件。") << std::endl;
	std::cout << "\t-l <file>\t" << D("指定日志文件：<file>，同时忽略掉配置文件中指定的日志文件。") << std::endl;
	std::cout << "\t-s <start|reload|trystart>\t" << D("操作模式：start=开始,reload=重新加载配置（不会断开现有连接）,trystart=能够启动的时候尝试启动，比start温柔一些")  << std::endl;
	std::cout << "\t-t <file>\t" << D("测试配置文件：<file>，不会真正执行。") << std::endl;
	std::cout << "\t-k <pid>\t"  << D("使指定的mudis进程和平结束。") << std::endl;
}

const int delay_msec = 50;
const int need_delay_times = 3000 / delay_msec;
pid_t chpid = 0;

void mudis_sig_leave(int sig)
{
	lyramilk::mudis::redis_strategy_master::instance()->leave = true;
	if(chpid){
		kill(chpid,SIGUSR1);
		exit(0);
	}
}

struct bind_cfgs
{
	unsigned short port;
	lyramilk::data::string host;
};


int main(int argc,const char* argv[])
{
	lyramilk::init_setproctitle(argc,argv);

	bool isdaemon = false;
	bool daemon_wait = false;
	lyramilk::data::string configure_file;
	lyramilk::data::string operate = "start";
	lyramilk::data::string selfname = argv[0];
	lyramilk::data::string pidfilename;
	lyramilk::data::string logfile;

	bool testconfig = false;
	{
		int oc;
		while((oc = getopt(argc, (char**)argv, "c:t:dDp:s:k:?")) != -1){
			switch(oc)
			{
			  case 's':
				operate = optarg;
				break;
			  case 'd':
				isdaemon = true;
				break;
			  case 'D':
				isdaemon = true;
				daemon_wait = true;
				break;
			  case 'c':
				configure_file = optarg;
				break;
			  case 't':
				configure_file = optarg;
				testconfig = true;
				break;
			  case 'p':
				pidfilename = optarg;
				break;
			  case 'l':
				logfile = optarg;
				break;
			  case 'k':
				kill(atoi(optarg),SIGUSR1);
			  	return 0;
			  case '?':
			  default:
				useage(selfname);
				return 0;
			}
		}
	}
	for(int argi = optind;argi < argc;++argi){
		configure_file = argv[argi];
	}

	if(configure_file.empty()){
		useage(selfname);
		return 0;
	}

	lyramilk::data::var vconf = lyramilk::util::get_config_from_file(configure_file);

	if(lyramilk::data::var::t_map != vconf.type()){
		lyramilk::klog(lyramilk::log::error,"mudis.main") << D("加载配置文件%s出现错误。",configure_file.c_str()) << std::endl;
		return -2;
	}


	/* 解析配置 */
	{
		std::map<lyramilk::data::string,int> norepeatmap;

		lyramilk::data::var & vproxy_cfg = vconf["proxy"];
		if(vproxy_cfg.type() == lyramilk::data::var::t_map){
			lyramilk::data::map& mproxy_cfg = vproxy_cfg; 
			for(lyramilk::data::map::iterator mproxy_cfg_it = mproxy_cfg.begin();mproxy_cfg_it != mproxy_cfg.end();++mproxy_cfg_it){
				if(mproxy_cfg_it->second.type() != lyramilk::data::var::t_map){
					lyramilk::klog(lyramilk::log::error,"mudis.main") << D("加载配置文件%s出现错误。",configure_file.c_str()) << std::endl;
					return -2;
				}
				lyramilk::data::string group = mproxy_cfg_it->first;

				if(++norepeatmap[group] > 1){
					lyramilk::klog(lyramilk::log::error,"mudis.main") << D("加载配置文件%s出现错误：发现重复的组%s",configure_file.c_str(),group.c_str()) << std::endl;
					return -2;
				}

				lyramilk::data::map& mgroup_cfg = mproxy_cfg_it->second;

				if(mgroup_cfg["strategy"].type() != lyramilk::data::var::t_str){
					lyramilk::klog(lyramilk::log::error,"mudis.main") << D("加载配置文件%s出现错误。",configure_file.c_str()) << std::endl;
					return -2;
				}
				lyramilk::data::string strategy = mgroup_cfg["strategy"].str();

				if(mgroup_cfg["upstream"].type() == lyramilk::data::var::t_invalid){
					mgroup_cfg["upstream"].type(lyramilk::data::var::t_array);
				}

				if(mgroup_cfg["upstream"].type() != lyramilk::data::var::t_array){
					lyramilk::klog(lyramilk::log::error,"mudis.main") << D("加载配置文件%s出现错误。",configure_file.c_str()) << std::endl;
					return -2;
				}


				lyramilk::data::array& upstream = mgroup_cfg["upstream"];
				if(!lyramilk::mudis::redis_strategy_master::instance()->load_group_config(group,strategy,upstream)){
					lyramilk::klog(lyramilk::log::error,"mudis.main") << D("加载配置文件%s出现错误。",configure_file.c_str()) << std::endl;
					return -2;
				}
			}

		}else if(vproxy_cfg.type() == lyramilk::data::var::t_array){
			TODO();
		}else{
			lyramilk::klog(lyramilk::log::error,"mudis.main") << D("加载配置文件%s出现错误：类型%s错误",configure_file.c_str(),vproxy_cfg.type_name().c_str()) << std::endl;
			return -2;
		}
	
	}


	if(testconfig){
		lyramilk::klog(lyramilk::log::trace,"mudis.main") << D("测试配置文件%s未发现错误。",configure_file.c_str()) << std::endl;
		return 0;
	}


	/* 配置 server */
	lyramilk::data::string emptystr;
	int threads_count = vconf.path("/server/thread").conv(1);


	std::vector<bind_cfgs> bindconf;

	if(vconf.path("/server/host").type() == lyramilk::data::var::t_invalid && vconf.path("/server/port").type() == lyramilk::data::var::t_invalid && vconf.path("/server/bind").type() == lyramilk::data::var::t_array){
		lyramilk::data::array ar = vconf.path("/server/bind");
		for(lyramilk::data::array::iterator it = ar.begin();it!=ar.end();++it){
			bind_cfgs cfg;
			cfg.host = it->at("host").conv(emptystr);
			cfg.port = it->at("port").conv(6379);
			bindconf.push_back(cfg);
		}
	}else{
		bind_cfgs cfg;
		cfg.port = vconf.path("/server/port").conv(6379);
		cfg.host = vconf.path("/server/host").conv(emptystr);
		bindconf.push_back(cfg);
	}



	if(pidfilename.empty()){
		lyramilk::data::string emptystr;
		pidfilename = vconf.path("/server/pidfile").conv(emptystr);
	}

	if(logfile.empty()){
		logfile = vconf.path("/server/logfile").conv(emptystr);
	}
	/* reload功能 */
	if(operate == "reload"){
		pid_t pid = 0;
		lyramilk::proc::pidfile::lookup(pidfilename,&pid);
		if(pid != 0){
			kill(pid,SIGUSR1);
		}
	}else if(operate == "start"){
	}else if(operate == "trystart"){
	}

	/* 己经正式决定要执行下去了。 */
	signal(SIGUSR1, mudis_sig_leave);

	if(isdaemon){
		if(daemon_wait){
			pid_t middle_pid = fork();
			if(middle_pid != 0){
				while(true){
					sleep(100);
				}
			}
		}


		::daemon(1,0);
		if(!logfile.empty()){
			lyramilk::klog.rebase(new simple_log_logfile(logfile));
		}
		lyramilk::klog(lyramilk::log::trace,"mudis.main") << D("以守护进程方式方式启动%s。",configure_file.c_str()) << std::endl;
	}else{
		enable_log_debug = true;
	}
	lyramilk::log::logss log(lyramilk::klog,"mudis");


	lyramilk::proc::pidfile* pf = lyramilk::proc::pidfile::create(pidfilename);
	/* 创建pid文件 */
	if(pf == nullptr){
		int times = need_delay_times;
		do{
			usleep(delay_msec * 1000);
			--times;
			pf = lyramilk::proc::pidfile::create(pidfilename);
			if(pf == nullptr && operate == "trystart"){

				pid_t pid = 0;
				lyramilk::proc::pidfile::lookup(pidfilename,&pid);
				if(pid > 0){
					char filename[1024] = {0};
					snprintf(filename,sizeof(filename),"/proc/%u/exe",pid);
					char buff[1024] = {0};
					readlink(filename,buff,sizeof(buff));
					if(strstr(buff,"mudisd") != nullptr){
						log(lyramilk::log::trace) << D("检查：%u(%s)存在，不启动。",pid,buff) << std::endl;
						return 0;
					}

				}
			}
		} while((EEXIST == errno || EAGAIN == errno) && pf == nullptr && operate == "reload" && times > 0);

		if(pf == NULL || !pf->good()){
			log(lyramilk::log::error) << D("创建PID文件%s错误：%s",pidfilename.c_str(),strerror(errno)) << std::endl;
			return -1;
		}
	}

	if(isdaemon){
		do{
			chpid = fork();
			if(chpid == 0){
				lyramilk::setproctitle("mudisd[%u]: worker process",getppid());
				break;
			}
			log(lyramilk::log::trace,__FUNCTION__) << D("启动子进程：\t%lu",(unsigned long)chpid) << std::endl;
			sleep(1);
		}while(waitpid(chpid,NULL,0));

	}else{
		log(lyramilk::log::debug,__FUNCTION__) << D("控制台模式，自动忽略日志文件。") << std::endl;
	}


	signal(SIGPIPE, SIG_IGN);

	log(lyramilk::log::trace,__FUNCTION__) << D("线程数:\t %d",threads_count) << std::endl;
	if(!pidfilename.empty()){
		log(lyramilk::log::trace,__FUNCTION__) << D("PID文件:\t%s",pidfilename.c_str()) << std::endl;
	}


	lyramilk::io::aiopoll_safe pool(threads_count);
	pool.active();


	/* 开始服务 */
	std::vector<redis_proxy_server*> servers;
	for(std::vector<bind_cfgs>::iterator it = bindconf.begin();it != bindconf.end();++it){
		redis_proxy_server* ins = new redis_proxy_server;
		bool isok = false;
		{
			for(int i=0;i<3;++i){
				if(it->host.empty()){
					if(ins->open(it->port)){
						isok = true;
						break;
					}
				}else{
					if(ins->open(it->host,it->port)){
						isok = true;
						break;
					}
				}
				if(operate != "reload") break;
				sleep(1);
			}
		}

		if(isok){
			//ins->setkeepalive(20,3);
			//ins->setnodelay(true);

			if(pool.add_to_thread(0,ins,EPOLLIN)){
				log(lyramilk::log::trace,__FUNCTION__) << D("连接成功：IP=%s,端口=%d",it->host.c_str(),it->port) << std::endl;
				servers.push_back(ins);
			}
		}else{
			log(lyramilk::log::error,__FUNCTION__) << D("连接失败：IP=%s,端口=%d",it->host.c_str(),it->port) << std::endl;
		}
	}


	time_t tlast12 = 0;
	time_t tlast2 = 0;

	lyramilk::mudis::redis_strategy_master::instance()->check_groups();
	lyramilk::mudis::redis_strategy_master::instance()->check_groups_changes();

	while(pool.get_fd_count() > 0){
		//lyramilk::mudis::redis_strategy_master::instance()->check_clients();
		if(lyramilk::mudis::redis_strategy_master::instance()->leave){
			if(servers.size() > 0){
				for(std::vector<redis_proxy_server*>::iterator it = servers.begin();it != servers.end();++it){
					redis_proxy_server* ins = *it;
					pool.remove(ins);
					delete ins;
					ins = nullptr;
				}
				servers.clear();
				lyramilk::proc::pidfile::destroy(pf);
				pf = nullptr;
				log(lyramilk::log::trace,__FUNCTION__) << D("进入维持状态，不再接受新连接。") << std::endl;
				continue;
			}else{
				time_t tnow = time(nullptr);
				if(tnow >= tlast12 + 12){
					tlast12 = tnow;

					log(lyramilk::log::debug,__FUNCTION__) << D("总链接数：") << pool.get_fd_count() << std::endl;
/*
					std::map<lyramilk::data::string,std::set<lyramilk::mudis::redis_session_info> >& clients = lyramilk::mudis::redis_strategy_master::instance()->clients;
					std::map<lyramilk::data::string,std::set<lyramilk::mudis::redis_session_info> >::const_iterator it = clients.begin();

					for(;it!=clients.end();++it){
						if(it->second.size() > 0){
							log(lyramilk::log::debug,__FUNCTION__) << D("分组会话数：") << it->first << "  " << it->second.size() << std::endl;
						}
					}*/
				}
			}
		}
		time_t tnow = time(nullptr);
		if(tnow >= tlast2 + 2){
			lyramilk::mudis::redis_strategy_master::instance()->check_groups();
			lyramilk::mudis::redis_strategy_master::instance()->check_upstreams();
			tlast2 = tnow;
		}

		lyramilk::mudis::redis_strategy_master::instance()->check_groups_changes();
		
		usleep(delay_msec * 10);
	}
	log(lyramilk::log::error,__FUNCTION__) << D("进程退出") << std::endl;
	return 0;
}
