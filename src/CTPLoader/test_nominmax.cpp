#ifdef _WIN32
#include <windows.h>

#ifdef min
#error Windows min macro leaked into CTPLoader
#endif

#ifdef max
#error Windows max macro leaked into CTPLoader
#endif
#endif

#include "../FasterLibs/tsl/robin_map.h"

#include <limits>

int main()
{
	tsl::robin_map<int, int> values;
	values.emplace(1, (std::numeric_limits<int>::max)());
	return values.at(1) == (std::numeric_limits<int>::max)() ? 0 : 1;
}
