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

#ifndef __XRD_CL_POST_MASTER_INTERFACES_HH__
#define __XRD_CL_POST_MASTER_INTERFACES_HH__

#include <stdint.h>
#include <ctime>

#include "XrdCl/XrdClStatus.hh"
#include "XrdCl/XrdClAnyObject.hh"
#include "XrdCl/XrdClURL.hh"

namespace XrdCl
{
  class Channel;
  class Message;
  class Socket;
  class URL;

  //----------------------------------------------------------------------------
  //! Message filter
  //----------------------------------------------------------------------------
  class MessageFilter
  {
    public:
      virtual ~MessageFilter() {}

      //------------------------------------------------------------------------
      //! Examine the message and return true if the message should be picked
      //! up (usually removed from the queue and to the caller)
      //------------------------------------------------------------------------
      virtual bool Filter( const Message *msg ) = 0;
  };

  //----------------------------------------------------------------------------
  //! Message handler
  //----------------------------------------------------------------------------
  class IncomingMsgHandler
  {
    public:
      //------------------------------------------------------------------------
      //! Actions to be taken after a message is processed by the handler
      //------------------------------------------------------------------------
      enum Action
      {
        Take          = 0x01,     //!< Take ownership over the message
        Ignore        = 0x02,     //!< Ignore the message
        RemoveHandler = 0x04      //!< Remove the handler from the notification
                                  //!< list
      };

      //------------------------------------------------------------------------
      //! Events that may have occured to the stream
      //------------------------------------------------------------------------
      enum StreamEvent
      {
        Ready      = 1, //!< The stream has become connected
        Broken     = 2, //!< The stream is broken
        Timeout    = 3, //!< The declared timeout has occured
        FatalError = 4  //!< Stream has been broken and won't be recovered
      };

      //------------------------------------------------------------------------
      //! Event types that the message handler may receive
      //------------------------------------------------------------------------

      virtual ~IncomingMsgHandler() {}

      //------------------------------------------------------------------------
      //! Examine an incomming message, and decide on the action to be taken
      //!
      //! @param msg    the message, may be zero if receive failed
      //! @return       action type that needs to be take wrt the message and
      //!               the handler
      //------------------------------------------------------------------------
      virtual uint8_t OnIncoming( Message *msg ) = 0;

      //------------------------------------------------------------------------
      //! Handle an event other that a message arrival
      //!
      //! @param event     type of the event
      //! @param streamNum stream concerned
      //! @param status    status info
      //! @return          Action::RemoveHandler or 0
      //------------------------------------------------------------------------
      virtual uint8_t OnStreamEvent( StreamEvent event,
                                     uint16_t    streamNum,
                                     Status      status )
      {
        return 0;
      };
  };

  //----------------------------------------------------------------------------
  //! Message status handler
  //----------------------------------------------------------------------------
  class OutgoingMsgHandler
  {
    public:
      virtual ~OutgoingMsgHandler() {}

      //------------------------------------------------------------------------
      //! The requested action has been performed and the status is available
      //------------------------------------------------------------------------
      virtual void OnStatusReady( const Message *message,
                                  Status         status ) = 0;

      //------------------------------------------------------------------------
      //! Called just before the message is going to be sent through
      //! a valid connection, so that the user can still make some
      //! modifications that were impossible before (ie. protocol version
      //! dependent adjustments)
      //!
      //! @param msg       message concerned
      //! @param streamNum number of the stream the message will go through
      //------------------------------------------------------------------------
      virtual void OnReadyToSend( Message *msg, uint16_t streamNum ) {};
  };

  //----------------------------------------------------------------------------
  //! Channel event handler
  //----------------------------------------------------------------------------
  class ChannelEventHandler
  {
    public:
      //------------------------------------------------------------------------
      //! Events that may have occured to the channel
      //------------------------------------------------------------------------
      enum ChannelEvent
      {
        StreamReady  = 1, //!< The stream has become connected
        StreamBroken = 2, //!< The stream is broken
        FatalError   = 4  //!< Stream has been broken and won't be recovered
      };

      //------------------------------------------------------------------------
      //! Destructor
      //------------------------------------------------------------------------
      virtual ~ChannelEventHandler() {};

      //------------------------------------------------------------------------
      //! Event callback
      //!
      //! @param event   the event that has occured
      //! @param stream  the stream concerned
      //! @param status  the status info
      //! @return true if the handler should be kept
      //!         false if it should be removed from further consideration
      //------------------------------------------------------------------------
      virtual bool OnChannelEvent( ChannelEvent event,
                                   Status       status,
                                   uint16_t     stream ) = 0;
  };

