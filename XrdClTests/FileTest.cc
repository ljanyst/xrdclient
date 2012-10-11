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

#include <cppunit/extensions/HelperMacros.h>
#include "TestEnv.hh"
#include "Utils.hh"
#include "CppUnitXrdHelpers.hh"
#include "XrdCl/XrdClFile.hh"
#include "XrdCl/XrdClDefaultEnv.hh"
#include "XrdCl/XrdClMessage.hh"
#include "XrdCl/XrdClSIDManager.hh"
#include "XrdCl/XrdClPostMaster.hh"
#include "XrdCl/XrdClXRootDTransport.hh"
#include "XrdCl/XrdClMessageUtils.hh"
#include "XrdCl/XrdClXRootDMsgHandler.hh"

using namespace XrdClTests;

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
      CPPUNIT_TEST( VectorReadTest );
    CPPUNIT_TEST_SUITE_END();
    void RedirectReturnTest();
    void ReadTest();
    void WriteTest();
    void VectorReadTest();
};

CPPUNIT_TEST_SUITE_REGISTRATION( FileTest );

//------------------------------------------------------------------------------
// Redirect return test
//------------------------------------------------------------------------------
void FileTest::RedirectReturnTest()
{
  using namespace XrdCl;

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
  XRootDTransport::SetDescription( msg );

  SyncResponseHandler *handler = new SyncResponseHandler();
  MessageSendParams params; params.followRedirects = false;
  MessageUtils::ProcessSendParams( params );
  st = MessageUtils::SendMessage( url, msg, handler, params );
  RedirectInfo *response = 0;
  CPPUNIT_ASSERT( st.IsOK() );
  XRootDStatus st1 = MessageUtils::WaitForResponse( handler, response );
  delete handler;
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
  using namespace XrdCl;

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
  uint32_t bytesRead1 = 0;
  uint32_t bytesRead2 = 0;
  File f;
  StatInfo *stat;

  //----------------------------------------------------------------------------
  // Open the file
  //----------------------------------------------------------------------------
  CPPUNIT_ASSERT_XRDST( f.Open( fileUrl, OpenFlags::Read ) );

  //----------------------------------------------------------------------------
  // Stat1
  //----------------------------------------------------------------------------
  CPPUNIT_ASSERT_XRDST( f.Stat( false, stat ) );
  CPPUNIT_ASSERT( stat );
  CPPUNIT_ASSERT( stat->GetSize() == 1048576000 );
  CPPUNIT_ASSERT( stat->TestFlags( StatInfo::IsReadable ) );
  delete stat;
  stat = 0;

  //----------------------------------------------------------------------------
  // Stat2
  //----------------------------------------------------------------------------
  CPPUNIT_ASSERT_XRDST( f.Stat( true, stat ) );
  CPPUNIT_ASSERT( stat );
  CPPUNIT_ASSERT( stat->GetSize() == 1048576000 );
  CPPUNIT_ASSERT( stat->TestFlags( StatInfo::IsReadable ) );
  delete stat;

  //----------------------------------------------------------------------------
  // Read test
  //----------------------------------------------------------------------------
  CPPUNIT_ASSERT_XRDST( f.Read( 10*MB, 4*MB, buffer1, bytesRead1 ) );
  CPPUNIT_ASSERT_XRDST( f.Read( 20*MB, 4*MB, buffer2, bytesRead2 ) );
  CPPUNIT_ASSERT( bytesRead1 == 4*MB );
  CPPUNIT_ASSERT( bytesRead2 == 4*MB );
  uint32_t crc = Utils::ComputeCRC32( buffer1, 4*MB );
  crc = Utils::UpdateCRC32( crc, buffer2, 4*MB );
  CPPUNIT_ASSERT( crc == 1304813676 );
  delete [] buffer1;
  delete [] buffer2;

  CPPUNIT_ASSERT_XRDST( f.Close() );
}


