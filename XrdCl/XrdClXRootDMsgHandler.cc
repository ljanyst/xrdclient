//------------------------------------------------------------------------------
// Copyright (c) 2011-2012 by European Organization for Nuclear Research (CERN)
// Author: Lukasz Janyst <ljanyst@cern.ch>
//------------------------------------------------------------------------------
// XRootD is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// XRootD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with XRootD.  If not, see <http://www.gnu.org/licenses/>.
//------------------------------------------------------------------------------

#include "XrdCl/XrdClXRootDMsgHandler.hh"
#include "XrdCl/XrdClLog.hh"
#include "XrdCl/XrdClDefaultEnv.hh"
#include "XrdCl/XrdClConstants.hh"
#include "XrdCl/XrdClXRootDTransport.hh"
#include "XrdCl/XrdClMessage.hh"
#include "XrdCl/XrdClURL.hh"
#include "XrdCl/XrdClUtils.hh"
#include "XrdCl/XrdClTaskManager.hh"
#include "XrdCl/XrdClSIDManager.hh"
#include "XrdCl/XrdClMessageUtils.hh"

#include <arpa/inet.h>              // for network unmarshalling stuff
#include "XrdSys/XrdSysPlatform.hh" // same as above
#include <memory>
#include <sstream>

namespace
{
  //----------------------------------------------------------------------------
  // We need an extra task what will run the handler in the future, because
  // tasks get deleted and we need the handler
  //----------------------------------------------------------------------------
  class WaitTask: public XrdCl::Task
  {
    public:
      WaitTask( XrdCl::XRootDMsgHandler *handler ): pHandler( handler )
      {
        std::ostringstream o;
        o << "WaitTask for: 0x" << handler->GetRequest();
        SetName( o.str() );
      }

      virtual time_t Run( time_t now )
      {
        pHandler->WaitDone( now );
        return 0;
      }
    private:
      XrdCl::XRootDMsgHandler *pHandler;
  };
};

