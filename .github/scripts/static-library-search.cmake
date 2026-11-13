# CMake initializes this variable while enabling each language, so passing it
# as an untyped -D cache entry is overwritten before pkg-config resolves deps.
# The portable archive intentionally resolves every dependency to a static lib.
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
