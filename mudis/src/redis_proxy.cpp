#include "redis_proxy.h"
#include <libmilk/stringutil.h>

namespace lyramilk{ namespace mudis
{


	redis_session::redis_session()
	{
		array_item_count = 0;
		bulk_bytes_count = 0;
		s = s_0;
	}

	redis_session::~redis_session()
	{
	}

	redis_session::result_status redis_session::parsing(char c,void* userdata)
	{
		switch(s){
		case s_0:
			if(c == '+') s = s_str_0;
			else if(c == '-') s = s_err_0;
			else if(c == ':') s = s_num_0;
			else if(c == '$') s = s_bulk_0;
			else if(c == '*') s = s_array_0;
			else if(c == '4'){
				s = s_ssdb_str_0;
				is_ssdb = true;
				tmpstr.push_back(c);
			}else{
				s = s_str_0;
				tmpstr.push_back(c);
			}
			break;
		case s_str_0:
		case s_err_0:
		case s_num_0:
		case s_array_0:
		case s_bulk_0:
			if(c =='\r') s = (stream_status)(s + 1);
			else tmpstr.push_back(c);
			break;
		case s_str_cr:
			if(c =='\n'){
				data.push_back(tmpstr);
				tmpstr.clear();
				if(array_item_count > 0){
					--array_item_count;
				}
				s = s_0;
			}else return rs_parse_error;
			break;
		case s_err_cr:
			if(c =='\n'){
				data.push_back(tmpstr);
				tmpstr.clear();
				if(array_item_count > 0){
					--array_item_count;
				}
				s = s_0;
			}else return rs_parse_error;
			break;
		case s_num_cr:
			if(c =='\n'){
				char* p;
				lyramilk::data::int64 i = strtoll(tmpstr.c_str(),&p,10);
				data.push_back(i);
				tmpstr.clear();
				if(array_item_count > 0){
					--array_item_count;
				}
				s = s_0;
			}else return rs_parse_error;
			break;
		case s_bulk_cr:
			if(c =='\n'){
				char* p;
				bulk_bytes_count = strtoll(tmpstr.c_str(),&p,10);
				tmpstr.clear();
				s = s_bulk_data;
			}else return rs_parse_error;
			break;
		case s_array_cr:
			if(c =='\n'){
				char* p;
				array_item_count = strtoll(tmpstr.c_str(),&p,10);
				tmpstr.clear();
				s = s_0;
			}else return rs_parse_error;
			break;
		case s_bulk_data:
			if(bulk_bytes_count == 0){
				if(c == '\r') s = s_bulk_data_cr;
				else return rs_parse_error;
			}else{
				--bulk_bytes_count;
				tmpstr.push_back(c);
			}
			break;
		case s_bulk_data_cr:
			if(c == '\n'){
				data.push_back(tmpstr);
				tmpstr.clear();
				if(array_item_count > 0){
					--array_item_count;
				}
				s = s_0;
			}else return rs_parse_error;
			break;
		case s_ssdb_s0:
			if(c =='\n'){
				result_status r = notify_cmd(data,userdata);
				data.clear();
				return r;
			}
			s = s_ssdb_str_0;
		case s_ssdb_str_0:
			if(c =='\r'){
				s = s_ssdb_str_cr;
				break;
			}else if(c == '\n'){
				//流转到下一状态
			}else{
				tmpstr.push_back(c);
				break;
			}
		case s_ssdb_str_cr:
			if(c =='\n'){
				char* p;
				bulk_bytes_count = strtoll(tmpstr.c_str(),&p,10);
				tmpstr.clear();
				s = s_ssdb_str_data;
			}else return rs_parse_error;
			break;
		case s_ssdb_str_data:
			if(bulk_bytes_count == 0){
				if(c == '\r'){
					s = s_ssdb_str_data_cr;
					break;
				}else if(c == '\n'){
					//流转到下一状态
				}else return rs_parse_error;
			}else{
				--bulk_bytes_count;
				tmpstr.push_back(c);
				break;
			}
		case s_ssdb_str_data_cr:
			if(c == '\n'){
				data.push_back(tmpstr);
				tmpstr.clear();
				s = s_ssdb_s0;
			}else return rs_parse_error;
			break;
		default:
			if(c >= '0' && c <= '9'){
				s = s_ssdb_str_0;
				tmpstr.push_back(c);
				break;
			}
			return rs_parse_error;
		}
		if(s == s_0 && array_item_count == 0){
			result_status r = notify_cmd(data,userdata);
			data.clear();
			return r;
		}
		return rs_continue;
	}
	redis_session::result_status redis_session::parsing(const char* cache, int size,int* bytesused,void* userdata)
	{
		result_status r = rs_continue;
		int i = 0;
		for(const char *p = cache;p<cache+size && r == rs_continue;++p){
			r = parsing(*p,userdata);
			++i;
		}
		*bytesused = i;
		return r;
	}


