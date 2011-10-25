//------------------------------------------------------------------------------
// author: Lukasz Janyst <ljanyst@cern.ch>
// desc:   Poller test
//------------------------------------------------------------------------------

#include <cppunit/extensions/HelperMacros.h>
#include "XrdCl/XrdClPoller.hh"
#include "Server.hh"
#include "Utils.hh"
#include "XrdCl/XrdClUtils.hh"
#include "XrdCl/XrdClSocket.hh"

#include <vector>

#ifdef HAVE_LIBEVENT
#include "XrdCl/XrdClPollerLibEvent.hh"
#endif

//------------------------------------------------------------------------------
// Declaration
//------------------------------------------------------------------------------
class PollerTest: public CppUnit::TestCase
{
  public:
    CPPUNIT_TEST_SUITE( PollerTest );
      CPPUNIT_TEST( FunctionTestLibEvent );
    CPPUNIT_TEST_SUITE_END();
    void FunctionTestLibEvent();
    void FunctionTest( XrdClient::Poller *poller );
};

CPPUNIT_TEST_SUITE_REGISTRATION( PollerTest );

//------------------------------------------------------------------------------
// Client handler
//------------------------------------------------------------------------------
class RandomPumpHandler: public ClientHandler
{
  public:
    //--------------------------------------------------------------------------
    // Pump some random data through the socket
    //--------------------------------------------------------------------------
    virtual void HandleConnection( int socket, XrdClient::Log *log )
    {
      XrdClient::ScopedDescriptor scopetDesc( socket );

      uint8_t  packets = random() % 100;
      uint16_t packetSize;
      char     buffer[50000];
      log->Debug( 1, "Sending %d packets to the client", packets );

      for( int i = 0; i < packets; ++i )
      {
        packetSize = random() % 50000;
        log->Dump( 1, "Sending %d packet, %d bytes of data", i, packetSize );
        if( Utils::GetRandomBytes( buffer, packetSize ) != packetSize )
        {
          log->Error( 1, "Unable to get %d bytes of random data", packetSize );
          return;
        }

        if( ::write( socket, buffer, packetSize ) != packetSize )
        {
          log->Error( 1, "Unable to send the %d bytes of random data",
                      packetSize );
          return;
        }
        UpdateSentData( buffer, packetSize );
      }
    }
};

//------------------------------------------------------------------------------
// Client handler factory
//------------------------------------------------------------------------------
class RandomPumpHandlerFactory: public ClientHandlerFactory
{
  public:
    virtual ClientHandler *CreateHandler()
    {
      return new RandomPumpHandler();
    }
};

//------------------------------------------------------------------------------
// Socket listener
//------------------------------------------------------------------------------
class SocketHandler: public XrdClient::SocketEventListener
{
  public:
    //--------------------------------------------------------------------------
    // Handle an event
    //--------------------------------------------------------------------------
    virtual void Event( uint8_t type,
                        XrdClient::Socket *socket,
                        XrdClient::Poller *poller )
    {
      //------------------------------------------------------------------------
      // Read event
      //------------------------------------------------------------------------
      if( type & ReadyToRead )
      {
        char    buffer[50000];
        int     desc = socket->GetFD();
        ssize_t ret;

        while( 1 )
        {
          char     *current   = buffer;
          uint32_t  spaceLeft = 50000;
          while( (spaceLeft > 0) &&
                 ((ret = ::read( desc, current, spaceLeft )) > 0) )
          {
            current   += ret;
            spaceLeft -= ret;
          }

          UpdateTransferMap( socket->GetSockName(), buffer, 50000-spaceLeft );

          if( ret == 0 )
          {
            poller->RemoveSocket( socket );
            return;
          }

          if( ret < 0 )
          {
            if( errno != EAGAIN && errno != EWOULDBLOCK )
              poller->RemoveSocket( socket );
            return;
          }
        }
      }

      //------------------------------------------------------------------------
      // Timeout
      //------------------------------------------------------------------------
      if( type & TimeOut )
        poller->RemoveSocket( socket );
    }

