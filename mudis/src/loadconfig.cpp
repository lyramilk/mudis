#include "loadconfig.h"
#include <fstream>
#include <linux/limits.h>
#include <libmilk/json.h>
#include <libmilk/dict.h>
#include <libmilk/log.h>
#include <libmilk/yaml1.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
/*
#include <fcntl.h>
#include <stdlib.h>
#include <sys/file.h>
#include <unistd.h>
*/
namespace lyramilk{ namespace util
{
	lyramilk::data::var get_configv_from_file(const lyramilk::data::string& pathfilename,const lyramilk::data::string& _filetype);
	lyramilk::data::var pull_include_file(const lyramilk::data::string& pathfilename,lyramilk::data::var vconf);



	lyramilk::data::var get_configv_from_file(const lyramilk::data::string& pathfilename,const lyramilk::data::string& _filetype)
	{
		lyramilk::data::string realpathname;
		char buff[PATH_MAX] = {0};
		if(NULL == realpath(pathfilename.c_str(),buff)){
			lyramilk::klog(lyramilk::log::error,"lyramilk.get_config_from_file") << D("加载%s配置文件%s失败：%s",_filetype.empty()?"<unknow type>":_filetype.c_str(),pathfilename.c_str(),"获取文件路径失败") << std::endl;
			return lyramilk::data::var::nil;
		}
		realpathname = buff;

		lyramilk::data::string pathname;
		{
			std::size_t rdot = realpathname.rfind("/");
			if(rdot == pathfilename.npos){
				pathname = "/";
			}else{
				pathname = realpathname.substr(0,rdot);
			}
		}

		lyramilk::data::string filetype;
		if(_filetype.empty()){
			std::size_t rdot = pathfilename.rfind(".");
			if(rdot == pathfilename.npos){
				lyramilk::klog(lyramilk::log::error,"lyramilk.get_config_from_file") << D("加载%s配置文件%s失败：%s","<unknow type>",pathfilename.c_str(),"无法确定文件类型") << std::endl;
				return lyramilk::data::var::nil;
			}

			filetype = pathfilename.substr(rdot + 1);
		}else{
			filetype = _filetype;
		}

		if(filetype == "json"){
			lyramilk::data::var vconf;
			lyramilk::data::string filedata;
			{
				std::ifstream ifs;
				ifs.open(pathfilename.c_str(),std::ifstream::binary|std::ifstream::in);
				while(ifs){
					char buff[4096];
					ifs.read(buff,sizeof(buff));
					filedata.append(buff,ifs.gcount());
				}
				ifs.close();
			}

			if(!lyramilk::data::json::parse(filedata,&vconf)){
				vconf.clear();
			}
			if(vconf.type() == lyramilk::data::var::t_map){
				lyramilk::klog(lyramilk::log::trace,"lyramilk.get_config_from_file") << D("加载%s配置文件%s成功",filetype.c_str(),pathfilename.c_str()) << std::endl;
				return pull_include_file(pathname,vconf);
			}

			if(vconf.type() != lyramilk::data::var::t_invalid){
				lyramilk::klog(lyramilk::log::trace,"lyramilk.get_config_from_file") << D("加载%s配置文件%s成功",filetype.c_str(),pathfilename.c_str()) << std::endl;
				return vconf;
			}

			lyramilk::klog(lyramilk::log::error,"lyramilk.get_config_from_file") << D("加载%s配置文件%s失败：%s",filetype.c_str(),pathfilename.c_str(),"解析失败") << std::endl;
			return lyramilk::data::var::nil;
#ifdef USE_YAML
		}else if(filetype == "yaml"){
			lyramilk::data::var vconf;
			lyramilk::data::string filedata;
			{
				std::ifstream ifs;
				ifs.open(pathfilename.c_str(),std::ifstream::binary|std::ifstream::in);
				while(ifs){
					char buff[4096];
					ifs.read(buff,sizeof(buff));
					filedata.append(buff,ifs.gcount());
				}
				ifs.close();
			}

			try{
				lyramilk::data::array ar;
				if(lyramilk::data::yaml::parse(filedata + "\n...\nok\n",&ar)){
					if(ar.size() > 1 && ar[ar.size() - 1] == "ok"){
						//防止出现解析不完全的情况
						vconf = ar[0];
						if(vconf.type() == lyramilk::data::var::t_map){
							lyramilk::klog(lyramilk::log::trace,"lyramilk.get_config_from_file") << D("加载%s配置文件%s成功",filetype.c_str(),pathfilename.c_str()) << std::endl;
							return pull_include_file(pathname,vconf);
						}

						if(vconf.type() != lyramilk::data::var::t_invalid){
							lyramilk::klog(lyramilk::log::trace,"lyramilk.get_config_from_file") << D("加载%s配置文件%s成功",filetype.c_str(),pathfilename.c_str()) << std::endl;
							return vconf;
						}

						lyramilk::klog(lyramilk::log::error,"lyramilk.get_config_from_file") << D("加载%s配置文件%s失败：%s",filetype.c_str(),pathfilename.c_str(),"解析失败") << std::endl;
						return lyramilk::data::var::nil;
					}
				}
			}catch(std::exception& e){
				lyramilk::klog(lyramilk::log::error,"lyramilk.get_config_from_file") << D("加载%s配置文件%s失败：%s",filetype.c_str(),pathfilename.c_str(),e.what()) << std::endl;
				return lyramilk::data::var::nil;
			}
#endif
		}else{
			lyramilk::klog(lyramilk::log::error,"lyramilk.get_config_from_file") << D("加载%s配置文件%s失败：%s",filetype.c_str(),pathfilename.c_str(),"不支持该类型配置文件") << std::endl;
			return lyramilk::data::var::nil;
		}

		return lyramilk::data::var::nil;
	}

