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

  const int chip_0_bndry_0 = get_node_id(0, 0, x_len - 1);
  const int chip_0_bndry_1 = get_node_id(0, 1, x_len - 1);

  const int chip_1_bndry_0 = get_node_id(1, 0, 0);
  const int chip_1_bndry_1 = get_node_id(1, 1, 0);

  node_conn_2(chip_0_bndry_0, chip_1_bndry_0, right_port, left_port, 32);
  node_conn_2(chip_0_bndry_1, chip_1_bndry_1, right_port, left_port, 32);
}