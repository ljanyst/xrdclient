//------------------------------------------------------------------------------
// Copyright (c) 2011 by European Organization for Nuclear Research (CERN)
// Author: Lukasz Janyst <ljanyst@cern.ch>
// See the LICENCE file for details.
//------------------------------------------------------------------------------

#include <cstdlib>

#include "XrdCl/XrdClEnv.hh"
#include "XrdCl/XrdClLog.hh"
#include "XrdCl/XrdClUtils.hh"
#include "XrdCl/XrdClConstants.hh"

namespace XrdClient
{
  //----------------------------------------------------------------------------
  // Get string
  //----------------------------------------------------------------------------
  bool Env::GetString( const std::string &key, std::string &value )
  {
    XrdSysRWLockHelper( pLock );
    StringMap::iterator it;
    it = pStringMap.find( key );
    if( it == pStringMap.end() )
    {
      Log *log = Utils::GetDefaultLog();
      log->Debug( UtilityMsg,
                  "Env: trying to get a non-existent string entry: %s",
                  key.c_str() );
      return false;
    }
    value = it->second.first;
    return true;
  }

  //----------------------------------------------------------------------------
  // Put string
  //----------------------------------------------------------------------------
  bool Env::PutString( const std::string &key, const std::string &value )
  {
    XrdSysRWLockHelper( pLock, false );

    //--------------------------------------------------------------------------
    // Insert the string if it's not there yet
    //--------------------------------------------------------------------------
    StringMap::iterator it;
    it = pStringMap.find( key );
    if( it == pStringMap.end() )
    {
      pStringMap[key] = std::make_pair( value, false );
      return true;
    }

    //--------------------------------------------------------------------------
    // The entry exists and it has been imported from the shell
    //--------------------------------------------------------------------------
    Log *log = Utils::GetDefaultLog();
    if( it->second.second )
    {
      log->Debug( UtilityMsg,
                  "Env: trying to override a shell-imported string entry: %s",
                  key.c_str() );
      return false;
    }
    log->Debug( UtilityMsg,
                "Env: overriding entry: %s=%s with %s",
                key.c_str(), it->second.first.c_str(), value.c_str() );
    pStringMap[key] = std::make_pair( value, false );
    return true;
  }

  //----------------------------------------------------------------------------
  // Get int
  //----------------------------------------------------------------------------
  bool Env::GetInt( const std::string &key, int &value )
  {
    XrdSysRWLockHelper( pLock );
    IntMap::iterator it;
    it = pIntMap.find( key );
    if( it == pIntMap.end() )
    {
      Log *log = Utils::GetDefaultLog();
      log->Debug( UtilityMsg,
                  "Env: trying to get a non-existent integer entry: %s",
                  key.c_str() );
      return false;
    }
    value = it->second.first;
    return true;
  }

  //----------------------------------------------------------------------------
  // Put int
  //----------------------------------------------------------------------------
  bool Env::PutInt( const std::string &key, int value )
  {
    XrdSysRWLockHelper( pLock, false );

    //--------------------------------------------------------------------------
    // Insert the string if it's not there yet
    //--------------------------------------------------------------------------
    IntMap::iterator it;
    it = pIntMap.find( key );
    if( it == pIntMap.end() )
    {
      pIntMap[key] = std::make_pair( value, false );
      return true;
    }

    //--------------------------------------------------------------------------
    // The entry exists and it has been imported from the shell
    //--------------------------------------------------------------------------
    Log *log = Utils::GetDefaultLog();
    if( it->second.second )
    {
      log->Debug( UtilityMsg,
                  "Env: trying to override a shell-imported integer entry: %s",
                  key.c_str() );
      return false;
    }
    log->Debug( UtilityMsg,
                "Env: overriding entry: %s=%d with %d",
                key.c_str(), it->second.first, value );

    pIntMap[key] = std::make_pair( value, false );
    return true;
  }

  //----------------------------------------------------------------------------
  // Import int
  //----------------------------------------------------------------------------
  bool Env::ImportInt( const std::string &key, const std::string &shellKey )
  {
    XrdSysRWLockHelper( pLock, false );
    std::string strValue = GetEnv( shellKey );
    if( strValue == "" )
      return false;

    Log *log = Utils::GetDefaultLog();
    char *endPtr;
    int value = (int)strtol( strValue.c_str(), &endPtr, 0 );
    if( *endPtr )
    {
      log->Error( UtilityMsg,
                  "Env: Unable to import %s as %s: %s is not a proper integer",
                  shellKey.c_str(), key.c_str(), strValue.c_str() );
      return false;
    }

    log->Info( UtilityMsg, "Env: Importing from shell %s=%d as %s",
               shellKey.c_str(), value, key.c_str() );

    pIntMap[key] = std::make_pair( value, true );
    return true;
  }

  //----------------------------------------------------------------------------
  // Import string
  //----------------------------------------------------------------------------
  bool Env::ImportString( const std::string &key, const std::string &shellKey )
  {
    XrdSysRWLockHelper( pLock, false );
    std::string value = GetEnv( shellKey );
    if( value == "" )
      return false;

    Log *log = Utils::GetDefaultLog();
    log->Info( UtilityMsg, "Env: Importing from shell %s=%s as %s",
               shellKey.c_str(), value.c_str(), key.c_str() );
    pStringMap[key] = std::make_pair( value, true );
    return true;
  }

  //----------------------------------------------------------------------------
  // Get a string from the environment
  //----------------------------------------------------------------------------
  std::string Env::GetEnv( const std::string &key )
  {
    Log *log = Utils::GetDefaultLog();
    char *var = getenv( key.c_str() );
    if( !var )
      return "";
    return var;
  }
}
