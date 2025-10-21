#ifndef _CHIPLET_MESH_HPP_
#define _CHIPLET_MESH_HPP_

#include "network.hpp"
#include "chiplet_network.hpp"

class ChipletMesh : public ChipletNetwork {
  public:
  const int chip_num = 4;
  ChipletMesh ( const Configuration &config, const string & name);
  void _BuildNet( const Configuration &config ) override;
  void _ComputeSize( const Configuration &config ) override;
};

#endif // _CHIPLET_MESH_HPP_