namespace XrdCl
{
  //----------------------------------------------------------------------------
  // Examine an incomming message, and decide on the action to be taken
  //----------------------------------------------------------------------------
  uint8_t XRootDMsgHandler::OnIncoming( Message *msg )
  {
    Log *log = DefaultEnv::GetLog();

    ServerResponse *rsp = (ServerResponse *)msg->GetBuffer();
    ClientRequest  *req = (ClientRequest *)pRequest->GetBuffer();

    //--------------------------------------------------------------------------
    // We got an async message
    //--------------------------------------------------------------------------
    if( rsp->hdr.status == kXR_attn )
    {
      //------------------------------------------------------------------------
      // We only care about async responses
      //------------------------------------------------------------------------
      if( rsp->body.attn.actnum != (int32_t)htonl(kXR_asynresp) )
        return Ignore;

      //------------------------------------------------------------------------
      // Check if the message has the stream ID that we're interested in
      //------------------------------------------------------------------------
      ServerResponse *embRsp = (ServerResponse*)msg->GetBuffer(16);
      if( embRsp->hdr.streamid[0] != req->header.streamid[0] ||
          embRsp->hdr.streamid[1] != req->header.streamid[1] )
        return Ignore;

      //------------------------------------------------------------------------
      // OK, it looks like we care
      //------------------------------------------------------------------------
      log->Dump( XRootDMsg, "[%s] Got an async response to message %s, "
                 "processing it", pUrl.GetHostId().c_str(),
                 pRequest->GetDescription().c_str() );
      Message *embededMsg = new Message( rsp->hdr.dlen-8 );
      embededMsg->Append( msg->GetBuffer( 16 ), rsp->hdr.dlen-8 );
      // we need to unmarshall the header by hand
      XRootDTransport::UnMarshallHeader( embededMsg );
      delete msg;
      return OnIncoming( embededMsg );
    }

    //--------------------------------------------------------------------------
    // The message is not async, check if it belongs to us
    //--------------------------------------------------------------------------
    if( rsp->hdr.streamid[0] != req->header.streamid[0] ||
        rsp->hdr.streamid[1] != req->header.streamid[1] )
      return Ignore;

    //--------------------------------------------------------------------------
    // We got an answer, check who we were talking to
    //--------------------------------------------------------------------------
    AnyObject  qryResult;
    int       *qryResponse = 0;
    pPostMaster->QueryTransport( pUrl, XRootDQuery::ServerFlags, qryResult );
    qryResult.Get( qryResponse );
    pHosts->back().flags = *qryResponse; delete qryResponse; qryResponse = 0;
    pPostMaster->QueryTransport( pUrl, XRootDQuery::ProtocolVersion, qryResult );
    qryResult.Get( qryResponse );
    pHosts->back().protocol = *qryResponse; delete qryResponse;

    std::auto_ptr<Message> msgPtr( msg );

    //--------------------------------------------------------------------------
    // Process the message
    //--------------------------------------------------------------------------
    XRootDTransport::UnMarshallBody( msg, req->header.requestid );
    switch( rsp->hdr.status )
    {
      //------------------------------------------------------------------------
      // kXR_ok - we're done here
      //------------------------------------------------------------------------
      case kXR_ok:
      {
        log->Dump( XRootDMsg, "[%s] Got a kXR_ok response to request %s",
                   pUrl.GetHostId().c_str(),
                   pRequest->GetDescription().c_str() );
        pResponse = msgPtr.release();
        pStatus   = Status();
        HandleResponse();
        return Take | RemoveHandler;
      }

      //------------------------------------------------------------------------
      // kXR_error - we've got a problem
      //------------------------------------------------------------------------
      case kXR_error:
      {
        log->Dump( XRootDMsg, "[%s] Got a kXR_error response to request %s "
                   "[%d] %s", pUrl.GetHostId().c_str(),
                   pRequest->GetDescription().c_str(), rsp->body.error.errnum,
                   rsp->body.error.errmsg );

        pResponse = msgPtr.release();
        HandleError( Status(stError, errErrorResponse), pResponse );
        return Take | RemoveHandler;
      }

      //------------------------------------------------------------------------
      // kXR_redirect - they tell us to go elsewhere
      //------------------------------------------------------------------------
      case kXR_redirect:
      {
        char *urlInfoBuff = new char[rsp->hdr.dlen-3];
        urlInfoBuff[rsp->hdr.dlen-4] = 0;
        memcpy( urlInfoBuff, rsp->body.redirect.host, rsp->hdr.dlen-4 );
        std::string urlInfo = urlInfoBuff;
        delete [] urlInfoBuff;
        log->Dump( XRootDMsg, "[%s] Got kXR_redirect response to "
                   "message %s: %s, port %d", pUrl.GetHostId().c_str(),
                   pRequest->GetDescription().c_str(), urlInfo.c_str(),
                   rsp->body.redirect.port );

        //----------------------------------------------------------------------
        // Check if we can proceed
        //----------------------------------------------------------------------
        if( !pRedirectCounter )
        {
          log->Dump( XRootDMsg, "[%s] Redirect limit has been reached for"
                     "message %s", pUrl.GetHostId().c_str(),
                     pRequest->GetDescription().c_str() );

          pStatus = Status( stFatal, errRedirectLimit );
          HandleResponse();
          return Take | RemoveHandler;
        }
        --pRedirectCounter;

        //----------------------------------------------------------------------
        // Keep the info about this server if we still need to find a load
        // balancer
        //----------------------------------------------------------------------
        if( !pHasLoadBalancer )
        {
          uint32_t flags = pHosts->back().flags;
          if( flags & kXR_isManager )
          {
            //------------------------------------------------------------------
            // If the current server is a meta manager then it superseeds
            // any existing load balancer, otherwise we assign a load-balancer
            // only if it has not been already assigned
            //------------------------------------------------------------------
            if( flags & kXR_attrMeta || !pLoadBalancer.url.IsValid() )
            {
              pLoadBalancer = pHosts->back();
              log->Dump( XRootDMsg, "[%s] Current server has been assigned "
                         "as a load-balancer for message %s",
                         pUrl.GetHostId().c_str(),
                         pRequest->GetDescription().c_str() );
              HostList::iterator it;
              for( it = pHosts->begin(); it != pHosts->end(); ++it )
                it->loadBalancer = false;
              pHosts->back().loadBalancer = true;
            }
          }
        }

        //----------------------------------------------------------------------
        // Build the URL and check it's validity
        //----------------------------------------------------------------------
        std::vector<std::string> urlComponents;
        std::string newCgi;
        Utils::splitString( urlComponents, urlInfo, "?" );
        std::ostringstream o;
        o << urlComponents[0] << ":" << rsp->body.redirect.port << "/";
        pUrl = URL( o.str() );
        if( !pUrl.IsValid() )
        {
          pStatus = Status( stError, errInvalidRedirectURL );
          log->Error( XRootDMsg, "[%s] Got invalid redirection URL: %s",
                                pUrl.GetHostId().c_str(), urlInfo.c_str() );
          HandleResponse();
          return Take | RemoveHandler;
        }

        URL cgiURL;
        if( urlComponents.size() > 1 )
        {
          std::ostringstream o;
          o << "fake://fake:111//fake?";
          o << urlComponents[1];
          cgiURL = URL( o.str() );
          pRedirectCgi = urlComponents[1];
        }

        //----------------------------------------------------------------------
        // Check if we need to return the URL as a response
        //----------------------------------------------------------------------
        if( pRedirectAsAnswer )
        {
          pStatus   = Status( stOK, suXRDRedirect );
          pResponse = msgPtr.release();
          HandleResponse();
          return Take | RemoveHandler;
        }

        //----------------------------------------------------------------------
        // Rewrite the message in a way required to send it to another server
        //----------------------------------------------------------------------
        Status st = RewriteRequestRedirect( cgiURL.GetParams() );
        if( !st.IsOK() )
        {
          pStatus = st;
          HandleResponse();
          return Take | RemoveHandler;
        }

        //----------------------------------------------------------------------
        // Send the request to the new location
        //----------------------------------------------------------------------
        pHosts->push_back( pUrl );
        HandleError( RetryAtServer(pUrl) );
        return Take | RemoveHandler;
      }

      //------------------------------------------------------------------------
      // kXR_wait - we wait, and re-issue the request later
      //------------------------------------------------------------------------
      case kXR_wait:
      {
        char *infoMsg = new char[rsp->hdr.dlen-3];
        infoMsg[rsp->hdr.dlen-4] = 0;
        memcpy( infoMsg, rsp->body.wait.infomsg, rsp->hdr.dlen-4 );
        log->Dump( XRootDMsg, "[%s] Got kXR_wait response of %d seconds to "
                   "message %s: %s", pUrl.GetHostId().c_str(),
                   rsp->body.wait.seconds, pRequest->GetDescription().c_str(),
                   infoMsg );
        delete [] infoMsg;

        //----------------------------------------------------------------------
        // Some messages require rewriting before they can be sent again
        // after wait
        //----------------------------------------------------------------------
        Status st = RewriteRequestWait();
        if( !st.IsOK() )
        {
          pStatus = st;
          HandleResponse();
          return Take | RemoveHandler;
        }

        //----------------------------------------------------------------------
        // Register a task to resend the message in some seconds
        //----------------------------------------------------------------------
        TaskManager *taskMgr = pPostMaster->GetTaskManager();
        taskMgr->RegisterTask( new WaitTask( this ),
                               ::time(0)+rsp->body.wait.seconds );
        return Take | RemoveHandler;
      }

      //------------------------------------------------------------------------
      // kXR_waitresp - the response will be returned in some seconds
      // as an unsolicited message
      //------------------------------------------------------------------------
      case kXR_waitresp:
      {
        log->Dump( XRootDMsg, "[%s] Got kXR_waitresp response of %d seconds to "
                   "message %s", pUrl.GetHostId().c_str(),
                   rsp->body.waitresp.seconds,
                   pRequest->GetDescription().c_str() );
        return Take;
      }

      //------------------------------------------------------------------------
      // We've got a partial answer. Wait for more
      //------------------------------------------------------------------------
      case kXR_oksofar:
      {
        log->Dump( XRootDMsg, "[%s] Got a kXR_oksofar response to request "
                   "%s", pUrl.GetHostId().c_str(),
                   pRequest->GetDescription().c_str() );
        pPartialResps.push_back( msgPtr.release() );
        return Take;
      }

      //------------------------------------------------------------------------
      // Default - unrecognized/unsupported response, declare an error
      //------------------------------------------------------------------------
      default:
      {
        log->Dump( XRootDMsg, "[%s] Got unrecognized response %d to "
                   "message %s", pUrl.GetHostId().c_str(),
                   rsp->hdr.status, pRequest->GetDescription().c_str() );
        pStatus   = Status( stError, errInvalidResponse );
        HandleResponse();
        return Take | RemoveHandler;
      }
    }

    return Ignore;
  }

