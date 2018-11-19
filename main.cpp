#include <iostream>
#include <vector>
#include <string>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <zmq.hpp>
#include <unistd.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <openssl/sha.h>
#include <openssl/ripemd.h>

namespace pt = boost::property_tree;

enum { DIFFICULTY = 5 };

zmq::context_t context(1);
zmq::socket_t rep_socket(context, ZMQ_DEALER);
zmq::socket_t req_socket(context, ZMQ_DEALER);

std::string sha256(const std::string str) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, str.c_str(), str.size());
    SHA256_Final(hash, &sha256);
    
    std::stringstream ss;
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }

    return ss.str();
}

class Transaction {
  public:
  	std::string data;

  	Transaction() { }

  	Transaction(std::string data) {
  		this->data = data;
  	}

  	std::string to_string() {
  		return data;
  	}
};

class Block {
  public:
	size_t block_id;
	std::string timestamp;
	size_t nonce;
	std::vector<Transaction> txs;
	std::string prev_block_hash;

	Block(size_t block_id, std::vector<Transaction> txs,
		std::string timestamp, size_t nonce, std::string prev_block_hash) {
		this->block_id = block_id;
		this->txs = txs;
		this->timestamp = timestamp;
		this->nonce = nonce;
		this->prev_block_hash = prev_block_hash;
	}

	std::string to_string() {
		std::string res;
		res += std::to_string(block_id) + ",";
		res += timestamp + ",";
		res += std::to_string(nonce) + ",";

		for (size_t i = 0; i < txs.size(); i++) {
			res += txs[i].to_string() + ",";
		}

		res += prev_block_hash;

		return res;
	}

	std::string get_sha256() {
      	return sha256(this->to_string());
    }
};

class BlockChain {
  public:
  	std::string node_id;
  	std::vector<Block> chain;
  	std::vector<Transaction> txs_pool;
  	std::vector<std::string> other_nodes;
  	std::string port;

  	std::string now() {
  		return std::to_string(std::time(0));
  	}

  	BlockChain(std::string node_id, std::string port, std::vector<std::string> other_nodes) {
  		this->node_id = node_id;
  		this->chain.push_back(Block(0, std::vector<Transaction>(0), this->now(), 0, "0"));
  		this->other_nodes = other_nodes;
  		this->port = port;
  		rep_socket.bind("tcp://*:" + port);
  		this->add_self_to_network();
  	}

  	bool validate_chain(std::vector<Block> new_chain) {
  		size_t chain_size = new_chain.size();

  		if (chain_size == 0) {
  			// chain must be non-empty
  			return false;
  		}

  		if (new_chain[0].prev_block_hash != "0") {
  			// invalid genesis block
  			return false;
  		}

  		for (size_t i = 1; i < chain_size; i++) {
  			if (new_chain[i].prev_block_hash != new_chain[i - 1].get_sha256()) {
  				// invalid block sequence
  				return false;
  			}
  			for (size_t j = 0; j < DIFFICULTY; j++) {
  				if (new_chain[i].get_sha256()[j] != '0') {
  					// invalid difficulty
  					return false;
  				}
  			}
  		}

  		return true;

  	}

  	void add_self_to_network() {
  		std::cout << "adding self to network" << std::endl;
  		for (size_t i = 0; i < other_nodes.size(); i++) {
  			req_socket.connect("tcp://localhost:" + other_nodes[i]);

  			pt::ptree tree;
  			tree.put("query_type", "new_node");
  			tree.put("port", this->port);

  			std::ostringstream oss;
  			pt::write_json(oss, tree);
  			std::string msg = oss.str();

  			zmq::message_t request(msg.size());
        	std::memcpy(request.data(), msg.c_str(), msg.size());
        	req_socket.send(request);

        	// zmq::message_t reply;
        	// req_socket.recv(&reply);
        	// std::cout << reply.data() << std::endl;
        	req_socket.disconnect("tcp://localhost:" + other_nodes[i]);
  		}
  		std::cout << "successful" << std::endl;
  	}

  	void send_chain_to_other_nodes() {
  		std::cout << "sending chain to other nodes" << std::endl;
  		for (size_t i = 0; i < other_nodes.size(); i++) {
  			req_socket.connect("tcp://localhost:" + other_nodes[i]);

  			pt::ptree tree;
  			tree.put("query_type", "new_chain");
  			tree.put("node_id", this->node_id);
  			tree.put("chain_size", chain.size());

  			for (size_t block_id = 0; block_id < chain.size(); block_id++) {
  				pt::ptree child_block;
  				child_block.put("block_id", chain[block_id].block_id);
  				child_block.put("timestamp", chain[block_id].timestamp);
  				child_block.put("nonce", chain[block_id].nonce);
  				child_block.put("prev_block_hash", chain[block_id].prev_block_hash);
  				pt::ptree child_txs;
  				for (size_t tx_id = 0; tx_id < chain[block_id].txs.size(); tx_id++) {
  					child_txs.put("tx" + std::to_string(tx_id + 1), chain[block_id].txs[tx_id].to_string());
  				}
  				child_block.add_child("transactions", child_txs);
  				tree.add_child("block" + std::to_string(block_id), child_block);
  			}

  			std::ostringstream oss;
  			pt::write_json(oss, tree);
  			std::string msg = oss.str();

  			zmq::message_t request(msg.size());
        	std::memcpy(request.data(), msg.c_str(), msg.size());
        	req_socket.send(request);

        	req_socket.disconnect("tcp://localhost:" + other_nodes[i]);
  		}
  		std::cout << "successful" << std::endl;
  	}

