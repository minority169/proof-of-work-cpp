#include <iostream>
#include <vector>
#include <string>

#include "BlockChain.hpp"

zmq::context_t context(1);
zmq::socket_t rep_socket(context, ZMQ_DEALER);
zmq::socket_t req_socket(context, ZMQ_DEALER);

int main(int argc, char *argv[]) {
	if (argc < 3) {
		fprintf(stderr, "Usage: %s node_id port (node1 node2 ...)", argv[1]);
		return 1;
	}
	std::vector<std::string> other_nodes;
	for (size_t i = 3; i < argc; i++) {
		other_nodes.push_back(argv[i]);
	}

	std::cout << "creating blockchain..." << std::endl;
	BlockChain bc(argv[1], argv[2], other_nodes);
	std::cout << "blockchain created" << std::endl;

	bc.do_work();
}
