#include "../WtDtCore/ShmCaster.h"
#include "../ParserShm/ParserShm.h"
#include "../Share/AtomicCompat.hpp"
#include "../Share/BoostMappingFile.hpp"
#include "../Share/FilesystemCompat.hpp"
#include "../Includes/WTSDataDef.hpp"
#include "../Includes/WTSVariant.hpp"

#include "gtest/gtest/gtest.h"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#if !defined(_WIN32)
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
template<typename Queue>
using readable_field_t = std::remove_reference_t<decltype(std::declval<Queue&>()._readable)>;

template<typename Queue>
using writable_field_t = std::remove_reference_t<decltype(std::declval<Queue&>()._writable)>;

template<typename Queue>
using readable_value_t = std::remove_cv_t<readable_field_t<Queue>>;

template<typename Queue>
using writable_value_t = std::remove_cv_t<writable_field_t<Queue>>;

template <typename Queue>
bool queue_index_addresses_are_aligned(Queue& queue)
{
	constexpr std::size_t alignment = wt::atomic_required_alignment<uint64_t>();
	return wt::atomic_is_aligned(&queue._readable, alignment)
		&& wt::atomic_is_aligned(&queue._writable, alignment);
}

uint64_t test_load_relaxed(uint64_t& value)
{
	return wt::atomic_load_u64(value, std::memory_order_relaxed);
}

uint64_t test_load_acquire(uint64_t& value)
{
	return wt::atomic_load_u64(value, std::memory_order_acquire);
}

void test_store_relaxed(uint64_t& target, uint64_t value)
{
	wt::atomic_store_u64(target, value, std::memory_order_relaxed);
}

void test_store_release(uint64_t& target, uint64_t value)
{
	wt::atomic_store_u64(target, value, std::memory_order_release);
}

#if !defined(_WIN32)
class TempShmPath
{
public:
	explicit TempShmPath(const char* suffix)
	{
		_path = (wt::fs::temp_directory_path()
			/ (std::string("wondertrader_shmqueue_") + std::to_string(static_cast<unsigned long long>(::getpid()))
				+ "_" + suffix + ".dat")).string();
		wt::fs::remove(_path);
	}

	~TempShmPath()
	{
		wt::fs::remove(_path);
	}

	const std::string& str() const { return _path; }

private:
	std::string _path;
};

bool write_signal(int fd)
{
	const char signal = 'x';
	return ::write(fd, &signal, 1) == 1;
}

bool read_signal(int fd, int timeoutSeconds = 10)
{
	fd_set readSet;
	FD_ZERO(&readSet);
	FD_SET(fd, &readSet);
	timeval timeout = {timeoutSeconds, 0};
	const int selected = ::select(fd + 1, &readSet, nullptr, nullptr, &timeout);
	if (selected != 1)
		return false;
	char signal = 0;
	return ::read(fd, &signal, 1) == 1;
}

bool wait_for_u64_at_least(uint64_t& value, uint64_t expected)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (wt::atomic_load_u64(value, std::memory_order_acquire) >= expected)
			return true;
		std::this_thread::yield();
	}
	return false;
}

bool wait_for_u32(uint32_t& value, uint32_t expected)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (wt::atomic_load_u32(value, std::memory_order_acquire) == expected)
			return true;
		std::this_thread::yield();
	}
	return false;
}

bool initialize_caster(ShmCaster& caster, const std::string& path)
{
	WTSVariant* config = WTSVariant::createObject();
	config->append("active", true);
	config->append("path", path.c_str());
	const bool initialized = caster.init(config);
	config->release();
	return initialized;
}

double sequence_price(uint32_t sequence)
{
	return static_cast<double>(sequence) + 0.25;
}

