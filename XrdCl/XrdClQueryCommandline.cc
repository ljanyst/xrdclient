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

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

using namespace XrdClient;

//------------------------------------------------------------------------------
// Print help
//------------------------------------------------------------------------------
XRootDStatus PrintHelp( Query *query, Env *env,
                        const std::list<std::string> &args )
{
  std::cout << "Usage:" << std::endl;
  std::cout << "   xrdquery host[:port]              - interactive mode";
  std::cout << std::endl;
  std::cout << "   xrdquery host[:port] command args - batch mode";
  std::cout << std::endl << std::endl;

  std::cout << "Available commands:" << std::endl << std::endl;

  std::cout << "   chmod <path> <user> <group> <other>" << std::endl;
  std::cout << "     Modify file permissions. Permission example:";
  std::cout << std::endl;
  std::cout << "     rwx r-x --x ---" << std::endl << std::endl;

  std::cout << "   ls [dirname]" << std::endl;
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

  std::cout << "   mkdir <dirname> [MakePath] [<user> <group> <other>]";
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
  executor->AddCommand( "chmod",       PrintHelp );
  executor->AddCommand( "ls",          PrintHelp );
  executor->AddCommand( "help",        PrintHelp );
  executor->AddCommand( "stat",        PrintHelp );
  executor->AddCommand( "statvfs",     PrintHelp );
  executor->AddCommand( "locate",      PrintHelp );
  executor->AddCommand( "deep-locate", PrintHelp );
  executor->AddCommand( "mv",          PrintHelp );
  executor->AddCommand( "mkdir",       PrintHelp );
  executor->AddCommand( "rm",          PrintHelp );
  executor->AddCommand( "rmdir",       PrintHelp );
  executor->AddCommand( "query",       PrintHelp );
  executor->AddCommand( "truncate",    PrintHelp );
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
int ExecuteInteractive( const URL &url)
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
  if( argc == 1 )
  {
    PrintHelp( 0, 0, std::list<std::string>() );
    return 1;
  }

  if( !strcmp( argv[1], "--help" ) ||
      !strcmp( argv[1], "-h" ) )
  {
    PrintHelp( 0, 0, std::list<std::string>() );
    return 0;
  }

  URL url( argv[1] );
  if( !url.IsValid() )
  {
    PrintHelp( 0, 0, std::list<std::string>() );
    return 1;
  }

  if( argc == 2 )
    return ExecuteInteractive( url );
  return ExecuteCommand( url, argc-2, argv+2 );
}