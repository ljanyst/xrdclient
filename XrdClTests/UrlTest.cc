//------------------------------------------------------------------------------
// author: Lukasz Janyst <ljanyst@cern.ch>
// desc:   Url test
//------------------------------------------------------------------------------

#include <cppunit/extensions/HelperMacros.h>
#include "XrdCl/XrdClURL.hh"

//------------------------------------------------------------------------------
// Declaration
//------------------------------------------------------------------------------
class UrlTest: public CppUnit::TestCase
{
  public:
    CPPUNIT_TEST_SUITE( UrlTest );
      CPPUNIT_TEST( parsingTest );
    CPPUNIT_TEST_SUITE_END();
    void parsingTest();
};

CPPUNIT_TEST_SUITE_REGISTRATION( UrlTest );

//------------------------------------------------------------------------------
// Parsing test
//------------------------------------------------------------------------------
void UrlTest::parsingTest()
{
  XrdClient::URL url1( "root://user1:passwd1@host1:123//path?param1=val1&param2=val2" );
  XrdClient::URL url2( "root://user1@host1//path?param1=val1&param2=val2" );
  XrdClient::URL url3( "root://host1" );
  XrdClient::URL urlInvalid1( "root://user1:passwd1@host1:asd//path?param1=val1&param2=val2" );
  XrdClient::URL urlInvalid2( "root://user1:passwd1host1:123//path?param1=val1&param2=val2" );
  XrdClient::URL urlInvalid3( "root:////path?param1=val1&param2=val2" );
  XrdClient::URL urlInvalid4( "root://@//path?param1=val1&param2=val2" );
  XrdClient::URL urlInvalid5( "root://:@//path?param1=val1&param2=val2" );
  XrdClient::URL urlInvalid6( "root://" );
  XrdClient::URL urlInvalid7( "://asds" );
  XrdClient::URL urlInvalid8( "root://asd@://path?param1=val1&param2=val2" );

  //----------------------------------------------------------------------------
  // Full url
  //----------------------------------------------------------------------------
  CPPUNIT_ASSERT( url1.IsValid() == true );
  CPPUNIT_ASSERT( url1.GetProtocol() == "root" );
  CPPUNIT_ASSERT( url1.GetUserName() == "user1" );
  CPPUNIT_ASSERT( url1.GetPassword() == "passwd1" );
  CPPUNIT_ASSERT( url1.GetHostName() == "host1" );
  CPPUNIT_ASSERT( url1.GetPort() == 123 );
  CPPUNIT_ASSERT( url1.GetPathWithParams() == "/path?param1=val1&param2=val2" );
  CPPUNIT_ASSERT( url1.GetPath() == "/path" );
  CPPUNIT_ASSERT( url1.GetParams().size() == 2 );

  XrdClient::URL::ParamsMap::const_iterator it;
  it = url1.GetParams().find( "param1" );
  CPPUNIT_ASSERT( it != url1.GetParams().end() );
  CPPUNIT_ASSERT( it->second == "val1" );
  it = url1.GetParams().find( "param2" );
  CPPUNIT_ASSERT( it != url1.GetParams().end() );
  CPPUNIT_ASSERT( it->second == "val2" );
  it = url1.GetParams().find( "param3" );
  CPPUNIT_ASSERT( it == url1.GetParams().end() );

  //----------------------------------------------------------------------------
  // No password, no port
  //----------------------------------------------------------------------------
  CPPUNIT_ASSERT( url2.IsValid() == true );
  CPPUNIT_ASSERT( url2.GetProtocol() == "root" );
  CPPUNIT_ASSERT( url2.GetUserName() == "user1" );
  CPPUNIT_ASSERT( url2.GetPassword() == "" );
  CPPUNIT_ASSERT( url2.GetHostName() == "host1" );
  CPPUNIT_ASSERT( url2.GetPort() == -1 );
  CPPUNIT_ASSERT( url2.GetPath() == "/path" );
  CPPUNIT_ASSERT( url2.GetPathWithParams() == "/path?param1=val1&param2=val2" );
  CPPUNIT_ASSERT( url1.GetParams().size() == 2 );

  it = url2.GetParams().find( "param1" );
  CPPUNIT_ASSERT( it != url2.GetParams().end() );
  CPPUNIT_ASSERT( it->second == "val1" );
  it = url2.GetParams().find( "param2" );
  CPPUNIT_ASSERT( it != url2.GetParams().end() );
  CPPUNIT_ASSERT( it->second == "val2" );
  it = url2.GetParams().find( "param3" );
  CPPUNIT_ASSERT( it == url2.GetParams().end() );

  //----------------------------------------------------------------------------
  // Just the host and the protocol
  //----------------------------------------------------------------------------
  CPPUNIT_ASSERT( url3.IsValid() == true );
  CPPUNIT_ASSERT( url3.GetProtocol() == "root" );
  CPPUNIT_ASSERT( url3.GetUserName() == "" );
  CPPUNIT_ASSERT( url3.GetPassword() == "" );
  CPPUNIT_ASSERT( url3.GetHostName() == "host1" );
  CPPUNIT_ASSERT( url3.GetPort() == -1 );
  CPPUNIT_ASSERT( url3.GetPath() == "" );
  CPPUNIT_ASSERT( url3.GetPathWithParams() == "" );
  CPPUNIT_ASSERT( url3.GetParams().size() == 0 );

  //----------------------------------------------------------------------------
  // Bunch od invalid ones
  //----------------------------------------------------------------------------
  CPPUNIT_ASSERT( urlInvalid1.IsValid() == false );
  CPPUNIT_ASSERT( urlInvalid2.IsValid() == false );
  CPPUNIT_ASSERT( urlInvalid3.IsValid() == false );
  CPPUNIT_ASSERT( urlInvalid4.IsValid() == false );
  CPPUNIT_ASSERT( urlInvalid5.IsValid() == false );
  CPPUNIT_ASSERT( urlInvalid6.IsValid() == false );
  CPPUNIT_ASSERT( urlInvalid7.IsValid() == false );
  CPPUNIT_ASSERT( urlInvalid8.IsValid() == false );
}