void broadcast_sequence(ShmCaster& caster, uint32_t sequence)
{
	const uint32_t type = sequence % 4;
	if (type == 0)
	{
		WTSTickStruct value = {};
		value.price = sequence_price(sequence);
		WTSTickData* data = WTSTickData::create(value);
		caster.broadcast(data);
		data->release();
	}
	else if (type == 1)
	{
		WTSOrdQueStruct value = {};
		value.price = sequence_price(sequence);
		WTSOrdQueData* data = WTSOrdQueData::create(value);
		caster.broadcast(data);
		data->release();
	}
	else if (type == 2)
	{
		WTSOrdDtlStruct value = {};
		value.price = sequence_price(sequence);
		WTSOrdDtlData* data = WTSOrdDtlData::create(value);
		caster.broadcast(data);
		data->release();
	}
	else
	{
		WTSTransStruct value = {};
		value.price = sequence_price(sequence);
		WTSTransData* data = WTSTransData::create(value);
		caster.broadcast(data);
		data->release();
	}
}

bool item_matches_sequence(const ShmCaster::DataItem& item, uint32_t sequence)
{
	const uint32_t type = sequence % 4;
	if (item._type != type)
		return false;
	const double expected = sequence_price(sequence);
	if (type == 0)
		return item._tick.price == expected;
	if (type == 1)
		return item._queue.price == expected;
	if (type == 2)
		return item._order.price == expected;
	return item._trans.price == expected;
}

bool map_cast_queue(const std::string& path, BoostMappingFile& mapping, ShmCaster::CastQueue*& queue)
{
	try
	{
		if (!mapping.map(path.c_str()))
			return false;
	}
	catch (...)
	{
		return false;
	}
	if (wt::shm_wire::validate_cast_queue<8 * 1024>(mapping.addr(), mapping.size())
		!= wt::shm_wire::ValidationError::ok)
		return false;
	queue = static_cast<ShmCaster::CastQueue*>(mapping.addr());
	return true;
}

bool child_exited_successfully(pid_t pid)
{
	int status = 0;
	return ::waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

pid_t spawn_restart_producer(const std::string& path, uint32_t startSequence, int readyFd, int goFd)
{
	const pid_t pid = ::fork();
	if (pid != 0)
		return pid;

	ShmCaster caster;
	bool ok = initialize_caster(caster, path);
	if (ok)
		ok = write_signal(readyFd);
	if (ok)
		ok = read_signal(goFd);
	if (ok)
	{
		for (uint32_t offset = 0; offset < 16; ++offset)
			broadcast_sequence(caster, startSequence + offset);
	}
	::close(readyFd);
	::close(goFd);
	::_exit(ok ? 0 : 1);
}
#endif
}

TEST(test_compile_warnings_shmqueue, producer_and_consumer_castqueue_layouts_match)
{
	using ProducerQueue = ShmCaster::CastQueue;
	using ConsumerQueue = ParserShm::CastQueue;

	static_assert(std::is_same_v<ProducerQueue, ConsumerQueue>);
	static_assert(sizeof(ProducerQueue) == sizeof(ConsumerQueue));
	static_assert(offsetof(ProducerQueue, _wire) == 0);
	static_assert(offsetof(ProducerQueue, _capacity) == 32);
	static_assert(offsetof(ProducerQueue, _readable) == 40);
	static_assert(offsetof(ProducerQueue, _writable) == 48);
	static_assert(offsetof(ProducerQueue, _pid) == 56);
	static_assert(offsetof(ProducerQueue, _items) == 64);
	static_assert(offsetof(ProducerQueue, _capacity) == offsetof(ConsumerQueue, _capacity));
	static_assert(offsetof(ProducerQueue, _readable) == offsetof(ConsumerQueue, _readable));
	static_assert(offsetof(ProducerQueue, _writable) == offsetof(ConsumerQueue, _writable));
	static_assert(offsetof(ProducerQueue, _pid) == offsetof(ConsumerQueue, _pid));
	static_assert(offsetof(ProducerQueue, _items) == offsetof(ConsumerQueue, _items));
	static_assert(offsetof(ProducerQueue, _readable) % alignof(uint64_t) == 0);
	static_assert(offsetof(ProducerQueue, _writable) % alignof(uint64_t) == 0);
	static_assert(offsetof(ConsumerQueue, _readable) % alignof(uint64_t) == 0);
	static_assert(offsetof(ConsumerQueue, _writable) % alignof(uint64_t) == 0);
	static_assert(alignof(ProducerQueue) >= alignof(uint64_t));
	static_assert(alignof(ConsumerQueue) >= alignof(uint64_t));
	static_assert(alignof(readable_value_t<ProducerQueue>) == sizeof(uint64_t));
	static_assert(alignof(writable_value_t<ProducerQueue>) == sizeof(uint64_t));
	static_assert(alignof(readable_value_t<ConsumerQueue>) == sizeof(uint64_t));
	static_assert(alignof(writable_value_t<ConsumerQueue>) == sizeof(uint64_t));

	SUCCEED();
}

