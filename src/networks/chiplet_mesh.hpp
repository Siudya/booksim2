#ifndef _CHIPLET_MESH_HPP_
#define _CHIPLET_MESH_HPP_

#include "network.hpp"
#include "chiplet_network.hpp"

class ChipletMesh : public ChipletNetwork {
  private:
  int bundry_router_0;
  int bundry_router_1;
  int bundry_router_2;
  int bundry_router_3;
  int bundry_router_4;
  int bundry_router_5;
  int bundry_router_6;
  int bundry_router_7;
  
  public:
  const int chip_num = 4;
  ChipletMesh ( const Configuration &config, const string & name);
  void _BuildNet( const Configuration &config ) override;
  void _ComputeSize( const Configuration &config ) override;
  const int get_boundary_router(const int inject_node_id, const int dest_node_id) override;
  const bool is_boundary_router(const int node_id) override;
};

#endif // _CHIPLET_MESH_HPP_