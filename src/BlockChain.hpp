#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <fstream>

#include <zmq.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "Block.hpp"

namespace pt = boost::property_tree;

enum { DIFFICULTY = 5, MAX_MSG_SIZE = 10000000, TAIL_SIZE = 20, MAX_TXS_PER_BLOCK = 20, MAX_TX_SIZE = 100 };

extern zmq::context_t context;
extern zmq::socket_t rep_socket;
extern zmq::socket_t req_socket;

class BlockChain {
  public:
    std::string node_id;
    std::vector<Block> chain;
    std::vector<Transaction> txs_pool;
    std::vector<std::string> other_nodes;
    std::string port;

    std::string now();

    BlockChain(std::string node_id, std::string port, std::vector<std::string> other_nodes);

    bool validate_full_chain(std::vector<Block> new_chain);

    bool validate_partial_chain(std::vector<Block> new_chain);

    void add_self_to_network();

    void send_full_chain(std::string port);

    void send_partial_chain(std::string port);

    void resubmit_transactions(std::vector<Transaction> bad_txs);

    void add_tail(std::vector<Block> tail);

    void send_chain_to_other_nodes();

    void check_messages();

    bool is_mined(std::string hash);

    Block get_last_block();

    Block mine_block(std::vector<Transaction> txs);

    Block create_block();

    void submit_transaction(std::string tx_data);

    void save_blockchain_to_disk();

    void generate_transactions(int txs_amt);

    void do_work();
};


std::string BlockChain::now() {
    return std::to_string(std::time(0));
}

BlockChain::BlockChain(std::string node_id, std::string port, std::vector<std::string> other_nodes) {
    this->node_id = node_id;
    this->chain.push_back(Block(0, std::vector<Transaction>(0), this->now(), 0, "0"));
    this->other_nodes = other_nodes;
    this->port = port;
    rep_socket.bind("tcp://*:" + port);
    this->add_self_to_network();
}