TEST(test_compile_warnings_shmqueue, versioned_wire_header_is_valid)
{
	auto queue = std::make_unique<ShmCaster::CastQueue>();
	EXPECT_EQ(wt::shm_wire::ValidationError::ok,
		wt::shm_wire::validate_cast_queue<8 * 1024>(queue.get(), sizeof(*queue)));
	EXPECT_EQ(wt::shm_wire::kCastQueueMagic, queue->_wire._magic);
	EXPECT_EQ(wt::shm_wire::kLayoutVersion, queue->_wire._version);
	EXPECT_EQ(wt::shm_wire::kNativeEndianness, queue->_wire._endianness);
	EXPECT_EQ(sizeof(*queue), queue->_wire._total_size);
}

TEST(test_compile_warnings_shmqueue, version_mismatch_and_unaligned_mapping_are_rejected)
{
	auto queue = std::make_unique<ShmCaster::CastQueue>();
	queue->_wire._version++;
	EXPECT_EQ(wt::shm_wire::ValidationError::bad_version,
		wt::shm_wire::validate_cast_queue<8 * 1024>(queue.get(), sizeof(*queue)));
	queue->_wire._version = wt::shm_wire::kLayoutVersion;
	queue->_reserved = 1;
	EXPECT_EQ(wt::shm_wire::ValidationError::bad_reserved,
		wt::shm_wire::validate_cast_queue<8 * 1024>(queue.get(), sizeof(*queue)));

	alignas(8) unsigned char storage[sizeof(wt::shm_wire::WireHeader) + 1] = {};
	EXPECT_EQ(wt::shm_wire::ValidationError::unexpected_size,
		wt::shm_wire::validate_cast_queue<8 * 1024>(storage + 1, sizeof(storage) - 1));
	EXPECT_EQ(wt::shm_wire::ValidationError::base_alignment,
		wt::shm_wire::validate_header(storage + 1, sizeof(ShmCaster::CastQueue),
			alignof(ShmCaster::CastQueue), wt::shm_wire::kCastQueueMagic,
			sizeof(ShmCaster::CastQueue), offsetof(ShmCaster::CastQueue, _items),
			wt::shm_wire::kCastQueueCapabilities));
}

TEST(test_compile_warnings_shmqueue, queue_indices_are_plain_uint64_fields)
{
	using ProducerQueue = ShmCaster::CastQueue;
	using ConsumerQueue = ParserShm::CastQueue;

	static_assert(std::is_same_v<readable_value_t<ProducerQueue>, uint64_t>);
	static_assert(std::is_same_v<writable_value_t<ProducerQueue>, uint64_t>);
	static_assert(std::is_same_v<readable_value_t<ConsumerQueue>, uint64_t>);
	static_assert(std::is_same_v<writable_value_t<ConsumerQueue>, uint64_t>);
	static_assert(!std::is_volatile_v<readable_field_t<ProducerQueue>>);
	static_assert(!std::is_volatile_v<writable_field_t<ProducerQueue>>);
	static_assert(!std::is_volatile_v<readable_field_t<ConsumerQueue>>);
	static_assert(!std::is_volatile_v<writable_field_t<ConsumerQueue>>);

	SUCCEED();
}

