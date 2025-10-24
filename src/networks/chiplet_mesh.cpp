#include "booksim.hpp"
#include "chiplet_mesh.hpp"
#include <sstream>

ChipletMesh::ChipletMesh ( const Configuration &config, const string & name):ChipletNetwork( config, name ) {
  _ComputeSize( config );
  _Alloc();
  _BuildNet( config );
}

void ChipletMesh::_ComputeSize( const Configuration &config )
{
  _size = chip_num * chip_size;
  _channels = 4 * _size;
  _nodes = _size;
}

void ChipletMesh::_BuildNet( const Configuration &config )
{
  for(int i = 0; i < chip_num; ++i) single_chip_conn(config, i);
  
  bundry_router_0 = get_node_id(0, y_len - 2, x_len - 1);
  bundry_router_1 = get_node_id(1, y_len - 2, 0);
  bundry_router_2 = get_node_id(1, y_len - 1, 1);
  bundry_router_3 = get_node_id(2, 0, 1);
  bundry_router_4 = get_node_id(2, 1, 0);
  bundry_router_5 = get_node_id(3, 1, x_len - 1);
  bundry_router_6 = get_node_id(3, 0, x_len - 2);
  bundry_router_7 = get_node_id(0, y_len - 1, x_len - 2);

  node_conn_d2d(bundry_router_0, bundry_router_1, right_port, left_port, 32);
  node_conn_d2d(bundry_router_2, bundry_router_3, down_port, up_port, 32);
  node_conn_d2d(bundry_router_4, bundry_router_5, left_port, right_port, 32);
  node_conn_d2d(bundry_router_6, bundry_router_7, up_port, down_port, 32);

  const int dummy_0 = get_node_id(0, y_len - 2, 0);
  const int dummy_1 = get_node_id(1, y_len - 2, x_len - 1);
  const int dummy_2 = get_node_id(1, 0, 1);
  const int dummy_3 = get_node_id(2, y_len - 1, 1);
  const int dummy_4 = get_node_id(2, 1, x_len - 1);
  const int dummy_5 = get_node_id(3, 1, 0);
  const int dummy_6 = get_node_id(3, y_len - 1, x_len - 2);
  const int dummy_7 = get_node_id(0, 0, x_len - 2);

  node_conn_d2d(dummy_0, dummy_1, right_port, left_port, 32);
  node_conn_d2d(dummy_2, dummy_3, down_port, up_port, 32);
  node_conn_d2d(dummy_4, dummy_5, left_port, right_port, 32);
  node_conn_d2d(dummy_6, dummy_7, up_port, down_port, 32);
}

const int ChipletMesh::get_boundary_router(const int inject_node_id, const int dest_node_id) {
  const int inj_chip_id = get_chip(inject_node_id);
  const int dst_chip_id = get_chip(dest_node_id);
  assert(inj_chip_id >= 0 && inj_chip_id < chip_num);
  assert(dst_chip_id >= 0 && dst_chip_id < chip_num);
  assert(inj_chip_id != dst_chip_id);
  if(inj_chip_id == 0) {
    if(dst_chip_id == 1) {
      return bundry_router_0;
    } else if(dst_chip_id == 2) {
      return bundry_router_0;
    } else if(dst_chip_id == 3) {
      return bundry_router_7;
    } else {
      assert(false);
      return -1;
    }
  } else if(inj_chip_id == 1) {
    if(dst_chip_id == 0) {
      return bundry_router_1;
    } else if(dst_chip_id == 2) {
      return bundry_router_2;
    } else if(dst_chip_id == 3) {
      return bundry_router_1;
    } else {
      assert(false);
      return -1;
    }
  } else if(inj_chip_id == 2) {
    if(dst_chip_id == 0) {
      return bundry_router_4;
    } else if(dst_chip_id == 1) {
      return bundry_router_3;
    } else if(dst_chip_id == 3) {
      return bundry_router_4;
    } else {
      assert(false);
      return -1;
    }
  } else if(inj_chip_id == 3) {
    if(dst_chip_id == 0) {
      return bundry_router_6;
    } else if(dst_chip_id == 1) {
      return bundry_router_5;
    } else if(dst_chip_id == 2) {
      return bundry_router_5;
    } else {
      assert(false);
      return -1;
    }
  }
  assert(false);
  return -1;
}

const bool ChipletMesh::is_boundary_router(const int node_id) {
  if(node_id == bundry_router_0 || node_id == bundry_router_1 || node_id == bundry_router_2 || node_id == bundry_router_3 || node_id == bundry_router_4 || node_id == bundry_router_5 || node_id == bundry_router_6 || node_id == bundry_router_7) {
    return true;
  } else {
    return false;
  }
}