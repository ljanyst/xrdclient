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

#include "XrdCl/XrdClStream.hh"
#include "XrdCl/XrdClSocket.hh"
#include "XrdCl/XrdClChannel.hh"
#include "XrdCl/XrdClConstants.hh"
#include "XrdCl/XrdClLog.hh"
#include "XrdCl/XrdClMessage.hh"
#include "XrdCl/XrdClDefaultEnv.hh"
#include "XrdCl/XrdClUtils.hh"
#include "XrdCl/XrdClOutQueue.hh"
#include "XrdCl/XrdClAsyncSocketHandler.hh"
#include "XrdSys/XrdSysDNS.hh"

#include <sys/types.h>
#include <sys/socket.h>

namespace XrdCl
{
  //----------------------------------------------------------------------------
  // Message helper
  //----------------------------------------------------------------------------
  struct OutMessageHelper
  {
    OutMessageHelper( Message              *message = 0,
                      OutgoingMsgHandler   *hndlr   = 0,
                      time_t                expir   = 0,
                      bool                  statefu = 0 ):
      msg( message ), handler( hndlr ), expires( expir ), stateful( statefu ) {}
    void Reset()
    {
      msg = 0; handler = 0; expires = 0; stateful = 0;
    }
    Message              *msg;
    OutgoingMsgHandler   *handler;
    time_t                expires;
    bool                  stateful;
  };

  //----------------------------------------------------------------------------
  // Sub stream helper
  //----------------------------------------------------------------------------
  struct SubStreamData
  {
    SubStreamData(): socket( 0 ), status( Socket::Disconnected )
    {
      outQueue = new OutQueue();
    }
    ~SubStreamData()
    {
      delete socket;
      delete outQueue;
    }
    AsyncSocketHandler   *socket;
    OutQueue             *outQueue;
    OutMessageHelper      msgHelper;
    Socket::SocketStatus  status;
  };

  //----------------------------------------------------------------------------
  // Constructor
  //----------------------------------------------------------------------------
  Stream::Stream( const URL *url, uint16_t streamNum ):
    pUrl( url ),
    pStreamNum( streamNum ),
    pTransport( 0 ),
    pPoller( 0 ),
    pTaskManager( 0 ),
    pIncomingQueue( 0 ),
    pChannelData( 0 ),
    pLastStreamError( 0 ),
    pConnectionCount( 0 ),
    pConnectionInitTime( 0 ),
    pSessionId( 0 )
  {
    std::ostringstream o;
    o << pUrl->GetHostId() << " #" << pStreamNum;
    pStreamName = o.str();

    Env *env = DefaultEnv::GetEnv();
    int connectionWindow = DefaultConnectionWindow;
    env->GetInt( "ConnectionWindow", connectionWindow );
    pConnectionWindow = connectionWindow;

    int connectionRetry = DefaultConnectionRetry;
    env->GetInt( "ConnectionRetry", connectionRetry );
    pConnectionRetry = connectionRetry;

    int streamErrorWindow = DefaultStreamErrorWindow;
    env->GetInt( "StreamErrorWindow", streamErrorWindow );
    pStreamErrorWindow = streamErrorWindow;
  }

  //----------------------------------------------------------------------------
  // Destructor
  //----------------------------------------------------------------------------
  Stream::~Stream()
  {
    Disconnect( true );
    SubStreamList::iterator it;
    for( it = pSubStreams.begin(); it != pSubStreams.end(); ++it )
      delete *it;
  }

  //----------------------------------------------------------------------------
  // Initializer
  //----------------------------------------------------------------------------
  Status Stream::Initialize()
  {
    if( !pTransport || !pPoller || !pChannelData )
      return Status( stError, errUninitialized );

    AsyncSocketHandler *s = new AsyncSocketHandler( pPoller,
                                                    pTransport,
                                                    pChannelData,
                                                    0 );
    s->SetStream( this );
    pSubStreams.push_back( new SubStreamData() );
    pSubStreams[0]->socket = s;
    return Status();
  }

