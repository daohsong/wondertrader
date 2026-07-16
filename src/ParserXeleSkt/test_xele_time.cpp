#include "XeleTime.hpp"

#include <cstdint>
#include <limits>

int main()
{
	std::uint32_t baseDate = 0;
	std::uint32_t baseTime = 0;
	if (!xele::decodeSnapDateTime(0, baseDate, baseTime))
		return 1;

	std::uint32_t halfSecondDate = 0;
	std::uint32_t halfSecondTime = 0;
	if (!xele::decodeSnapDateTime(1, halfSecondDate, halfSecondTime))
		return 2;
	if (halfSecondDate != baseDate || halfSecondTime != baseTime + 500)
		return 3;

	std::uint32_t nextSecondDate = 0;
	std::uint32_t nextSecondTime = 0;
	if (!xele::decodeSnapDateTime(16, nextSecondDate, nextSecondTime))
		return 4;
	if (nextSecondDate != baseDate || nextSecondTime != baseTime + 1000)
		return 5;

	std::uint32_t invalidDate = 12345678;
	std::uint32_t invalidTime = 123456789;
	if (xele::decodeSnapDateTime((std::numeric_limits<std::uint64_t>::max)(),
		invalidDate, invalidTime))
		return 6;
	if (invalidDate != 12345678 || invalidTime != 123456789)
		return 7;

	return 0;
}
