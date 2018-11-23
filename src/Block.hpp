#include <iostream>
#include <string>
#include <vector>

#include "Transaction.hpp"
#include "Crypto.hpp"

class Block {
  public:
  	size_t block_id;
	std::string timestamp;
	size_t nonce;
	std::vector<Transaction> txs;
	std::string prev_block_hash;

	Block(size_t block_id, std::vector<Transaction> txs,
		std::string timestamp, size_t nonce, std::string prev_block_hash);

	std::string to_string();

	std::string get_sha256();
};

Block::Block(size_t block_id, std::vector<Transaction> txs,
    std::string timestamp, size_t nonce, std::string prev_block_hash) {
    this->block_id = block_id;
    this->txs = txs;
    this->timestamp = timestamp;
    this->nonce = nonce;
    this->prev_block_hash = prev_block_hash;
}

std::string Block::to_string() {
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

std::string Block::get_sha256() {
    return sha256(this->to_string());
}
