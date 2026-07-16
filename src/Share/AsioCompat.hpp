#pragma once

#include <utility>

#include <boost/asio/buffer.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/io_context_strand.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/multicast.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/system/error_code.hpp>

namespace wt_asio
{
namespace ip = boost::asio::ip;
namespace placeholders = boost::asio::placeholders;

using io_context = boost::asio::io_context;
using io_strand = boost::asio::io_context::strand;
using udp_endpoint = boost::asio::ip::udp::endpoint;
using udp_socket = boost::asio::ip::udp::socket;
using tcp_endpoint = boost::asio::ip::tcp::endpoint;
using tcp_socket = boost::asio::ip::tcp::socket;
using socket_base = boost::asio::socket_base;
using error_code = boost::system::error_code;

using boost::asio::buffer;
using boost::asio::make_work_guard;
using boost::asio::post;
using boost::asio::read;
}
