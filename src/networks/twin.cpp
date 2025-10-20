#include "booksim.hpp"
#include "twin.hpp"
#include <sstream>

TwinTopo::TwinTopo ( const Configuration &config, const string & name):ChipletNetwork( config, name ) {
  _size = 2 * chip_size;
  _channels = 4 * _size;
  _nodes = _size;

  _Alloc();

  for(int i = 0; i < 2; ++i){
    single_chip_conn(config, i);
  }

  const int chip_0_bndry_0_y = 0;
  const int chip_0_bndry_0_x = x_len - 1;
  const int chip_0_bndry_1_y = 1;
  const int chip_0_bndry_1_x = x_len - 1;
  const int chip_0_bndry_0 = get_node_id(0, chip_0_bndry_0_y, chip_0_bndry_0_x);
  const int chip_0_bndry_1 = get_node_id(0, chip_0_bndry_1_y, chip_0_bndry_1_x);

  const int chip_1_bndry_0_y = 0;
  const int chip_1_bndry_0_x = 0;
  const int chip_1_bndry_1_y = 1;
  const int chip_1_bndry_1_x = 0;
  const int chip_1_bndry_0 = get_node_id(1, chip_1_bndry_0_y, chip_1_bndry_0_x);
  const int chip_1_bndry_1 = get_node_id(1, chip_1_bndry_1_y, chip_1_bndry_1_x);

  node_conn(chip_0_bndry_0, left_channel(chip_1_bndry_0), right_channel(chip_0_bndry_0), 32, 32);
  node_conn(chip_1_bndry_0, right_channel(chip_0_bndry_0), left_channel(chip_1_bndry_0), 32, 32);
  node_conn(chip_0_bndry_1, left_channel(chip_1_bndry_1), right_channel(chip_0_bndry_1), 32, 32);
  node_conn(chip_1_bndry_1, right_channel(chip_0_bndry_1), left_channel(chip_1_bndry_1), 32, 32);
}

