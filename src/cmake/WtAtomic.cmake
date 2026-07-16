include(CheckCXXSourceCompiles)

function(WT_VALIDATE_LOCK_FREE_IPC_ATOMICS)
	set(WT_ATOMIC_PROBE_SOURCE [[
		#include <atomic>
		#include <cstdint>

		#if defined(__GNUC__) || defined(__clang__)
		static_assert(__atomic_always_lock_free(sizeof(std::uint32_t), nullptr));
		static_assert(__atomic_always_lock_free(sizeof(std::uint64_t), nullptr));
		#elif defined(_MSC_VER)
		static_assert(sizeof(std::uint32_t) == sizeof(long));
		static_assert(sizeof(std::uint64_t) == sizeof(__int64));
		#elif defined(__cpp_lib_atomic_ref) && __cpp_lib_atomic_ref >= 201806L
		static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free);
		static_assert(std::atomic_ref<std::uint64_t>::is_always_lock_free);
		#else
		#error No supported cross-process atomic primitive
		#endif

		int main() { return 0; }
	]])

	check_cxx_source_compiles(
		"${WT_ATOMIC_PROBE_SOURCE}"
		WT_IPC_ATOMICS_ARE_LOCK_FREE
	)
	if(NOT WT_IPC_ATOMICS_ARE_LOCK_FREE)
		message(FATAL_ERROR
			"WonderTrader shared-memory IPC requires lock-free 32-bit and 64-bit "
			"atomics. The selected ${CMAKE_CXX_COMPILER_ID} "
			"${CMAKE_CXX_COMPILER_VERSION} toolchain for ${CMAKE_SYSTEM_NAME}/"
			"${CMAKE_SYSTEM_PROCESSOR} does not satisfy that contract. See "
			"CMakeFiles/CMakeError.log for the probe output."
		)
	endif()

	message(STATUS "Shared-memory IPC has lock-free 32-bit and 64-bit atomics")
endfunction()