	//
	bool redis_session::parse(lyramilk::data::istream& is,lyramilk::data::var& v,bool* onerr)
	{
		*onerr = false;
		char c;
		for(is.get(c);is && (c == '\r' || c == '\n');is.get(c));
		if(is){
			switch(c){
			  case '*':{
					lyramilk::data::string slen;
					slen.reserve(256);
					while(is.get(c)){
						if(c == '\r') continue;
						if(c == '\n') break;
						slen.push_back(c);
					}

					lyramilk::data::var len = slen;
					int ilen = len;
					v.type(lyramilk::data::var::t_array);

					lyramilk::data::array& ar = v;
					ar.resize(ilen);

					lyramilk::data::var* e = ar.data();
					for(int i=0;i<ilen;++i,++e){
						parse(is,*e,onerr);
					}
					return true;
				}
				break;
			  case '$':{
					lyramilk::data::string slen;
					slen.reserve(256);
					while(is.get(c)){
						if(c == '\r') continue;
						if(c == '\n') break;
						slen.push_back(c);
					}
					if(slen == "-1") return true;
					lyramilk::data::var len = slen;
					int ilen = len;
					lyramilk::data::string buf;
					buf.resize(ilen);
					is.read((char*)buf.c_str(),ilen);
					buf.erase(buf.begin() + is.gcount(),buf.end());
					v = buf;
					char buff[2];
					is.read(buff,2);
					return true;
				}
				break;
			  case '+':{
					lyramilk::data::string str;
					str.reserve(4096);
					while(is.get(c)){
						if(c == '\r') continue;
						if(c == '\n') break;
						str.push_back(c);
					}
					v = str;
					return true;
				}
				break;
			  case '-':{
					lyramilk::data::string str;
					str.reserve(4096);
					while(is.get(c)){
						if(c == '\r') continue;
						if(c == '\n') break;
						str.push_back(c);
					}
					v = str;
					*onerr = true;
					return true;
				}
				break;
			  case ':':{
					lyramilk::data::string str;
					str.reserve(4096);
					while(is.get(c)){
						if(c == '\r') continue;
						if(c == '\n') break;
						str.push_back(c);
					}

					/* 断言这个数字恒为非负整数，如果不是需要修改代码。 */
					v = str;
					v.type(lyramilk::data::var::t_int);
					return true;
				}
				break;
			  default:
				return false;
			}
		}
		return false;
	}

	bool inline parse_ssdb(std::istream& is,lyramilk::data::strings& ret)
	{
		lyramilk::data::string str;
		str.reserve(512);
		char c = 0;
		while(is.good()){
			while(is.good()){
				c = is.get();
label_bodys:
				if(c >= '0' && c <= '9'){
					str.push_back(c);
				}
				if((c == '\r' && '\n' == is.get()) || c == '\n'){
					break;
				}
			}
			char* p;
			lyramilk::data::uint64 sz = strtoull(str.c_str(),&p,10);
			if(sz>0 || (str.size() > 0 && str[0] == '0')){
				str.clear();
				while(sz>0){
					str.push_back(is.get());
					--sz;
				}
				ret.push_back(str);
				c = is.get();
				if((c == '\r' && '\n' == is.get()) || c == '\n'){
					c = is.get();
					if(c >= '0' && c <= '9'){
						str.clear();
						goto label_bodys;
					}
					if((c == '\r' && '\n' == is.get()) || c == '\n'){
						return true;
					}
					//log(lyramilk::log::error,"parse") << D("ssdb 错误：响应格式错误%d",1) << std::endl;
					return false;
				}
				//log(lyramilk::log::error,"parse") << D("ssdb 错误：响应格式错误%d",2) << std::endl;
				return false;
			}
			//log(lyramilk::log::error,"parse") << D("ssdb 错误：响应格式错误str=") << str << std::endl;
			return false;
		}
		//log(lyramilk::log::error,"parse") << D("ssdb 错误：响应格式错误%d",4) << std::endl;
		//throw lyramilk::exception(D("ssdb 错误：响应格式错误%d",(unsigned int)c));
		return false;
	}