  	void check_messages() {
  		std::cout << "receiving messages..." << std::endl;
  		while (true) {
  			zmq::message_t request;
  			rep_socket.recv(&request, ZMQ_NOBLOCK);
  			if (request.size() != 0) {
  				std::cout << "message received!" << std::endl;

		        char *msg = (char *) malloc(10000000);
		        std::memcpy(msg, request.data(), request.size());

		        pt::ptree tree;
			    std::stringstream ss;

		        try {
	  				ss << msg;
	  				pt::read_json(ss, tree);
		        } catch(const pt::ptree_error &e) {
        			std::cout << e.what() << std::endl;
		        	continue;
		        }

		        if (tree.get<std::string>("query_type") == "new_node") {
		        	std::cout << "processing new node..." << std::endl;
		        	other_nodes.push_back(tree.get<std::string>("port"));
		        } else if (tree.get<std::string>("query_type") == "new_chain") {
		        	std::cout << "processing new chain from " << tree.get<std::string>("node_id") << std::endl;

		        	int chain_size = tree.get<int>("chain_size");
		        	std::vector<Block> new_chain;

		        	if (chain_size <= this->chain.size()) {
		        		std::cout << "chain was not replaced" << std::endl;
		        		continue;
		        	}

		        	for (size_t block_id = 0; block_id < chain_size; block_id++) {
		        		std::vector<Transaction> block_txs;
		        		pt::ptree txs_child = tree.get_child("block" + std::to_string(block_id) + ".transactions");
		        		for (auto pair : txs_child) {
		        			block_txs.push_back(Transaction(pair.second.data()));
		        		}

		        		Block new_block(tree.get<int>("block" + std::to_string(block_id) + ".block_id"),
		        						block_txs,
		        						tree.get<std::string>("block" + std::to_string(block_id) + ".timestamp"),
		        						tree.get<size_t>("block" + std::to_string(block_id) + ".nonce"),
		        						tree.get<std::string>("block" + std::to_string(block_id) + ".prev_block_hash"));

		        		new_chain.push_back(new_block);
		        	}

		        	if (validate_chain(new_chain)) {
		        		std::cout << "chain replaced" << std::endl;
		        		this->chain = new_chain;
		        	} else {
		        		std::cout << "invalid chain" << std::endl;
		        	}
		        }
  			} else {
  				std::cout << "no more messages to process" << std::endl;
  				return;
  			}
  		}
  	}

  	bool is_mined(std::string hash) {
  		for (size_t i = 0; i < DIFFICULTY; i++) {
  			if (hash[i] != '0') {
  				return false;
  			}
  		}
  		return true;
  	}

  	Block get_last_block() {
  		return this->chain.back();
  	}

  	Block mine_block(std::vector<Transaction> txs) {
  		Block last_block = this->get_last_block();
  		Block new_block(last_block.block_id + 1, txs, this->now(), 0, last_block.get_sha256());

  		std::cout << "mining block " << new_block.block_id << std::endl;
  		while (!is_mined(new_block.get_sha256())) {
  			new_block.nonce++;
  		}
  		std::cout << "block mined" << std::endl;

  		return new_block;
  	}

  	Block create_block() {
  		Block new_block = this->mine_block(this->txs_pool);
  		this->chain.push_back(new_block);
  		this->txs_pool.clear();
  		this->save_blockchain_to_disk();

  		return new_block;
  	}

  	void submit_transaction(std::string tx_data) {
  		this->txs_pool.push_back(Transaction(tx_data));
  	}

  	void save_blockchain_to_disk() {
  		std::cout << "saving blockchain to disk..." << std::endl;
  		std::ofstream ofs;
  		ofs.open(this->node_id + ".txt", std::ios::out | std::ios::trunc);
  		for (size_t i = 0; i < chain.size(); i++) {
  			ofs << chain[i].to_string() << '\n';
  		}
  		ofs.close();
  		std::cout << "blockchain saved" << std::endl;
  	}

  	void generate_transactions(int txs_amt) {
  		std::cout << "generating transactions..." << std::endl;
  		for (size_t i = 0; i < txs_amt; i++) {
  			this->submit_transaction(this->node_id + " tx " + std::to_string(rand()));
  		}
  		std::cout << txs_amt << " transactions generated" << std::endl;
  	}

  	void do_work() {
  		while (true) {
  			this->check_messages();
  			this->generate_transactions(rand() % 10 + 1);
  			this->create_block();
  			this->send_chain_to_other_nodes();
  			for (size_t i = 0; i < other_nodes.size(); i++) {
  				std::cout << other_nodes[i] << ' ';
  			}
  			std::cout << std::endl;
  		}
  	}

};

int main(int argc, char *argv[]) {
	if (argc < 3) {
		std::cout << "Usage: ./run node_id port (node1 node2 ...)" << std::endl;
		return 0;
	}
	std::vector<std::string> other_nodes;
	for (size_t i = 3; i < argc; i++) {
		other_nodes.push_back(argv[i]);
	}

	std::cout << "creating blockchain" << std::endl;
	BlockChain bc(argv[1], argv[2], other_nodes);
	std::cout << "blockchain created" << std::endl;

	bc.do_work();
}
