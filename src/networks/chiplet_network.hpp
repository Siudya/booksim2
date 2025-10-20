#ifndef _CHIPLET_NETWORK_HPP_
#define _CHIPLET_NETWORK_HPP_

#include "network.hpp"

class ChipletNetwork : public Network {
  private:
  int left_node(int node_id);
  int right_node(int node_id);
  int up_node(int node_id);
  int down_node(int node_id);

  public:
  // node[chip_id][y][x]
  const int x_len = 4;
  const int y_len = 4;
  const int chip_size = x_len * y_len;
  void single_chip_conn( const Configuration &config, int chip_id);
  inline int get_chip(int node_id) { return node_id / chip_size; }
  inline int get_x(int node_id) { return node_id % chip_size % x_len; };
  inline int get_y(int node_id) { return node_id % chip_size / x_len; };
  inline int get_node_id(int chip_id, int y, int x) {
    return chip_id * chip_size + y * x_len + x;
  };
  int left_channel(int node_id);
  int right_channel(int node_id);
  int up_channel(int node_id);
  int down_channel(int node_id);
  ChipletNetwork ( const Configuration &config, const string & name);
  void node_conn(int node, int in_chn, int out_chn, int in_lat, int out_lat);
};

#endif // _CHIPLET_NETWORK_HPP_