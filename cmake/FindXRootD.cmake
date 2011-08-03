# Try to find XROOTD
# Once done, this will define
#
# XROOTD_FOUND       - system has XRootD
# XROOTD_INCLUDES    - the XRootD include directory
# XROOTD_LIB_DIR     - the XRootD library directory
#
# XROOTD_DIR may be defined as a hint for where to look

FIND_PATH(XROOTD_INCLUDES XrdVersion.hh
  HINTS
  ${XROOTD_DIR}
  $ENV{XROOTD_DIR}
  /usr
  /opt/xrootd/
  PATH_SUFFIXES include/xrootd
  PATHS /opt/xrootd
)

FIND_LIBRARY(XROOTD_OUC XrdOuc
  HINTS
  ${XROOTD_DIR}
  $ENV{XROOTD_DIR}
  /usr
  /opt/xrootd/
  PATH_SUFFIXES lib
)

GET_FILENAME_COMPONENT( XROOTD_LIB_DIR ${XROOTD_OUC} PATH )

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(XRootD DEFAULT_MSG XROOTD_LIB_DIR XROOTD_INCLUDES)
