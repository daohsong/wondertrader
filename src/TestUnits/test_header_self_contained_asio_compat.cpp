#include "../Share/AsioCompat.hpp"
#include "gtest/gtest/gtest.h"

TEST(test_header_self_contained_asio_compat, compiles_when_included_first)
{
	wt_asio::io_context ioService;
	wt_asio::udp_endpoint endpoint(wt_asio::ip::udp::v4(), 0);
	wt_asio::udp_socket socket(ioService);

	EXPECT_EQ(0u, endpoint.port());
}
