//------------------------------------------------------------------------------
// Copyright (c) 2012 by European Organization for Nuclear Research (CERN)
// Author: Lukasz Janyst <ljanyst@cern.ch>
// See the LICENCE file for details.
//------------------------------------------------------------------------------

#include "XrdCl/XrdClXRootDMsgHandler.hh"
#include "XrdCl/XrdClLog.hh"
#include "XrdCl/XrdClDefaultEnv.hh"
#include "XrdCl/XrdClConstants.hh"
#include "XrdCl/XrdClXRootDTransport.hh"
#include "XrdCl/XrdClMessage.hh"
#include "XrdCl/XrdClURL.hh"
#include "XrdCl/XrdClTaskManager.hh"
#include "XrdCl/XrdClSIDManager.hh"

#include <memory>

namespace
{
  //----------------------------------------------------------------------------
  // We need an extra task what will run the handler in the future, because
  // tasks get deleted and we need the handler
  //----------------------------------------------------------------------------
  class WaitTask: public XrdClient::Task
  {
    public:
      WaitTask( XrdClient::XRootDMsgHandler *handler ): pHandler( handler ) {}
      virtual time_t Run( time_t now )
      {
        pHandler->WaitDone( now );
        return 0;
      }
    private:
      XrdClient::XRootDMsgHandler *pHandler;
  };
};

namespace XrdClient
{
  //----------------------------------------------------------------------------
  // Examine an incomming message, and decide on the action to be taken
  //----------------------------------------------------------------------------
  uint8_t XRootDMsgHandler::HandleMessage( Message *msg )
  {
    Log *log = DefaultEnv::GetLog();

    ServerResponse *rsp = (ServerResponse *)msg->GetBuffer();
    ClientRequest  *req = (ClientRequest *)pRequest->GetBuffer();

    //--------------------------------------------------------------------------
    // Check if the message belongs to us
    //--------------------------------------------------------------------------
    if( rsp->hdr.streamid[0] != req->header.streamid[0] ||
        rsp->hdr.streamid[1] != req->header.streamid[1] )
      return Ignore;

    std::auto_ptr<Message> msgPtr( msg );

    //--------------------------------------------------------------------------
    // Check if we're interested in this message
    //--------------------------------------------------------------------------
    XRootDTransport::UnMarshallBody( msg, req->header.requestid );
    switch( rsp->hdr.status )
    {
      //------------------------------------------------------------------------
      // kXR_ok - we're done here
      //------------------------------------------------------------------------
      case kXR_ok:
      {
        log->Dump( XRootDMsg, "[%s] Got a kXR_ok response to request 0x%x",
                             pUrl->GetHostId().c_str(), pRequest );
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
        log->Info( XRootDMsg, "[%s] Got a kXR_error response to request 0x%x: "
                             "[%d] %s",
                             pUrl->GetHostId().c_str(), pRequest,
                             rsp->body.error.errnum, rsp->body.error.errmsg );
        pResponse = msgPtr.release();
        pStatus = Status( stError, errErrorResponse );
        HandleResponse();
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
        delete urlInfoBuff;
        log->Dump( XRootDMsg, "[%s] Got kXR_redirect response to "
                             "message 0x%x: %s, port %d",
                             pUrl->GetHostId().c_str(), pRequest,
                             urlInfo.c_str(), rsp->body.redirect.port );

        //----------------------------------------------------------------------
        // Check the validity of the url
        //----------------------------------------------------------------------
        delete pUrl;
        pUrl = new URL( urlInfo, rsp->body.redirect.port );

        if( !pUrl->IsValid() )
        {
          pStatus = Status( stError, errInvalidRedirectURL );
          log->Error( XRootDMsg, "[%s] Got invalid redirection URL: %s",
                                pUrl->GetHostId().c_str(), urlInfo.c_str() );
          HandleResponse();
          return Take | RemoveHandler;
        }

        //----------------------------------------------------------------------
        // Rewrite the message in a way required to send it to another server
        //----------------------------------------------------------------------
        Status st = RewriteRequestRedirect();
        if( !st.IsOK() )
        {
          pStatus = st;
          HandleResponse();
          return Take | RemoveHandler;
        }

        //----------------------------------------------------------------------
        // Send the request to the new location
        //----------------------------------------------------------------------
        st = pPostMaster->Send( *pUrl, pRequest, this, 300 );

        if( !st.IsOK() )
        {
          log->Error( XRootDMsg, "[%s] Unable to redirect message 0x%x to: %s",
                                pUrl->GetHostId().c_str(), pRequest,
                                urlInfo.c_str() );
          pStatus = st;
          HandleResponse();
          return Take | RemoveHandler;
        }
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
                             "message 0x%x: %s",
                             pUrl->GetHostId().c_str(), rsp->body.wait.seconds,
                             pRequest, infoMsg );
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
                             "message 0x%x",
                             pUrl->GetHostId().c_str(),
                             rsp->body.waitresp.seconds, pRequest );

        // FIXME: we have to think of taking into account the new timeout value
        return Take;
      }

