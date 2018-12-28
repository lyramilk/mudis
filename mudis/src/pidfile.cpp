#include "pidfile.h"

namespace lyramilk{ namespace proc
{
	pidfile::pidfile(const lyramilk::data::string& pf)
	{
		FILE *fp = fopen(pf.c_str(),"w");
		if (fp) {
			fprintf(fp,"%d\n",(int)getpid());
			fclose(fp);
			this->pf = pf;
		}
	}

	pidfile::~pidfile()
	{
		unlink(this->pf.c_str());
	}



}}
