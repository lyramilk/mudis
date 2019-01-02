#include <fstream>
#include "redis_proxy.h"
#include "strategy.h"
#include "config.h"
#include <libmilk/var.h>
#include <libmilk/yaml1.h>
#include <libmilk/json.h>
#include <libmilk/log.h>
#include <libmilk/dict.h>
#include <unistd.h>
#include <signal.h>
#include <libintl.h>
#include <sys/epoll.h>

#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <limits.h>
#include "pidfile.h"


std::map<lyramilk::data::string,bool> logswitch;


class teapoy_log_base:public lyramilk::log::logb
{
	lyramilk::log::logb* old;
  public:
	const bool *enable_log_debug;
	const bool *enable_log_trace;
	const bool *enable_log_warning;
	const bool *enable_log_error;

	teapoy_log_base()
	{
		old = lyramilk::klog.rebase(this);
		enable_log_debug = &logswitch["debug"];
		enable_log_trace = &logswitch["trace"];
		enable_log_warning = &logswitch["warning"];
		enable_log_error = &logswitch["error"];
	}

	virtual ~teapoy_log_base()
	{
		lyramilk::klog.rebase(old);
	}

	virtual bool ok()
	{
		return true;
	}
};

class teapoy_log_logfile:public teapoy_log_base
{
	mutable FILE* fp;
	lyramilk::data::string logfilepath;
	lyramilk::data::string pidstr;
	lyramilk::data::string str_debug;
	lyramilk::data::string str_trace;
	lyramilk::data::string str_warning;
	lyramilk::data::string str_error;
	mutable lyramilk::threading::mutex_os lock;
	mutable tm daytime;
  public:
	teapoy_log_logfile(lyramilk::data::string logfilepath)
	{
		this->logfilepath = logfilepath;
		fp = fopen(logfilepath.c_str(),"a");

		if(!fp) fp = stdout;
		pid_t pid = getpid();
		lyramilk::data::stringstream ss;
		ss << pid << " ";
		pidstr = ss.str();
		str_debug = "[" + D("debug") + "] ";
		str_trace = "[" + D("trace") + "] ";
		str_warning = "[" + D("warning") + "] ";
		str_error = "[" + D("error") + "] ";

		time_t stime = time(0);
		daytime = *localtime(&stime);
	}
	virtual ~teapoy_log_logfile()
	{
		if(fp != stdout) fclose(fp);
	}

	virtual bool ok()
	{
		return fp != nullptr;
	}

	virtual void log(time_t ti,lyramilk::log::type ty,const lyramilk::data::string& usr,const lyramilk::data::string& app,const lyramilk::data::string& module,const lyramilk::data::string& str) const
	{
		tm t;
		localtime_r(&ti,&t);
		if(daytime.tm_year != t.tm_year || daytime.tm_mon != t.tm_mon || daytime.tm_mday != t.tm_mday){
			lyramilk::threading::mutex_sync _(lock);
			if(daytime.tm_year != t.tm_year || daytime.tm_mon != t.tm_mon || daytime.tm_mday != t.tm_mday){
				char buff[64];
				snprintf(buff,sizeof(buff),".%04d%02d%02d",(1900 + daytime.tm_year),(daytime.tm_mon + 1),daytime.tm_mday);
				daytime = t;
				lyramilk::data::string destfilename = logfilepath;
				destfilename.append(buff);
				rename(logfilepath.c_str(),destfilename.c_str());
				FILE* newfp = fopen(logfilepath.c_str(),"a");
				if(newfp){
					FILE* oldfp = fp;
					fp = newfp;
					if(oldfp && oldfp != stdout) fclose(oldfp);
				}
			}
		}

		lyramilk::data::string cache;
		cache.reserve(1024);
		switch(ty){
		  case lyramilk::log::debug:
			if(!*enable_log_debug)return;
			cache.append(pidstr);
			cache.append(str_debug);
			cache.append(strtime(ti));
			cache.append(" [");
			cache.append(module);
			cache.append("] ");
			cache.append(str);
			fwrite(cache.c_str(),cache.size(),1,fp);
			fflush(fp);
			break;
		  case lyramilk::log::trace:
			if(!*enable_log_trace)return;
			cache.append(pidstr);
			cache.append(str_trace);
			cache.append(strtime(ti));
			cache.append(" [");
			cache.append(module);
			cache.append("] ");
			cache.append(str);
			fwrite(cache.c_str(),cache.size(),1,fp);
			fflush(fp);
			break;
		  case lyramilk::log::warning:
			if(!*enable_log_warning)return;
			cache.append(pidstr);
			cache.append(str_warning);
			cache.append(strtime(ti));
			cache.append(" [");
			cache.append(module);
			cache.append("] ");
			cache.append(str);
			fwrite(cache.c_str(),cache.size(),1,fp);
			fflush(fp);
			break;
		  case lyramilk::log::error:
			if(!*enable_log_error)return;
			cache.append(pidstr);
			cache.append(str_error);
			cache.append(strtime(ti));
			cache.append(" [");
			cache.append(module);
			cache.append("] ");
			cache.append(str);
			fwrite(cache.c_str(),cache.size(),1,fp);
			fflush(fp);
			break;
		}
	}
};
teapoy_log_base* logger = nullptr;




