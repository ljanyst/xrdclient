//------------------------------------------------------------------------------
// Copyright (c) 2012 by European Organization for Nuclear Research (CERN)
// Author: Lukasz Janyst <ljanyst@cern.ch>
// See the LICENCE file for details.
//------------------------------------------------------------------------------

#include "XrdCl/XrdClQuery.hh"
#include "XrdCl/XrdClQueryExecutor.hh"
#include "XrdCl/XrdClURL.hh"
#include "XrdCl/XrdClLog.hh"
#include "XrdCl/XrdClDefaultEnv.hh"
#include "XrdCl/XrdClConstants.hh"

#include <cstdlib>
#include <iostream>
#include <iomanip>

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

using namespace XrdClient;

//------------------------------------------------------------------------------
// Build a path
//------------------------------------------------------------------------------
XRootDStatus BuildPath( std::string &newPath, Env *env,
                        const std::string &path )
{
  if( path.empty() )
    return XRootDStatus( stError, errInvalidArgs );

  if( path[0] == '/' )
  {
    newPath = path;
    return XRootDStatus();
  }

  std::string cwd = "/";
  env->GetString( "CWD", cwd );
  newPath  = cwd;
  newPath += "/";
  newPath += path;

  //----------------------------------------------------------------------------
  // Collapse the dots
  //----------------------------------------------------------------------------
  std::list<std::string> pathComponents;
  std::list<std::string>::iterator it;
  XrdClient::Utils::splitString( pathComponents, newPath, "/" );
  newPath = "/";
  for( it = pathComponents.begin(); it != pathComponents.end(); )
  {
    if( *it == "." )
    {
      it = pathComponents.erase( it );
      continue;
    }

    if( *it == ".." )
    {
      if( it == pathComponents.begin() )
        return XRootDStatus( stError, errInvalidArgs );
      std::list<std::string>::iterator it1 = it;
      --it1;
      it = pathComponents.erase( it1 );
      it = pathComponents.erase( it );
      continue;
    }
    ++it;
  }

  newPath = "/";
  for( it = pathComponents.begin(); it != pathComponents.end(); ++it )
  {
    newPath += *it;
    newPath += "/";
  }
  if( newPath.length() > 1 )
    newPath.erase( newPath.length()-1, 1 );

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Convert mode string to uint16_t
//------------------------------------------------------------------------------
XRootDStatus ConvertMode( uint16_t &mode, const std::string &modeStr )
{
  if( modeStr.length() != 9 )
    return XRootDStatus( stError, errInvalidArgs );

  mode = 0;
  for( int i = 0; i < 3; ++i )
  {
    if( modeStr[i] == 'r' )
      mode |= Access::UR;
    else if( modeStr[i] == 'w' )
      mode |= Access::UW;
    else if( modeStr[i] == 'x' )
      mode |= Access::UX;
    else if( modeStr[i] != '-' )
      return XRootDStatus( stError, errInvalidArgs );
  }
  for( int i = 3; i < 6; ++i )
  {
    if( modeStr[i] == 'r' )
      mode |= Access::GR;
    else if( modeStr[i] == 'w' )
      mode |= Access::GW;
    else if( modeStr[i] == 'x' )
      mode |= Access::GX;
    else if( modeStr[i] != '-' )
      return XRootDStatus( stError, errInvalidArgs );
  }
  for( int i = 6; i < 9; ++i )
  {
    if( modeStr[i] == 'r' )
      mode |= Access::OR;
    else if( modeStr[i] == 'w' )
      mode |= Access::OW;
    else if( modeStr[i] == 'x' )
      mode |= Access::OX;
    else if( modeStr[i] != '-' )
      return XRootDStatus( stError, errInvalidArgs );
  }
  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Change current working directory
//------------------------------------------------------------------------------
XRootDStatus DoCD( Query *query, Env *env,
                   const QueryExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log *log = DefaultEnv::GetLog();
  if( args.size() != 2 )
  {
    log->Error( AppMsg, "Invalid arguments. Expected a path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string newPath;
  if( !BuildPath( newPath, env, args[1] ).IsOK() )
  {
    log->Error( AppMsg, "Invalid path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  //----------------------------------------------------------------------------
  // Check if the path exist and is not a directory
  //----------------------------------------------------------------------------
  StatInfo *info;
  XRootDStatus st = query->Stat( newPath, info );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable to stat the path: %s", st.ToStr().c_str() );
    return st;
  }

  if( !info->TestFlags( StatInfo::IsDir ) )
  {
    log->Error( AppMsg, "%s is not a directory.", newPath.c_str() );
    return XRootDStatus( stError, errInvalidArgs );
  }

  env->PutString( "CWD", newPath );
  return XRootDStatus();
}

//------------------------------------------------------------------------------
// List a directory
//------------------------------------------------------------------------------
XRootDStatus DoLS( Query *query, Env *env,
                   const QueryExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log *log = DefaultEnv::GetLog();
  uint32_t    argc = args.size();
  uint8_t     flags = DirListFlags::Locate;
  bool        stats = false;
  std::string path;

  if( argc > 3 )
  {
    log->Error( AppMsg, "Too many arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  QueryExecutor::CommandParams::const_iterator argIt = args.begin();
  ++argIt;
  for( ; argIt != args.end(); ++argIt )
  {
    if( *argIt == "-l" )
    {
      stats = true;
      flags |= DirListFlags::Stat;
    }
    else
      path = *argIt;
  }

  std::string newPath;
  if( path.empty() )
    env->GetString( "CWD", newPath );
  else
  {
    if( !BuildPath( newPath, env, path ).IsOK() )
    {
      log->Error( AppMsg, "Invalid arguments. Invalid path." );
      return XRootDStatus( stError, errInvalidArgs );
    }
  }

  log->Debug( AppMsg, "Attempting to list: %s", newPath.c_str() );

  //----------------------------------------------------------------------------
  // Ask for the list
  //----------------------------------------------------------------------------
  DirectoryList *list;
  XRootDStatus st = query->DirList( newPath, flags, list );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable to list the path: %s", st.ToStr().c_str() );
    return st;
  }

  if( st.code == suPartial )
    log->Info( AppMsg, "Some of the requests failed. The result may be "
                       "incomplete" );

  //----------------------------------------------------------------------------
  // Print the results
  //----------------------------------------------------------------------------
  DirectoryList::Iterator it;
  for( it = list->Begin(); it != list->End(); ++it )
  {
    if( stats )
    {
      StatInfo *info = (*it)->GetStatInfo();
      if( !info )
      {
        std::cout << "---- 0000-00-00 00:00:00            ? ";
      }
      else
      {
        if( info->TestFlags( StatInfo::IsDir ) )
          std::cout << "d";
        else
          std::cout << "-";

        if( info->TestFlags( StatInfo::IsReadable ) )
          std::cout << "r";
        else
          std::cout << "-";

        if( info->TestFlags( StatInfo::IsWritable ) )
          std::cout << "w";
        else
          std::cout << "-";

        if( info->TestFlags( StatInfo::XBitSet ) )
          std::cout << "x";
        else
          std::cout << "-";

        std::cout << " " << info->GetModTimeAsString();

        std::cout << std::setw(12) << info->GetSize() << " ";
      }
    }
    std::cout << "root://" << (*it)->GetHostAddress() << "/";
    std::cout << list->GetParentName() << (*it)->GetName() << std::endl;
  }

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Create a directory
//------------------------------------------------------------------------------
XRootDStatus DoMkDir( Query *query, Env *env,
                      const QueryExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc < 2 || argc > 4 )
  {
    log->Error( AppMsg, "Too few arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  uint8_t      flags   = MkDirFlags::None;
  uint16_t     mode    = 0;
  std::string  modeStr = "rwxr-x---";
  std::string  path    = "";

  for( int i = 1; i < args.size(); ++i )
  {
    if( args[i] == "-p" )
      flags |= MkDirFlags::MakePath;
    else if( !args[i].compare( 0, 2, "-m" ) )
      modeStr = args[i].substr( 2, 9 );
    else
      path = args[i];
  }

  XRootDStatus st = ConvertMode( mode, modeStr );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Invalid mode string." );
    return st;
  }

  std::string newPath;
  if( !BuildPath( newPath, env, path ).IsOK() )
  {
    log->Error( AppMsg, "Invalid path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  //----------------------------------------------------------------------------
  // Run the query
  //----------------------------------------------------------------------------
  st = query->MkDir( newPath, flags, mode );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable create directory %s: %s",
                        newPath.c_str(),
                        st.ToStr().c_str() );
    return st;
  }

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Remove a directory
//------------------------------------------------------------------------------
XRootDStatus DoRmDir( Query *query, Env *env,
                      const QueryExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc != 2 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string fullPath;
  if( !BuildPath( fullPath, env, args[1] ).IsOK() )
  {
    log->Error( AppMsg, "Invalid path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  //----------------------------------------------------------------------------
  // Run the query
  //----------------------------------------------------------------------------
  XRootDStatus st = query->RmDir( fullPath );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable remove directory %s: %s",
                        fullPath.c_str(),
                        st.ToStr().c_str() );
    return st;
  }

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Move a file or directory
//------------------------------------------------------------------------------
XRootDStatus DoMv( Query *query, Env *env,
                   const QueryExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc != 3 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string fullPath1;
  if( !BuildPath( fullPath1, env, args[1] ).IsOK() )
  {
    log->Error( AppMsg, "Invalid source path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string fullPath2;
  if( !BuildPath( fullPath2, env, args[2] ).IsOK() )
  {
    log->Error( AppMsg, "Invalid destination path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  //----------------------------------------------------------------------------
  // Run the query
  //----------------------------------------------------------------------------
  XRootDStatus st = query->Mv( fullPath1, fullPath2 );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable move %s to %s: %s",
                        fullPath1.c_str(), fullPath2.c_str(),
                        st.ToStr().c_str() );
    return st;
  }

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Remove a file
//------------------------------------------------------------------------------
XRootDStatus DoRm( Query *query, Env *env,
                   const QueryExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc != 2 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string fullPath;
  if( !BuildPath( fullPath, env, args[1] ).IsOK() )
  {
    log->Error( AppMsg, "Invalid path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  //----------------------------------------------------------------------------
  // Run the query
  //----------------------------------------------------------------------------
  XRootDStatus st = query->Rm( fullPath );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable remove %s: %s",
                        fullPath.c_str(),
                        st.ToStr().c_str() );
    return st;
  }

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Truncate a file
//------------------------------------------------------------------------------
XRootDStatus DoTruncate( Query *query, Env *env,
                         const QueryExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc != 3 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string fullPath;
  if( !BuildPath( fullPath, env, args[1] ).IsOK() )
  {
    log->Error( AppMsg, "Invalid path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  char *result;
  uint64_t size = ::strtoll( args[2].c_str(), &result, 0 );
  if( *result != 0 )
  {
    log->Error( AppMsg, "Size parameter needs to be an integer" );
    return XRootDStatus( stError, errInvalidArgs );
  }

  //----------------------------------------------------------------------------
  // Run the query
  //----------------------------------------------------------------------------
  XRootDStatus st = query->Truncate( fullPath, size );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable truncate %s: %s",
                        fullPath.c_str(),
                        st.ToStr().c_str() );
    return st;
  }

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Change the access rights to a file
//------------------------------------------------------------------------------
XRootDStatus DoChMod( Query *query, Env *env,
                      const QueryExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc != 3 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string fullPath;
  if( !BuildPath( fullPath, env, args[1] ).IsOK() )
  {
    log->Error( AppMsg, "Invalid path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  uint16_t mode;
  XRootDStatus st = ConvertMode( mode, args[2] );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Invalid mode string." );
    return st;
  }

  //----------------------------------------------------------------------------
  // Run the query
  //----------------------------------------------------------------------------
  st = query->ChMod( fullPath, mode );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable change mode of %s: %s",
                        fullPath.c_str(),
                        st.ToStr().c_str() );
    return st;
  }

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Locate a path
//------------------------------------------------------------------------------
XRootDStatus DoLocate( Query *query, Env *env,
                       const QueryExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc > 4 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string path;
  uint16_t    flags        = OpenFlags::None;
  bool        hasPath      = false;
  bool        doDeepLocate = false;
  for( int i = 1; i < argc; ++i )
  {
    if( args[i] == "-n" )
      flags |= OpenFlags::NoWait;
    else if( args[i] == "-r" )
      flags |= OpenFlags::Refresh;
    else if( args[i] == "-d" )
      doDeepLocate = true;
    else if( !hasPath )
    {
      path = args[i];
      hasPath = true;
    }
    else
    {
      log->Error( AppMsg, "Invalid argument: %s.", args[i].c_str() );
      return XRootDStatus( stError, errInvalidArgs );
    }
  }

  std::string fullPath;
  if( !BuildPath( fullPath, env, path ).IsOK() )
  {
    log->Error( AppMsg, "Invalid path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  //----------------------------------------------------------------------------
  // Run the query
  //----------------------------------------------------------------------------
  LocationInfo *info = 0;
  XRootDStatus  st;
  if( doDeepLocate )
    st = query->DeepLocate( fullPath, flags, info );
  else
    st = query->Locate( fullPath, flags, info );

  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable locate %s: %s",
                        fullPath.c_str(),
                        st.ToStr().c_str() );
    return st;
  }

  //----------------------------------------------------------------------------
  // Print the result
  //----------------------------------------------------------------------------
  LocationInfo::Iterator it;
  for( it = info->Begin(); it != info->End(); ++it )
  {
    std::cout << it->GetAddress() << " ";
    switch( it->GetType() )
    {
      case LocationInfo::ManagerOnline:
        std::cout << "Manager ";
        break;
      case LocationInfo::ManagerPending:
        std::cout << "ManagerPending ";
        break;
      case LocationInfo::ServerOnline:
        std::cout << "Server ";
        break;
      case LocationInfo::ServerPending:
        std::cout << "ServerPending ";
        break;
      default:
        std::cout << "Unknown ";
    };

    switch( it->GetAccessType() )
    {
      case LocationInfo::Read:
        std::cout << "Read";
        break;
      case LocationInfo::ReadWrite:
        std::cout << "ReadWrite ";
        break;
      default:
        std::cout << "Unknown ";
    };
    std::cout << std::endl;
  }

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Stat a path
//------------------------------------------------------------------------------
XRootDStatus DoStat( Query *query, Env *env,
                     const QueryExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc != 2 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string fullPath;
  if( !BuildPath( fullPath, env, args[1] ).IsOK() )
  {
    log->Error( AppMsg, "Invalid path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  //----------------------------------------------------------------------------
  // Run the query
  //----------------------------------------------------------------------------
  StatInfo *info = 0;
  XRootDStatus st = query->Stat( fullPath, info );

  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable stat %s: %s",
                        fullPath.c_str(),
                        st.ToStr().c_str() );
    return st;
  }

  //----------------------------------------------------------------------------
  // Print the result
  //----------------------------------------------------------------------------
  std::string flags;

  if( info->TestFlags( StatInfo::XBitSet ) )
    flags += "XBitSet|";
  if( info->TestFlags( StatInfo::IsDir ) )
    flags += "IsDir|";
  if( info->TestFlags( StatInfo::Other ) )
    flags += "Other|";
  if( info->TestFlags( StatInfo::Offline ) )
    flags += "Offline|";
  if( info->TestFlags( StatInfo::POSCPending ) )
    flags += "POSCPending|";
  if( info->TestFlags( StatInfo::IsReadable ) )
    flags += "IsReadable|";
  if( info->TestFlags( StatInfo::IsWritable ) )
    flags += "IsWritable|";

  if( !flags.empty() )
    flags.erase( flags.length()-1, 1 );

  std::cout << "Path:  " << fullPath << std::endl;
  std::cout << "Id:    " << info->GetId() << std::endl;
  std::cout << "Size:  " << info->GetSize() << std::endl;
  std::cout << "Flags: " << info->GetFlags() << " (" << flags << ")";
  std::cout << std::endl;

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Stat a VFS
//------------------------------------------------------------------------------
XRootDStatus DoStatVFS( Query *query, Env *env,
                        const QueryExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc != 2 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string fullPath;
  if( !BuildPath( fullPath, env, args[1] ).IsOK() )
  {
    log->Error( AppMsg, "Invalid path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  //----------------------------------------------------------------------------
  // Run the query
  //----------------------------------------------------------------------------
  StatInfoVFS *info = 0;
  XRootDStatus st = query->StatVFS( fullPath, info );

  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable stat VFS at %s: %s",
                        fullPath.c_str(),
                        st.ToStr().c_str() );
    return st;
  }

  //----------------------------------------------------------------------------
  // Print the result
  //----------------------------------------------------------------------------
  std::cout << "Path:                             ";
  std::cout << fullPath << std::endl;
  std::cout << "Nodes with RW space:              ";
  std::cout << info->GetNodesRW() << std::endl;
  std::cout << "Size of RW space (MB):            ";
  std::cout << info->GetFreeRW() << std::endl;
  std::cout << "Utilization of RW space (%):      ";
  std::cout << info->GetUtilizationRW() << std::endl;
  std::cout << "Nodes with staging space:         ";
  std::cout << info->GetNodesStaging() << std::endl;
  std::cout << "Size of staging space (MB):       ";
  std::cout << info->GetFreeStaging() << std::endl;
  std::cout << "Utilization of staging space (%): ";
  std::cout << info->GetUtilizationStaging() << std::endl;

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Print help
//------------------------------------------------------------------------------
XRootDStatus PrintHelp( Query *query, Env *env,
                        const QueryExecutor::CommandParams &args )
{
  std::cout << "Usage:" << std::endl;
  std::cout << "   xrdquery host[:port]              - interactive mode";
  std::cout << std::endl;
  std::cout << "   xrdquery host[:port] command args - batch mode";
  std::cout << std::endl << std::endl;

  std::cout << "Available commands:" << std::endl << std::endl;

  std::cout << "   chmod <path> <user><group><other>" << std::endl;
  std::cout << "     Modify file permissions. Permission example:";
  std::cout << std::endl;
  std::cout << "     rwxr-x--x" << std::endl << std::endl;

  std::cout << "   cd <path>" << std::endl;
  std::cout << "     Change the current working directory";
  std::cout << std::endl << std::endl;

  std::cout << "   ls [-l] [dirname]" << std::endl;
  std::cout << "     Get directory listing." << std::endl << std::endl;

  std::cout << "   exit" << std::endl;
  std::cout << "     Exits from the program." << std::endl << std::endl;

  std::cout << "   help" << std::endl;
  std::cout << "     This help screen." << std::endl << std::endl;

  std::cout << "   stat <path>" << std::endl;
  std::cout << "     Get info about the file or directory." << std::endl;
  std::cout << std::endl;

  std::cout << "   statvfs [path]" << std::endl;
  std::cout << "     Get info about a virtual file system." << std::endl;
  std::cout << std::endl;

  std::cout << "   locate <path> [NoWait|Refresh]" << std::endl;
  std::cout << "     Get the locations of the path." << std::endl;
  std::cout << std::endl;

  std::cout << "   deep-locate <path> [NoWait|Refresh]" << std::endl;
  std::cout << "     Find file servers hosting the path." << std::endl;
  std::cout << std::endl;

  std::cout << "   mv <path1> <path2>" << std::endl;
  std::cout << "     Move path1 to path2 locally on the same server.";
  std::cout << std::endl << std::endl;

  std::cout << "   mkdir <dirname> [-p] [-m<user><group><other>]";
  std::cout << std::endl;
  std::cout << "     Creates a directory/tree of directories.";
  std::cout << std::endl << std::endl;

  std::cout << "   rm <filename>" << std::endl;
  std::cout << "     Remove a file." << std::endl << std::endl;

  std::cout << "   rmdir <dirname>" << std::endl;
  std::cout << "     Remove a directory." << std::endl << std::endl;

  std::cout << "   query <code> <parms>";
  std::cout << "Obtain server information. Query codes:" << std::endl;

  std::cout << "     Config <what>              ";
  std::cout << "Query server configuration"     << std::endl;

  std::cout << "     ChecksumCancel <path>      ";
  std::cout << "File checksum cancelation"      << std::endl;

  std::cout << "     Checksum <path>            ";
  std::cout << "Query file checksum"            << std::endl;

  std::cout << "     Opaque <arg>               ";
  std::cout << "Implementation dependent"       << std::endl;

  std::cout << "     OpaqueFile <arg>           ";
  std::cout << "Implementation dependent"       << std::endl;

  std::cout << "     Space <spacename>          ";
  std::cout << "Query logical space stats"      << std::endl;

  std::cout << "     Stats <what>               ";
  std::cout << "Query server stats"             << std::endl;

  std::cout << "     XAttr <path>               ";
  std::cout << "Query file extended attributes" << std::endl;
  std::cout << std::endl;

  std::cout << "   truncate <filename> <length> " << std::endl;
  std::cout << "     Truncate a file." << std::endl << std::endl;

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Create the executor object
//------------------------------------------------------------------------------
QueryExecutor *CreateExecutor( const URL &url )
{
  Env *env = new Env();
  env->PutString( "CWD", "/" );
  QueryExecutor *executor = new QueryExecutor( url, env );
  executor->AddCommand( "cd",          DoCD         );
  executor->AddCommand( "chmod",       DoChMod      );
  executor->AddCommand( "ls",          DoLS         );
  executor->AddCommand( "help",        PrintHelp    );
  executor->AddCommand( "stat",        DoStat       );
  executor->AddCommand( "statvfs",     DoStatVFS    );
  executor->AddCommand( "locate",      DoLocate     );
  executor->AddCommand( "mv",          DoMv         );
  executor->AddCommand( "mkdir",       DoMkDir      );
  executor->AddCommand( "rm",          DoRm         );
  executor->AddCommand( "rmdir",       DoRmDir      );
  executor->AddCommand( "query",       PrintHelp    );
  executor->AddCommand( "truncate",    DoTruncate   );
  return executor;
}

//------------------------------------------------------------------------------
// Execute command
//------------------------------------------------------------------------------
int ExecuteCommand( QueryExecutor *ex, const std::string &commandline )
{
  Log *log = DefaultEnv::GetLog();
  XRootDStatus st = ex->Execute( commandline );
  if( !st.IsOK() )
    log->Error( AppMsg, "Error executing %s: %s", commandline.c_str(),
                        st.ToStr().c_str() );
  return st.GetShellCode();
}

//------------------------------------------------------------------------------
// Define some functions required to function when build without readline
//------------------------------------------------------------------------------
#ifndef HAVE_READLINE
char *readline(const char *prompt)
{
    std::cout << prompt << std::flush;
    std::string input;
    std::getline( std::cin, input );

    if( !std::cin.good() )
        return 0;

    char *linebuf = (char *)malloc( input.size()+1 );
    strncpy( linebuf, input.c_str(), input.size()+1 );

    return linebuf;
}

void add_history( const char * )
{
}

void rl_bind_key( char c, uint16_t action )
{
}

uint16_t rl_abort = 0;

int read_history( const char * )
{
  return 0;
}

int write_history( const char * )
{
  return 0;
}
#endif

//------------------------------------------------------------------------------
// Build the command prompt
//------------------------------------------------------------------------------
std::string BuildPrompt( Env *env, const URL &url )
{
  std::ostringstream prompt;
  std::string cwd = "/";
  env->GetString( "CWD", cwd );
  prompt << "[" << url.GetHostId() << "] " << cwd << " > ";
  return prompt.str();
}

//------------------------------------------------------------------------------
// Execute interactive
//------------------------------------------------------------------------------
int ExecuteInteractive( const URL &url )
{
  //----------------------------------------------------------------------------
  // Set up the environment
  //----------------------------------------------------------------------------
  std::string historyFile = getenv( "HOME" );
  historyFile += "/.xrdquery.history";
  rl_bind_key( '\t', rl_abort );
  read_history( historyFile.c_str() );
  QueryExecutor *ex = CreateExecutor( url );

  //----------------------------------------------------------------------------
  // Execute the commands
  //----------------------------------------------------------------------------
  while(1)
  {
    char *linebuf = 0;
    linebuf = readline( BuildPrompt( ex->GetEnv(), url ).c_str() );
    if( !linebuf || !strcmp( linebuf, "exit" ))
    {
      std::cout << "Goodbye." << std::endl << std::endl;
      break;
    }
    if( !*linebuf)
    {
      free( linebuf );
      continue;
    }
    Status xs = XRootDStatus();
    ex->Execute( linebuf );
    add_history( linebuf );
    free( linebuf );
  }

  //----------------------------------------------------------------------------
  // Cleanup
  //----------------------------------------------------------------------------
  delete ex;
  write_history( historyFile.c_str() );
  return 0;
}

//------------------------------------------------------------------------------
// Execute command
//------------------------------------------------------------------------------
int ExecuteCommand( const URL &url, int argc, char **argv )
{
  //----------------------------------------------------------------------------
  // Build the command to be executed
  //----------------------------------------------------------------------------
  std::string commandline;
  for( int i = 0; i < argc; ++i )
  {
    commandline += argv[i];
    commandline += " ";
  }

  QueryExecutor *ex = CreateExecutor( url );
  int st = ExecuteCommand( ex, commandline );
  delete ex;
  return st;
}

//------------------------------------------------------------------------------
// Start the show
//------------------------------------------------------------------------------
int main( int argc, char **argv )
{
  //----------------------------------------------------------------------------
  // Check the commandline parameters
  //----------------------------------------------------------------------------
  XrdClient::QueryExecutor::CommandParams params;
  if( argc == 1 )
  {
    PrintHelp( 0, 0, params );
    return 1;
  }

  if( !strcmp( argv[1], "--help" ) ||
      !strcmp( argv[1], "-h" ) )
  {
    PrintHelp( 0, 0, params );
    return 0;
  }

  URL url( argv[1] );
  if( !url.IsValid() )
  {
    PrintHelp( 0, 0, params );
    return 1;
  }

  if( argc == 2 )
    return ExecuteInteractive( url );
  return ExecuteCommand( url, argc-2, argv+2 );
}