  //----------------------------------------------------------------------------
  // Handle an event other that a message arrival - may be timeout
  //----------------------------------------------------------------------------
  uint8_t XRootDMsgHandler::OnStreamEvent( StreamEvent event,
                                           uint16_t    streamNum,
                                           Status      status )
  {
    Log *log = DefaultEnv::GetLog();
    log->Dump( XRootDMsg, "[%s] Stream event reported for msg %s",
               pUrl.GetHostId().c_str(), pRequest->GetDescription().c_str() );


    if( event == Ready )
      return 0;

    if( streamNum != 0 )
      return 0;

    HandleError( status, 0 );
    return RemoveHandler;
  }

  //----------------------------------------------------------------------------
  // We're here when we requested sending something over the wire
  // and there has been a status update on this action
  //----------------------------------------------------------------------------
  void XRootDMsgHandler::OnStatusReady( const Message *message,
                                        Status         status )
  {
    Log *log = DefaultEnv::GetLog();

    //--------------------------------------------------------------------------
    // We were successfull, so we now need to listen for a response
    //--------------------------------------------------------------------------
    if( status.IsOK() )
    {
      log->Dump( XRootDMsg, "[%s] Message %s has been successfully sent.",
                 pUrl.GetHostId().c_str(), message->GetDescription().c_str() );
      Status st = pPostMaster->Receive( pUrl, this, pExpiration );
      if( st.IsOK() )
        return;
    }

    //--------------------------------------------------------------------------
    // We have failed, recover if possible
    //--------------------------------------------------------------------------
    log->Error( XRootDMsg, "[%s] Impossible to send message %s. Trying to "
                "recover.", pUrl.GetHostId().c_str(),
                message->GetDescription().c_str() );
    HandleError( status, 0 );
  }

