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
  
  const int bundry_router_0 = get_node_id(0, y_len - 2, x_len - 1);
  const int bundry_router_1 = get_node_id(1, y_len - 2, 0);
  const int bundry_router_2 = get_node_id(1, y_len - 1, 1);
  const int bundry_router_3 = get_node_id(2, 0, 1);
  const int bundry_router_4 = get_node_id(2, 1, 0);
  const int bundry_router_5 = get_node_id(3, 1, x_len - 1);
  const int bundry_router_6 = get_node_id(3, 0, x_len - 2);
  const int bundry_router_7 = get_node_id(0, y_len - 1, x_len - 2);

  node_conn_2(bundry_router_0, bundry_router_1, right_port, left_port, 32);
  node_conn_2(bundry_router_2, bundry_router_3, down_port, up_port, 32);
  node_conn_2(bundry_router_4, bundry_router_5, left_port, right_port, 32);
  node_conn_2(bundry_router_6, bundry_router_7, up_port, down_port, 32);
}