bool BlockChain::validate_full_chain(std::vector<Block> new_chain) {
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

bool BlockChain::validate_partial_chain(std::vector<Block> new_chain) {
    size_t chain_size = new_chain.size();

    if (chain_size != TAIL_SIZE) {
        // invalid chain size
        return false;
    }

    size_t first_id = new_chain[0].block_id;

    if (new_chain[0].prev_block_hash != chain[first_id - 1].get_sha256()) {
        // invalid partial chain
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

void BlockChain::add_self_to_network() {
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

void BlockChain::send_full_chain(std::string other_port) {
    req_socket.connect("tcp://localhost:" + other_port);

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

    req_socket.disconnect("tcp://localhost:" + other_port);
}

void BlockChain::send_partial_chain(std::string other_port) {
    req_socket.connect("tcp://localhost:" + other_port);

    pt::ptree tree;
    tree.put("query_type", "partial_chain");
    tree.put("node_id", this->node_id);
    tree.put("chain_size", chain.size());

    for (size_t block_id = chain.size() - TAIL_SIZE; block_id < chain.size(); block_id++) {
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

    req_socket.disconnect("tcp://localhost:" + other_port);
}

void BlockChain::send_chain_to_other_nodes() {
    std::cout << "sending chain to other nodes..." << std::endl;
    for (size_t i = 0; i < other_nodes.size(); i++) {
        if (chain.size() <= TAIL_SIZE) {
            send_full_chain(other_nodes[i]);
        } else {
            send_partial_chain(other_nodes[i]);
        }
    }
    std::cout << "successful" << std::endl;
}

void BlockChain::resubmit_transactions(std::vector<Transaction> bad_txs) {
    for (size_t i = 0; i < bad_txs.size(); i++) {
        this->submit_transaction(bad_txs[i].to_string());
    }
}

void BlockChain::add_tail(std::vector<Block> tail) {
    size_t first_id = tail[0].block_id;
    for (size_t i = first_id + 1; i < first_id + tail.size(); i++) {
        if (i < chain.size()) {
            this->resubmit_transactions(chain[i].txs);
            this->chain[i] = tail[i - first_id];
        } else {
            this->chain.push_back(tail[i - first_id]);
        }
    }
}

void BlockChain::check_messages() {
    std::cout << "receiving messages..." << std::endl;
    while (true) {
        zmq::message_t request;
        rep_socket.recv(&request, ZMQ_NOBLOCK);
        if (request.size() != 0) {
            std::cout << "message received!" << std::endl;

            char *msg = (char *) malloc(MAX_MSG_SIZE * sizeof(char));
            if (request.size() >= MAX_MSG_SIZE) {
                std::cout << "Message is too long" << std::endl;
                continue;
            }
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
                this->other_nodes.push_back(tree.get<std::string>("port"));
                this->send_full_chain(tree.get<std::string>("port"));
                std::cout << "new node accepted" << std::endl;
            } else if (tree.get<std::string>("query_type") == "new_chain") {
                std::cout << "processing new chain from " << tree.get<std::string>("node_id") << std::endl;

                int chain_size = tree.get<int>("chain_size");
                std::vector<Block> new_chain;

                if (chain_size <= this->chain.size()) {
                    std::cout << "chain was not replaced" << std::endl;
                    continue;
                }

                bool is_valid = true;

                for (size_t block_id = 0; block_id < chain_size; block_id++) {
                    std::vector<Transaction> block_txs;
                    pt::ptree txs_child = tree.get_child("block" + std::to_string(block_id) + ".transactions");
                    for (auto pair : txs_child) {
                        if (pair.second.data().size() > MAX_TX_SIZE) {
                            is_valid = false;
                        }
                        block_txs.push_back(Transaction(pair.second.data()));
                    }

                    Block new_block(tree.get<int>("block" + std::to_string(block_id) + ".block_id"),
                                    block_txs,
                                    tree.get<std::string>("block" + std::to_string(block_id) + ".timestamp"),
                                    tree.get<size_t>("block" + std::to_string(block_id) + ".nonce"),
                                    tree.get<std::string>("block" + std::to_string(block_id) + ".prev_block_hash"));

                    new_chain.push_back(new_block);
                }

                if (is_valid && validate_full_chain(new_chain)) {
                    std::cout << "chain accepted" << std::endl;
                    this->chain = new_chain;
                } else {
                    std::cout << "invalid chain" << std::endl;
                }
            } else if (tree.get<std::string>("query_type") == "partial_chain") {
                std::cout << "processing partial chain from " << tree.get<std::string>("node_id") << std::endl;

                int chain_size = tree.get<int>("chain_size");
                std::vector<Block> new_chain;

                if (chain_size <= this->chain.size() || chain_size <= TAIL_SIZE) {
                    std::cout << "chain was not replaced" << std::endl;
                    continue;
                }

                bool is_valid = true;

                for (size_t block_id = chain_size - TAIL_SIZE; block_id < chain_size; block_id++) {
                    std::vector<Transaction> block_txs;
                    pt::ptree txs_child = tree.get_child("block" + std::to_string(block_id) + ".transactions");
                    for (auto pair : txs_child) {
                        if (pair.second.data().size() > MAX_TX_SIZE) {
                            is_valid = false;
                        }
                        block_txs.push_back(Transaction(pair.second.data()));
                    }

                    Block new_block(tree.get<int>("block" + std::to_string(block_id) + ".block_id"),
                                    block_txs,
                                    tree.get<std::string>("block" + std::to_string(block_id) + ".timestamp"),
                                    tree.get<size_t>("block" + std::to_string(block_id) + ".nonce"),
                                    tree.get<std::string>("block" + std::to_string(block_id) + ".prev_block_hash"));

                    new_chain.push_back(new_block);
                }

                if (is_valid && validate_partial_chain(new_chain)) {
                    std::cout << "chain accepted" << std::endl;
                    this->add_tail(new_chain);
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

bool BlockChain::is_mined(std::string hash) {
    for (size_t i = 0; i < DIFFICULTY; i++) {
        if (hash[i] != '0') {
            return false;
        }
    }
    return true;
}

Block BlockChain::get_last_block() {
    return this->chain.back();
}

Block BlockChain::mine_block(std::vector<Transaction> txs) {
    Block last_block = this->get_last_block();
    Block new_block(last_block.block_id + 1, txs, this->now(), 0, last_block.get_sha256());

    std::cout << "mining block " << new_block.block_id << std::endl;
    while (!is_mined(new_block.get_sha256())) {
        new_block.nonce++;
    }
    std::cout << "block mined" << std::endl;

    return new_block;
}

Block BlockChain::create_block() {
    std::vector<Transaction> txs_to_be_mined;
    for (size_t i = 0; i < std::min((size_t) MAX_TXS_PER_BLOCK, txs_pool.size()); i++) {
        txs_to_be_mined.push_back(txs_pool.back());
        txs_pool.pop_back();
    }
    Block new_block = this->mine_block(txs_to_be_mined);
    this->chain.push_back(new_block);
    this->save_blockchain_to_disk();

    return new_block;
}

void BlockChain::submit_transaction(std::string tx_data) {
    this->txs_pool.push_back(Transaction(tx_data));
}

void BlockChain::save_blockchain_to_disk() {
    std::cout << "saving blockchain to disk..." << std::endl;
    std::ofstream ofs;
    ofs.open("../node_chains/" + this->node_id + "_chain.txt", std::ios::out | std::ios::trunc);
    for (size_t i = 0; i < chain.size(); i++) {
        ofs << chain[i].to_string() << '\n';
    }
    ofs.close();
    std::cout << "blockchain saved" << std::endl;
}

void BlockChain::generate_transactions(int txs_amt) {
    std::cout << "generating transactions..." << std::endl;
    for (size_t i = 0; i < txs_amt; i++) {
        this->submit_transaction(this->node_id + " tx " + std::to_string(rand()));
    }
    std::cout << txs_amt << " transactions generated" << std::endl;
}

void BlockChain::do_work() {
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
