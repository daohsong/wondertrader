if(NOT DEFINED WT_C_COMPILER OR NOT DEFINED WT_LMDB_SOURCE)
	message(FATAL_ERROR "WT_C_COMPILER and WT_LMDB_SOURCE are required")
endif()

execute_process(
	COMMAND "${WT_C_COMPILER}" -D__APPLE__ -E -dD "${WT_LMDB_SOURCE}"
	RESULT_VARIABLE WT_PREPROCESS_RESULT
	OUTPUT_VARIABLE WT_PREPROCESSED_SOURCE
	ERROR_VARIABLE WT_PREPROCESS_ERROR
)
if(NOT WT_PREPROCESS_RESULT EQUAL 0)
	message(FATAL_ERROR "Failed to preprocess LMDB Apple branch: ${WT_PREPROCESS_ERROR}")
endif()

string(REGEX MATCH "#define[ \t]+ESECT[^\r\n]*" WT_ESECT_DEFINITION "${WT_PREPROCESSED_SOURCE}")
if(NOT WT_ESECT_DEFINITION)
	message(FATAL_ERROR "Preprocessed LMDB source does not define ESECT")
endif()
if(WT_ESECT_DEFINITION MATCHES "section")
	message(FATAL_ERROR "Modern Apple builds must not force LMDB ESECT into a custom Mach-O section: ${WT_ESECT_DEFINITION}")
endif()
if(NOT WT_ESECT_DEFINITION MATCHES "cold")
	message(FATAL_ERROR "Modern Apple builds must use the compiler cold attribute: ${WT_ESECT_DEFINITION}")
endif()
