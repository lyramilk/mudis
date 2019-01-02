#ifndef _lyramilk_util_proc_h_
#define _lyramilk_util_proc_h_

#include <libmilk/var.h>


namespace lyramilk{ namespace proc
{
	class pidfile
	{
		lyramilk::data::string pf;
	  public:
		pidfile(const lyramilk::data::string& pf);
	  	void detach();
	  	virtual ~pidfile();
	};
}}
#endif