  //----------------------------------------------------------------------------
  // We're here when we got a time event. We needed to re-issue the request
  // in some time in the future, and that moment has arrived
  //----------------------------------------------------------------------------
  void XRootDMsgHandler::WaitDone( time_t )
  {
    HandleError( RetryAtServer(pUrl) );
  }

  //----------------------------------------------------------------------------
  // Unpack the message and call the response handler
  //----------------------------------------------------------------------------
  void XRootDMsgHandler::HandleResponse()
  {
    //--------------------------------------------------------------------------
    // Process the response and notify the listener
    //--------------------------------------------------------------------------
    XRootDTransport::UnMarshallRequest( pRequest );
    XRootDStatus *status   = ProcessStatus();
    AnyObject    *response = 0;

    if( status->IsOK() )
    {
      Status st = ParseResponse( response );
      if( !st.IsOK() )
      {
        delete status;
        status   = new XRootDStatus( st );
        response = 0;
      }
    }

    //--------------------------------------------------------------------------
    // Release the stream id
    //--------------------------------------------------------------------------
    ClientRequest *req = (ClientRequest *)pRequest->GetBuffer();
    if( !status->IsOK() && status->code == errOperationExpired )
      pSidMgr->TimeOutSID( req->header.streamid );
    else
      pSidMgr->ReleaseSID( req->header.streamid );

    pResponseHandler->HandleResponseWithHosts( status, response, pHosts );

    //--------------------------------------------------------------------------
    // As much as I hate to say this, we cannot do more, so we commit
    // a suicide... just make sure that this is the last stateful thing
    // we'll ever do
    //--------------------------------------------------------------------------
    delete this;
  }


  //----------------------------------------------------------------------------
  // Extract the status information from the stuff that we got
  //----------------------------------------------------------------------------
  XRootDStatus *XRootDMsgHandler::ProcessStatus()
  {
    XRootDStatus   *st  = new XRootDStatus( pStatus );
    ServerResponse *rsp = 0;
    if( pResponse )
      rsp = (ServerResponse *)pResponse->GetBuffer();
    if( !pStatus.IsOK() && pStatus.code == errErrorResponse && rsp )
    {
      st->errNo = rsp->body.error.errnum;
      st->SetErrorMessage( rsp->body.error.errmsg );
    }
    return st;
  }