	lyramilk::data::var pull_include_file(const lyramilk::data::string& pathfilename,lyramilk::data::var vconf)
	{
		if(vconf.type() == lyramilk::data::var::t_map){
			lyramilk::data::map& m = vconf;
			lyramilk::data::map n;
			lyramilk::data::map::iterator it = m.begin();
			for(;it!=m.end();++it){
				if(it->first.size() > 0 && it->first[0] == '!' && it->second.type() == lyramilk::data::var::t_array){
					lyramilk::data::var vsubconfs;

					lyramilk::data::array& ar = it->second;
					for(lyramilk::data::array::iterator nit = ar.begin();nit!=ar.end();++nit){
						if(nit->type()!=lyramilk::data::var::t_str){
							return lyramilk::data::var::nil;
						}

						lyramilk::data::var vsubconfitem;

						lyramilk::data::string subrealpathfile;
						{
							lyramilk::data::string subpathfile = nit->str();
							if(subpathfile.size() > 1 && subpathfile[0] == '/'){
								subrealpathfile = subpathfile;
							}else{
								subrealpathfile = pathfilename + "/" + subpathfile;
							}
						}

						lyramilk::data::strings files;

						{
							struct stat st = {0};
							if(0 !=::stat(subrealpathfile.c_str(),&st)){
								return lyramilk::data::var::nil;
							}
							if(st.st_mode&S_IFDIR){
								DIR* dp = opendir(subrealpathfile.c_str());
								struct dirent* ep;
								for(ep = readdir(dp);ep!=nullptr;ep=readdir(dp)){
									if(ep->d_type == DT_REG){
										files.push_back(subrealpathfile + "/" + ep->d_name);
									}else if(ep->d_type != DT_DIR){
										lyramilk::klog(lyramilk::log::warning,"lyramilk.get_config_from_file") << D("配置文件%s类型%d可能不正确",(subrealpathfile + "/" + ep->d_name).c_str(),ep->d_type) << std::endl;
										files.push_back(subrealpathfile + "/" + ep->d_name);
									}else if(ep->d_type == DT_DIR){
									}
								}
								closedir(dp);
							}else{
								files.push_back(subrealpathfile);
							}

						}

						for(lyramilk::data::strings::iterator filesit = files.begin();filesit!=files.end();++filesit){
							lyramilk::data::string pathname;
							{
								std::size_t rdot = filesit->rfind("/");
								if(rdot == pathfilename.npos){
									pathname = "/";
								}else{
									pathname = filesit->substr(0,rdot);
								}
							}


							vsubconfitem = get_configv_from_file(*filesit,"");
							if(vsubconfitem.type() == lyramilk::data::var::t_invalid) return lyramilk::data::var::nil;
							if(vsubconfs.type() == lyramilk::data::var::t_invalid){
								vsubconfs = vsubconfitem;
							}else if(vsubconfs.type() == lyramilk::data::var::t_array && vsubconfitem.type() == lyramilk::data::var::t_array){
								lyramilk::data::array & ar = vsubconfs;
								lyramilk::data::array & subar = vsubconfitem;

								ar.insert(ar.end(),subar.begin(),subar.end());
							}else if(vsubconfs.type() == lyramilk::data::var::t_map && vsubconfitem.type() == lyramilk::data::var::t_map){
								lyramilk::data::map & m = vsubconfs;
								lyramilk::data::map & subm = vsubconfitem;
								for(lyramilk::data::map::iterator submit = subm.begin();submit!=subm.end();++submit){
									if(m.find(submit->first)!=m.end()) return lyramilk::data::var::nil;

									lyramilk::data::var v = pull_include_file(pathname,submit->second);
									if(v.type() == lyramilk::data::var::t_invalid) return lyramilk::data::var::nil;
									m[submit->first] = v;
								}
							}else{
								return lyramilk::data::var::nil;
							}
						}
					}

					n[it->first.substr(1)] = vsubconfs;
				}else if(it->second.type() == lyramilk::data::var::t_map){
					lyramilk::data::var vsubconfs = pull_include_file(pathfilename,it->second);
					n[it->first] = vsubconfs;
				}else{
					n[it->first] = it->second;
				}
			}
			return n;
		}



		return vconf;
	}



	lyramilk::data::var get_config_from_file(const lyramilk::data::string& pathfilename,const lyramilk::data::string& _filetype)
	{
		lyramilk::data::var v = get_configv_from_file(pathfilename,_filetype);
		if(v.type() != lyramilk::data::var::t_map){
			lyramilk::klog(lyramilk::log::error,"lyramilk.get_config_from_file") << D("加载%s配置文件%s失败：%s",_filetype.c_str(),pathfilename.c_str(),"解析失败") << std::endl;
			return lyramilk::data::var::nil;
		}
		return v;
	}

}}
