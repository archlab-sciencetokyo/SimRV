if(NOT DEFINED CMAKE_SOURCE_DIR OR NOT EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
  get_filename_component(CMAKE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.."
                         ABSOLUTE)
endif()

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
  message(FATAL_ERROR "MIT audit: missing root LICENSE")
endif()
file(READ "${CMAKE_SOURCE_DIR}/LICENSE" license_text)
if(NOT license_text MATCHES "MIT License")
  message(FATAL_ERROR "MIT audit: root LICENSE is not MIT")
endif()
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md")
  message(FATAL_ERROR "MIT audit: missing THIRD_PARTY_NOTICES.md")
endif()
file(
  GLOB_RECURSE vendored
  RELATIVE "${CMAKE_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/third_party/*" "${CMAKE_SOURCE_DIR}/vendor/*"
  "${CMAKE_SOURCE_DIR}/external/*")
if(vendored)
  message(
    FATAL_ERROR
      "MIT audit: vendored content requires an explicit notice inventory: ${vendored}"
  )
endif()
message(
  STATUS "MIT audit passed: no vendored source or fetched build dependencies")
