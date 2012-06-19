//------------------------------------------------------------------------------
// Copyright (c) 2012 by European Organization for Nuclear Research (CERN)
// Author: Lukasz Janyst <ljanyst@cern.ch>
// See the LICENCE file for details.
//------------------------------------------------------------------------------

#include "XrdCl/XrdClMessageUtils.hh"
#include "XrdCl/XrdClFileSystem.hh"
#include "XrdCl/XrdClDefaultEnv.hh"
#include "XrdCl/XrdClLog.hh"
#include "XrdCl/XrdClConstants.hh"
#include "XrdCl/XrdClMessage.hh"
#include "XrdCl/XrdClXRootDResponses.hh"
#include "XrdCl/XrdClRequestSync.hh"
#include "XrdSys/XrdSysPthread.hh"

#include <memory>

namespace
{
  //----------------------------------------------------------------------------
  // Deep locate handler
  //----------------------------------------------------------------------------
  class DeepLocateHandler: public XrdCl::ResponseHandler
  {
    public:
      //------------------------------------------------------------------------
      // Constructor
      //------------------------------------------------------------------------
      DeepLocateHandler( XrdCl::ResponseHandler *handler,
                         const std::string          &path,
                         uint16_t                    flags ):
        pFirstTime( true ),
        pOutstanding( 1 ),
        pHandler( handler ),
        pPath( path ),
        pFlags( flags )
      {
        pLocations = new XrdCl::LocationInfo();
      }

      //------------------------------------------------------------------------
      // Destructor
      //------------------------------------------------------------------------
      ~DeepLocateHandler()
      {
        delete pLocations;
      }

      //------------------------------------------------------------------------
      // Handle the response
      //------------------------------------------------------------------------
      virtual void HandleResponse( XrdCl::XRootDStatus *status,
                                   XrdCl::AnyObject    *response )
      {
        using namespace XrdCl;
        Log *log = DefaultEnv::GetLog();
        --pOutstanding;

        //----------------------------------------------------------------------
        // We've got an error, react accordingly
        //----------------------------------------------------------------------
        if( !status->IsOK() )
        {
          log->Dump( QueryMsg, "[DeepLocate] Got error response" );

          //--------------------------------------------------------------------
          // We have failed with the first request
          //--------------------------------------------------------------------
          if( pFirstTime )
          {
            log->Debug( QueryMsg, "[DeepLocate] Failed to get the initial "
                                  "location list" );
            pHandler->HandleResponse( status, response );
            delete this;
            return;
          }

          //--------------------------------------------------------------------
          // We have no more outstanding requests, so let give to the client
          // what we have
          //--------------------------------------------------------------------
          if( !pOutstanding )
          {
            log->Debug( QueryMsg, "[DeepLocate] No outstanding requests, "
                                  "give out what we've got" );
            HandleResponse();
          }
          delete status;
          return;
        }
        pFirstTime = false;

        //----------------------------------------------------------------------
        // Extract the answer
        //----------------------------------------------------------------------
        LocationInfo *info = 0;
        response->Get( info );
        LocationInfo::Iterator it;

        log->Dump( QueryMsg, "[DeepLocate] Got %d locations",
                             info->GetSize() );

        for( it = info->Begin(); it != info->End(); ++it )
        {
          //--------------------------------------------------------------------
          // Add the location to the list
          //--------------------------------------------------------------------
          if( it->IsServer() )
          {
            pLocations->Add( *it );
            continue;
          }

          //--------------------------------------------------------------------
          // Ask the manager for the location of servers
          //--------------------------------------------------------------------
          if( it->IsManager() )
          {
            FileSystem fs( it->GetAddress() );
            //!! FIXME timeout
            if( fs.Locate( pPath, pFlags, this, 300 ).IsOK() )
              ++pOutstanding;
          }
        }

        //----------------------------------------------------------------------
        // Clean up and check if we have anything else to do
        //----------------------------------------------------------------------
        delete response;
        delete status;
        if( !pOutstanding )
          HandleResponse();
      }

      //------------------------------------------------------------------------
      // Build the response for the client
      //------------------------------------------------------------------------
      void HandleResponse()
      {
        using namespace XrdCl;

        //----------------------------------------------------------------------
        // Nothing found
        //----------------------------------------------------------------------
        if( !pLocations->GetSize() )
        {
          pHandler->HandleResponse( new XRootDStatus( stError, errErrorResponse,
                                                 kXR_NotFound,
                                                 "No valid location found" ),
                                    0 );
        }
        //----------------------------------------------------------------------
        // We return an answer
        //----------------------------------------------------------------------
        else
        {
          AnyObject *obj = new AnyObject();
          obj->Set( pLocations );
          pLocations = 0;
          pHandler->HandleResponse( new XRootDStatus(), obj );
        }
        delete this;
      }

