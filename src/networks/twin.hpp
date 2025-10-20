#ifndef _TWIN_HPP_
#define _TWIN_HPP_

#include "network.hpp"
#include "chiplet_network.hpp"

class TwinTopo : public ChipletNetwork {
  public:
  TwinTopo ( const Configuration &config, const string & name);
};

#endif // _TWIN_HPP_