  //------------------------------------------------------------------------
  // Make sure that the underlying socket handler gets write readiness
  // events
  //------------------------------------------------------------------------
  Status Stream::EnableLink( PathID &path )
  {
    XrdSysMutexHelper scopedLock( pMutex );

    //--------------------------------------------------------------------------
    // We are in the process of connecting the main stream, so we do nothing
    // because when the main stream connection is established it will connect
    // all the other streams
    //--------------------------------------------------------------------------
    if( pSubStreams[0]->status == Socket::Connecting )
      return Status();

    //--------------------------------------------------------------------------
    // The main stream is connected, so we can verify whether we have
    // the up and the down stream connected and ready to handle data.
    // If anything is not right we fall back to stream 0.
    //--------------------------------------------------------------------------
    if( pSubStreams[0]->status == Socket::Connected )
    {
      if( pSubStreams[path.down]->status != Socket::Connected )
        path.down = 0;

      if( pSubStreams[path.up]->status == Socket::Disconnected )
      {
        path.up = 0;
        return pSubStreams[0]->socket->EnableUplink();
      }

      if( pSubStreams[path.up]->status == Socket::Connected )
        return pSubStreams[path.up]->socket->EnableUplink();

      return Status();
    }

    //--------------------------------------------------------------------------
    // The main stream is not connected, we need to check whether enough time
    // has passed since we last encoutnered an error (if any) so that we could
    // reattempt the connection
    //--------------------------------------------------------------------------
    Log *log = DefaultEnv::GetLog();
    time_t now = ::time(0);

    if( now-pLastStreamError < pStreamErrorWindow )
      return Status( stFatal, errConnectionError );

    pConnectionInitTime = now;
    ++pConnectionCount;

    //--------------------------------------------------------------------------
    // Resolve all the addresses of the host we're supposed to connect to
    //--------------------------------------------------------------------------
    Status st = Utils::GetHostAddresses( pAddresses, *pUrl );
    if( !st.IsOK() )
    {
      log->Error( PostMasterMsg, "[%s] Unable to resolve IP address for "
                  "the host", pStreamName.c_str() );
      pLastStreamError = now;
      st.status = stFatal;
      return st;
    }

    Utils::LogHostAddresses( log, PostMasterMsg, pUrl->GetHostId(),
                             pAddresses );

    //--------------------------------------------------------------------------
    // Initiate the connection process to the first one on the list
    //--------------------------------------------------------------------------
    sockaddr_in addr;
    memcpy( &addr, &pAddresses.back(), sizeof( sockaddr_in ) );
    pAddresses.pop_back();
    pSubStreams[0]->socket->SetAddress( addr );
    st = pSubStreams[0]->socket->Connect( pConnectionWindow );
    if( st.IsOK() )
      pSubStreams[0]->status = Socket::Connecting;
    return st;
  }

  //----------------------------------------------------------------------------
  // Queue the message for sending
  //----------------------------------------------------------------------------
  Status Stream::Send( Message              *msg,
                       OutgoingMsgHandler   *handler,
                       bool                  stateful,
                       time_t                expires )
  {
    XrdSysMutexHelper scopedLock( pMutex );
    Log *log = DefaultEnv::GetLog();

    //--------------------------------------------------------------------------
    // Check the session ID and bounce if needed
    //--------------------------------------------------------------------------
    if( msg->GetSessionId() &&
        (pSubStreams[0]->status != Socket::Connected ||
        pSessionId != msg->GetSessionId()) )
      return Status( stError, errInvalidSession );

    //--------------------------------------------------------------------------
    // Decide on the path to send the message
    //--------------------------------------------------------------------------
    PathID path = pTransport->MultiplexSubStream( msg, *pChannelData );
    if( pSubStreams.size() <= path.up )
    {
      log->Warning( PostMasterMsg, "[%s] Unable to send message %s through "
                    "substream %d using 0 instead", pStreamName.c_str(),
                    msg->GetDescription().c_str(), path.up );
      path.up = 0;
    }

    log->Dump( PostMasterMsg, "[%s] Sending message %s through substream %d "
               "expecting answer at %d", pStreamName.c_str(),
               msg->GetDescription().c_str(), path.up, path.down );

    //--------------------------------------------------------------------------
    // Enable *a* path and insert the message to the right queue
    //--------------------------------------------------------------------------
    Status st = EnableLink( path );
    if( st.IsOK() )
    {
      pTransport->MultiplexSubStream( msg, *pChannelData, &path );
      pSubStreams[path.up]->outQueue->PushBack( msg, handler,
                                                expires, stateful );
    }
    else
      st.status = stFatal;
    return st;
  }

