#include "mqlib.h"
#include <pthread.h>
#include <thread>
#include <signal.h>


int sock;


void messageListener()
{
	while(true) {
		std::string message = receive(sock);

		if(message.empty()) {
			close(sock);
			exit(1);
		}
		else {
			std::cout << message << std::endl;
		}
	}
}


void handlePublisher(int sock) 
{
	std::cout << "--> You are a publisher." << std::endl;
	std::cout << "--> Max characters of queue name: 50, Maxcharacters of message: 500" << std::endl << std::endl;

	while(true) {
		std::string queue;
		std::string message;

		std::cout << ">>> QUEUE NAME: ";
		std::getline(std::cin, queue);

		std::cout << ">>> MESSAGE: ";
		std::getline(std::cin, message);

		if(send(sock, queue, message) < 0)
			std::cout << std::endl << "Problem with sending a message." << std::endl << std::endl;
		else 
			std::cout << std::endl << "Message sent." << std::endl << std::endl;

		sleep(1);
	}
}


void handleSubscriber(int sock) 
{
	std::cout << "--> You are a subscriber." << std::endl;
	std::cout << "--> Type names of queues you want to subscribe, divide them with '|'." << std::endl << std::endl;

	std::string delimiter = "|";
	std::string queues;
	std::vector<std::string> queuesVector;

	std::cout << ">>> QUEUES NAMES: ";
	std::getline(std::cin, queues);
	
	auto start = 0U;
	auto end = queues.find(delimiter);
	while (end != std::string::npos) {
		queuesVector.push_back(queues.substr(start, end - start));
		start = end + delimiter.length();
		end = queues.find(delimiter, start);
	}
	queuesVector.push_back(queues.substr(start, end));

	std::thread listener(messageListener);

	for (std::string q : queuesVector) {
		if(subscribe(sock, q) < 0)
			std::cout << "Queue '" << q << "' not subscribed." << std::endl;
		// sleep(2);
	}

	listener.join();
}

void handleExit(int s)
{
	if(disconnect(sock)) {
		std::cout << std::endl << "...Closing..." << std::endl;
		exit(0);
	}
}


int main(int argc, char *argv[])
{
	if (argc != 4) {
		std::cout << "You have to pass 3 arguments: " << std::string(argv[0]) << " server_address port_number client_type" << std::endl;
		exit(1);
	}

	if (strcmp(argv[3], "publisher") != 0 && strcmp(argv[3], "subscriber") != 0) {
		std::cout << "Argument client_type must be equal 'publisher' or 'subscriber'." << std::endl;
		exit(1);
	}

	std::string address = std::string(argv[1]);
	int port = atoi(argv[2]);
	std::string type = std::string(argv[3]);

	sock = connect(address, port);

	if (sock < 0)
		exit(1);

	signal(SIGINT, handleExit);

	if (type.compare("publisher") == 0)
		handlePublisher(sock);
	else
		handleSubscriber(sock);

	return 0;
}