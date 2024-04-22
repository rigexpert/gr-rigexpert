find_package(PkgConfig)

PKG_CHECK_MODULES(PC_GR_RIGEXPERT gnuradio-RigExpert)

FIND_PATH(
    GR_RIGEXPERT_INCLUDE_DIRS
    NAMES gnuradio/RigExpert/api.h
    HINTS $ENV{RIGEXPERT_DIR}/include
        ${PC_RIGEXPERT_INCLUDEDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/include
          /usr/local/include
          /usr/include
)

FIND_LIBRARY(
    GR_RIGEXPERT_LIBRARIES
    NAMES gnuradio-RigExpert
    HINTS $ENV{RIGEXPERT_DIR}/lib
        ${PC_RIGEXPERT_LIBDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/lib
          ${CMAKE_INSTALL_PREFIX}/lib64
          /usr/local/lib
          /usr/local/lib64
          /usr/lib
          /usr/lib64
          )

include("${CMAKE_CURRENT_LIST_DIR}/gnuradio-RigExpertTarget.cmake")

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(GR_RIGEXPERT DEFAULT_MSG GR_RIGEXPERT_LIBRARIES GR_RIGEXPERT_INCLUDE_DIRS)
MARK_AS_ADVANCED(GR_RIGEXPERT_LIBRARIES GR_RIGEXPERT_INCLUDE_DIRS)