  //----------------------------------------------------------------------------
  // Force connection
  //----------------------------------------------------------------------------
  void Stream::ForceConnect()
  {
    XrdSysMutexHelper scopedLock( pMutex );
    pSubStreams[0]->status = Socket::Disconnected;
    XrdCl::PathID path( 0, 0 );
    XrdCl::Status st = EnableLink( path );
    if( !st.IsOK() )
      OnConnectError( 0, st );
  }

  //----------------------------------------------------------------------------
  // Disconnect the stream
  //----------------------------------------------------------------------------
  void Stream::Disconnect( bool /*force*/ )
  {
  }

  //----------------------------------------------------------------------------
  // Handle a clock event
  //----------------------------------------------------------------------------
  void Stream::Tick( time_t now )
  {
    pMutex.Lock();
    SubStreamList::iterator it;
    OutQueue q;
    for( it = pSubStreams.begin(); it != pSubStreams.end(); ++it )
      q.GrabExpired( *(*it)->outQueue, now );
    pMutex.UnLock();

    q.Report( Status( stError, errSocketTimeout ) );
    if( pStreamNum == 0 )
      pIncomingQueue->ReportTimeout( now );
  }
}

//------------------------------------------------------------------------------
// Handle message timeouts and reconnection in the future
//------------------------------------------------------------------------------
namespace
{
  class StreamConnectorTask: public XrdCl::Task
  {
    public:
      //------------------------------------------------------------------------
      // Constructor
      //------------------------------------------------------------------------
      StreamConnectorTask( XrdCl::Stream *stream ):
        pStream( stream )
      {
        std::string name = "StreamConnectorTask for ";
        name += stream->GetName();
        SetName( name );
      }

      //------------------------------------------------------------------------
      // Run the task
      //------------------------------------------------------------------------
      time_t Run( time_t )
      {
        pStream->ForceConnect();
        return 0;
      }

    private:
      XrdCl::Stream *pStream;
  };
}

namespace XrdCl
{
  //----------------------------------------------------------------------------
  // Call back when a message has been reconstructed
  //----------------------------------------------------------------------------
  void Stream::OnIncoming( uint16_t /*subStream*/, Message *msg )
  {
    msg->SetSessionId( pSessionId );
    if( pTransport->Highjack( msg, *pChannelData ) )
      return;
    pIncomingQueue->AddMessage( msg );
  }

  //----------------------------------------------------------------------------
  // Call when one of the sockets is ready to accept a new message
  //----------------------------------------------------------------------------
  Message *Stream::OnReadyToWrite( uint16_t subStream )
  {
    XrdSysMutexHelper scopedLock( pMutex );
    Log *log = DefaultEnv::GetLog();
    if( pSubStreams[subStream]->outQueue->IsEmpty() )
    {
      log->Dump( PostMasterMsg, "[%s] Nothing to write, disable uplink",
                 pSubStreams[subStream]->socket->GetStreamName().c_str() );

      pSubStreams[subStream]->socket->DisableUplink();
      return 0;
    }

    OutMessageHelper &h = pSubStreams[subStream]->msgHelper;
    h.msg = pSubStreams[subStream]->outQueue->PopMessage( h.handler,
                                                          h.expires,
                                                          h.stateful );
    scopedLock.UnLock();
    if( h.handler )
      h.handler->OnReadyToSend( h.msg, pStreamNum );
    return h.msg;
  }