    private:
      bool                        pFirstTime;
      uint16_t                    pOutstanding;
      XrdCl::ResponseHandler *pHandler;
      XrdCl::LocationInfo    *pLocations;
      std::string                 pPath;
      uint16_t                    pFlags;
  };

  //----------------------------------------------------------------------------
  // Handle stat results for a dirlist request
  //----------------------------------------------------------------------------
  class DirListStatHandler: public XrdCl::ResponseHandler
  {
    public:
      //------------------------------------------------------------------------
      // Constructor
      //------------------------------------------------------------------------
      DirListStatHandler( XrdCl::DirectoryList *list,
                          uint32_t                  index,
                          XrdCl::RequestSync   *sync ):
        pList( list ),
        pIndex( index ),
        pSync( sync )
      {
      }

      //------------------------------------------------------------------------
      // Check if we were successful and if so put the StatInfo object
      // in the appropriate entry info
      //------------------------------------------------------------------------
      virtual void HandleResponse( XrdCl::XRootDStatus *status,
                                   XrdCl::AnyObject    *response )
      {
        if( !status->IsOK() )
        {
          delete status;
          pSync->TaskDone( false );
          delete this;
          return;
        }

        XrdCl::StatInfo *info = 0;
        response->Get( info );
        response->Set( (char*) 0 );
        pList->At( pIndex )->SetStatInfo( info );
        delete status;
        delete response;
        pSync->TaskDone();
        delete this;
      }

    private:
      XrdCl::DirectoryList *pList;
      uint32_t                  pIndex;
      XrdCl::RequestSync   *pSync;
  };
}

namespace XrdCl
{
  //----------------------------------------------------------------------------
  // Constructor
  //----------------------------------------------------------------------------
  FileSystem::FileSystem( const URL &url )
  {
    pUrl = new URL( url.GetURL() );
  }

  //----------------------------------------------------------------------------
  // Destructor
  //----------------------------------------------------------------------------
  FileSystem::~FileSystem()
  {
    delete pUrl;
  }

  //----------------------------------------------------------------------------
  // Locate a file - async
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::Locate( const std::string &path,
                                   uint16_t           flags,
                                   ResponseHandler   *handler,
                                   uint16_t           timeout )
  {
    Log *log = DefaultEnv::GetLog();
    log->Dump( QueryMsg, "[%s] Sending a kXR_locate request for path %s",
                         pUrl->GetHostId().c_str(), path.c_str() );

    Message             *msg;
    ClientLocateRequest *req;
    MessageUtils::CreateRequest( msg, req, path.length() );

    req->requestid = kXR_locate;
    req->options   = flags;
    req->dlen      = path.length();
    msg->Append( path.c_str(), path.length(), 24 );

    Status st = MessageUtils::SendMessage( *pUrl, msg, handler, timeout );

    if( !st.IsOK() )
      return st;

    return XRootDStatus();
  }

  //----------------------------------------------------------------------------
  // Locate a file - sync
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::Locate( const std::string  &path,
                                   uint16_t            flags,
                                   LocationInfo      *&response,
                                   uint16_t            timeout )
  {
    SyncResponseHandler handler;
    Status st = Locate( path, flags, &handler, timeout );
    if( !st.IsOK() )
      return st;

    return MessageUtils::WaitForResponse( &handler, response );
  }

  //----------------------------------------------------------------------------
  // Locate a file, recursively locate all disk servers - async
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::DeepLocate( const std::string &path,
                                       uint16_t           flags,
                                       ResponseHandler   *handler,
                                       uint16_t           timeout )
  {
    return Locate( path, flags, new DeepLocateHandler( handler, path, flags ), timeout );
  }

  //----------------------------------------------------------------------------
  // Locate a file, recursively locate all disk servers - sync
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::DeepLocate( const std::string  &path,
                                  uint16_t            flags,
                                  LocationInfo      *&response,
                                  uint16_t            timeout )
  {
    SyncResponseHandler handler;
    Status st = DeepLocate( path, flags, &handler, timeout );
    if( !st.IsOK() )
      return st;

    return MessageUtils::WaitForResponse( &handler, response );
  }

