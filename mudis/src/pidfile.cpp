#include "pidfile.h"
#include <fcntl.h>
#include <stdlib.h>
#include <sys/file.h>
#include <unistd.h>

namespace lyramilk{ namespace proc
{
	pidfile::pidfile()
	{
	}

	bool pidfile::good()
	{
		return fd >= 0;
	}

	pidfile::~pidfile()
	{
		if(fd >= 0){
			close(fd);
			unlink(filename.c_str());
			filename.clear();
		}
	}



	void pidfile::destroy(pidfile* pf)
	{
		delete pf;
	}

	pidfile* pidfile::create(const lyramilk::data::string& filename)
	{
		int fd = open(filename.c_str(),O_RDWR| O_CREAT,0666);
		if (fd >= 0) {
			int ret = flock(fd, LOCK_EX | LOCK_NB);
			if(ret == 0){
				char buff[64];
				int r = snprintf(buff,sizeof(buff),"%llu\n",(unsigned long long)getpid());
				write(fd,buff,r);
				pidfile* pf = new pidfile;
				pf->filename = filename;
				pf->fd = fd;
				return pf;
			}
			close(fd);
		}

		return nullptr;
	}


	bool pidfile::lookup(const lyramilk::data::string& filename,pid_t* pid)
	{
		if(pid == nullptr) return false;
	
		FILE* fp = fopen(filename.c_str(),"r");
		if(fp){
			char pid_buff[64] = {0};
			fread(pid_buff,1,sizeof(pid_buff),fp);
			*pid = atoi(pid_buff);
			fclose(fp);
		}

		return nullptr;
	}

}}
