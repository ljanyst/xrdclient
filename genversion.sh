#!/bin/bash

#-------------------------------------------------------------------------------
# Process the git decoration expansion and try to derive version number
#-------------------------------------------------------------------------------
EXP='^v[0-9]+\.[0-9]+$'

#-------------------------------------------------------------------------------
# Extract version number from git references
#-------------------------------------------------------------------------------
function getVersionFromRefs()
{
  REFS=${1/RefNames:/}
  REFS=${REFS//,/}
  REFS=${REFS/(/}
  REFS=${REFS/)/}
  REFS=($REFS)

  VERSION="unknown"

  for i in ${REFS[@]}; do
    if test x`echo $i | egrep $EXP` != x; then
      VERSION="$i"
    fi
  done
  echo $VERSION
  return 0
}

#-------------------------------------------------------------------------------
# Generate the version string from the date and the hash
#-------------------------------------------------------------------------------
function getVersionFromLog()
{
  AWK=gawk
  EX="`which gawk`"
  if test x"${EX}" == x -o ! -x "${EX}"; then
    AWK=awk
  fi

  VERSION="`echo $@ | $AWK '{ gsub("-","",$1); print $1"-"$4; }'`"
  if test $? -ne 0; then
    echo "unknown";
    return 1
  fi
  echo v$VERSION
}

#-------------------------------------------------------------------------------
# Print help
#-------------------------------------------------------------------------------
function printHelp()
{
  echo "Usage:"                                                1>&2
  echo "${0} [--help] [SOURCEPATH]"                            1>&2
  echo "  --help       prints this message"                    1>&2
}

#-------------------------------------------------------------------------------
# Check the parameters
#-------------------------------------------------------------------------------
while test ${#} -ne 0; do
  if test x${1} = x--help; then
    PRINTHELP=1
  else
    SOURCEPATH=${1}
  fi
  shift
done

if test x$PRINTHELP != x; then
  printHelp ${0}
  exit 0
fi

if test x$SOURCEPATH != x; then
  SOURCEPATH=${SOURCEPATH}/
  if test ! -d $SOURCEPATH; then
    echo "The given source path does not exist: ${SOURCEPATH}" 1>&2
    exit 1
  fi
fi

VERSION="unknown"

#-------------------------------------------------------------------------------
# We're not inside a git repo
#-------------------------------------------------------------------------------
if test ! -d ${SOURCEPATH}.git; then
  #-----------------------------------------------------------------------------
  # We cannot figure out what version we are
  #----------------------------------------------------------------------------
  echo "[I] No git repository info found. Trying to interpret VERSION_INFO" 1>&2
  if test ! -r ${SOURCEPATH}VERSION_INFO; then
    echo "[!] VERSION_INFO file absent. Unable to determine the version. Using \"unknown\"" 1>&2
  elif test x"`grep Format ${SOURCEPATH}VERSION_INFO`" != x; then
    echo "[!] VERSION_INFO file invalid. Unable to determine the version. Using \"unknown\"" 1>&2

  #-----------------------------------------------------------------------------
  # The version file exists and seems to be valid so we know the version
  #----------------------------------------------------------------------------
  else
    REFNAMES="`grep RefNames ${SOURCEPATH}VERSION_INFO`"
    VERSION="`getVersionFromRefs "$REFNAMES"`"
    if test x$VERSION == xunknown; then
      SHORTHASH="`grep ShortHash ${SOURCEPATH}VERSION_INFO`"
      SHORTHASH=${SHORTHASH/ShortHash:/}
      SHORTHASH=${SHORTHASH// /}
      DATE="`grep Date ${SOURCEPATH}VERSION_INFO`"
      DATE=${DATE/Date:/}
      VERSION="`getVersionFromLog $DATE $SHORTHASH`"
    fi
  fi

#-------------------------------------------------------------------------------
# We're in a git repo so we can try to determine the version using that
#-------------------------------------------------------------------------------
else
  echo "[I] Determining version from git" 1>&2
  EX="`which git`"
  if test x"${EX}" == x -o ! -x "${EX}"; then
    echo "[!] Unable to find git in the path: setting the version tag to unknown" 1>&2
  else
    #---------------------------------------------------------------------------
    # Sanity check
    #---------------------------------------------------------------------------
    CURRENTDIR=$PWD
    if [ x${SOURCEPATH} != x ]; then
      cd ${SOURCEPATH}
    fi
    git log -1 >/dev/null 2>&1
    if test $? -ne 0; then
      echo "[!] Error while generating src/XrdVersion.hh, the git repository may be corrupted" 1>&2
      echo "[!] Setting the version tag to unknown" 1>&2
    else
      #-------------------------------------------------------------------------
      # Can we match the exact tag?
      #-------------------------------------------------------------------------
      git describe --tags --abbrev=0 --exact-match >/dev/null 2>&1
      if test ${?} -eq 0; then
        VERSION="`git describe --tags --abbrev=0 --exact-match`"
      else
        LOGINFO="`git log -1 --format='%ai %h'`"
        if test ${?} -eq 0; then
          VERSION="`getVersionFromLog $LOGINFO`"
        fi
      fi
    fi
    cd $CURRENTDIR
  fi
fi

echo $VERSION