      //------------------------------------------------------------------------
      // kXR_attn - async response to our query
      //------------------------------------------------------------------------
      case kXR_attn:
      {
        log->Dump( XRootDMsg, "[%s] Got kXR_attn response to message 0x%x, "
                             "with action code of %d",
                             pUrl->GetHostId().c_str(), pRequest,
                             rsp->body.attn.actnum );

        //----------------------------------------------------------------------
        // We actually got an async response
        //----------------------------------------------------------------------
        if( rsp->body.attn.actnum == kXR_asynresp )
        {
          log->Dump( XRootDMsg, "[%s] Got an async response to message 0x%x, "
                               "processing it",
                               pUrl->GetHostId().c_str(), pRequest );
          Message *embededMsg = new Message( rsp->hdr.dlen-8 );
          embededMsg->Append( msg->GetBuffer( 16 ), rsp->hdr.dlen-8 );
          // we need to unmarshall the header by hand
          XRootDTransport::UnMarshallHeader( embededMsg );
          return HandleMessage( embededMsg );
        }
        else
        {
          log->Error( XRootDMsg, "[%s] Got an unknown async response to message "
                                "0x%x",
                                pUrl->GetHostId().c_str(), pRequest );
          HandleResponse();
          return Take | RemoveHandler;
        }
      }