  //----------------------------------------------------------------------------
  // Move a directory or a file - async
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::Mv( const std::string &source,
                               const std::string &dest,
                               ResponseHandler   *handler,
                               uint16_t           timeout )
  {
    Log *log = DefaultEnv::GetLog();
    log->Dump( QueryMsg, "[%s] Sending a kXR_mv request to move %s to %s",
                         pUrl->GetHostId().c_str(),
                         source.c_str(), dest.c_str() );

    Message         *msg;
    ClientMvRequest *req;
    MessageUtils::CreateRequest( msg, req, source.length()+dest.length()+1 );

    req->requestid = kXR_mv;
    req->dlen      = source.length()+dest.length()+1;
    msg->Append( source.c_str(), source.length(), 24 );
    *msg->GetBuffer(24+source.length()) = ' ';
    msg->Append( dest.c_str(), dest.length(), 25+source.length() );
    Status st = MessageUtils::SendMessage( *pUrl, msg, handler, timeout );

    if( !st.IsOK() )
      return st;

    return XRootDStatus();
  }

  //----------------------------------------------------------------------------
  // Move a directory or a file - sync
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::Mv( const std::string &source,
                               const std::string &dest,
                               uint16_t           timeout )
  {
    SyncResponseHandler handler;
    Status st = Mv( source, dest, &handler, timeout );
    if( !st.IsOK() )
      return st;

    return MessageUtils::WaitForStatus( &handler );
  }

  //----------------------------------------------------------------------------
  // Obtain server information - async
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::Query( QueryCode::Code  queryCode,
                                  const Buffer    &arg,
                                  ResponseHandler *handler,
                                  uint16_t         timeout )
  {
    Log *log = DefaultEnv::GetLog();
    log->Dump( QueryMsg, "[%s] Sending a kXR_query request [%d]",
                         pUrl->GetHostId().c_str(), queryCode );

    Message            *msg;
    ClientQueryRequest *req;
    MessageUtils::CreateRequest( msg, req, arg.GetSize() );

    req->requestid = kXR_query;
    req->infotype  = queryCode;
    req->dlen      = arg.GetSize();
    msg->Append( arg.GetBuffer(), arg.GetSize(), 24 );

    Status st = MessageUtils::SendMessage( *pUrl, msg, handler, timeout );

    if( !st.IsOK() )
      return st;

    return XRootDStatus();
  }

  //----------------------------------------------------------------------------
  // Obtain server information - sync
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::Query( QueryCode::Code   queryCode,
                                  const Buffer     &arg,
                                  Buffer          *&response,
                                  uint16_t          timeout )
  {
    SyncResponseHandler handler;
    Status st = Query( queryCode, arg, &handler, timeout );
    if( !st.IsOK() )
      return st;

    return MessageUtils::WaitForResponse( &handler, response );
  }

  //----------------------------------------------------------------------------
  // Truncate a file - async
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::Truncate( const std::string &path,
                                     uint64_t           size,
                                     ResponseHandler   *handler,
                                     uint16_t           timeout )
  {
    Log    *log = DefaultEnv::GetLog();
    log->Dump( QueryMsg, "[%s] Sending a kXR_truncate request for path %s",
                         pUrl->GetHostId().c_str(), path.c_str() );

    Message               *msg;
    ClientTruncateRequest *req;
    MessageUtils::CreateRequest( msg, req, path.length() );

    req->requestid = kXR_truncate;
    req->offset    = size;
    req->dlen      = path.length();
    msg->Append( path.c_str(), path.length(), 24 );

    Status st = MessageUtils::SendMessage( *pUrl, msg, handler, timeout );

    if( !st.IsOK() )
      return st;

    return XRootDStatus();
  }

  //----------------------------------------------------------------------------
  // Truncate a file - sync
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::Truncate( const std::string &path,
                                     uint64_t           size,
                                     uint16_t           timeout )
  {
    SyncResponseHandler handler;
    Status st = Truncate( path, size, &handler, timeout );
    if( !st.IsOK() )
      return st;

    return MessageUtils::WaitForStatus( &handler );
  }

