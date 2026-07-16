/*!
 * \file WtHelper.cpp
 * \project	WonderTrader
 *
 * \author Wesley
 * \date 2020/03/30
 * 
 * \brief 
 */
#include "WtHelper.h"

#include "../Share/CurrentDirCompat.hpp"
#include "../Share/StrUtil.hpp"
#include <filesystem>
namespace fs = std::filesystem;

std::string WtHelper::_inst_dir;
std::string WtHelper::_out_dir = "./outputs_bt/";

std::string WtHelper::getCWD()
{
	static std::string _cwd;
	if(_cwd.empty())
	{
		_cwd = StrUtil::standardisePath(wt_current_working_directory());
	}	
	return _cwd;
}

void WtHelper::setOutputDir(const char* out_dir)
{
	_out_dir = StrUtil::standardisePath(std::string(out_dir));
}

const char* WtHelper::getOutputDir()
{
	if (!fs::exists(_out_dir.c_str()))
        fs::create_directories(_out_dir.c_str());
	return _out_dir.c_str();
}
