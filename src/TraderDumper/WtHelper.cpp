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

std::string WtHelper::_bin_dir;

const char* WtHelper::get_cwd()
{
	static std::string _cwd;
	if(_cwd.empty())
	{
		_cwd = StrUtil::standardisePath(wt_current_working_directory());
	}	
	return _cwd.c_str();
}
