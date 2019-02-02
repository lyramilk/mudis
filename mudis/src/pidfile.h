#ifndef _lyramilk_util_proc_h_
#define _lyramilk_util_proc_h_

#include <libmilk/var.h>


namespace lyramilk{ namespace proc
{
	class pidfile
	{
		lyramilk::data::string filename;
		int fd;
		pidfile();
	  public:
		bool good();
	  	virtual ~pidfile();

		static void destroy(pidfile* pf);
		static pidfile* create(const lyramilk::data::string& filename);
		static bool lookup(const lyramilk::data::string& filename,pid_t* pid);
	};
}}
#endif
