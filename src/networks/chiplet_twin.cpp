#include "booksim.hpp"
#include "chiplet_twin.hpp"
#include <sstream>

ChipletTwin::ChipletTwin ( const Configuration &config, const string & name):ChipletNetwork( config, name ) {
  _ComputeSize( config );
  _Alloc();
  _BuildNet( config );
}

void ChipletTwin::_ComputeSize( const Configuration &config )
{
  _size = chip_num * chip_size;
  _channels = 4 * _size;
  _nodes = _size;
}

void ChipletTwin::_BuildNet( const Configuration &config )
{
  for(int i = 0; i < chip_num; ++i) single_chip_conn(config, i);

  bundry_router_0 = get_node_id(0, 0, x_len - 1);
  bundry_router_1 = get_node_id(0, 1, x_len - 1);

  bundry_router_2 = get_node_id(1, 0, 0);
  bundry_router_3 = get_node_id(1, 1, 0);

  node_conn_d2d(bundry_router_0, bundry_router_2, right_port, left_port, 32);
  node_conn_d2d(bundry_router_1, bundry_router_3, right_port, left_port, 32);

  //Dummy connection
  const int chip_0_dummy_0 = get_node_id(0, 0, 0);
  const int chip_0_dummy_1 = get_node_id(0, 1, 0);
  const int chip_1_dummy_0 = get_node_id(1, 0, x_len - 1);
  const int chip_1_dummy_1 = get_node_id(1, 1, x_len - 1);
  node_conn_d2d(chip_0_dummy_0, chip_1_dummy_0, left_port, right_port, 1);
  node_conn_d2d(chip_0_dummy_1, chip_1_dummy_1, left_port, right_port, 1);
}

const int ChipletTwin::get_boundary_router(const int inject_node_id, const int dest_node_id) {
  const int inj_chip_id = get_chip(inject_node_id);
  assert(inj_chip_id >= 0 && inj_chip_id < chip_num);
  if(inj_chip_id == 0) {
    return get_node_id(0, get_y(inject_node_id) % 2, x_len - 1);
  } else if(inj_chip_id == 1) {
    return get_node_id(1, get_y(inject_node_id) % 2, 0);
  } else {
    assert(false);
    return -1;
  }
}

const bool ChipletTwin::is_boundary_router(const int node_id) {
  if(node_id == bundry_router_0 || node_id == bundry_router_1 || node_id == bundry_router_2 || node_id == bundry_router_3) {
    return true;
  } else {
    return false;
  }
}