  //----------------------------------------------------------------------------
  //! Data structure that carries the handshake information
  //----------------------------------------------------------------------------
  struct HandShakeData
  {
    //--------------------------------------------------------------------------
    //! Constructor
    //--------------------------------------------------------------------------
    HandShakeData( const URL *addr, uint16_t stream, uint16_t subStream ):
      step(0), out(0), in(0), url(addr), streamId(stream),
      subStreamId( subStream ), startTime( time(0) ), serverAddr(0)
    {}
    uint16_t     step;           //!< Handshake step
    Message     *out;            //!< Message to be sent out
    Message     *in;             //!< Message that has been received
    const URL   *url;            //!< Destination URL
    uint16_t     streamId;       //!< Stream number
    uint16_t     subStreamId;    //!< Sub-stream id
    time_t       startTime;      //!< Timestamp of when the handshake started
    const void  *serverAddr;     //!< Server address in the form of sockaddr
    std::string  clientName;     //!< Client name (an IPv6 representation)
    std::string  streamName;     //!< Name of the stream
  };

  //----------------------------------------------------------------------------
  //! Path ID - a pair of integers describing the up and down stream
  //! for given interaction
  //----------------------------------------------------------------------------
  struct PathID
  {
    PathID( uint16_t u = 0, uint16_t d = 0 ): up(u), down(d) {}
    uint16_t up;
    uint16_t down;
  };

  //----------------------------------------------------------------------------
  //! Transport query definitions
  //! The transports may support other queries, with ids > 1000
  //----------------------------------------------------------------------------
  struct TransportQuery
  {
    static const uint16_t Name = 1; //!< Transport name, returns const char *
    static const uint16_t Auth = 2; //!< Transport name, returns std::string *
  };

  //----------------------------------------------------------------------------
  //! Perform the handshake and the authentication for each physical stream
  //----------------------------------------------------------------------------
  class TransportHandler
  {
    public:
      virtual ~TransportHandler() {}

      //------------------------------------------------------------------------
      //! Read a message from the socket, the socket is non blocking, so if
      //! there is not enough data the function should retutn errRetry in which
      //! case it will be called again when more data arrives with the data
      //! previousely read stored in the message buffer
      //!
      //! @param message the message
      //! @param socket  the socket
      //! @return        stOK if the message has been processed properly,
      //!                stError & errRetry when the method needs to be called
      //!                again to finish reading the message
      //!                stError on faiure
      //------------------------------------------------------------------------
      virtual Status GetMessage( Message *message, Socket *socket ) = 0;

      //------------------------------------------------------------------------
      //! Initialize channel
      //------------------------------------------------------------------------
      virtual void InitializeChannel( AnyObject &channelData ) = 0;

      //------------------------------------------------------------------------
      //! Finalize channel
      //------------------------------------------------------------------------
      virtual void FinalizeChannel( AnyObject &channelData ) = 0;

      //------------------------------------------------------------------------
      //! HandHake
      //------------------------------------------------------------------------
      virtual Status HandShake( HandShakeData *handShakeData,
                                AnyObject     &channelData ) = 0;

      //------------------------------------------------------------------------
      //! Check if the stream should be disconnected
      //------------------------------------------------------------------------
      virtual bool IsStreamTTLElapsed( time_t     inactiveTime,
                                       AnyObject &channelData ) = 0;

      //------------------------------------------------------------------------
      //! Return the ID for the up stream this message should be sent by
      //! and the down stream which the answer should be expected at.
      //! Modify the message itself if necessary.
      //! If hint is non-zero then the message should be modified such that
      //! the answer will be returned via the hinted stream.
      //------------------------------------------------------------------------
      virtual PathID Multiplex( Message   *msg,
                                AnyObject &channelData,
                                PathID    *hint = 0 ) = 0;

      //------------------------------------------------------------------------
      //! Return the ID for the up substream this message should be sent by
      //! and the down substream which the answer should be expected at.
      //! Modify the message itself if necessary.
      //! If hint is non-zero then the message should be modified such that
      //! the answer will be returned via the hinted stream.
      //------------------------------------------------------------------------
      virtual PathID MultiplexSubStream( Message   *msg,
                                         AnyObject &channelData,
                                         PathID    *hint = 0 ) = 0;

      //------------------------------------------------------------------------
      //! Return a number of streams that should be created
      //------------------------------------------------------------------------
      virtual uint16_t StreamNumber( AnyObject &channelData ) = 0;

      //------------------------------------------------------------------------
      //! Return a number of substreams per stream that should be created
      //------------------------------------------------------------------------
      virtual uint16_t SubStreamNumber( AnyObject &channelData ) = 0;

      //------------------------------------------------------------------------
      //! The stream has been disconnected, do the cleanups
      //------------------------------------------------------------------------
      virtual void Disconnect( AnyObject &channelData,
                               uint16_t   streamId,
                               uint16_t   subStreamId ) = 0;

      //------------------------------------------------------------------------
      //! Query the channel
      //------------------------------------------------------------------------
      virtual Status Query( uint16_t   query,
                            AnyObject &result,
                            AnyObject &channelData ) = 0;

      //------------------------------------------------------------------------
      //! Check whether the transport can highjack the message
      //------------------------------------------------------------------------
      virtual bool Highjack( Message *msg, AnyObject &channelData ) = 0;

  };
}

#endif // __XRD_CL_POST_MASTER_INTERFACES_HH__
