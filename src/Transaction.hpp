#include <iostream>
#include <string>

class Transaction {
  public:
  	std::string data;

  	Transaction();

  	Transaction(std::string data);

  	std::string to_string();
};

Transaction::Transaction() {}

Transaction::Transaction(std::string data) {
	this->data = data;
}

std::string Transaction::to_string() {
	return this->data;
}
