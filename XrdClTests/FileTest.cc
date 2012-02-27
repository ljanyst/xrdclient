//------------------------------------------------------------------------------
// Copyright (c) 2012 by European Organization for Nuclear Research (CERN)
// Author: Lukasz Janyst <ljanyst@cern.ch>
// See the LICENCE file for details.
//------------------------------------------------------------------------------

#include <cppunit/extensions/HelperMacros.h>
#include "TestEnv.hh"
#include "Utils.hh"
#include "XrdCl/XrdClFile.hh"
#include "XrdCl/XrdClDefaultEnv.hh"
#include "XrdCl/XrdClMessage.hh"
#include "XrdCl/XrdClSIDManager.hh"
#include "XrdCl/XrdClPostMaster.hh"
#include "XrdCl/XrdClXRootDTransport.hh"
#include "XrdCl/XrdClMessageUtils.hh"
#include "XrdCl/XrdClXRootDMsgHandler.hh"

//------------------------------------------------------------------------------
// Declaration
//------------------------------------------------------------------------------
class FileTest: public CppUnit::TestCase
{
  public:
    CPPUNIT_TEST_SUITE( FileTest );
      CPPUNIT_TEST( RedirectReturnTest );
      CPPUNIT_TEST( ReadTest );
      CPPUNIT_TEST( WriteTest );
    CPPUNIT_TEST_SUITE_END();
    void RedirectReturnTest();
    void ReadTest();
    void WriteTest();
};

CPPUNIT_TEST_SUITE_REGISTRATION( FileTest );

//------------------------------------------------------------------------------
// Redirect return test
//------------------------------------------------------------------------------
void FileTest::RedirectReturnTest()
{
  using namespace XrdClient;

  //----------------------------------------------------------------------------
  // Initialize
  //----------------------------------------------------------------------------
  Env *testEnv = TestEnv::GetEnv();

  std::string address;
  std::string dataPath;

  CPPUNIT_ASSERT( testEnv->GetString( "MainServerURL", address ) );
  CPPUNIT_ASSERT( testEnv->GetString( "DataPath", dataPath ) );

  URL url( address );
  CPPUNIT_ASSERT( url.IsValid() );

  std::string path = dataPath + "/cb4aacf1-6f28-42f2-b68a-90a73460f424.dat";
  std::string fileUrl = address + "/" + path;

  //----------------------------------------------------------------------------
  // Get the SID manager
  //----------------------------------------------------------------------------
  PostMaster *postMaster = DefaultEnv::GetPostMaster();
  AnyObject   sidMgrObj;
  SIDManager *sidMgr    = 0;
  Status      st;
  st = postMaster->QueryTransport( url, XRootDQuery::SIDManager, sidMgrObj );

  CPPUNIT_ASSERT( st.IsOK() );
  sidMgrObj.Get( sidMgr );

  //----------------------------------------------------------------------------
  // Build the open request
  //----------------------------------------------------------------------------
  Message           *msg;
  ClientOpenRequest *req;
  MessageUtils::CreateRequest( msg, req, path.length() );
  req->requestid = kXR_open;
  req->options   = kXR_open_read | kXR_retstat;
  req->dlen      = path.length();
  msg->Append( path.c_str(), path.length(), 24 );

  XRootDTransport::MarshallRequest( msg );

  SyncResponseHandler *handler = new SyncResponseHandler();

  XRootDMsgHandler *msgHandler;
  msgHandler = new XRootDMsgHandler( msg, handler, &url, sidMgr );
  msgHandler->SetTimeout( 300 );
  msgHandler->SetRedirectAsAnswer( true );

  st = postMaster->Send( url, msg, msgHandler, 300 );
  URL *response = 0;
  CPPUNIT_ASSERT( st.IsOK() );
  XRootDStatus st1 = MessageUtils::WaitForResponse( handler, response );
  CPPUNIT_ASSERT( st1.IsOK() );
  CPPUNIT_ASSERT( st1.code == suXRDRedirect );
  CPPUNIT_ASSERT( response );
  delete response;
}

