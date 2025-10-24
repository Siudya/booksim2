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

void ChipletNetwork::node_conn(int node, int in_chn, int out_chn, int in_lat, int out_lat) {
  _routers[node]->AddInputChannel( _chan[in_chn].get(), _chan_cred[in_chn].get() );
  _chan[in_chn]->SetLatency( in_lat );
  _chan_cred[in_chn]->SetLatency( in_lat );

  _routers[node]->AddOutputChannel( _chan[out_chn].get(), _chan_cred[out_chn].get() );
  _chan[out_chn]->SetLatency( out_lat );
  _chan_cred[out_chn]->SetLatency( out_lat );
}

void ChipletNetwork::node_conn_d2d(int n0, int n1, int n0_port, int n1_port, int lat) {

  auto r0 = _routers.at(n0).get();
  auto r1 = _routers.at(n1).get();

  r0->AlterInputChannel(n0_port, r1->GetOutputChannel(n1_port), r1->GetOutputCreditChannel(n1_port), true);
  r1->AlterInputChannel(n1_port, r0->GetOutputChannel(n0_port), r0->GetOutputCreditChannel(n0_port), true);

  r0->GetOutputChannel(n0_port)->SetLatency(lat);
  r1->GetOutputChannel(n1_port)->SetLatency(lat);
  r0->GetOutputCreditChannel(n0_port)->SetLatency(lat);
  r1->GetOutputCreditChannel(n1_port)->SetLatency(lat);
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

    _routers[i] = Router::NewRouter( config, this, router_name.str( ),i, 5, 5);
    _timed_modules.push_back(_routers[i].get());

    // Do not connect boundary edges
    // right:0 left:1 down:2 up:3
    node_conn(i, right_input, right_output, 1, 1);
    node_conn(i, left_input, left_output, 1, 1);
    node_conn(i, down_input, down_output, 1, 1);
    node_conn(i, up_input, up_output, 1, 1);

    //injection and ejection channel, always 1 latency
    // local: 4
    _routers[i]->AddInputChannel( _inject[i].get(), _inject_cred[i].get() );
    _routers[i]->AddOutputChannel( _eject[i].get(), _eject_cred[i].get() );
    _inject[i]->SetLatency( 1 );
    _eject[i]->SetLatency( 1 );

    router_name.str("");
  }
}