TEST(test_compile_warnings_shmqueue, queue_index_member_addresses_are_uint64_aligned)
{
	std::unique_ptr<ShmCaster::CastQueue> producer_queue(new ShmCaster::CastQueue());
	std::unique_ptr<ParserShm::CastQueue> consumer_queue(new ParserShm::CastQueue());

	EXPECT_TRUE(queue_index_addresses_are_aligned(*producer_queue));
	EXPECT_TRUE(queue_index_addresses_are_aligned(*consumer_queue));
}

TEST(test_compile_warnings_shmqueue, single_thread_compile_layout_protocol_smoke_test)
{
	ShmCaster::_DataQueue<2> queue;

	const uint64_t writable = test_load_relaxed(queue._writable);
	test_store_relaxed(queue._writable, writable + 1);

	ShmCaster::DataItem& item = queue._items[writable % queue._capacity];
	item._type = 3;
	item._trans.price = 12345;

	test_store_release(queue._readable, writable);

	const uint64_t readable = test_load_acquire(queue._readable);
	const ShmCaster::DataItem& consumed = queue._items[readable % queue._capacity];

	EXPECT_EQ(writable, readable);
	EXPECT_EQ(3u, consumed._type);
	EXPECT_EQ(12345, consumed._trans.price);
}

TEST(test_compile_warnings_shmqueue, single_process_thread_protocol_smoke_test)
{
	ShmCaster::_DataQueue<2> queue;

	std::thread producer([&queue]() {
		const uint64_t writable = test_load_relaxed(queue._writable);
		test_store_relaxed(queue._writable, writable + 1);

		ShmCaster::DataItem& item = queue._items[writable % queue._capacity];
		item._type = 2;
		item._order.price = 67890;

		test_store_release(queue._readable, writable);
	});

	uint64_t readable = UINT64_MAX;
	while (readable == UINT64_MAX)
	{
		readable = test_load_acquire(queue._readable);
		std::this_thread::yield();
	}

	const ShmCaster::DataItem& consumed = queue._items[readable % queue._capacity];
	EXPECT_EQ(0u, readable);
	EXPECT_EQ(2u, consumed._type);
	EXPECT_EQ(67890, consumed._order.price);

	producer.join();
}

TEST(test_compile_warnings_shmqueue, multi_process_wrap_and_mixed_payloads)
{
#if defined(_WIN32)
	SUCCEED() << "POSIX fork-based shared-memory regression is not applicable on Windows";
	return;
#else
	TempShmPath path("wrap_mixed");
	ShmCaster caster;
	ASSERT_TRUE(initialize_caster(caster, path.str()));

	int producerToConsumer[2] = {-1, -1};
	int consumerToProducer[2] = {-1, -1};
	ASSERT_EQ(0, ::pipe(producerToConsumer));
	ASSERT_EQ(0, ::pipe(consumerToProducer));

	const pid_t consumerPid = ::fork();
	ASSERT_GE(consumerPid, 0);
	if (consumerPid == 0)
	{
		::close(producerToConsumer[1]);
		::close(consumerToProducer[0]);
		BoostMappingFile mapping;
		ShmCaster::CastQueue* queue = nullptr;
		bool ok = map_cast_queue(path.str(), mapping, queue);
		if (ok)
			ok = write_signal(consumerToProducer[1]);

		const uint32_t capacity = 8 * 1024;
		for (uint32_t chunk = 0; ok && chunk < 3; ++chunk)
		{
			ok = read_signal(producerToConsumer[0]);
			const uint32_t first = chunk * capacity;
			const uint32_t last = first + capacity - 1;
			if (ok)
				ok = wait_for_u64_at_least(queue->_readable, last);
			for (uint32_t sequence = first; ok && sequence <= last; ++sequence)
			{
				const ShmCaster::DataItem& item = queue->_items[sequence % queue->_capacity];
				ok = item_matches_sequence(item, sequence);
			}
			if (ok)
				ok = write_signal(consumerToProducer[1]);
		}
		::close(producerToConsumer[0]);
		::close(consumerToProducer[1]);
		::_exit(ok ? 0 : 1);
	}

	::close(producerToConsumer[0]);
	::close(consumerToProducer[1]);
	ASSERT_TRUE(read_signal(consumerToProducer[0]));
	const uint32_t capacity = 8 * 1024;
	for (uint32_t chunk = 0; chunk < 3; ++chunk)
	{
		const uint32_t first = chunk * capacity;
		const uint32_t last = first + capacity;
		for (uint32_t sequence = first; sequence < last; ++sequence)
			broadcast_sequence(caster, sequence);
		ASSERT_TRUE(write_signal(producerToConsumer[1]));
		ASSERT_TRUE(read_signal(consumerToProducer[0]));
	}
	::close(producerToConsumer[1]);
	::close(consumerToProducer[0]);
	EXPECT_TRUE(child_exited_successfully(consumerPid));
#endif
}