  //----------------------------------------------------------------------------
  // Remove a file - async
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::Rm( const std::string &path,
                               ResponseHandler   *handler,
                               uint16_t           timeout )
  {
    Log    *log = DefaultEnv::GetLog();
    log->Dump( QueryMsg, "[%s] Sending a kXR_rm request for path %s",
                         pUrl->GetHostId().c_str(), path.c_str() );

    Message         *msg;
    ClientRmRequest *req;
    MessageUtils::CreateRequest( msg, req, path.length() );

    req->requestid = kXR_rm;
    req->dlen      = path.length();
    msg->Append( path.c_str(), path.length(), 24 );

    Status st = MessageUtils::SendMessage( *pUrl, msg, handler, timeout );

    if( !st.IsOK() )
      return st;

    return XRootDStatus();
  }

  //----------------------------------------------------------------------------
  // Remove a file - sync
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::Rm( const std::string &path,
                               uint16_t           timeout )
  {
    SyncResponseHandler handler;
    Status st = Rm( path, &handler, timeout );
    if( !st.IsOK() )
      return st;

    return MessageUtils::WaitForStatus( &handler );
  }

  //----------------------------------------------------------------------------
  // Create a directory - async
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::MkDir( const std::string &path,
                                  uint8_t            flags,
                                  uint16_t           mode,
                                  ResponseHandler   *handler,
                                  uint16_t           timeout )
  {
    Log *log = DefaultEnv::GetLog();
    log->Dump( QueryMsg, "[%s] Sending a kXR_mkdir request for path %s",
                         pUrl->GetHostId().c_str(), path.c_str() );

    Message            *msg;
    ClientMkdirRequest *req;
    MessageUtils::CreateRequest( msg, req, path.length() );

    req->requestid  = kXR_mkdir;
    req->options[0] = flags;
    req->mode       = mode;
    req->dlen       = path.length();
    msg->Append( path.c_str(), path.length(), 24 );

    Status st = MessageUtils::SendMessage( *pUrl, msg, handler, timeout );

    if( !st.IsOK() )
      return st;

    return XRootDStatus();
  }

  //----------------------------------------------------------------------------
  // Create a directory - sync
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::MkDir( const std::string &path,
                                  uint8_t            flags,
                                  uint16_t           mode,
                                  uint16_t           timeout )
  {
    SyncResponseHandler handler;
    Status st = MkDir( path, flags, mode, &handler, timeout );
    if( !st.IsOK() )
      return st;

    return MessageUtils::WaitForStatus( &handler );
  }

  //----------------------------------------------------------------------------
  // Remove a directory - async
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::RmDir( const std::string &path,
                                  ResponseHandler   *handler,
                                  uint16_t           timeout )
  {
    Log    *log = DefaultEnv::GetLog();
    log->Dump( QueryMsg, "[%s] Sending a kXR_rmdir request for path %s",
                         pUrl->GetHostId().c_str(), path.c_str() );

    Message            *msg;
    ClientRmdirRequest *req;
    MessageUtils::CreateRequest( msg, req, path.length() );

    req->requestid  = kXR_rmdir;
    req->dlen       = path.length();
    msg->Append( path.c_str(), path.length(), 24 );

    Status st = MessageUtils::SendMessage( *pUrl, msg, handler, timeout );

    if( !st.IsOK() )
      return st;

    return XRootDStatus();
  }

  //----------------------------------------------------------------------------
  // Remove a directory - sync
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::RmDir( const std::string &path,
                                  uint16_t           timeout )
  {
    SyncResponseHandler handler;
    Status st = RmDir( path, &handler, timeout );
    if( !st.IsOK() )
      return st;

    return MessageUtils::WaitForStatus( &handler );
  }

  //----------------------------------------------------------------------------
  // Change access mode on a directory or a file - async
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::ChMod( const std::string &path,
                                  uint16_t           mode,
                                  ResponseHandler   *handler,
                                  uint16_t           timeout )
  {
    Log    *log = DefaultEnv::GetLog();
    log->Dump( QueryMsg, "[%s] Sending a kXR_chmod request for path %s",
                         pUrl->GetHostId().c_str(), path.c_str() );

    Message            *msg;
    ClientChmodRequest *req;
    MessageUtils::CreateRequest( msg, req, path.length() );

    req->requestid  = kXR_chmod;
    req->mode       = mode;
    req->dlen       = path.length();
    msg->Append( path.c_str(), path.length(), 24 );

    Status st = MessageUtils::SendMessage( *pUrl, msg, handler, timeout );

    if( !st.IsOK() )
      return st;

    return XRootDStatus();
  }

