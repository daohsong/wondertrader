cmake_minimum_required(VERSION 3.20)
project(WonderTraderBoostDependency LANGUAGES CXX)

if(NOT WT_DEPENDENCIES_MODULE)
	message(FATAL_ERROR "WT_DEPENDENCIES_MODULE is required")
endif()

foreach(WT_BOOST_FIXTURE_TARGET IN ITEMS headers filesystem thread chrono atomic)
	add_library(Boost::${WT_BOOST_FIXTURE_TARGET} INTERFACE IMPORTED)
endforeach()

include("${WT_DEPENDENCIES_MODULE}")
WT_SETUP_BOOST_DEPENDENCY()

foreach(WT_EXPECTED_TARGET IN ITEMS
	WT::Boost
	WT::BoostFilesystem
	WT::BoostThread
	WT::BoostChrono
	WT::BoostAtomic
)
	if(NOT TARGET ${WT_EXPECTED_TARGET})
		message(FATAL_ERROR "Boost provider did not create ${WT_EXPECTED_TARGET}")
	endif()
endforeach()

get_target_property(WT_BOOST_LINK_LIBRARIES WT_BOOST INTERFACE_LINK_LIBRARIES)
if(NOT "Boost::headers" IN_LIST WT_BOOST_LINK_LIBRARIES)
	message(FATAL_ERROR "WT::Boost does not publish the upstream headers target")
endif()

get_target_property(WT_BOOST_THREAD_LINK_LIBRARIES WT_BOOST_THREAD INTERFACE_LINK_LIBRARIES)
foreach(WT_EXPECTED_LINK IN ITEMS WT::Boost Boost::thread)
	if(NOT WT_EXPECTED_LINK IN_LIST WT_BOOST_THREAD_LINK_LIBRARIES)
		message(FATAL_ERROR "WT::BoostThread does not publish ${WT_EXPECTED_LINK}")
	endif()
endforeach()
