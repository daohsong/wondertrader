#include "gtest/gtest.h"

#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "../WTSUtils/SignalHook.hpp"
#include "../WTSUtils/SignalHookInternal.hpp"

#ifndef _WIN32
namespace
{
enum class WriteScenario
{
	Complete,
	InterruptedOnce,
	ShortWrites,
	Unavailable,
	ZeroProgress
};

WriteScenario g_writeScenario = WriteScenario::Complete;
size_t g_writeCalls = 0;
std::string g_writtenData;

ssize_t scriptedWrite(int, const void* data, size_t size)
{
	++g_writeCalls;
	if (g_writeScenario == WriteScenario::InterruptedOnce && g_writeCalls == 1)
	{
		errno = EINTR;
		return -1;
	}
	if (g_writeScenario == WriteScenario::Unavailable)
	{
		errno = EAGAIN;
		return -1;
	}
	if (g_writeScenario == WriteScenario::ZeroProgress)
		return 0;

	const size_t written = g_writeScenario == WriteScenario::ShortWrites && size > 2 ? 2 : size;
	g_writtenData.append(static_cast<const char*>(data), written);
	return static_cast<ssize_t>(written);
}

void resetScriptedWrite(WriteScenario scenario)
{
	g_writeScenario = scenario;
	g_writeCalls = 0;
	g_writtenData.clear();
}

void waitForSignalDispatch()
{
	std::this_thread::sleep_for(std::chrono::seconds(2));
	std::_Exit(9);
}
}

TEST(SignalHook, BestEffortWriteCompletesAfterInterruptionAndShortWrites)
{
	static const char payload[] = "signal";
	for (const WriteScenario scenario : {
		WriteScenario::Complete,
		WriteScenario::InterruptedOnce,
		WriteScenario::ShortWrites
	})
	{
		resetScriptedWrite(scenario);
		EXPECT_TRUE(signal_hook_detail::writeBestEffort(
			1,
			payload,
			sizeof(payload) - 1,
			scriptedWrite
		));
		EXPECT_EQ(g_writtenData, payload);
	}
	EXPECT_GT(g_writeCalls, 1u);
}

TEST(SignalHook, BestEffortWriteStopsWhenDescriptorCannotProgress)
{
	static const char payload[] = "signal";
	for (const WriteScenario scenario : {
		WriteScenario::Unavailable,
		WriteScenario::ZeroProgress
	})
	{
		resetScriptedWrite(scenario);
		EXPECT_FALSE(signal_hook_detail::writeBestEffort(
			1,
			payload,
			sizeof(payload) - 1,
			scriptedWrite
		));
		EXPECT_EQ(g_writeCalls, 1u);
		EXPECT_TRUE(g_writtenData.empty());
	}
}

TEST(SignalHook, SigIntCallbackRunsOutsideTheSignalHandler)
{
	ASSERT_EXIT({
		const std::thread::id signalThread = std::this_thread::get_id();
		install_signal_hooks(TracerLogCallback{}, [signalThread](int signal) {
			std::_Exit(signal == SIGINT && std::this_thread::get_id() != signalThread ? 0 : 8);
		});
		raise(SIGINT);
		waitForSignalDispatch();
	}, ::testing::ExitedWithCode(0), "");
}

TEST(SignalHook, SigTermCallbackRunsOutsideTheSignalHandler)
{
	ASSERT_EXIT({
		const std::thread::id signalThread = std::this_thread::get_id();
		install_signal_hooks(TracerLogCallback{}, [signalThread](int signal) {
			std::_Exit(signal == SIGTERM && std::this_thread::get_id() != signalThread ? 0 : 8);
		});
		raise(SIGTERM);
		waitForSignalDispatch();
	}, ::testing::ExitedWithCode(0), "");
}

TEST(SignalHook, SigTermWithoutCallbackKeepsThePreviousExitCode)
{
	ASSERT_EXIT({
		install_signal_hooks(TracerLogCallback{});
		raise(SIGTERM);
		waitForSignalDispatch();
	}, ::testing::ExitedWithCode(SIGTERM), "");
}

TEST(SignalHook, SigAbrtKeepsItsDefaultTerminationSemantics)
{
	ASSERT_EXIT({
		install_signal_hooks(TracerLogCallback{});
		raise(SIGABRT);
		std::_Exit(9);
	}, ::testing::KilledBySignal(SIGABRT), "fatal signal");
}

TEST(SignalHook, SigSegvKeepsItsDefaultTerminationSemantics)
{
	ASSERT_EXIT({
		install_signal_hooks(TracerLogCallback{});
		raise(SIGSEGV);
		std::_Exit(9);
	}, ::testing::KilledBySignal(SIGSEGV), "fatal signal");
}
#endif