  //----------------------------------------------------------------------------
  // Change access mode on a directory or a file - async
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::ChMod( const std::string &path,
                                  uint16_t           mode,
                                  uint16_t           timeout )
  {
    SyncResponseHandler handler;
    Status st = ChMod( path, mode, &handler, timeout );
    if( !st.IsOK() )
      return st;

    return MessageUtils::WaitForStatus( &handler );
  }

  //----------------------------------------------------------------------------
  // Check if the server is alive - async
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::Ping( ResponseHandler *handler,
                                 uint16_t        timeout )
  {
    Log    *log = DefaultEnv::GetLog();
    log->Dump( QueryMsg, "[%s] Sending a kXR_ping request",
                         pUrl->GetHostId().c_str() );

    Message           *msg;
    ClientPingRequest *req;
    MessageUtils::CreateRequest( msg, req );

    req->requestid  = kXR_ping;

    Status st = MessageUtils::SendMessage( *pUrl, msg, handler, timeout );

    if( !st.IsOK() )
      return st;

    return XRootDStatus();
  }

  //----------------------------------------------------------------------------
  // Check if the server is alive - sync
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::Ping( uint16_t timeout  )
  {
    SyncResponseHandler handler;
    Status st = Ping( &handler, timeout );
    if( !st.IsOK() )
      return st;

    return MessageUtils::WaitForStatus( &handler );
  }

  //----------------------------------------------------------------------------
  // Obtain status information for a path - async
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::Stat( const std::string &path,
                                 ResponseHandler   *handler,
                                 uint16_t           timeout )
  {
    Log *log = DefaultEnv::GetLog();
    log->Dump( QueryMsg, "[%s] Sending a kXR_stat request for path %s",
                         pUrl->GetHostId().c_str(), path.c_str() );

    Message           *msg;
    ClientStatRequest *req;
    MessageUtils::CreateRequest( msg, req, path.length() );

    req->requestid  = kXR_stat;
    req->options    = 0;
    req->dlen       = path.length();
    msg->Append( path.c_str(), path.length(), 24 );

    Status st = MessageUtils::SendMessage( *pUrl, msg, handler, timeout );

    if( !st.IsOK() )
      return st;

    return XRootDStatus();
  }

  //----------------------------------------------------------------------------
  // Obtain status information for a path - sync
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::Stat( const std::string  &path,
                                 StatInfo          *&response,
                                 uint16_t            timeout )
  {
    SyncResponseHandler handler;
    Status st = Stat( path, &handler, timeout );
    if( !st.IsOK() )
      return st;

    return MessageUtils::WaitForResponse( &handler, response );
  }

  //----------------------------------------------------------------------------
  // Obtain status information for a path - async
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::StatVFS( const std::string &path,
                                    ResponseHandler   *handler,
                                    uint16_t           timeout )
  {
    Log *log = DefaultEnv::GetLog();
    log->Dump( QueryMsg, "[%s] Sending a kXR_stat + VFS request for path %s",
                         pUrl->GetHostId().c_str(), path.c_str() );

    Message           *msg;
    ClientStatRequest *req;
    MessageUtils::CreateRequest( msg, req, path.length() );

    req->requestid  = kXR_stat;
    req->options    = kXR_vfs;
    req->dlen       = path.length();
    msg->Append( path.c_str(), path.length(), 24 );

    Status st = MessageUtils::SendMessage( *pUrl, msg, handler, timeout );

    if( !st.IsOK() )
      return st;

    return XRootDStatus();
  }

  //----------------------------------------------------------------------------
  // Obtain status information for a path - sync
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::StatVFS( const std::string  &path,
                                    StatInfoVFS       *&response,
                                    uint16_t            timeout )
  {
    SyncResponseHandler handler;
    Status st = StatVFS( path, &handler, timeout );
    if( !st.IsOK() )
      return st;

    return MessageUtils::WaitForResponse( &handler, response );
  }

  //----------------------------------------------------------------------------
  // Obtain server protocol information - async
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::Protocol( ResponseHandler *handler,
                                     uint16_t         timeout )
  {
    Log    *log = DefaultEnv::GetLog();
    log->Dump( QueryMsg, "[%s] Sending a kXR_protocol",
                         pUrl->GetHostId().c_str() );

    Message               *msg;
    ClientProtocolRequest *req;
    MessageUtils::CreateRequest( msg, req );

    req->requestid = kXR_protocol;
    req->clientpv  = kXR_PROTOCOLVERSION;

    Status st = MessageUtils::SendMessage( *pUrl, msg, handler, timeout );

    if( !st.IsOK() )
      return st;

    return XRootDStatus();
  }

