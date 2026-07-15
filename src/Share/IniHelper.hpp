/*!
 * \file IniHelper.hpp
 * \project	WonderTrader
 *
 * \author Wesley
 * \date 2020/03/30
 * 
 * \brief Ini文件辅助类,利用boost的property_tree来实现,可以跨平台使用
 */
#pragma once

#include <string>
#include <vector>
#include <map>

#include <boost/property_tree/ptree.hpp>  
#include <boost/property_tree/ini_parser.hpp>

typedef std::vector<std::string>			FieldArray;
typedef std::map<std::string, std::string>	FieldMap;

class IniHelper
{
private:
	boost::property_tree::ptree	_root;
	std::string					_fname;
	bool						_loaded;

	static bool buildPath(const char* section, const char* key, std::string& path)
	{
		if (section == nullptr || key == nullptr || section[0] == '\0' || key[0] == '\0')
			return false;

		path.reserve(std::char_traits<char>::length(section) + std::char_traits<char>::length(key) + 1);
		path.assign(section);
		path.push_back('.');
		path.append(key);
		return true;
	}

	template<class T>
	T readSectionValue(const char* section, const char* key, T defaultValue)
	{
		std::string path;
		if (!buildPath(section, key, path))
			return defaultValue;
		return readValue<T>(path.c_str(), defaultValue);
	}

	template<class T>
	void writeSectionValue(const char* section, const char* key, T value)
	{
		std::string path;
		if (!buildPath(section, key, path))
			return;
		writeValue<T>(path.c_str(), value);
	}

public:
	IniHelper(): _loaded(false){}

	void	load(const char* szFile)
	{
		_fname = szFile;
		try
		{
			boost::property_tree::ini_parser::read_ini(szFile, _root);
		}
		catch(...)
		{

		}
		
		_loaded = true;
	}

	void	save(const char* filename = "")
	{
		if (strlen(filename) > 0)
			boost::property_tree::ini_parser::write_ini(filename, _root);
		else
			boost::property_tree::ini_parser::write_ini(_fname.c_str(), _root);
	}

	inline bool isLoaded() const{ return _loaded; }

public:
	void	removeValue(const char* szSec, const char* szKey)
	{
		try
		{
			boost::property_tree::ptree& sec = _root.get_child(szSec);
			sec.erase(szKey);
		}
		catch (...)
		{
			
		}
	}

	void	removeSection(const char* szSec)
	{
		try
		{
			_root.erase(szSec);
		}
		catch (...)
		{

		}
	}

	template<class T>
	T	readValue(const char* szPath, T defVal)
	{
		try
		{
			return _root.get<T>(szPath, defVal);
		}
		catch (...)
		{
			return defVal;
		}
	}

	std::string	readString(const char* szSec, const char* szKey, const char* defVal = "")
	{
		return readSectionValue<std::string>(szSec, szKey, defVal == nullptr ? "" : defVal);
	}

	int			readInt(const char* szSec, const char* szKey, int defVal = 0)
	{
		return readSectionValue<int>(szSec, szKey, defVal);
	}

	uint32_t	readUInt(const char* szSec, const char* szKey, uint32_t defVal = 0)
	{
		return readSectionValue<uint32_t>(szSec, szKey, defVal);
	}

	bool		readBool(const char* szSec, const char* szKey, bool defVal = false)
	{
		return readSectionValue<bool>(szSec, szKey, defVal);
	}

	double		readDouble(const char* szSec, const char* szKey, double defVal = 0.0)
	{
		return readSectionValue<double>(szSec, szKey, defVal);
	}

	int			readSections(FieldArray &aySection)
	{
		for (auto it = _root.begin(); it != _root.end(); it++)
		{
			aySection.emplace_back(it->first.data());
		}

		return (int)_root.size();
	}

	int			readSecKeyArray(const char* szSec, FieldArray &ayKey)
	{
		try
		{
			const boost::property_tree::ptree& _sec = _root.get_child(szSec);
			for (auto it = _sec.begin(); it != _sec.end(); it++)
			{
				ayKey.emplace_back(it->first.data());
			}

			return (int)_sec.size();
		}
		catch (...)
		{
			return 0;
		}
		
	}

	int			readSecKeyValArray(const char* szSec, FieldArray &ayKey, FieldArray &ayVal)
	{
		try
		{
			const boost::property_tree::ptree& _sec = _root.get_child(szSec);
			for (auto it = _sec.begin(); it != _sec.end(); it++)
			{
				ayKey.emplace_back(it->first.data());
				ayVal.emplace_back(it->second.data());
			}

			return (int)_sec.size();
		}
		catch (...)
		{
			return 0;
		}
	}

	template<class T>
	void		writeValue(const char* szPath, T val)
	{
		_root.put<T>(szPath, val);
	}

	void		writeString(const char* szSec, const char* szKey, const char* val)
	{
		if (val != nullptr)
			writeSectionValue<std::string>(szSec, szKey, val);
	}

	void		writeInt(const char* szSec, const char* szKey, int val)
	{
		writeSectionValue<int>(szSec, szKey, val);
	}

	void		writeUInt(const char* szSec, const char* szKey, uint32_t val)
	{
		writeSectionValue<uint32_t>(szSec, szKey, val);
	}

	void		writeBool(const char* szSec, const char* szKey, bool val)
	{
		writeSectionValue<bool>(szSec, szKey, val);
	}

	void		writeDouble(const char* szSec, const char* szKey, double val)
	{
		writeSectionValue<double>(szSec, szKey, val);
	}
};