  //----------------------------------------------------------------------------
  // Call when a message is written to the socket
  //----------------------------------------------------------------------------
  void Stream::OnMessageSent( uint16_t subStream, Message *msg )
  {
    OutMessageHelper &h = pSubStreams[subStream]->msgHelper;
    if( h.handler )
      h.handler->OnStatusReady( msg, Status() );
    pSubStreams[subStream]->msgHelper.Reset();
  }

  //----------------------------------------------------------------------------
  // Call back when a message has been reconstructed
  //----------------------------------------------------------------------------
  void Stream::OnConnect( uint16_t subStream )
  {
    XrdSysMutexHelper scopedLock( pMutex );
    pSubStreams[subStream]->status = Socket::Connected;
    Log *log = DefaultEnv::GetLog();
    log->Debug( PostMasterMsg, "[%s] Stream %d connected.", pStreamName.c_str(),
                subStream );

    if( subStream == 0 )
    {
      pLastStreamError = 0;
      pConnectionCount = 0;
      uint16_t numSub = pTransport->SubStreamNumber( *pChannelData );
      ++pSessionId;

      //------------------------------------------------------------------------
      // Create the streams if they don't exist yet
      //------------------------------------------------------------------------
      if( pSubStreams.size() == 1 && numSub > 1 )
      {
        for( uint16_t i = 1; i < numSub; ++i )
        {
          AsyncSocketHandler *s = new AsyncSocketHandler( pPoller, pTransport,
                                                          pChannelData, i );
          s->SetStream( this );
          pSubStreams.push_back( new SubStreamData() );
          pSubStreams[i]->socket = s;
        }
      }

      //------------------------------------------------------------------------
      // Connect the extra streams, if we fail we move all the outgoing items
      // to stream 0, we don't need to enable the uplink here, because it
      // should be already enabled after the handshaking process is completed.
      //------------------------------------------------------------------------
      if( pSubStreams.size() > 1 )
      {
        log->Debug( PostMasterMsg, "[%s] Attempting to connect %d additional "
                    "streams.", pStreamName.c_str(), pSubStreams.size()-1 );
        for( size_t i = 1; i < pSubStreams.size(); ++i )
        {
          pSubStreams[i]->socket->SetAddress( pSubStreams[0]->socket->GetAddress() );
          Status st = pSubStreams[i]->socket->Connect( pConnectionWindow );
          if( !st.IsOK() )
          {
            pSubStreams[0]->outQueue->GrabItems( *pSubStreams[i]->outQueue );
            pSubStreams[i]->socket->Close();
          }
          else
          {
            pSubStreams[i]->status = Socket::Connecting;
          }
        }
      }
    }
  }

