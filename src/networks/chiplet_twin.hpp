#ifndef _CHIPLET_TWIN_HPP_
#define _CHIPLET_TWIN_HPP_

#include "network.hpp"
#include "chiplet_network.hpp"

class ChipletTwin : public ChipletNetwork {
  private:
  int bundry_router_0;
  int bundry_router_1;
  int bundry_router_2;
  int bundry_router_3;
  
  public:
  const int chip_num = 2;
  ChipletTwin ( const Configuration &config, const string & name);
  void _BuildNet( const Configuration &config ) override;
  void _ComputeSize( const Configuration &config ) override;
  const int get_boundary_router(const int inject_node_id, const int dest_node_id) override;
  const bool is_boundary_router(const int node_id) override;
};

#endif // _CHIPLET_TWIN_HPP_