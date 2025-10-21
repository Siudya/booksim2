#ifndef _CHIPLET_P2P_HPP_
#define _CHIPLET_P2P_HPP_

#include "network.hpp"
#include "chiplet_network.hpp"

class ChipletP2P : public ChipletNetwork {
  public:
  const int chip_num = 4;
  ChipletP2P ( const Configuration &config, const string & name);
  void _BuildNet( const Configuration &config ) override;
  void _ComputeSize( const Configuration &config ) override;
};

#endif // _CHIPLET_P2P_HPP_