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
  static const int x_len = 4;
  static const int y_len = 4;
  static const int chip_size = x_len * y_len;
  static const int right_port = 0;
  static const int left_port = 1;
  static const int down_port = 2;
  static const int up_port = 3;
  static const int local_port = 4;
  static inline const int get_chip(const int node_id) { return node_id / chip_size; }
  static inline const int get_x(const int node_id) { return node_id % chip_size % x_len; };
  static inline const int get_y(const int node_id) { return node_id % chip_size / x_len; };
  static inline const int get_node_id(const int chip_id, const int y, const int x) {
    return chip_id * chip_size + y * x_len + x;
  };
  static inline const int left_channel(const int node_id) { return node_id * 4 + left_port; };
  static inline const int right_channel(const int node_id) { return node_id * 4 + right_port; };
  static inline const int up_channel(const int node_id) { return node_id * 4 + up_port; };
  static inline const int down_channel(const int node_id) { return node_id * 4 + down_port; };

  ChipletNetwork ( const Configuration &config, const string & name);
  void single_chip_conn( const Configuration &config, int chip_id);
  void node_conn(int node, int in_chn, int out_chn, int in_lat, int out_lat);
  void node_conn_2(int n0, int n1, int n0_out_chn, int n1_out_chn, int lat);
};

#endif // _CHIPLET_NETWORK_HPP_