	lyramilk::data::var redis_session::exec_redis(lyramilk::netio::client& c,const lyramilk::data::array& cmd,bool* onerr)
	{
		lyramilk::data::array::const_iterator it = cmd.begin();

		{
			lyramilk::netio::socket_ostream ss(&c);
			ss << "*" << cmd.size() << "\r\n";
			for(;it!=cmd.end();++it){
				lyramilk::data::string str = it->str();
				ss << "$" << str.size() << "\r\n";
				ss << str << "\r\n";
			}
			ss.flush();
		}

		bool err = false;
		lyramilk::data::var ret;

		lyramilk::data::stringstream iss;
		while(c.check_read(20)){
			char buff[4096];
			int r = c.read(buff,4096);
			if(r > 0){
				iss.write(buff,r);
			}else{
				break;
			}
		}

		bool suc = parse(iss,ret,&err);

		if(suc) return ret;
		return lyramilk::data::var::nil;
	}

	lyramilk::data::strings redis_session::exec_ssdb(lyramilk::netio::client& c,const lyramilk::data::array& cmd,bool* onerr)
	{
		lyramilk::data::array::const_iterator it = cmd.begin();

		{
			lyramilk::netio::socket_ostream ss(&c);
			for(;it!=cmd.end();++it){
				lyramilk::data::string str = it->str();
				ss << str.size() << "\n";
				ss << str << "\n";
			}
			ss << "\n";
			ss.flush();
		}

		lyramilk::data::strings ret;

		lyramilk::data::stringstream iss;
		while(c.check_read(20)){
			char buff[4096];
			int r = c.read(buff,4096);
			if(r > 0){
				iss.write(buff,r);
			}else{
				break;
			}
		}

		bool suc = parse_ssdb(iss,ret);

		if(suc) return ret;
		return lyramilk::data::strings();
	}


	// redis_proxy

	redis_proxy::redis_proxy()
	{
		strategy = nullptr;
		group = nullptr;
	}

	redis_proxy::~redis_proxy()
	{
		if(group && strategy){
			group->destory(strategy);
		}
	}

	bool redis_proxy::onrequest(const char* cache, int size,int* bytesused, lyramilk::data::ostream& os)
	{
		if(strategy){
			return strategy->onrequest(cache,size,bytesused,os);
		}

		result_status r = parsing(cache,size,bytesused,&os);
		if(r == rs_error || r == rs_parse_error){
			return false;
		}
		return true;
	}

	redis_proxy::result_status redis_proxy::notify_cmd(const lyramilk::data::var::array& cmd, void* userdata)
	{
		if(cmd.size() < 1){
			return redis_proxy::rs_error;
		}

		std::ostream& os = *(std::ostream*)userdata;
		lyramilk::data::string scmd = lyramilk::data::lower_case(cmd[0].str());

		if(scmd == "auth"){
			if(cmd.size() != 2){
				if(is_ssdb){
					lyramilk::data::string err = "client_error";
					lyramilk::data::string msg = "wrong number of arguments";
					os << err.size() << "\n" << err << "\n" << msg.size() << "\n" << msg << "\n\n";
				}else{
					os << "-ERR wrong number of arguments for '" << cmd[0].str() << "' command\r\n";
				}
				os.flush();
				return redis_proxy::rs_ok;
			}
			lyramilk::data::string password = cmd[1].str();
			group = redis_strategy_master::instance()->get_by_groupname(password);
			if(group){
				strategy = group->create(is_ssdb);
				if(strategy){
					if(strategy->onauth(os,this)){
						return redis_proxy::rs_ok;
					}
					return redis_proxy::rs_error;
				}
			}


			if(is_ssdb){
				lyramilk::data::string err = "error";
				lyramilk::data::string msg = "invalid password";
				os << err.size() << "\n" << err << "\n" << msg.size() << "\n" << msg << "\n\n";
			}else{
				os << "-ERR invalid password\r\n";
			}
			return redis_proxy::rs_ok;
		}else{
			if(is_ssdb){
				lyramilk::data::string err = "noauth";
				lyramilk::data::string msg = "authentication required.";
				os << err.size() << "\n" << err << "\n" << msg.size() << "\n" << msg << "\n\n";
			}else{
				os << "-NOAUTH authentication required.\r\n";
			}
		}
		return redis_proxy::rs_ok;
	}

}}