  //------------------------------------------------------------------------
  // Parse the response and put it in an object that could be passed to
  // the user
  //------------------------------------------------------------------------
  Status XRootDMsgHandler::ParseResponse( AnyObject *&response )
  {
    ServerResponse *rsp = (ServerResponse *)pResponse->GetBuffer();
    ClientRequest  *req = (ClientRequest *)pRequest->GetBuffer();
    Log            *log = DefaultEnv::GetLog();

    //--------------------------------------------------------------------------
    // Handle redirect as an answer
    //--------------------------------------------------------------------------
    if( rsp->hdr.status == kXR_redirect )
    {
      if( !pRedirectAsAnswer )
      {
        log->Error( XRootDMsg, "Internal Error: trying to pass redirect as an "
                    "answer even though this has never been requested" );
        return 0;
      }
      log->Dump( XRootDMsg, "Parsing the response to %s as RedirectInfo",
                 pRequest->GetDescription().c_str() );
      AnyObject    *obj  = new AnyObject();
      RedirectInfo *info = new RedirectInfo( pUrl.GetHostName(),
                                             pUrl.GetPort(),
                                             pRedirectCgi );
      obj->Set( info );
      response = obj;
      return Status();
    }

    //--------------------------------------------------------------------------
    // We only handle the kXR_ok responses further down
    //--------------------------------------------------------------------------
    if( rsp->hdr.status != kXR_ok )
      return 0;

    Buffer    buff;
    uint32_t  length = 0;
    char     *buffer = 0;

    //--------------------------------------------------------------------------
    // We don't have any partial answers so pass what we have
    //--------------------------------------------------------------------------
    if( pPartialResps.empty() )
    {
      buffer = rsp->body.buffer.data;
      length = rsp->hdr.dlen;
    }
    //--------------------------------------------------------------------------
    // Partial answers, we need to glue them together before parsing
    //--------------------------------------------------------------------------
    else
    {
      for( uint32_t i = 0; i < pPartialResps.size(); ++i )
      {
        ServerResponse *part = (ServerResponse*)pPartialResps[i]->GetBuffer();
        length += part->hdr.dlen;
      }
      length += rsp->hdr.dlen;

      buff.Allocate( length );
      uint32_t offset = 0;
      for( uint32_t i = 0; i < pPartialResps.size(); ++i )
      {
        ServerResponse *part = (ServerResponse*)pPartialResps[i]->GetBuffer();
        buff.Append( part->body.buffer.data, part->hdr.dlen, offset );
        offset += part->hdr.dlen;
      }
      buff.Append( rsp->body.buffer.data, rsp->hdr.dlen, offset );
      buffer = buff.GetBuffer();
    }

    //--------------------------------------------------------------------------
    // Right, but what was the question?
    //--------------------------------------------------------------------------
    switch( req->header.requestid )
    {
      //------------------------------------------------------------------------
      // kXR_mv, kXR_truncate, kXR_rm, kXR_mkdir, kXR_rmdir, kXR_chmod,
      // kXR_ping, kXR_close, kXR_write, kXR_sync
      //------------------------------------------------------------------------
      case kXR_mv:
      case kXR_truncate:
      case kXR_rm:
      case kXR_mkdir:
      case kXR_rmdir:
      case kXR_chmod:
      case kXR_ping:
      case kXR_close:
      case kXR_write:
      case kXR_sync:
        return Status();

      //------------------------------------------------------------------------
      // kXR_locate
      //------------------------------------------------------------------------
      case kXR_locate:
      {
        AnyObject *obj = new AnyObject();
        log->Dump( XRootDMsg, "[%s] Parsing the response to %s as "
                   "LocateInfo: %s", pUrl.GetHostId().c_str(),
                   pRequest->GetDescription().c_str(), buffer );
        LocationInfo *data = new LocationInfo( buffer );
        obj->Set( data );
        response = obj;
        return Status();
      }

      //------------------------------------------------------------------------
      // kXR_stat
      //------------------------------------------------------------------------
      case kXR_stat:
      {
        AnyObject *obj = new AnyObject();

        //----------------------------------------------------------------------
        // Virtual File System stat (kXR_vfs)
        //----------------------------------------------------------------------
        if( req->stat.options & kXR_vfs )
        {
          log->Dump( XRootDMsg, "[%s] Parsing the response to %s as "
                     "StatInfoVFS", pUrl.GetHostId().c_str(),
                     pRequest->GetDescription().c_str() );

          StatInfoVFS *data = new StatInfoVFS( buffer );
          obj->Set( data );
        }
        //----------------------------------------------------------------------
        // Normal stat
        //----------------------------------------------------------------------
        else
        {
          log->Dump( XRootDMsg, "[%s] Parsing the response to %s as StatInfo",
                     pUrl.GetHostId().c_str(),
                     pRequest->GetDescription().c_str() );

          StatInfo *data = new StatInfo( buffer );
          obj->Set( data );
        }

        response = obj;
        return Status();
      }

      //------------------------------------------------------------------------
      // kXR_protocol
      //------------------------------------------------------------------------
      case kXR_protocol:
      {
        AnyObject *obj = new AnyObject();
        log->Dump( XRootDMsg, "[%s] Parsing the response to %s as ProtocolInfo",
                   pUrl.GetHostId().c_str(),
                   pRequest->GetDescription().c_str() );

        ProtocolInfo *data = new ProtocolInfo( rsp->body.protocol.pval,
                                               rsp->body.protocol.flags );
        obj->Set( data );
        response = obj;
        return Status();
      }

      //------------------------------------------------------------------------
      // kXR_dirlist
      //------------------------------------------------------------------------
      case kXR_dirlist:
      {
        AnyObject *obj = new AnyObject();
        log->Dump( XRootDMsg, "[%s] Parsing the response to %s as "
                   "DirectoryList", pUrl.GetHostId().c_str(),
                   pRequest->GetDescription().c_str() );

        char *path = new char[req->dirlist.dlen+1];
        path[req->dirlist.dlen] = 0;
        memcpy( path, pRequest->GetBuffer(24), req->dirlist.dlen );
        DirectoryList *data = new DirectoryList( pUrl.GetHostId(), path,
                                                 length ? buffer : 0 );
        delete [] path;
        obj->Set( data );
        response = obj;
        return Status();
      }

      //------------------------------------------------------------------------
      // kXR_open - if we got the statistics, otherwise return 0
      //------------------------------------------------------------------------
      case kXR_open:
      {
        log->Dump( XRootDMsg, "[%s] Parsing the response to %s as OpenInfo",
                   pUrl.GetHostId().c_str(),
                   pRequest->GetDescription().c_str() );

        AnyObject *obj      = new AnyObject();
        StatInfo  *statInfo = 0;

        if( req->open.options & kXR_retstat )
        {
          log->Dump( XRootDMsg, "[%s] Found StatInfo in response to %s",
                     pUrl.GetHostId().c_str(),
                     pRequest->GetDescription().c_str() );
          if( req->open.dlen >= 12 )
            statInfo = new StatInfo( buffer+12 );
        }

        OpenInfo *data = new OpenInfo( (uint8_t*)buffer,
                                       pResponse->GetSessionId(),
                                       statInfo );
        obj->Set( data );
        response = obj;
        return Status();
      }

      //------------------------------------------------------------------------
      // kXR_read - we need to pass the length of the buffer to the user code
      //------------------------------------------------------------------------
      case kXR_read:
      {
        log->Dump( XRootDMsg, "[%s] Parsing the response to %s as ChunkInfo",
                   pUrl.GetHostId().c_str(),
                   pRequest->GetDescription().c_str() );

        ChunkInfo info = pChunkList->front();

        if( info.length < length )
        {
          log->Error( XRootDMsg, "[%s] Handling response to %s: user "
                      "supplied buffer is to small: %d bytes; got %d bytes "
                      "of response data", pUrl.GetHostId().c_str(),
                      pRequest->GetDescription().c_str(), info.length,
                      length );
          return Status( stError, errInvalidResponse );
        }
        memcpy( info.buffer, buffer, length );

        AnyObject *obj   = new AnyObject();
        ChunkInfo *chunk = new ChunkInfo( info.offset, length, info.buffer );
        obj->Set( chunk );
        response = obj;
        return Status();
      }

      //------------------------------------------------------------------------
      // kXR_readv - we need to pass the length of the buffer to the user code
      //------------------------------------------------------------------------
      case kXR_readv:
      {
        log->Dump( XRootDMsg, "[%s] Parsing the response to 0x%x as "
                   "VectorReadInfo", pUrl.GetHostId().c_str(),
                   pRequest->GetDescription().c_str() );

        VectorReadInfo *info = new VectorReadInfo();
        Status st = UnpackVectorRead( info, pChunkList, buffer, length );
        if( !st.IsOK() )
        {
          delete info;
          return st;
        }

        AnyObject *obj = new AnyObject();
        obj->Set( info );
        response = obj;
        return Status();
      }

      //------------------------------------------------------------------------
      // kXR_query
      //------------------------------------------------------------------------
      case kXR_query:
      case kXR_set:
      case kXR_prepare:
      default:
      {
        AnyObject *obj = new AnyObject();
        log->Dump( XRootDMsg, "[%s] Parsing the response to %s as BinaryData",
                   pUrl.GetHostId().c_str(),
                   pRequest->GetDescription().c_str() );

        BinaryDataInfo *data = new BinaryDataInfo();
        data->Allocate( length );
        data->Append( buffer, length );
        obj->Set( data );
        response = obj;
        return Status();
      }
    };
    return Status( stError, errInvalidMessage );
  }