//------------------------------------------------------------------------------
// Read test
//------------------------------------------------------------------------------
void FileTest::ReadTest()
{
  using namespace XrdClient;

  //----------------------------------------------------------------------------
  // Initialize
  //----------------------------------------------------------------------------
  Env *testEnv = TestEnv::GetEnv();

  std::string address;
  std::string dataPath;

  CPPUNIT_ASSERT( testEnv->GetString( "MainServerURL", address ) );
  CPPUNIT_ASSERT( testEnv->GetString( "DataPath", dataPath ) );

  URL url( address );
  CPPUNIT_ASSERT( url.IsValid() );

  std::string filePath = dataPath + "/cb4aacf1-6f28-42f2-b68a-90a73460f424.dat";
  std::string fileUrl = address + "/";
  fileUrl += filePath;

  //----------------------------------------------------------------------------
  // Fetch some data and checksum
  //----------------------------------------------------------------------------
  const uint32_t MB = 1024*1024;
  char *buffer1 = new char[4*MB];
  char *buffer2 = new char[4*MB];
  File f;
  StatInfo *stat;

  //----------------------------------------------------------------------------
  // Open the file
  //----------------------------------------------------------------------------
  CPPUNIT_ASSERT( f.Open( fileUrl, OpenFlags::Read ).IsOK() );

  //----------------------------------------------------------------------------
  // Stat1
  //----------------------------------------------------------------------------
  CPPUNIT_ASSERT( f.Stat( false, stat ).IsOK() );
  CPPUNIT_ASSERT( stat );
  CPPUNIT_ASSERT( stat->GetSize() == 1048576000 );
  CPPUNIT_ASSERT( stat->TestFlags( StatInfo::IsReadable ) );
  delete stat;

  //----------------------------------------------------------------------------
  // Stat2
  //----------------------------------------------------------------------------
//  CPPUNIT_ASSERT( f.Stat( true, stat ).IsOK() );
//  CPPUNIT_ASSERT( stat );
//  CPPUNIT_ASSERT( stat->GetSize() == 1048576000 );
//  CPPUNIT_ASSERT( stat->TestFlags( StatInfo::IsReadable ) );
//  delete stat;

  //----------------------------------------------------------------------------
  // Read test
  //----------------------------------------------------------------------------
  CPPUNIT_ASSERT( f.Read( 10*MB, 4*MB, buffer1 ).IsOK() );
  CPPUNIT_ASSERT( f.Read( 20*MB, 4*MB, buffer2 ).IsOK() );
  uint32_t crc = Utils::ComputeCRC32( buffer1, 4*MB );
  crc = Utils::UpdateCRC32( crc, buffer2, 4*MB );
  CPPUNIT_ASSERT( crc == 1304813676 );
  delete [] buffer1;
  delete [] buffer2;

  CPPUNIT_ASSERT( f.Close().IsOK() );
}


//------------------------------------------------------------------------------
// Read test
//------------------------------------------------------------------------------
void FileTest::WriteTest()
{
  using namespace XrdClient;

  //----------------------------------------------------------------------------
  // Initialize
  //----------------------------------------------------------------------------
  Env *testEnv = TestEnv::GetEnv();

  std::string address;
  std::string dataPath;

  CPPUNIT_ASSERT( testEnv->GetString( "MainServerURL", address ) );
  CPPUNIT_ASSERT( testEnv->GetString( "DataPath", dataPath ) );

  URL url( address );
  CPPUNIT_ASSERT( url.IsValid() );

  std::string filePath = dataPath + "/testFile.dat";
  std::string fileUrl = address + "/";
  fileUrl += filePath;

  //----------------------------------------------------------------------------
  // Fetch some data and checksum
  //----------------------------------------------------------------------------
  const uint32_t MB = 1024*1024;
  char *buffer1 = new char[4*MB];
  char *buffer2 = new char[4*MB];
  File f1, f2;

  CPPUNIT_ASSERT( Utils::GetRandomBytes( buffer1, 4*MB ) == 4*MB );
  CPPUNIT_ASSERT( Utils::GetRandomBytes( buffer2, 4*MB ) == 4*MB );
  uint32_t crc1 = Utils::ComputeCRC32( buffer1, 4*MB );
  crc1 = Utils::UpdateCRC32( crc1, buffer2, 4*MB );

  //----------------------------------------------------------------------------
  // Write the data
  //----------------------------------------------------------------------------
  CPPUNIT_ASSERT( f1.Open( fileUrl, OpenFlags::Delete | OpenFlags::Update,
                           Access::UR | Access::UW ).IsOK() );
  CPPUNIT_ASSERT( f1.Write( 0, 4*MB, buffer1 ).IsOK() );
  CPPUNIT_ASSERT( f1.Write( 4*MB, 4*MB, buffer2 ).IsOK() );
  CPPUNIT_ASSERT( f1.Sync().IsOK() );
  CPPUNIT_ASSERT( f1.Close().IsOK() );

  //----------------------------------------------------------------------------
  // Read the data and verify the checksums
  //----------------------------------------------------------------------------
  CPPUNIT_ASSERT( f2.Open( fileUrl, OpenFlags::Read ).IsOK() );
  CPPUNIT_ASSERT( f2.Read( 0, 4*MB, buffer1 ).IsOK() );
  CPPUNIT_ASSERT( f2.Read( 4*MB, 4*MB, buffer2 ).IsOK() );
  uint32_t crc2 = Utils::ComputeCRC32( buffer1, 4*MB );
  crc2 = Utils::UpdateCRC32( crc2, buffer2, 4*MB );
  CPPUNIT_ASSERT( f2.Close().IsOK() );

  CPPUNIT_ASSERT( crc1 == crc2 );

  //----------------------------------------------------------------------------
  // Truncate test
  //----------------------------------------------------------------------------
  CPPUNIT_ASSERT( f1.Open( fileUrl, OpenFlags::Delete | OpenFlags::Update,
                           Access::UR | Access::UW ).IsOK() );
  CPPUNIT_ASSERT( f1.Truncate( 20*MB ).IsOK() );
  CPPUNIT_ASSERT( f1.Close().IsOK() );
  Query query( url );
  StatInfo *response = 0;
  CPPUNIT_ASSERT( query.Stat( filePath, response ).IsOK() );
  CPPUNIT_ASSERT( response );
  CPPUNIT_ASSERT( response->GetSize() == 20*MB );
  CPPUNIT_ASSERT( query.Rm( filePath ).IsOK() );
}
