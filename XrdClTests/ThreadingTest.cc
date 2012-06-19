//------------------------------------------------------------------------------
// Copyright (c) 2012 by European Organization for Nuclear Research (CERN)
// Author: Lukasz Janyst <ljanyst@cern.ch>
// See the LICENCE file for details.
//------------------------------------------------------------------------------

#include <cppunit/extensions/HelperMacros.h>
#include "TestEnv.hh"
#include "Utils.hh"
#include "CppUnitXrdHelpers.hh"
#include "XrdCl/XrdClFile.hh"
#include <pthread.h>

//------------------------------------------------------------------------------
// Declaration
//------------------------------------------------------------------------------
class ThreadingTest: public CppUnit::TestCase
{
  public:
    CPPUNIT_TEST_SUITE( ThreadingTest );
      CPPUNIT_TEST( ReadTest );
    CPPUNIT_TEST_SUITE_END();
    void ReadTest();
};

CPPUNIT_TEST_SUITE_REGISTRATION( ThreadingTest );

//------------------------------------------------------------------------------
// Thread helper struct
//------------------------------------------------------------------------------
struct ThreadData
{
  ThreadData():
    file( 0 ), startOffset( 0 ), length( 0 ), checkSum( 0 ) {}
  XrdCl::File *file;
  uint64_t         startOffset;
  uint64_t         length;
  uint32_t         checkSum;
};

//------------------------------------------------------------------------------
// Reader thread
//------------------------------------------------------------------------------
void *DataReader( void *arg )
{
  ThreadData *td = (ThreadData*)arg;

  uint64_t  offset    = td->startOffset;
  uint64_t  dataLeft  = td->length;
  uint64_t  chunkSize = 0;
  uint32_t  bytesRead = 0;
  uint32_t  MB        = 1024*1024;
  char     *buffer    = new char[4*MB];

  while( 1 )
  {
    chunkSize = 4*MB;
    if( chunkSize > dataLeft )
      chunkSize = dataLeft;

    if( chunkSize == 0 )
      break;

    CPPUNIT_ASSERT_XRDST( td->file->Read( offset, chunkSize, buffer,
                                          bytesRead ) );

    offset   += bytesRead;
    dataLeft -= bytesRead;
    td->checkSum = Utils::UpdateCRC32( td->checkSum, buffer, bytesRead );
  }

  delete [] buffer;

  return 0;
}

//------------------------------------------------------------------------------
// Read test
//------------------------------------------------------------------------------
void ThreadingTest::ReadTest()
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

  std::string fileUrl[5];
  std::string path[5];
  path[0] = dataPath + "/1db882c8-8cd6-4df1-941f-ce669bad3458.dat";
  path[1] = dataPath + "/3c9a9dd8-bc75-422c-b12c-f00604486cc1.dat";
  path[2] = dataPath + "/7235b5d1-cede-4700-a8f9-596506b4cc38.dat";
  path[3] = dataPath + "/7e480547-fe1a-4eaf-a210-0f3927751a43.dat";
  path[4] = dataPath + "/89120cec-5244-444c-9313-703e4bee72de.dat";

  for( int i = 0; i < 5; ++i )
    fileUrl[i] = address + "/" + path[i];

  //----------------------------------------------------------------------------
  // Open and stat the files
  //----------------------------------------------------------------------------
  ThreadData threadData[20];

  for( int i = 0; i < 5; ++i )
  {
    File     *f = new File();
    StatInfo *si = 0;
    CPPUNIT_ASSERT_XRDST( f->Open( fileUrl[i], OpenFlags::Read ) );
    CPPUNIT_ASSERT_XRDST( f->Stat( false, si ) );
    CPPUNIT_ASSERT( si );
    CPPUNIT_ASSERT( si->TestFlags( StatInfo::IsReadable ) );

    uint64_t step = si->GetSize()/4;

    for( int j = 0; j < 4; ++j )
    {
      threadData[j*5+i].file        = f;
      threadData[j*5+i].startOffset = j*step;
      threadData[j*5+i].length      = step;
      threadData[j*5+i].checkSum    = Utils::GetInitialCRC32();
    }

    threadData[15+i].length = si->GetSize() - threadData[15+i].startOffset;
    delete si;
  }

  //----------------------------------------------------------------------------
  // Spawn the threads and wait for them to finish
  //----------------------------------------------------------------------------
  pthread_t thread[20];
  for( int i = 0; i < 20; ++i )
    CPPUNIT_ASSERT_PTHREAD( pthread_create( &(thread[i]), 0,
                            ::DataReader, &(threadData[i]) ) );

  for( int i = 0; i < 20; ++i )
    CPPUNIT_ASSERT_PTHREAD( pthread_join( thread[i], 0 ) );

  //----------------------------------------------------------------------------
  // Glue up and compare the checksums
  //----------------------------------------------------------------------------
  uint32_t checkSums[5];
  for( int i = 0; i < 5; ++i )
  {
    checkSums[i] = threadData[i].checkSum;
    for( int j = 1; j < 4; ++j )
    {
      checkSums[i] = Utils::CombineCRC32( checkSums[i],
                                          threadData[j*5+i].checkSum,
                                          threadData[j*5+i].length );
    }

    Buffer  arg; arg.FromString( path[i] );
    Buffer *cksResponse = 0;
    FileSystem fs( url );
    CPPUNIT_ASSERT_XRDST( fs.Query( QueryCode::Checksum, arg, cksResponse ) );
    CPPUNIT_ASSERT( cksResponse );
    uint32_t remoteCRC32 = 0;
    CPPUNIT_ASSERT( Utils::CRC32TextToInt( remoteCRC32,
                                           cksResponse->ToString() ) );
    CPPUNIT_ASSERT( remoteCRC32 == checkSums[i] );
  }

  //----------------------------------------------------------------------------
  // Close the files
  //----------------------------------------------------------------------------
  for( int i = 0; i < 5; ++i )
  {
    CPPUNIT_ASSERT_XRDST( threadData[i].file->Close() );
    delete threadData[i].file;
  }
}