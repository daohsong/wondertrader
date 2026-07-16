get_filename_component(NANOMSG_FIXTURE_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_library(nanomsg INTERFACE IMPORTED)
set_target_properties(nanomsg PROPERTIES
	INTERFACE_INCLUDE_DIRECTORIES "${NANOMSG_FIXTURE_PREFIX}/include"
)