class redis_proxy_server:public lyramilk::netio::aioserver<lyramilk::mudis::redis_proxy>
{
};

void useage(lyramilk::data::string selfname)
{
	std::cout << "useage:" << selfname << " [optional] <file>" << std::endl;
	std::cout << "version: " << MUDIS_VERSION << std::endl;
	std::cout << "\t-c <file>\t" << "use config file <file>" << std::endl;
	std::cout << "\t-d       \t" << "start as daemon" << std::endl;
	std::cout << "\t-p <file>\t" << "use pid file <file>" << std::endl;
	std::cout << "\t-l <file>\t" << "use log file <file>" << std::endl;
	std::cout << "\t-s <start|reload|stop>\t"  << std::endl;
}

int main(int argc,char* argv[])
{
	lyramilk::log::logss log(lyramilk::klog,"mudis.main");

	bool isdaemon = false;
	lyramilk::data::string configure_file;
	lyramilk::data::string operate = "start";
	lyramilk::data::string selfname = argv[0];
	lyramilk::data::string pidfile;
	lyramilk::data::string logfile;

	{
		int oc;
		while((oc = getopt(argc, argv, "c:dp:s:?")) != -1){
			switch(oc)
			{
			  case 's':
				operate = optarg;
				break;
			  case 'd':
				isdaemon = true;
				break;
			  case 'c':
				configure_file = optarg;
				break;
			  case 'p':
				pidfile = optarg;
				break;
			  case 'l':
				logfile = optarg;
				break;
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

	signal(SIGPIPE, SIG_IGN);

	lyramilk::data::var vconf;

	if(configure_file.compare(configure_file.size()-5,configure_file.size(),".json") == 0){
		std::ifstream ifs;
		ifs.open(configure_file.c_str(),std::ifstream::binary|std::ifstream::in);
		lyramilk::data::json j(vconf);
		ifs >> j;
		ifs.close();
		log(lyramilk::log::trace) << D("加载json配置文件%s成功。",configure_file.c_str()) << std::endl;
	}else if(configure_file.compare(configure_file.size()-5,configure_file.size(),".yaml") == 0){
		std::ifstream ifs;
		ifs.open(configure_file.c_str(),std::ifstream::binary|std::ifstream::in);

		lyramilk::data::array ar;
		lyramilk::data::yaml j(ar);
		ifs >> j;
		ifs.close();

		if(ar.size() > 0){
			vconf = ar[0];
		}
		log(lyramilk::log::trace) << D("加载yaml配置文件%s成功。",configure_file.c_str()) << std::endl;
	}



	if(lyramilk::data::var::t_map != vconf.type()){
		log(lyramilk::log::error) << D("加载配置文件%s出现错误。",configure_file.c_str()) << std::endl;
		return -2;
	}

	lyramilk::data::string emptystr;
	/* 配置 server */
	int threads_count = vconf.path("/server/thread").conv(1);
	unsigned short port = vconf.path("/server/port").conv(6379);
	lyramilk::data::string host = vconf.path("/server/host").conv(emptystr);
	if(pidfile.empty()){
		pidfile = vconf.path("/server/pidfile").conv(emptystr);
	}

	if(logfile.empty()){
		logfile = vconf.path("/server/logfile").conv(emptystr);
	}

	if(!host.empty()){
		lyramilk::klog(lyramilk::log::trace,"mudis.main") << D("IP:%s",host.c_str()) << std::endl;
	}

	lyramilk::klog(lyramilk::log::trace,"mudis.main") << D("端口:%d",port) << std::endl;
	lyramilk::klog(lyramilk::log::trace,"mudis.main") << D("线程数:%d",threads_count) << std::endl;
	if(!pidfile.empty()){
		lyramilk::klog(lyramilk::log::trace,"mudis.main") << D("PID文件:%s",pidfile.c_str()) << std::endl;
	}





	if(isdaemon){
		log(lyramilk::log::trace) << D("即将以守护进程方式方式启动。",configure_file.c_str()) << std::endl;
		::daemon(0,0);

		logswitch["debug"] = true;
		logswitch["trace"] = true;
		logswitch["warning"] = true;
		logswitch["error"] = true;
		if(!logfile.empty()){
			teapoy_log_base* logbase = new teapoy_log_logfile(logfile);
			if(logbase && logbase->ok()){
				if(logger)delete logger;
				logger = logbase;
				log(lyramilk::log::debug,__FUNCTION__) << D("切换日志成功") << std::endl;
			}else{
				log(lyramilk::log::error,__FUNCTION__) << D("切换日志失败:%s",logfile.c_str()) << std::endl;
			}
		}
	}else{
		log(lyramilk::log::debug,__FUNCTION__) << D("控制台模式，自动忽略日志文件。") << std::endl;
	}

	lyramilk::proc::pidfile pf(pidfile);
	if(!pidfile.empty()){
		pidfile = vconf.path("/server/pidfile").conv(emptystr);
	}

	char pipe_file_name[PATH_MAX] = {0};
	snprintf(pipe_file_name,sizeof(pipe_file_name),"/var/run/mudis_%u.pipe",port);
	mkfifo(pipe_file_name,0777);

	if(operate == "reload"){
		int fd = open(pipe_file_name, O_WRONLY | O_NONBLOCK);
		if(fd != -1){
			char buff[] = "mudis:reload";
			write(fd,buff,sizeof(buff));
			close(fd);
		}
	}


	/* 开始服务 */
	redis_proxy_server* ins = new redis_proxy_server;
	bool isok = false;
	if(host.empty()){
		for(int i=0;i<3;++i){
			if(ins->open(port)){
				isok = true;
				break;
			}
			sleep(1);
		}
	}else{
		for(int i=0;i<3;++i){
			if(ins->open(host,port)){
				isok = true;
				break;
			}
			sleep(1);
		}
	}

	lyramilk::mudis::redis_strategy_master::instance()->load_config(vconf);

	lyramilk::io::aiopoll_safe pool(threads_count);
	pool.add_to_thread(0,ins,EPOLLIN);
	while(pool.get_fd_count() > 0){
		if(lyramilk::mudis::redis_strategy_master::instance()->leave && ins){
			pool.remove_on_thread(0,ins);
			delete ins;
			ins = nullptr;
		}else if(ins){
			int fd = open(pipe_file_name, O_RDONLY);
			char buff[4096];
			read(fd,buff,4096);
			close(fd);
			if(strstr(buff,"mudis:reload")){
				lyramilk::mudis::redis_strategy_master::instance()->leave = true;
				pf.detach();
				log(lyramilk::log::error,__FUNCTION__) << D("重载中，进程即将退出。") << std::endl;
			}
		}else{
			sleep(1);
		}
	}
	log(lyramilk::log::error,__FUNCTION__) << D("进程退出") << std::endl;
	return 0;
}
