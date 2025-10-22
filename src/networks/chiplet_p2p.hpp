#ifndef _CHIPLET_P2P_HPP_
#define _CHIPLET_P2P_HPP_

#include "network.hpp"
#include "chiplet_network.hpp"

class ChipletP2P : public ChipletNetwork {
  private:
  int bundry_router_0;
  int bundry_router_1;
  int bundry_router_2;
  int bundry_router_3;
  int bundry_router_4;
  int bundry_router_5;
  int bundry_router_6;
  int bundry_router_7;
  int bundry_router_8;
  int bundry_router_9;
  int bundry_router_10;
  int bundry_router_11;
  
  public:
  const int chip_num = 4;
  ChipletP2P ( const Configuration &config, const string & name);
  void _BuildNet( const Configuration &config ) override;
  void _ComputeSize( const Configuration &config ) override;
  const int get_boundary_router(const int inject_node_id, const int dest_node_id) override;
  const bool is_boundary_router(const int node_id) override;
};

#endif // _CHIPLET_P2P_HPP_