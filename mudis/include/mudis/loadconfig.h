#ifndef _lyramilk_util_loadconfig_h_
#define _lyramilk_util_loadconfig_h_

#include <libmilk/var.h>


namespace lyramilk{ namespace util
{
	lyramilk::data::var get_config_from_file(const lyramilk::data::string& filetype,const lyramilk::data::string& pathfilename = "");
}}
#endif
