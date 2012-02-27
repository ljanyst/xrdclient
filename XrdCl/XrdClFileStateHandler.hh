//------------------------------------------------------------------------------
// Copyright (c) 2012 by European Organization for Nuclear Research (CERN)
// Author: Lukasz Janyst <ljanyst@cern.ch>
// See the LICENCE file for details.
//------------------------------------------------------------------------------

#ifndef __XRD_CL_FILE_STATE_HANDLER_HH__
#define __XRD_CL_FILE_STATE_HANDLER_HH__

#include "XrdCl/XrdClXRootDResponses.hh"
#include "XrdSys/XrdSysPthread.hh"

namespace XrdClient
{
  class Message;

  //----------------------------------------------------------------------------
  //! Handle the statefull operations
  //----------------------------------------------------------------------------
  class FileStateHandler
  {
    public:
      //------------------------------------------------------------------------
      //! State of the file
      //------------------------------------------------------------------------
      enum FileStatus
      {
        Closed,           //! The file is closed
        Opened,           //! Opening has succeeded
        Error,            //! Opening has failed
        OpenInProgress,   //! Opening is in progress
        CloseInProgress   //! Closing operation is in progress
      };

      //------------------------------------------------------------------------
      //! Constructor
      //------------------------------------------------------------------------
      FileStateHandler();

      //------------------------------------------------------------------------
      //! Destructor
      //------------------------------------------------------------------------
      ~FileStateHandler();

      //------------------------------------------------------------------------
      //! Open the file pointed to by the given URL
      //!
      //! @param url     url of the file to be opened
      //! @param flags   OpenFlags::Flags
      //! @param mode    Access::Mode for new files, 0 otherwise
      //! @param handler handler to be notified about the status of the operation
      //! @param timeout timeout value, if 0 the environment default will be
      //!                used
      //! @return        status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Open( const std::string &url,
                         uint16_t           flags,
                         uint16_t           mode,
                         ResponseHandler   *handler,
                         uint16_t           timeout  = 0 );

      //------------------------------------------------------------------------
      //! Close the file object
      //!
      //! @param handler handler to be notified about the status of the operation
      //! @param timeout timeout value, if 0 the environment default will be
      //!                used
      //! @return        status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Close( ResponseHandler *handler,
                          uint16_t         timeout = 0 );

      //------------------------------------------------------------------------
      //! Obtain status information for this file - async
      //!
      //! @param force   do not use the cached information, force re-stating
      //! @param handler handler to be notified when the response arrives,
      //!                the response parameter will hold a StatInfo object
      //!                if the procedure is successfull
      //! @param timeout timeout value, if 0 the environment default will
      //!                be used
      //! @return        status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Stat( bool             force,
                         ResponseHandler *handler,
                         uint16_t         timeout = 0 );


      //------------------------------------------------------------------------
      //! Read a data chunk at a given offset - sync
      //!
      //! @param offset  offset from the beginning of the file
      //! @param size    number of bytes to be read
      //! @param buffer  a pointer to a buffer big enough to hold the data
      //!                or 0 if the buffer should be allocated by the system
      //! @param handler handler to be notified when the response arrives,
      //!                the response parameter will hold a buffer object if
      //!                the procedure was successful, if a prealocated
      //!                buffer was specified then the buffer object will
      //!                "wrap" this buffer
      //! @param timeout timeout value, if 0 the environment default will be
      //!                used
      //! @return        status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Read( uint64_t         offset,
                         uint32_t         size,
                         void            *buffer,
                         ResponseHandler *handler,
                         uint16_t         timeout = 0 );

      //------------------------------------------------------------------------
      //! Process the results of the opening operation
      //------------------------------------------------------------------------
      void SetOpenStatus( const XRootDStatus             *status,
                          const OpenInfo                 *openInfo,
                          const ResponseHandler::URLList *hostList );

      //------------------------------------------------------------------------
      //! Process the results of the closing operation
      //------------------------------------------------------------------------
      void SetCloseStatus( const XRootDStatus *status );

      //------------------------------------------------------------------------
      //! Handle an error while sending a stateful message
      //------------------------------------------------------------------------
      void HandleStateError( XRootDStatus    *status,
                             Message         *message,
                             ResponseHandler *userHandler );

      //------------------------------------------------------------------------
      //! Handle stateful redirect
      //------------------------------------------------------------------------
      void HandleRedirection( URL             *targetUrl,
                              Message         *message,
                              ResponseHandler *userHandler );

      //------------------------------------------------------------------------
      //! Handle stateful response
      //------------------------------------------------------------------------
      void HandleResponse( XRootDStatus             *status,
                           Message                  *message,
                           AnyObject                *response,
                           ResponseHandler::URLList *urlList );

    private:
      XrdSysMutex   pMutex;
      FileStatus    pFileState;
      XRootDStatus  pStatus;
      StatInfo     *pStatInfo;
      URL          *pFileUrl;
      URL          *pDataServer;
      URL          *pLoadBalancer;
      uint8_t      *pFileHandle;
  };
}

#endif // __XRD_CL_FILE_STATE_HANDLER_HH__