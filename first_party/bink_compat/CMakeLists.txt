add_library(bink_compat STATIC
    "include/bink_compat.h"
    "src/bink_compat.c"
    "src/avi_reader.c"
    "src/avi_reader.h"
    "src/mjpeg_decoder.c"
    "src/mjpeg_decoder.h"
)

target_include_directories(bink_compat PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(bink_compat PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# Math (libm) is needed for cos() in the MJPEG IDCT cosine kernel build
# on POSIX targets; MSVC and Apple's libc roll it into libc directly.
if(UNIX AND NOT APPLE)
    target_link_libraries(bink_compat PRIVATE m)
endif()

set(BINK_COMPAT_LIBRARY "bink_compat" PARENT_SCOPE)
set(BINK_COMPAT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include" PARENT_SCOPE)
