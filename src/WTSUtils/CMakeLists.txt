
#1. 确定CMake的最低版本需求
cmake_minimum_required(VERSION 3.20)

#2. 确定工程名
project(WTSUtils LANGUAGES C CXX)

#7. 添加源码
SET(SRCS
	lmdb/mdb.c
	lmdb/midl.c
	yamlcpp/binary.cpp
	yamlcpp/convert.cpp
	yamlcpp/depthguard.cpp
	yamlcpp/directives.cpp
	yamlcpp/emit.cpp
	yamlcpp/emitfromevents.cpp
	yamlcpp/emitter.cpp
	yamlcpp/emitterstate.cpp
	yamlcpp/emitterutils.cpp
	yamlcpp/exceptions.cpp
	yamlcpp/exp.cpp
	yamlcpp/memory.cpp
	yamlcpp/node.cpp
	yamlcpp/node_data.cpp
	yamlcpp/nodebuilder.cpp
	yamlcpp/nodeevents.cpp
	yamlcpp/null.cpp
	yamlcpp/ostream_wrapper.cpp
	yamlcpp/parse.cpp
	yamlcpp/parser.cpp
	yamlcpp/regex_yaml.cpp
	yamlcpp/scanner.cpp
	yamlcpp/scanscalar.cpp
	yamlcpp/scantag.cpp
	yamlcpp/scantoken.cpp
	yamlcpp/simplekey.cpp
	yamlcpp/singledocparser.cpp
	yamlcpp/stream.cpp
	yamlcpp/tag.cpp
	zstdlib/cover.c
	zstdlib/debug.c
	zstdlib/divsufsort.c
	zstdlib/entropy_common.c
	zstdlib/error_private.c
	zstdlib/fastcover.c
	zstdlib/fse_compress.c
	zstdlib/fse_decompress.c
	zstdlib/hist.c
	zstdlib/huf_compress.c
	zstdlib/huf_decompress.c
	zstdlib/pool.c
	zstdlib/threading.c
	zstdlib/xxhash.c
	zstdlib/zbuff_common.c
	zstdlib/zbuff_compress.c
	zstdlib/zbuff_decompress.c
	zstdlib/zdict.c
	zstdlib/zstd_common.c
	zstdlib/zstd_compress.c
	zstdlib/zstd_compress_literals.c
	zstdlib/zstd_compress_sequences.c
	zstdlib/zstd_ddict.c
	zstdlib/zstd_decompress.c
	zstdlib/zstd_decompress_block.c
	zstdlib/zstd_double_fast.c
	zstdlib/zstd_fast.c
	zstdlib/zstd_lazy.c
	zstdlib/zstd_ldm.c
	zstdlib/zstd_opt.c
	zstdlib/zstdmt_compress.c
	SignalHook.cpp
	WTSCfgLoader.cpp
)
SET(HDRS
	zstdlib/bitstream.h
	zstdlib/compiler.h
	zstdlib/cover.h
	zstdlib/cpu.h
	zstdlib/debug.h
	zstdlib/divsufsort.h
	zstdlib/error_private.h
	zstdlib/fse.h
	zstdlib/hist.h
	zstdlib/huf.h
	zstdlib/mem.h
	zstdlib/pool.h
	zstdlib/threading.h
	zstdlib/xxhash.h
	zstdlib/zbuff.h
	zstdlib/zdict.h
	zstdlib/zstd.h
	zstdlib/zstd_compress_internal.h
	zstdlib/zstd_compress_literals.h
	zstdlib/zstd_compress_sequences.h
	zstdlib/zstd_cwksp.h
	zstdlib/zstd_ddict.h
	zstdlib/zstd_decompress_block.h
	zstdlib/zstd_decompress_internal.h
	zstdlib/zstd_double_fast.h
	zstdlib/zstd_errors.h
	zstdlib/zstd_fast.h
	zstdlib/zstd_internal.h
	zstdlib/zstd_lazy.h
	zstdlib/zstd_ldm.h
	zstdlib/zstd_opt.h
	zstdlib/zstdmt_compress.h
	SignalHook.hpp
	SignalHookInternal.hpp
	WTSCfgLoader.h
)

IF(MSVC)
LIST(APPEND SRCS "./StackTracer/StackTracer.cpp" "./StackTracer/StackWalker.cpp")
ELSE()
LIST(APPEND SRCS "./StackTracer/StackTracer.cpp")
ENDIF()

#8. 编译成动态库，并生成库文件
add_library(WTSUtils STATIC ${SRCS} ${HDRS})
set_target_properties(WTSUtils PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(WTSUtils PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/yamlcpp")
TARGET_LINK_LIBRARIES(WTSUtils PRIVATE WT::RapidJSON)
target_link_libraries(WTSUtils PUBLIC WT::Platform ${WT_ICONV_LIB})
if(WIN32)
	target_link_libraries(WTSUtils PRIVATE psapi dbghelp version)
endif()