  //----------------------------------------------------------------------------
  // Perform the changes to the original request needed by the redirect
  // procedure - allocate new streamid, append redirection data and such
  //----------------------------------------------------------------------------
  Status XRootDMsgHandler::RewriteRequestRedirect( const URL::ParamsMap &newCgi )
  {
    Log *log = DefaultEnv::GetLog();
    ClientRequest  *req = (ClientRequest *)pRequest->GetBuffer();

    //--------------------------------------------------------------------------
    // Assign a new stream id to the message
    //--------------------------------------------------------------------------
    Status st;
    pSidMgr->ReleaseSID( req->header.streamid );
    pSidMgr = 0;
    AnyObject sidMgrObj;
    st = pPostMaster->QueryTransport( pUrl, XRootDQuery::SIDManager,
                                      sidMgrObj );

    if( !st.IsOK() )
    {
      log->Error( XRootDMsg, "[%s] Impossible to send message %s.",
                  pUrl.GetHostId().c_str(),
                  pRequest->GetDescription().c_str() );
      return st;
    }

    sidMgrObj.Get( pSidMgr );
    st = pSidMgr->AllocateSID( req->header.streamid );
    if( !st.IsOK() )
    {
      log->Error( XRootDMsg, "[%s] Impossible to send message %s.",
                  pUrl.GetHostId().c_str(),
                  pRequest->GetDescription().c_str() );
      return st;
    }

    //--------------------------------------------------------------------------
    // Rewrite particular requests
    //--------------------------------------------------------------------------
    if( newCgi.empty() )
      return Status();

    XRootDTransport::UnMarshallRequest( pRequest );
    MessageUtils::AppendCGI( pRequest, newCgi, false );
    XRootDTransport::MarshallRequest( pRequest );
    return Status();
  }

