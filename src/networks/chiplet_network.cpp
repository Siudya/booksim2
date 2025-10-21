#include "booksim.hpp"
#include "chiplet_network.hpp"
#include <sstream>

ChipletNetwork::ChipletNetwork (const Configuration &config, const string & name):Network( config, name ) {}

int ChipletNetwork::left_node(int node_id) {
  int self_chip = get_chip(node_id);
  int self_x = get_x(node_id);
  int self_y = get_y(node_id);
  int left_node_x = (self_x - 1 + x_len) % x_len;
  int left_node_y = self_y;
  return get_node_id(self_chip, left_node_y, left_node_x);
}

int ChipletNetwork::right_node(int node_id) {
  int self_chip = get_chip(node_id);
  int self_x = get_x(node_id);
  int self_y = get_y(node_id);
  int right_node_x = (self_x + 1) % x_len;
  int right_node_y = self_y;
  return get_node_id(self_chip, right_node_y, right_node_x);
}

int ChipletNetwork::up_node(int node_id) {
  int self_chip = get_chip(node_id);
  int self_x = get_x(node_id);
  int self_y = get_y(node_id);
  int up_node_x = self_x;
  int up_node_y = (self_y - 1 + y_len) % y_len;
  return get_node_id(self_chip, up_node_y, up_node_x);
}

int ChipletNetwork::down_node(int node_id) {
  int self_chip = get_chip(node_id);
  int self_x = get_x(node_id);
  int self_y = get_y(node_id);
  int down_node_x = self_x;
  int down_node_y = (self_y + 1) % y_len;
  return get_node_id(self_chip, down_node_y, down_node_x);
}

int ChipletNetwork::left_channel(int node_id) {
  return node_id * 4 + 1;
}

int ChipletNetwork::right_channel(int node_id) {
  return node_id * 4 + 0;
}

int ChipletNetwork::up_channel(int node_id) {
  return node_id * 4 + 3;
}

int ChipletNetwork::down_channel(int node_id) {
  return node_id * 4 + 2;
}

void ChipletNetwork::node_conn(int node, int in_chn, int out_chn, int in_lat, int out_lat) {
  _routers[node]->AddInputChannel( _chan[in_chn].get(), _chan_cred[in_chn].get() );
  _chan[in_chn]->SetLatency( in_lat );
  _chan_cred[in_chn]->SetLatency( in_lat );

  _routers[node]->AddOutputChannel( _chan[out_chn].get(), _chan_cred[out_chn].get() );
  _chan[out_chn]->SetLatency( out_lat );
  _chan_cred[out_chn]->SetLatency( out_lat );
}

void ChipletNetwork::node_conn_2(int n0, int n1, int n0_out_chn, int n1_out_chn, int lat) {
  // n0 -> n1
  _routers[n0]->AddOutputChannel( _chan[n0_out_chn].get(), _chan_cred[n0_out_chn].get() );
  _chan[n0_out_chn]->SetLatency( lat );
  _chan_cred[n0_out_chn]->SetLatency( lat );

  _routers[n1]->AddInputChannel( _chan[n0_out_chn].get(), _chan_cred[n0_out_chn].get() );

  // n1 -> n0
  _routers[n1]->AddOutputChannel( _chan[n1_out_chn].get(), _chan_cred[n1_out_chn].get() );
  _chan[n1_out_chn]->SetLatency( lat );
  _chan_cred[n1_out_chn]->SetLatency( lat );

  _routers[n0]->AddInputChannel( _chan[n1_out_chn].get(), _chan_cred[n1_out_chn].get() );
}

void ChipletNetwork::single_chip_conn(const Configuration &config, int chip_id) {
  int node = chip_id * chip_size;

  ostringstream router_name;

  int left_node;
  int right_node;
  int up_node;
  int down_node;

  int right_input;
  int left_input;
  int up_input;
  int down_input;

  int right_output;
  int left_output;
  int up_output;
  int down_output;

  int x;
  int y;

  for(int i = node; i < node + chip_size; ++i) {
    left_node = this->left_node(i);
    right_node = this->right_node(i);
    up_node = this->up_node(i);
    down_node = this->down_node(i);

    right_input = this->left_channel(right_node);
    left_input = this->right_channel(left_node);
    up_input = this->down_channel(up_node);
    down_input = this->up_channel(down_node);

    right_output = this->right_channel(i);
    left_output = this->left_channel(i);
    up_output = this->up_channel(i);
    down_output = this->down_channel(i);

    x = get_x(i);
    y = get_y(i);

    router_name << "router";
    router_name << '_' << chip_id << '_' << y << '_' << x << '_' << i;

    _routers[node] = Router::NewRouter(
      config, this, router_name.str( ),
      node, 4, 5
    );
    _timed_modules.push_back(_routers[node].get());

    // Do not connect boundary edges
    if(x != 0)         node_conn(node, left_input, left_output, 1, 1);
    if(x != x_len - 1) node_conn(node, right_input, right_output, 1, 1);
    if(y != 0)         node_conn(node, up_input, up_output, 1, 1);
    if(y != y_len - 1) node_conn(node, down_input, down_output, 1, 1);

    //injection and ejection channel, always 1 latency
    _routers[i]->AddInputChannel( _inject[i].get(), _inject_cred[i].get() );
    _routers[i]->AddOutputChannel( _eject[i].get(), _eject_cred[i].get() );
    _inject[i]->SetLatency( 1 );
    _eject[i]->SetLatency( 1 );

    router_name.str("");
  }
}