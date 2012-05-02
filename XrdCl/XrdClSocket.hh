//------------------------------------------------------------------------------
// Copyright (c) 2012 by European Organization for Nuclear Research (CERN)
// Author: Lukasz Janyst <ljanyst@cern.ch>
// See the LICENCE file for details.
//------------------------------------------------------------------------------

#ifndef __XRD_CL_SOCKET_HH__
#define __XRD_CL_SOCKET_HH__

#include <stdint.h>
#include <string>
#include <sys/socket.h>

#include "XrdCl/XrdClStatus.hh"

namespace XrdCl
{
  //----------------------------------------------------------------------------
  //! A network socket
  //----------------------------------------------------------------------------
  class Socket
  {
    public:
      //------------------------------------------------------------------------
      //! Status of the socket
      //------------------------------------------------------------------------
      enum SocketStatus
      {
        Uninitialized = 0,      //!< The socket hasn't been initialized
        Initialized   = 1,      //!< The socket is initialized but not connected
        Connected     = 2,      //!< The socket is connected
        Connecting    = 3       //!< The connection process is in progress
      };

      //------------------------------------------------------------------------
      //! Constructor
      //!
      //! @param socket already connected socket if available, -1 otherwise
      //! @param status status of a socket if available
      //------------------------------------------------------------------------
      Socket( int socket = -1, SocketStatus status = Uninitialized ):
        pSocket(socket), pStatus( status ), pServerAddr( 0 )
      {
        if( pSocket != -1 && status == Uninitialized )
          pStatus = Initialized;
      };

      //------------------------------------------------------------------------
      //! Desctuctor
      //------------------------------------------------------------------------
      virtual ~Socket()
      {
        Close();
      };

      //------------------------------------------------------------------------
      //! Initialize the socket
      //------------------------------------------------------------------------
      Status Initialize();

      //------------------------------------------------------------------------
      //! Set the socket flags (man fcntl)
      //------------------------------------------------------------------------
      Status SetFlags( int flags );

      //------------------------------------------------------------------------
      //! Get the socket flags (man fcntl)
      //------------------------------------------------------------------------
      Status GetFlags( int &flags );

      //------------------------------------------------------------------------
      //! Get socket options
      //------------------------------------------------------------------------
      Status GetSockOpt( int level, int optname, void *optval,
                         socklen_t *optlen );

      //------------------------------------------------------------------------
      //! Set socket options
      //------------------------------------------------------------------------
      Status SetSockOpt( int level, int optname, const void *optval,
                         socklen_t optlen );

      //------------------------------------------------------------------------
      //! Connect to the given URL
      //!
      //! @param host   name of the host to connect to
      //! @param port   port to connect to
      //! @param timout timeout in seconds, 0 for no timeout handling (may be
      //!               used for non blocking IO)
      //------------------------------------------------------------------------
      Status Connect( const std::string &host,
                      uint16_t           port,
                      uint16_t           timout = 10 );

      //------------------------------------------------------------------------
      //! Disconnect
      //------------------------------------------------------------------------
      void Close();

      //------------------------------------------------------------------------
      //! Get the socket status
      //------------------------------------------------------------------------
      SocketStatus GetStatus() const
      {
        return pStatus;
      }

      //------------------------------------------------------------------------
      //! Set socket status - do not use unless you know what you're doing
      //------------------------------------------------------------------------
      void SetStatus( SocketStatus status )
      {
        pStatus = status;
      }

      //------------------------------------------------------------------------
      //! Read raw bytes from the socket
      //!
      //! @param buffer    data to be sent
      //! @param size      size of the data buffer
      //! @param timeout   timout value in seconds, -1 to wait indefinitely
      //! @param bytesRead the amount of data actually read
      //------------------------------------------------------------------------
      Status ReadRaw( void *buffer, uint32_t size, int32_t timeout,
                      uint32_t &bytesRead );

      //------------------------------------------------------------------------
      //! Write raw bytes to the socket
      //!
      //! @param buffer       data to be written
      //! @param size         size of the data buffer
      //! @param timeout      timeout value in seconds, -1 to wait indefinitely
      //! @param bytesWritten the amount of data actually written
      //------------------------------------------------------------------------
      Status WriteRaw( void *buffer, uint32_t size, int32_t timeout,
                       uint32_t &bytesWritten );

      //------------------------------------------------------------------------
      //! Get the file descriptor
      //------------------------------------------------------------------------
      int GetFD()
      {
        return pSocket;
      }

      //------------------------------------------------------------------------
      //! Get the name of the socket
      //------------------------------------------------------------------------
      std::string GetSockName() const;

      //------------------------------------------------------------------------
      //! Get the name of the remote peer
      //------------------------------------------------------------------------
      std::string GetPeerName() const;

      //------------------------------------------------------------------------
      //! Get the string representation of the socket
      //------------------------------------------------------------------------
      std::string GetName() const;

      //------------------------------------------------------------------------
      //! Get the server address
      //------------------------------------------------------------------------
      const sockaddr *GetServerAddress() const
      {
        return pServerAddr;
      }

    private:
      //------------------------------------------------------------------------
      //! Poll the socket to see whether it is ready for IO
      //!
      //! @param  readyForReading poll for readiness to read
      //! @param  readyForWriting poll for readiness to write
      //! @param  timeout         timeout in seconds, -1 to wait indefinitely
      //! @return stOK                  - ready for IO
      //!         errSocketDisconnected - on disconnection
      //!         errSocketError        - on socket error
      //!         errSocketTimeout      - on socket timeout
      //!         errInvalidOp          - when called on a non connected socket
      //------------------------------------------------------------------------
      Status Poll( bool readyForReading, bool readyForWriting,
                   int32_t timeout );

      int                  pSocket;
      SocketStatus         pStatus;
      sockaddr            *pServerAddr;
      mutable std::string  pSockName;     // mutable because it's for caching
      mutable std::string  pPeerName;
      mutable std::string  pName;
  };
}

#endif // __XRD_CL_SOCKET_HH__
 
