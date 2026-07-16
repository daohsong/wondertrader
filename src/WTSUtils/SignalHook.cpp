#include "SignalHook.hpp"
#include "SignalHookInternal.hpp"

#include <csignal>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <atomic>
#include <chrono>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace signal_hook_detail
{
#ifndef _WIN32
bool writeBestEffort(int fd, const void* data, size_t size, WriteFunction writer) noexcept
{
	const char* next = static_cast<const char*>(data);
	size_t remaining = size;
	while (remaining != 0)
	{
		const ssize_t written = writer(fd, next, remaining);
		if (written > 0)
		{
			next += written;
			remaining -= static_cast<size_t>(written);
			continue;
		}
		if (written < 0 && errno == EINTR)
			continue;
		return false;
	}
	return true;
}
#endif
}

namespace
{
struct SignalCallbacks
{
	std::mutex mutex;
	TracerLogCallback log;
	ExitHandler exit;
};

SignalCallbacks& callbacks()
{
	// The dispatcher may still be active while static objects are being destroyed.
	// Keep this process-wide state alive until the operating system tears it down.
	static SignalCallbacks* state = new SignalCallbacks();
	return *state;
}

const char* signalMessage(int signum)
{
	switch (signum)
	{
	case SIGINT:
		return "app interrupted";
	case SIGTERM:
		return "app terminated";
#ifndef _WIN32
	case SIGHUP:
		return "app has received SIGHUP";
	case SIGPIPE:
		return "app terminated by SIGPIPE";
	case SIGALRM:
		return "app terminated by SIGALRM";
	case SIGXCPU:
		return "app terminated by SIGXCPU";
	case SIGXFSZ:
		return "app terminated by SIGXFSZ";
	case SIGVTALRM:
		return "app terminated by SIGVTALRM";
	case SIGPROF:
		return "app terminated by SIGPROF";
#endif
	default:
		return "app terminated by signal";
	}
}

void dispatchSignal(int signum)
{
	TracerLogCallback log;
	ExitHandler exit;
	{
		std::lock_guard<std::mutex> lock(callbacks().mutex);
		log = callbacks().log;
		exit = callbacks().exit;
	}

	if (log)
		log(signalMessage(signum));

#ifndef _WIN32
	if (signum == SIGHUP)
		return;
#endif

	if (exit)
		exit(signum);
	else
		std::exit(signum);
}

#ifdef _WIN32
std::atomic<int> g_pendingSignal{0};

bool isFatalSignal(int signum)
{
	switch (signum)
	{
	case SIGILL:
	case SIGFPE:
	case SIGSEGV:
	case SIGABRT:
		return true;
	default:
		return false;
	}
}

void dispatchLoop()
{
	for (;;)
	{
		const int signum = g_pendingSignal.exchange(0, std::memory_order_acq_rel);
		if (signum != 0)
			dispatchSignal(signum);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

bool startDispatcher()
{
	static std::once_flag once;
	std::call_once(once, []() { std::thread(dispatchLoop).detach(); });
	return true;
}
#else
volatile sig_atomic_t g_signalWriteFd = -1;
int g_signalReadFd = -1;
bool g_dispatcherReady = false;

bool isFatalSignal(int signum)
{
	switch (signum)
	{
	case SIGQUIT:
	case SIGILL:
	case SIGTRAP:
	case SIGABRT:
	case SIGFPE:
	case SIGBUS:
	case SIGSEGV:
#ifdef SIGSYS
	case SIGSYS:
#endif
		return true;
	default:
		return false;
	}
}

void dispatchLoop()
{
	for (;;)
	{
		int signum = 0;
		const ssize_t size = ::read(g_signalReadFd, &signum, sizeof(signum));
		if (size == static_cast<ssize_t>(sizeof(signum)))
		{
			dispatchSignal(signum);
			continue;
		}
		if (size < 0 && errno == EINTR)
			continue;
	}
}

void setCloseOnExec(int fd)
{
	const int flags = ::fcntl(fd, F_GETFD);
	if (flags >= 0)
		::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

bool startDispatcher()
{
	static std::once_flag once;
	std::call_once(once, []() {
		int descriptors[2] = {-1, -1};
		if (::pipe(descriptors) != 0)
			return;

		setCloseOnExec(descriptors[0]);
		setCloseOnExec(descriptors[1]);
		const int flags = ::fcntl(descriptors[1], F_GETFL);
		if (flags < 0 || ::fcntl(descriptors[1], F_SETFL, flags | O_NONBLOCK) != 0)
		{
			::close(descriptors[0]);
			::close(descriptors[1]);
			return;
		}

		g_signalReadFd = descriptors[0];
		g_signalWriteFd = descriptors[1];
		std::thread(dispatchLoop).detach();
		g_dispatcherReady = true;
	});
	return g_dispatcherReady;
}

void installHandler(int signum, int flags)
{
	struct sigaction action = {};
	action.sa_handler = handle_signal;
	sigemptyset(&action.sa_mask);
	action.sa_flags = flags;
	sigaction(signum, &action, nullptr);
}
#endif
}

void handle_signal(int signum)
{
#ifdef _WIN32
	if (isFatalSignal(signum))
	{
		std::signal(signum, SIG_DFL);
		std::raise(signum);
		return;
	}
	g_pendingSignal.store(signum, std::memory_order_release);
#else
	const int savedErrno = errno;
	if (isFatalSignal(signum))
	{
		static const char message[] = "fatal signal\n";
		signal_hook_detail::writeBestEffort(
			STDERR_FILENO,
			message,
			sizeof(message) - 1,
			::write
		);

		// POSIX blocks the currently handled signal until the handler returns.
		// Restore its default disposition and unblock it before re-sending it so
		// macOS retains the signal's native termination semantics too.
		struct sigaction defaultAction = {};
		defaultAction.sa_handler = SIG_DFL;
		sigemptyset(&defaultAction.sa_mask);
		defaultAction.sa_flags = 0;
		::sigaction(signum, &defaultAction, nullptr);

		sigset_t signalSet;
		sigemptyset(&signalSet);
		sigaddset(&signalSet, signum);
		::sigprocmask(SIG_UNBLOCK, &signalSet, nullptr);

		errno = savedErrno;
		::kill(::getpid(), signum);
		::_exit(128 + signum);
	}

	const int fd = static_cast<int>(g_signalWriteFd);
	if (fd >= 0)
		signal_hook_detail::writeBestEffort(fd, &signum, sizeof(signum), ::write);
	errno = savedErrno;
#endif
}

void install_signal_hooks(TracerLogCallback cbLog, ExitHandler sigHandler)
{
	const bool dispatcherReady = startDispatcher();
	{
		std::lock_guard<std::mutex> lock(callbacks().mutex);
		callbacks().log = std::move(cbLog);
		callbacks().exit = std::move(sigHandler);
	}

#ifdef _WIN32
	if (dispatcherReady)
	{
		std::signal(SIGINT, handle_signal);
		std::signal(SIGTERM, handle_signal);
#ifdef SIGBREAK
		std::signal(SIGBREAK, handle_signal);
#endif
	}
	std::signal(SIGILL, handle_signal);
	std::signal(SIGFPE, handle_signal);
	std::signal(SIGSEGV, handle_signal);
	std::signal(SIGABRT, handle_signal);
#else
	if (dispatcherReady)
	{
		installHandler(SIGINT, SA_RESTART);
		installHandler(SIGTERM, SA_RESTART);
		installHandler(SIGHUP, SA_RESTART);
		installHandler(SIGPIPE, SA_RESTART);
		installHandler(SIGALRM, SA_RESTART);
		installHandler(SIGXCPU, SA_RESTART);
		installHandler(SIGXFSZ, SA_RESTART);
		installHandler(SIGVTALRM, SA_RESTART);
		installHandler(SIGPROF, SA_RESTART);
	}

	installHandler(SIGQUIT, SA_RESETHAND);
	installHandler(SIGILL, SA_RESETHAND);
	installHandler(SIGTRAP, SA_RESETHAND);
	installHandler(SIGABRT, SA_RESETHAND);
	installHandler(SIGFPE, SA_RESETHAND);
	installHandler(SIGBUS, SA_RESETHAND);
	installHandler(SIGSEGV, SA_RESETHAND);
#ifdef SIGSYS
	installHandler(SIGSYS, SA_RESETHAND);
#endif
#endif
}
