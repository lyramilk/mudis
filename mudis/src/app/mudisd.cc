#include <fstream>
#include "redis_proxy.h"
#include "strategy.h"
#include <libmilk/var.h>
#include <libmilk/json.h>
#include <libmilk/log.h>
#include <libmilk/dict.h>
#include <unistd.h>
#include <signal.h>


class redis_proxy_server:public lyramilk::netio::aioserver<lyramilk::mudis::redis_proxy>
{
};


int main(int argc,const char* argv[])
{
	std::cout << "正在启动" << argv[1] << std::endl;
	if(argc < 2){
		std::cout << "missing argument" << std::endl;
		return -1;
	}

	
	signal(SIGPIPE, SIG_IGN);

	lyramilk::data::var vconf;

	{
		std::cout << "use configure file " << argv[1] << std::endl;
		std::ifstream ifs;
		ifs.open(argv[1],std::ifstream::binary|std::ifstream::in);
		lyramilk::data::json j(vconf);
		ifs >> j;
		ifs.close();
		std::cout << "load configure ok " << argv[1] << std::endl;
	}
	if(lyramilk::data::var::t_map != vconf.type()){
		std::cout << "configure file error" << std::endl;
		return -2;
	}

	/* 配置 server */
	int threads_count = vconf.path("/server/thread").conv(1);
	bool runasdaemon = vconf.path("/server/daemon").conv(false);
	unsigned short port = vconf.path("/server/port").conv(6379);

	lyramilk::klog(lyramilk::log::trace,"mudis.main") << D("端口:%d",port) << std::endl;
	lyramilk::klog(lyramilk::log::trace,"mudis.main") << D("线程数:%d",threads_count) << std::endl;
	lyramilk::klog(lyramilk::log::trace,"mudis.main") << D("以守护模式运行:%s",runasdaemon?"是":"否") << std::endl;

	if(runasdaemon){
		::daemon(0,0);
	}


	lyramilk::mudis::redis_strategy_master::instance()->load_config(vconf);

	/* 开始服务 */
	lyramilk::io::aiopoll_safe pool(threads_count);

	redis_proxy_server ins;
	ins.open(port);
	pool.add(&ins);

	while(true){
		sleep(10);
	}

	return 0;
}