      //------------------------------------------------------------------------
      // Default - unrecognized/unsupported response, declare an error
      //------------------------------------------------------------------------
      default:
      {
        pStatus = Status( stError, errInvalidResponse );
        HandleResponse();
        return Take | RemoveHandler;
      }
    }

    return Ignore;
  }

  //----------------------------------------------------------------------------
  // Handle an event other that a message arrival - may be timeout
  //----------------------------------------------------------------------------
  void XRootDMsgHandler::HandleFault( Status status )
  {
    Log *log = DefaultEnv::GetLog();
    log->Error( XRootDMsg, "[%s] Unable to get the response to request 0x%x",
                          pUrl->GetHostId().c_str(), pRequest );
    pStatus = status;
    HandleResponse();
  }

  //----------------------------------------------------------------------------
  // We're here when we requested sending something over the wire
  // and there has been a status update on this action
  //----------------------------------------------------------------------------
  void XRootDMsgHandler::HandleStatus( const Message *message,
                                       Status         status )
  {
    Log *log = DefaultEnv::GetLog();

    //--------------------------------------------------------------------------
    // We were successfull, so we now need to listen for a response
    //--------------------------------------------------------------------------
    if( status.IsOK() )
    {
      log->Dump( XRootDMsg, "[%s] Message 0x%x has been successfully sent.",
                           pUrl->GetHostId().c_str(), message );
      Status st = pPostMaster->Receive( *pUrl, this, 300 );
      if( st.IsOK() )
        return;
    }

    log->Error( XRootDMsg, "[%s] Impossible to send message 0x%x.",
                          pUrl->GetHostId().c_str(), message );
    pStatus = status;
    HandleResponse();
  }

  //----------------------------------------------------------------------------
  // We're here when we got a time event. We needed to re-issue the request
  // in some time in the future, and that moment has arrived
  //----------------------------------------------------------------------------
  void XRootDMsgHandler::WaitDone( time_t now )
  {
    Log *log = DefaultEnv::GetLog();
    Status st = pPostMaster->Send( *pUrl, pRequest, this, 300 );
    if( !st.IsOK() )
    {
      log->Error( XRootDMsg, "[%s] Impossible to send message 0x%x after wait.",
                            pUrl->GetHostId().c_str(), pRequest );
      pStatus = st;
      HandleResponse();
    }
  }

  //----------------------------------------------------------------------------
  // Unpack the message and call the response handler
  //----------------------------------------------------------------------------
  void XRootDMsgHandler::HandleResponse()
  {
    XRootDStatus *status   = ProcessStatus();
    AnyObject    *response = 0;

    if( status->IsOK() )
      response = ParseResponse();
    pResponseHandler->HandleResponse( status, response );

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
    ServerResponse *rsp = (ServerResponse *)pResponse->GetBuffer();
    if( !pStatus.IsOK() && pStatus.code == errErrorResponse && rsp )
      st->SetErrorMessage( rsp->body.error.errmsg );
    return st;
  }

  //------------------------------------------------------------------------
  // Parse the response and put it in an object that could be passed to
  // the user
  //------------------------------------------------------------------------
  AnyObject *XRootDMsgHandler::ParseResponse()
  {
    ServerResponse *rsp = (ServerResponse *)pResponse->GetBuffer();
    ClientRequest  *req = (ClientRequest *)pRequest->GetBuffer();
    Log            *log = DefaultEnv::GetLog();

    XRootDTransport::UnMarshallRequest( pRequest );

    //--------------------------------------------------------------------------
    // We only handle the kXR_ok responses
    //--------------------------------------------------------------------------
    if( rsp->hdr.status != kXR_ok )
      return 0;

    //--------------------------------------------------------------------------
    // Right, but what was the question?
    //--------------------------------------------------------------------------
    switch( req->header.requestid )
    {
      //------------------------------------------------------------------------
      // kXR_mv
      //------------------------------------------------------------------------
      case kXR_mv:
        return 0;

      //------------------------------------------------------------------------
      // kXR_locate
      //------------------------------------------------------------------------
      case kXR_locate:
      {
        AnyObject *obj = new AnyObject();
        log->Dump( XRootDMsg, "[%s] Parsing the response to 0x%x as LocateInfo",
                             pUrl->GetHostId().c_str(), pRequest );
        LocationInfo *data = new LocationInfo( rsp->body.buffer.data );
        obj->Set( data );
        return obj;
      }

      //------------------------------------------------------------------------
      // kXR_query
      //------------------------------------------------------------------------
      case kXR_query:
      default:
      {
        AnyObject *obj = new AnyObject();
        log->Dump( XRootDMsg, "[%s] Parsing the response to 0x%x as BinaryData",
                             pUrl->GetHostId().c_str(), pRequest );

        BinaryDataInfo *data = new BinaryDataInfo();
        data->Allocate( rsp->hdr.dlen );
        data->Append( rsp->body.buffer.data, rsp->hdr.dlen );
        obj->Set( data );
        return obj;
      }
    };
    return 0;
  }

  //----------------------------------------------------------------------------
  // Perform the changes to the original request needed by the redirect
  // procedure - allocate new streamid, append redirection data and such
  //----------------------------------------------------------------------------
  Status XRootDMsgHandler::RewriteRequestRedirect()
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
    st = pPostMaster->QueryTransport( *pUrl, XRootDQuery::SIDManager,
                                      sidMgrObj );

    if( !st.IsOK() )
    {
      log->Error( XRootDMsg, "[%s] Impossible to send message 0x%x.",
                            pUrl->GetHostId().c_str(), pRequest );
      return st;
    }

    sidMgrObj.Get( pSidMgr );
    st = pSidMgr->AllocateSID( req->header.streamid );
    if( !st.IsOK() )
    {
      log->Error( XRootDMsg, "[%s] Impossible to send message 0x%x.",
                            pUrl->GetHostId().c_str(), pRequest );
      return st;
    }
    return Status();
  }

  //----------------------------------------------------------------------------
  // Some requests need to be rewriten also after getting kXR_wait - sigh
  //----------------------------------------------------------------------------
  Status XRootDMsgHandler::RewriteRequestWait()
  {
    Log *log = DefaultEnv::GetLog();
    ClientRequest *req = (ClientRequest *)pRequest->GetBuffer();

    XRootDTransport::UnMarshallRequest( pRequest );

    switch( req->header.requestid )
    {
      //------------------------------------------------------------------------
      // For kXR_locate request the kXR_refresh bit needs to be turned off
      // on wait
      //------------------------------------------------------------------------
      case kXR_locate:
      {
        uint16_t refresh = kXR_refresh;
        req->locate.options &= (~refresh);
        break;
      }
    }

    XRootDTransport::MarshallRequest( pRequest );
    return Status();
  }
}