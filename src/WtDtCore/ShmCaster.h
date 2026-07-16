#pragma once
#include "IDataCaster.h"
#include <stdint.h>
#include <cstddef>
#include <type_traits>
#include "../Includes/WTSStruct.h"
#include "../Share/BoostMappingFile.hpp"
#include "../Share/ShmWireLayout.hpp"

NS_WTP_BEGIN
class WTSVariant;
NS_WTP_END

USING_NS_WTP;

class ShmCaster : public IDataCaster
{
public:
	using DataItem = wt::shm_wire::CastDataItem;
	template <int N = 8 * 1024>
	using _DataQueue = wt::shm_wire::CastQueue<N>;
	using CastQueue = wt::shm_wire::DefaultCastQueue;

public:
	ShmCaster():_queue(NULL), _inited(false){}

	bool	init(WTSVariant* cfg);

	virtual void	broadcast(WTSTickData* curTick) override;
	virtual void	broadcast(WTSOrdQueData* curOrdQue) override;
	virtual void	broadcast(WTSOrdDtlData* curOrdDtl) override;
	virtual void	broadcast(WTSTransData* curTrans) override;

private:
	std::string		_path;
	typedef std::shared_ptr<BoostMappingFile> MappedFilePtr;
	MappedFilePtr	_mapfile;
	CastQueue*		_queue;
	bool			_inited;
};
