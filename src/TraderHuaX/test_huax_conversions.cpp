#include "HuaXConversions.hpp"

#include <limits>

int main()
{
	int destination = -1;
	if (!wt::huax::tryConvertOrderVolume(0.0, destination) || destination != 0)
		return 1;
	if (!wt::huax::tryConvertOrderVolume(1.0, destination) || destination != 1)
		return 2;

	const int maximum = (std::numeric_limits<int>::max)();
	if (!wt::huax::tryConvertOrderVolume(static_cast<double>(maximum), destination)
		|| destination != maximum)
		return 3;

	if (wt::huax::tryConvertOrderVolume(-1.0, destination))
		return 4;
	if (wt::huax::tryConvertOrderVolume(1.5, destination))
		return 5;
	if (wt::huax::tryConvertOrderVolume(static_cast<double>(maximum) + 1.0, destination))
		return 6;
	if (wt::huax::tryConvertOrderVolume((std::numeric_limits<double>::infinity)(), destination))
		return 7;
	if (wt::huax::tryConvertOrderVolume((std::numeric_limits<double>::quiet_NaN)(), destination))
		return 8;

	return 0;
}
