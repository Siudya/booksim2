#ifndef _CHIPLET_TWIN_HPP_
#define _CHIPLET_TWIN_HPP_

#include "network.hpp"
#include "chiplet_network.hpp"

class ChipletTwin : public ChipletNetwork {
  public:
  const int chip_num = 2;
  ChipletTwin ( const Configuration &config, const string & name);
  void _BuildNet( const Configuration &config ) override;
  void _ComputeSize( const Configuration &config ) override;
};

#endif // _CHIPLET_TWIN_HPP_