  //----------------------------------------------------------------------------
  // Some requests need to be rewriten also after getting kXR_wait
  //----------------------------------------------------------------------------
  Status XRootDMsgHandler::RewriteRequestWait()
  {
    ClientRequest *req = (ClientRequest *)pRequest->GetBuffer();

    XRootDTransport::UnMarshallRequest( pRequest );

    //------------------------------------------------------------------------
    // For kXR_locate and kXR_open request the kXR_refresh bit needs to be
    // turned off after wait
    //------------------------------------------------------------------------
    switch( req->header.requestid )
    {
      case kXR_locate:
      {
        uint16_t refresh = kXR_refresh;
        req->locate.options &= (~refresh);
        break;
      }

      case kXR_open:
      {
        uint16_t refresh = kXR_refresh;
        req->locate.options &= (~refresh);
        break;
      }
    }

    XRootDTransport::SetDescription( pRequest );
    XRootDTransport::MarshallRequest( pRequest );
    return Status();
  }

  //----------------------------------------------------------------------------
  // Unpack vector read - crazy stuff
  //----------------------------------------------------------------------------
  Status XRootDMsgHandler::UnpackVectorRead( VectorReadInfo *vReadInfo,
                                             ChunkList      *list,
                                             char           *sourceBuffer,
                                             uint32_t        sourceBufferSize )
  {
    Log *log = DefaultEnv::GetLog();
    char     *cursorSource = sourceBuffer;
    int64_t   len          = sourceBufferSize;
    uint32_t  offset       = 0;
    uint32_t  size         = 0;

    uint32_t  reqChunks  = list->size();
    uint32_t  reqCurrent = 0;

    while( 1 )
    {
      //------------------------------------------------------------------------
      // Check whether we should stop
      //------------------------------------------------------------------------
      if( offset > len-16 )
        break;

      if( reqCurrent >= reqChunks )
      {
        log->Error( XRootDMsg, "[%s] Handling response to %s: the server "
                    "responded with more chunks than it has been asked for.",
                    pUrl.GetHostId().c_str(),
                    pRequest->GetDescription().c_str() );
        return Status( stFatal, errInvalidResponse );
      }

      //------------------------------------------------------------------------
      // Extract and check the validity of the chunk
      //------------------------------------------------------------------------
      readahead_list *chunk = (readahead_list*)(cursorSource);
      chunk->rlen   = ntohl( chunk->rlen );
      chunk->offset = ntohll( chunk->offset );
      size += chunk->rlen;

      if( (uint32_t)chunk->rlen != (*list)[reqCurrent].length ||
          (uint64_t)chunk->offset != (*list)[reqCurrent].offset )
      {
        log->Error( XRootDMsg, "[%s] Handling response to %s: the response "
                    "chunk doesn't match the requested one.",
                    pUrl.GetHostId().c_str(),
                    pRequest->GetDescription().c_str() );
        return Status( stFatal, errInvalidResponse );
      }

      //------------------------------------------------------------------------
      // Extract the data
      //------------------------------------------------------------------------
      if( !(*list)[reqCurrent].buffer )
      {
        log->Error( XRootDMsg, "[%s] Handling response to %s: the user "
                    "supplied buffer is 0, discarding the data",
                    pUrl.GetHostId().c_str(),
                    pRequest->GetDescription().c_str() );
      }
      else
        memcpy( (*list)[reqCurrent].buffer, cursorSource+16, chunk->rlen );

      vReadInfo->GetChunks().push_back(
                      ChunkInfo( chunk->offset,
                                 chunk->rlen,
                                 (*list)[reqCurrent].buffer ) );

      offset += 16 + chunk->rlen;
      cursorSource = sourceBuffer+offset;
      ++reqCurrent;
    }
    vReadInfo->SetSize( size );
    return Status();
  }