TEST(test_compile_warnings_shmqueue, multi_process_producer_restart_resets_generation)
{
#if defined(_WIN32)
	SUCCEED() << "POSIX fork-based producer restart regression is not applicable on Windows";
	return;
#else
	TempShmPath path("restart");
	int producerAReady[2] = {-1, -1};
	int producerAGo[2] = {-1, -1};
	ASSERT_EQ(0, ::pipe(producerAReady));
	ASSERT_EQ(0, ::pipe(producerAGo));
	const pid_t producerA = spawn_restart_producer(path.str(), 0, producerAReady[1], producerAGo[0]);
	ASSERT_GT(producerA, 0);
	::close(producerAReady[1]);
	::close(producerAGo[0]);
	ASSERT_TRUE(read_signal(producerAReady[0]));
	::close(producerAReady[0]);

	BoostMappingFile mapping;
	ShmCaster::CastQueue* queue = nullptr;
	ASSERT_TRUE(map_cast_queue(path.str(), mapping, queue));
	EXPECT_TRUE(wait_for_u32(queue->_pid, static_cast<uint32_t>(producerA)));
	EXPECT_EQ(UINT64_MAX, wt::atomic_load_u64(queue->_readable, std::memory_order_acquire));
	ASSERT_TRUE(write_signal(producerAGo[1]));
	::close(producerAGo[1]);
	ASSERT_TRUE(child_exited_successfully(producerA));
	ASSERT_TRUE(wait_for_u64_at_least(queue->_readable, 15));
	EXPECT_TRUE(item_matches_sequence(queue->_items[15 % queue->_capacity], 15));

	int producerBReady[2] = {-1, -1};
	int producerBGo[2] = {-1, -1};
	ASSERT_EQ(0, ::pipe(producerBReady));
	ASSERT_EQ(0, ::pipe(producerBGo));
	const pid_t producerB = spawn_restart_producer(path.str(), 1000000, producerBReady[1], producerBGo[0]);
	ASSERT_GT(producerB, 0);
	::close(producerBReady[1]);
	::close(producerBGo[0]);
	ASSERT_TRUE(read_signal(producerBReady[0]));
	::close(producerBReady[0]);

	EXPECT_NE(producerA, producerB);
	EXPECT_TRUE(wait_for_u32(queue->_pid, static_cast<uint32_t>(producerB)));
	EXPECT_EQ(UINT64_MAX, wt::atomic_load_u64(queue->_readable, std::memory_order_acquire));
	EXPECT_EQ(0u, wt::atomic_load_u64(queue->_writable, std::memory_order_relaxed));
	ASSERT_TRUE(write_signal(producerBGo[1]));
	::close(producerBGo[1]);
	ASSERT_TRUE(child_exited_successfully(producerB));
	ASSERT_TRUE(wait_for_u64_at_least(queue->_readable, 15));
	EXPECT_TRUE(item_matches_sequence(queue->_items[15 % queue->_capacity], 1000015));
#endif
}