  //----------------------------------------------------------------------------
  // Obtain server protocol information - sync
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::Protocol( ProtocolInfo *&response,
                                     uint16_t       timeout )
  {
    SyncResponseHandler handler;
    Status st = Protocol( &handler, timeout );
    if( !st.IsOK() )
      return st;

    return MessageUtils::WaitForResponse( &handler, response );
  }

  //----------------------------------------------------------------------------
  // List entries of a directory - async
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::DirList( const std::string &path,
                                    ResponseHandler   *handler,
                                    uint16_t           timeout )
  {
    Log *log = DefaultEnv::GetLog();
    log->Dump( QueryMsg, "[%s] Sending a kXR_dirlist request for path %s",
                         pUrl->GetHostId().c_str(), path.c_str() );

    Message           *msg;
    ClientStatRequest *req;
    MessageUtils::CreateRequest( msg, req, path.length() );

    req->requestid  = kXR_dirlist;
    req->dlen       = path.length();
    msg->Append( path.c_str(), path.length(), 24 );

    Status st = MessageUtils::SendMessage( *pUrl, msg, handler, timeout );

    if( !st.IsOK() )
      return st;

    return XRootDStatus();
  }

  //----------------------------------------------------------------------------
  // List entries of a directory - sync
  //----------------------------------------------------------------------------
  XRootDStatus FileSystem::DirList( const std::string  &path,
                                    uint8_t            flags,
                                    DirectoryList    *&response,
                                    uint16_t           timeout )
  {
    //--------------------------------------------------------------------------
    // We do the deep locate and ask all the returned servers for the list
    //--------------------------------------------------------------------------
    if( flags & DirListFlags::Locate )
    {
      //------------------------------------------------------------------------
      // Locate all the disk servers holding the directory
      //------------------------------------------------------------------------
      LocationInfo *locations;
      std::string locatePath = "*"; locatePath += path;
      XRootDStatus st = DeepLocate( locatePath, OpenFlags::None, locations );

      if( !st.IsOK() )
        return st;

      if( locations->GetSize() == 0 )
      {
        delete locations;
        return XRootDStatus( stError, errNotFound );
      }

      //------------------------------------------------------------------------
      // Ask each server for a directory list
      //------------------------------------------------------------------------
      flags &= ~DirListFlags::Locate;
      FileSystem    *fs;
      DirectoryList *currentResp = 0;
      bool           errors = false;

      response = new DirectoryList( "", path, 0 );

      for( uint32_t i = 0; i < locations->GetSize(); ++i )
      {
        fs = new FileSystem( locations->At(i).GetAddress() );
        st = fs->DirList( path, flags, currentResp, timeout );
        if( !st.IsOK() )
        {
          errors = true;
          delete fs;
          continue;
        }

        if( st.code == suPartial )
          errors = true;

        DirectoryList::Iterator it;

        for( it = currentResp->Begin(); it != currentResp->End(); ++it )
        {
          response->Add( *it );
          *it = 0;
        }

        delete fs;
        delete currentResp;
        fs          = 0;
        currentResp = 0;
      }
      delete locations;

      if( errors )
        return XRootDStatus( stOK, suPartial );
      return XRootDStatus();
    };

    //--------------------------------------------------------------------------
    // We just ask the current server
    //--------------------------------------------------------------------------
    SyncResponseHandler handler;
    XRootDStatus st = DirList( path, &handler, timeout );
    if( !st.IsOK() )
      return st;

    st = MessageUtils::WaitForResponse( &handler, response );
    if( !st.IsOK() )
      return st;

    //--------------------------------------------------------------------------
    // Do the stats on all the entries if necessary
    //--------------------------------------------------------------------------
    if( !(flags && DirListFlags::Stat) )
      return st;

    uint32_t quota = response->GetSize() <= 1024 ? response->GetSize() : 1024;
    RequestSync sync( response->GetSize(), quota );
    for( uint32_t i = 0; i < response->GetSize(); ++i )
    {
      std::string fullPath = response->GetParentName()+response->At(i)->GetName();
      ResponseHandler *handler = new DirListStatHandler( response, i, &sync );
      st = Stat( fullPath, handler, timeout );
      if( !st.IsOK() )
      {
        sync.TaskDone( false );
        delete handler;
      }
      sync.WaitForQuota();
    }
    sync.WaitForAll();

    if( sync.FailureCount() )
      return XRootDStatus( stOK, suPartial );

    return XRootDStatus();
  }
}