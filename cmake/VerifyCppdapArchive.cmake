# Verifies that libcppdap.a contains all expected translation units.
# An interrupted or stale incremental build can leave a partial archive and
# cause undefined-reference link errors for dap::Session::create(), TypeOf, etc.

if(NOT ARCHIVE OR NOT EXISTS "${ARCHIVE}")
  message(FATAL_ERROR "cppdap archive not found: ${ARCHIVE}")
endif()

if(NOT EXPECTED)
  set(EXPECTED 13)
endif()

execute_process(
  COMMAND ar t "${ARCHIVE}"
  OUTPUT_VARIABLE _objects
  ERROR_VARIABLE _ar_err
  RESULT_VARIABLE _ar_rc
)
if(_ar_rc)
  message(FATAL_ERROR "Failed to inspect ${ARCHIVE}: ${_ar_err}")
endif()

string(REPLACE "\n" ";" _object_list "${_objects}")
list(FILTER _object_list EXCLUDE REGEX "^$")
list(LENGTH _object_list _count)

if(_count LESS EXPECTED)
  file(REMOVE "${ARCHIVE}")
  message(FATAL_ERROR
    "libcppdap.a is incomplete (${_count}/${EXPECTED} objects). "
    "The archive was removed; rebuild with:\n"
    "  cmake --build ${CMAKE_BINARY_DIR} --target cppdap")
endif()