  //----------------------------------------------------------------------------
  // Recover error
  //----------------------------------------------------------------------------
  void XRootDMsgHandler::HandleError( Status status, Message *msg )
  {
    //--------------------------------------------------------------------------
    // If there was no error then do nothing
    //--------------------------------------------------------------------------
    if( status.IsOK() )
      return;

    Log *log = DefaultEnv::GetLog();
    log->Error( XRootDMsg, "[%s] Handling error while processing %s: %s.",
                pUrl.GetHostId().c_str(), pRequest->GetDescription().c_str(),
                status.ToString().c_str() );

    //--------------------------------------------------------------------------
    // We have got an error message, we can recover it at the load balancer if:
    // 1) we haven't got it from the load balancer
    // 2) we have a load balancer assigned
    // 3) the error is either one of: kXR_FSError, kXR_IOError, kXR_ServerError,
    //    kXR_NotFound
    // 4) in the case of kXR_NotFound a kXR_refresh flags needs to be set
    //--------------------------------------------------------------------------
    if( status.code == errErrorResponse )
    {
      if( pLoadBalancer.url.IsValid() &&
          pUrl.GetHostId() != pLoadBalancer.url.GetHostId() &&
          (status.errNo == kXR_FSError || status.errNo == kXR_IOError ||
          status.errNo == kXR_ServerError || status.errNo == kXR_NotFound) )
      {
        UpdateTriedCGI();
        if( status.errNo == kXR_NotFound )
          SwitchOnRefreshFlag();
        HandleError( RetryAtServer( pLoadBalancer.url ) );
        delete pResponse;
        return;
      }
      else
      {
        pStatus = status;
        HandleResponse();
        return;
      }
    }

    //--------------------------------------------------------------------------
    // Nothing can be done if:
    // 1) a user timeout has occured
    // 2) has a non-zero session id
    // 3) if another error occured and the validity of the message expired
    //--------------------------------------------------------------------------
    if( status.code == errOperationExpired || pRequest->GetSessionId() ||
        time(0) >= pExpiration )
    {
      log->Error( XRootDMsg, "[%s] Unable to get the response to request %s",
                  pUrl.GetHostId().c_str(),
                  pRequest->GetDescription().c_str() );
      pStatus = status;
      HandleResponse();
      return;
    }

    //--------------------------------------------------------------------------
    // At this point we're left with connection errors, we recover them
    // at a load balancer if we have one and if not on the current server
    // until we get a response, an unrecoverable error or a timeout
    //--------------------------------------------------------------------------
    if( pLoadBalancer.url.IsValid() &&
        pLoadBalancer.url.GetHostId() != pUrl.GetHostId() )
    {
      UpdateTriedCGI();
      HandleError( RetryAtServer( pLoadBalancer.url ) );
      return;
    }
    else
    {
      if( !status.IsFatal() )
      {
        HandleError( RetryAtServer( pUrl ) );
        return;
      }
      pStatus = status;
      HandleResponse();
      return;
    }
  }

  //----------------------------------------------------------------------------
  // Retry the message at another server
  //----------------------------------------------------------------------------
  Status XRootDMsgHandler::RetryAtServer( const URL &url )
  {
    pUrl = url;
    pHosts->push_back( pUrl );
    return pPostMaster->Send( pUrl, pRequest, this, true, pExpiration );
  }

  //----------------------------------------------------------------------------
  // Update the "tried=" part of the CGI of the current message
  //----------------------------------------------------------------------------
  void XRootDMsgHandler::UpdateTriedCGI()
  {
    URL::ParamsMap cgi;
    cgi["tried"] = pUrl.GetHostName();
    XRootDTransport::UnMarshallRequest( pRequest );
    MessageUtils::AppendCGI( pRequest, cgi, false );
    XRootDTransport::MarshallRequest( pRequest );
  }

  //----------------------------------------------------------------------------
  // Switch on the refresh flag for some requests
  //----------------------------------------------------------------------------
  void XRootDMsgHandler::SwitchOnRefreshFlag()
  {
    XRootDTransport::UnMarshallRequest( pRequest );
    ClientRequest  *req = (ClientRequest *)pRequest->GetBuffer();
    switch( req->header.requestid )
    {
      case kXR_locate:
      {
        req->locate.options |= kXR_refresh;
        break;
      }

      case kXR_open:
      {
        req->locate.options |= kXR_refresh;
        break;
      }
    }
    XRootDTransport::SetDescription( pRequest );
    XRootDTransport::MarshallRequest( pRequest );
  }
}