  //----------------------------------------------------------------------------
  // On connect error
  //----------------------------------------------------------------------------
  void Stream::OnConnectError( uint16_t subStream, Status st )
  {
    XrdSysMutexHelper scopedLock( pMutex );
    Log *log = DefaultEnv::GetLog();
    pSubStreams[subStream]->socket->Close();
    time_t now = ::time(0);

    //--------------------------------------------------------------------------
    // If we connected subStream == 0 and cannot connect >0 then we just give
    // up and move the outgoing messages to another queue
    //--------------------------------------------------------------------------
    if( subStream > 0 )
    {
      pSubStreams[subStream]->status = Socket::Disconnected;
      pSubStreams[0]->outQueue->GrabItems( *pSubStreams[subStream]->outQueue );
      if( pSubStreams[0]->status == Socket::Connected )
      {
        Status st = pSubStreams[0]->socket->EnableUplink();
        if( !st.IsOK() )
          OnFatalError( 0, st, scopedLock );
        return;
      }

      if( pSubStreams[0]->status == Socket::Connecting )
        return;

      OnFatalError( subStream, st, scopedLock );
      return;
    }

    //--------------------------------------------------------------------------
    // Check if we still have time to try and do somethig in the current window
    //--------------------------------------------------------------------------
    time_t elapsed = now-pConnectionInitTime;
    if( elapsed < pConnectionWindow )
    {
      //------------------------------------------------------------------------
      // If we have some IP addresses left we try them
      //------------------------------------------------------------------------
      if( !pAddresses.empty() )
      {
        sockaddr_in addr;
        memcpy( &addr, &pAddresses.back(), sizeof( sockaddr_in ) );
        pAddresses.pop_back();
        pSubStreams[0]->socket->SetAddress( addr );

        Status st = pSubStreams[0]->socket->Connect( pConnectionWindow-elapsed );
        if( !st.IsOK() )
          OnFatalError( subStream, st, scopedLock );
        return;
      }

      //------------------------------------------------------------------------
      // If we still can retry with the same host name, we sleep until the end
      // of the connection window and try
      //------------------------------------------------------------------------
      else if( pConnectionCount < pConnectionRetry )
      {
        log->Info( PostMasterMsg, "[%s] Attempting reconnection in %d "
                   "seconds.", pStreamName.c_str(), pConnectionWindow-elapsed );

        Task *task = new ::StreamConnectorTask( this );
        pTaskManager->RegisterTask( task, pConnectionInitTime+pConnectionWindow );
        return;
      }

      //------------------------------------------------------------------------
      // Nothing can be done, we declare a failure
      //------------------------------------------------------------------------
      OnFatalError( subStream, Status( stFatal, errConnectionError ),
                    scopedLock );
      return;
    }

    //--------------------------------------------------------------------------
    // We are out of the connection window, the only thing we can do here
    // is re-resolving the host name and retrying if we still can
    //--------------------------------------------------------------------------
    if( pConnectionCount < pConnectionRetry )
    {
      pAddresses.clear();
      pSubStreams[0]->status = Socket::Disconnected;
      PathID path( 0, 0 );
      st = EnableLink( path );
      if( !st.IsOK() )
        OnFatalError( subStream, Status( stFatal, errConnectionError ),
                      scopedLock );
      return;
    }

    //--------------------------------------------------------------------------
    // Else, we fail
    //--------------------------------------------------------------------------
    OnFatalError( subStream, Status( stFatal, errConnectionError ),
                  scopedLock );
  }

