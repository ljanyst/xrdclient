//------------------------------------------------------------------------------
// Copyright (c) 2011 by European Organization for Nuclear Research (CERN)
// Author: Lukasz Janyst <ljanyst@cern.ch>
// See the LICENCE file for details.
//------------------------------------------------------------------------------

#ifndef __XRD_CL_DEFAULT_ENV_HH__
#define __XRD_CL_DEFAULT_ENV_HH__

#include "XrdSys/XrdSysPthread.hh"

#include "XrdCl/XrdClEnv.hh"

namespace XrdClient
{
  //----------------------------------------------------------------------------
  //! Default environment for the client. Responsible for setting/importing
  //! defaults for the variables used in the client.
  //----------------------------------------------------------------------------
  class DefaultEnv: public Env
  {
    public:
      //------------------------------------------------------------------------
      //! Constructor
      //------------------------------------------------------------------------
      DefaultEnv();

      //------------------------------------------------------------------------
      //! Get default client environment
      //------------------------------------------------------------------------
      static Env *GetEnv();

    private:
      static XrdSysMutex  sMutex;
      static Env         *sEnv;
  };
}

#endif // __XRD_CL_DEFAULT_ENV_HH__