//------------------------------------------------------------------------------
// Read test
//------------------------------------------------------------------------------
void FileTest::WriteTest()
{
  using namespace XrdCl;

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
  char *buffer3 = new char[4*MB];
  char *buffer4 = new char[4*MB];
  uint32_t bytesRead1 = 0;
  uint32_t bytesRead2 = 0;
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
  StatInfo *stat = 0;
  CPPUNIT_ASSERT( f2.Open( fileUrl, OpenFlags::Read ).IsOK() );
  CPPUNIT_ASSERT( f2.Stat( false, stat ).IsOK() );
  CPPUNIT_ASSERT( stat );
  CPPUNIT_ASSERT( stat->GetSize() == 8*MB );
  CPPUNIT_ASSERT( f2.Read( 0, 4*MB, buffer3, bytesRead1 ).IsOK() );
  CPPUNIT_ASSERT( f2.Read( 4*MB, 4*MB, buffer4, bytesRead2 ).IsOK() );
  CPPUNIT_ASSERT( bytesRead1 == 4*MB );
  CPPUNIT_ASSERT( bytesRead2 == 4*MB );
  uint32_t crc2 = Utils::ComputeCRC32( buffer3, 4*MB );
  crc2 = Utils::UpdateCRC32( crc2, buffer4, 4*MB );
  CPPUNIT_ASSERT( f2.Close().IsOK() );
  CPPUNIT_ASSERT( crc1 == crc2 );

  //----------------------------------------------------------------------------
  // Truncate test
  //----------------------------------------------------------------------------
  CPPUNIT_ASSERT( f1.Open( fileUrl, OpenFlags::Delete | OpenFlags::Update,
                           Access::UR | Access::UW ).IsOK() );
  CPPUNIT_ASSERT( f1.Truncate( 20*MB ).IsOK() );
  CPPUNIT_ASSERT( f1.Close().IsOK() );
  FileSystem fs( url );
  StatInfo *response = 0;
  CPPUNIT_ASSERT( fs.Stat( filePath, response ).IsOK() );
  CPPUNIT_ASSERT( response );
  CPPUNIT_ASSERT( response->GetSize() == 20*MB );
  CPPUNIT_ASSERT( fs.Rm( filePath ).IsOK() );
  delete [] buffer1;
  delete [] buffer2;
  delete [] buffer3;
  delete [] buffer4;
}


//------------------------------------------------------------------------------
// Vector read test
//------------------------------------------------------------------------------
void FileTest::VectorReadTest()
{
  using namespace XrdCl;

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

  std::string filePath = dataPath + "/a048e67f-4397-4bb8-85eb-8d7e40d90763.dat";
  std::string fileUrl = address + "/";
  fileUrl += filePath;

  //----------------------------------------------------------------------------
  // Fetch some data and checksum
  //----------------------------------------------------------------------------
  const uint32_t MB = 1024*1024;
  char *buffer = new char[40*MB];
  File f;

  //----------------------------------------------------------------------------
  // Build the chunk list
  //----------------------------------------------------------------------------
  ChunkList chunkList;
  for( int i = 0; i < 40; ++i )
    chunkList.push_back( ChunkInfo( (i+1)*10*MB, 1*MB ) );

  //----------------------------------------------------------------------------
  // Open the file
  //----------------------------------------------------------------------------
  CPPUNIT_ASSERT( f.Open( fileUrl, OpenFlags::Read ).IsOK() );
  VectorReadInfo *info = 0;
  CPPUNIT_ASSERT( f.VectorRead( chunkList, buffer, info ).IsOK() );
  CPPUNIT_ASSERT( info->GetSize() == 40*MB );
  delete info;
  uint32_t crc = Utils::ComputeCRC32( buffer, 40*MB );
  CPPUNIT_ASSERT( crc == 3695956670 );
  CPPUNIT_ASSERT( f.Close().IsOK() );

  delete [] buffer;
}