  //----------------------------------------------------------------------------
  // Call back when an error has occured
  //----------------------------------------------------------------------------
  void Stream::OnError( uint16_t subStream, Status status )
  {
    XrdSysMutexHelper scopedLock( pMutex );
    Log *log = DefaultEnv::GetLog();
    pSubStreams[subStream]->socket->Close();
    pSubStreams[subStream]->status = Socket::Disconnected;

    log->Debug( PostMasterMsg, "[%s] Recovering error for stream #%d: %s.",
                pStreamName.c_str(), subStream, status.ToString().c_str() );

    //--------------------------------------------------------------------------
    // Reinsert the stuff that we have failed to sent
    //--------------------------------------------------------------------------
    if( pSubStreams[subStream]->msgHelper.msg )
    {
      OutMessageHelper &h = pSubStreams[subStream]->msgHelper;
      pSubStreams[subStream]->outQueue->PushFront( h.msg, h.handler, h.expires,
                                                   h.stateful );
      pSubStreams[subStream]->msgHelper.Reset();
    }

    //--------------------------------------------------------------------------
    // We are dealing with an error of a peripheral stream. If we don't have
    // anything to send don't bother recovering. Otherwise move the requests
    // to stream 0 if possible.
    //--------------------------------------------------------------------------
    if( subStream > 0 )
    {
      if( pSubStreams[subStream]->outQueue->IsEmpty() )
        return;

      if( pSubStreams[0]->status != Socket::Disconnected )
      {
        pSubStreams[0]->outQueue->GrabItems( *pSubStreams[subStream]->outQueue );
        if( pSubStreams[0]->status == Socket::Connected )
        {
          Status st = pSubStreams[0]->socket->EnableUplink();
          if( !st.IsOK() )
            OnFatalError( 0, st, scopedLock );
          return;
        }
      }
      OnFatalError( subStream, status, scopedLock );
      return;
    }

    //--------------------------------------------------------------------------
    // If we lost the stream 0 we have lost the session, we re-enable the
    // stream if we still have things in one of the outgoing queues, otherwise
    // there is not point to recover at this point.
    //--------------------------------------------------------------------------
    if( subStream == 0 )
    {
      SubStreamList::iterator it;
      size_t outstanding = 0;
      for( it = pSubStreams.begin(); it != pSubStreams.end(); ++it )
        outstanding += (*it)->outQueue->GetSizeStateless();

      if( outstanding )
      {
        PathID path( 0, 0 );
        Status st = EnableLink( path );
        if( !st.IsOK() )
        {
          OnFatalError( 0, st, scopedLock );
          return;
        }
      }

      //------------------------------------------------------------------------
      // We're done here, unlock the stream mutex to avoid deadlocks and
      // report the disconnection event to the handlers
      //------------------------------------------------------------------------
      log->Debug( PostMasterMsg, "[%s] Reporting disconnection to queued "
                  "message handlers.", pStreamName.c_str() );
      OutQueue q;
      for( it = pSubStreams.begin(); it != pSubStreams.end(); ++it )
        q.GrabStateful( *(*it)->outQueue );
      scopedLock.UnLock();

      q.Report( status );
      pIncomingQueue->ReportStreamEvent( IncomingMsgHandler::Broken,
                                         pStreamNum, status );
      pChannelEvHandlers.ReportEvent( ChannelEventHandler::StreamBroken, status,
                                      pStreamNum );
      return;
    }
  }

  //----------------------------------------------------------------------------
  // On fatal error
  //----------------------------------------------------------------------------
  void Stream::OnFatalError( uint16_t           subStream,
                             Status             status,
                             XrdSysMutexHelper &lock )
  {
    Log    *log = DefaultEnv::GetLog();
    pSubStreams[subStream]->status = Socket::Disconnected;
    log->Error( PostMasterMsg, "[%s] Unable to recover: %s.",
                pStreamName.c_str(), status.ToString().c_str() );

    pConnectionCount = 0;
    pLastStreamError = ::time(0);

    SubStreamList::iterator it;
    OutQueue q;
    for( it = pSubStreams.begin(); it != pSubStreams.end(); ++it )
      q.GrabItems( *(*it)->outQueue );
    lock.UnLock();

    status.status = stFatal;
    q.Report( status );
    pIncomingQueue->ReportStreamEvent( IncomingMsgHandler::FatalError,
                                       pStreamNum, status );
    pChannelEvHandlers.ReportEvent( ChannelEventHandler::FatalError, status,
                                    pStreamNum );

  }

  //----------------------------------------------------------------------------
  // Call back when a message has been reconstructed
  //----------------------------------------------------------------------------
  void Stream::OnReadTimeout( uint16_t /*substream*/ )
  {
  }

  //----------------------------------------------------------------------------
  // Call back when a message has been reconstru
  //----------------------------------------------------------------------------
  void Stream::OnWriteTimeout( uint16_t /*substream*/ )
  {
  }

  //------------------------------------------------------------------------
  // Register channel event handler
  //------------------------------------------------------------------------
  void Stream::RegisterEventHandler( ChannelEventHandler *handler )
  {
    pChannelEvHandlers.AddHandler( handler );
  }

  //------------------------------------------------------------------------
  // Remove a channel event handler
  //------------------------------------------------------------------------
  void Stream::RemoveEventHandler( ChannelEventHandler *handler )
  {
    pChannelEvHandlers.RemoveHandler( handler );
  }

}
