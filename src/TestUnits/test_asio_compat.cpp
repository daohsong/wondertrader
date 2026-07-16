#include "../Share/AsioCompat.hpp"
#include "gtest/gtest/gtest.h"

#include <array>
#include <type_traits>

static_assert(std::is_same<wt_asio::io_context, boost::asio::io_context>::value,
	"io_context alias mismatch");
static_assert(std::is_same<wt_asio::io_strand, boost::asio::io_context::strand>::value,
	"io_strand alias mismatch");
static_assert(std::is_same<wt_asio::udp_endpoint, boost::asio::ip::udp::endpoint>::value,
	"udp_endpoint alias mismatch");
static_assert(std::is_same<wt_asio::udp_socket, boost::asio::ip::udp::socket>::value,
	"udp_socket alias mismatch");
static_assert(std::is_same<wt_asio::tcp_endpoint, boost::asio::ip::tcp::endpoint>::value,
	"tcp_endpoint alias mismatch");
static_assert(std::is_same<wt_asio::tcp_socket, boost::asio::ip::tcp::socket>::value,
	"tcp_socket alias mismatch");
static_assert(std::is_same<wt_asio::socket_base, boost::asio::socket_base>::value,
	"socket_base alias mismatch");
static_assert(std::is_same<wt_asio::error_code, boost::system::error_code>::value,
	"error_code alias mismatch");

TEST(test_asio_compat, exposes_expected_boost_asio_aliases_and_helpers)
{
	wt_asio::io_context ioService;
	wt_asio::io_strand strand(ioService);
	wt_asio::udp_endpoint udpEndpoint(wt_asio::ip::udp::v4(), 0);
	wt_asio::tcp_endpoint tcpEndpoint(wt_asio::ip::tcp::v4(), 0);
	wt_asio::udp_socket udpSocket(ioService);
	wt_asio::tcp_socket tcpSocket(ioService);
	wt_asio::error_code ec;
	std::array<char, 8> data = {};

	auto guard = wt_asio::make_work_guard(ioService);
	auto asioBuffer = wt_asio::buffer(data);
	wt_asio::post(strand, [] {});

	guard.reset();
	EXPECT_EQ(0u, udpEndpoint.port());
	EXPECT_EQ(0u, tcpEndpoint.port());
	EXPECT_FALSE(ec);
	EXPECT_EQ(data.size(), boost::asio::buffer_size(asioBuffer));
}