    //--------------------------------------------------------------------------
    // Update the checksums
    //--------------------------------------------------------------------------
    void UpdateTransferMap( const std::string &sockName,
                            const void        *buffer,
                            uint32_t           size )
    {
      //------------------------------------------------------------------------
      // Check if we have an entry in the map
      //------------------------------------------------------------------------
      std::pair<Server::TransferMap::iterator, bool> res;
      Server::TransferMap::iterator it;
      res = pMap.insert( std::make_pair( sockName, std::make_pair( 0, 0 ) ) );
      it = res.first;
      if( res.second == true )
      {
        it->second.first  = 0;
        it->second.second = Utils::ComputeCRC32( 0, 0 );
      }

      //------------------------------------------------------------------------
      // Update the entry
      //------------------------------------------------------------------------
      it->second.first += size;
      it->second.second = Utils::UpdateCRC32( it->second.second, buffer, size );
    }

    //--------------------------------------------------------------------------
    //! Get the stats of the received data
    //--------------------------------------------------------------------------
    std::pair<uint64_t, uint32_t> GetReceivedStats(
                                      const std::string sockName ) const
    {
      Server::TransferMap::const_iterator it = pMap.find( sockName );
      if( it == pMap.end() )
        return std::make_pair( 0, 0 );
      return it->second;
    }

  private:
    Server::TransferMap pMap;
};

//------------------------------------------------------------------------------
// Test the functionality of a poller
//------------------------------------------------------------------------------
void PollerTest::FunctionTest( XrdClient::Poller *poller )
{
  //----------------------------------------------------------------------------
  // Initialize
  //----------------------------------------------------------------------------
  using namespace XrdClient;
  Server server;
  Socket s[3];
  CPPUNIT_ASSERT( server.Setup( 9999, 3, new RandomPumpHandlerFactory() ) );
  CPPUNIT_ASSERT( server.Start() );
  CPPUNIT_ASSERT( poller->Initialize() );
  CPPUNIT_ASSERT( poller->Start() );

  //----------------------------------------------------------------------------
  // Connect the sockets
  //----------------------------------------------------------------------------
  SocketHandler *handler = new SocketHandler();
  for( int i = 0; i < 3; ++i )
  {
    CPPUNIT_ASSERT( s[i].Connect( URL( "localhost:9999" ) ).status == stOK );
    CPPUNIT_ASSERT( poller->AddSocket( &s[i], handler, 60 ) );
    CPPUNIT_ASSERT( poller->IsRegistered( &s[i] ) );
  }

  //----------------------------------------------------------------------------
  // All the business happens elsewhere so we have nothing better to do
  // here that wait, otherwise server->stop will hang.
  //----------------------------------------------------------------------------
  ::sleep(2);
  
  //----------------------------------------------------------------------------
  // Cleanup
  //----------------------------------------------------------------------------
  CPPUNIT_ASSERT( poller->Stop() );
  CPPUNIT_ASSERT( server.Stop() );
  CPPUNIT_ASSERT( poller->Finalize() );

  std::pair<uint64_t, uint32_t> stats[3];
  std::pair<uint64_t, uint32_t> statsServ[3];
  for( int i = 0; i < 3; ++i )
  {
    CPPUNIT_ASSERT( !poller->IsRegistered( &s[i] ) );
    stats[i] = handler->GetReceivedStats( s[i].GetSockName() );
    statsServ[i] = server.GetSentStats( s[i].GetSockName() );
    CPPUNIT_ASSERT( stats[i].first == statsServ[i].first );
    CPPUNIT_ASSERT( stats[i].second == statsServ[i].second );
  }

  for( int i = 0; i < 3; ++i )
    s[i].Disconnect();

  delete handler;
}

//------------------------------------------------------------------------------
// Test the functionality libEvent based poller
//------------------------------------------------------------------------------
void PollerTest::FunctionTestLibEvent()
{
#ifdef HAVE_LIBEVENT
  XrdClient::Poller *poller = new XrdClient::PollerLibEvent();
  FunctionTest( poller );
  delete poller;
#else
  CPPUNIT_ASSERT_MESSAGE( "LibEvent poller implementation is absent", false );
#endif
}