#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <unistd.h>
#include <cstdio>
#include <map>
#include <pthread.h>
#include <thread>
#include <sys/types.h>
#include <list>
#include <vector>
#include <bits/unique_ptr.h>
#include <ctime>
#include <signal.h>
#include <algorithm>

#define QUEUE_SIZE 5


struct Message {			// <-- Struktura przechowujaca pojedyncza wiadomosc wraz z czasem otrzymania jej przez serwer
	std::string text;
	time_t time;
};

struct ThreadData {         // <-- Struktura przekazywana do watku klienta
	int client_descriptor;
	int messageTime;
	std::map<std::string, std::list<int>> *queuesClientsList;
	std::map<std::string, std::vector<Message>> *queuesMessagesList;
	std::map<int, pthread_mutex_t> *clients_mutex;
	pthread_mutex_t *queues_clients_mutex;
	pthread_mutex_t *queues_messages_mutex;
};

struct CleanerData {		// <-- Struktura przekazywana do watku usuwajacego przestarzale wiadomosci
	int messageTime;
	std::map<std::string, std::vector<Message>> *queuesMessagesList;
	pthread_mutex_t *queues_messages_mutex;
};

time_t currTime;


void *cleanerThread(void *c_data)
{
	pthread_detach(pthread_self());

	CleanerData *cleaner_data = (CleanerData*)c_data;

	while(true)
	{
		pthread_mutex_lock(cleaner_data->queues_messages_mutex);

		for (const auto& iter : (*cleaner_data->queuesMessagesList)) {
			int position = 0;

			for (Message msg : iter.second) {
				if (time(& currTime)-msg.time > cleaner_data->messageTime) 
					position++;
				else break;
			}

			if (position != 0) {
				(*cleaner_data->queuesMessagesList)[iter.first].erase((*cleaner_data->queuesMessagesList)[iter.first].begin(), 
					(*cleaner_data->queuesMessagesList)[iter.first].begin() + position);
			}
		}

        pthread_mutex_unlock(cleaner_data->queues_messages_mutex);

		sleep(10);
	}
}


void *clientThread(void *t_data)
{
	pthread_detach(pthread_self());

	ThreadData *client_data = (ThreadData*)t_data;

	// Odpowiedz do klienta, ze polaczono z serwerem
	pthread_mutex_lock(&((*client_data->clients_mutex).at(client_data->client_descriptor)));

	if (write(client_data->client_descriptor, "CONNECTED", 9) < 0) {
		printf("Error when sending initial reply to client %i.\n", client_data->client_descriptor);

		pthread_mutex_unlock(&((*client_data->clients_mutex).at(client_data->client_descriptor)));
		
		close(client_data->client_descriptor);
		delete client_data;
		client_data = nullptr;
		return nullptr;
	}

	pthread_mutex_unlock(&((*client_data->clients_mutex).at(client_data->client_descriptor)));

	// Glówna petla, w której serwer obsluguje requesty od klienta
	while (true) {
		char messageLen[3] = "";
		char request[580] = "";

		// Odczytanie dlugosci wiadomosci
		int totalLength = 0;
		int currentLength = 0;
		std::string messageLenStr = "";

		while (totalLength < 3) {
			currentLength = read(client_data->client_descriptor, messageLen, 3-totalLength);
			if (currentLength <= 0) {
				printf("Error when receiving request length from client %d.\n", client_data->client_descriptor);

				// W przypadku bledu / rozlaczenia sie klienta usuwamy go z list subskrybcji 
				pthread_mutex_lock(client_data->queues_clients_mutex);

				typedef std::map<std::string, std::list<int>>::const_iterator MapIterator;
				for (MapIterator iter = (*client_data->queuesClientsList).begin(); iter != (*client_data->queuesClientsList).end(); iter++) {
					std::list<int> clients = iter->second;
					for (int list_iter : iter->second) {
						if (list_iter == client_data->client_descriptor) {
							clients.remove(client_data->client_descriptor);
							break;
						}
					}
					(*client_data->queuesClientsList).at(iter->first) = clients;
				}

				pthread_mutex_unlock(client_data->queues_clients_mutex);

				// A nastepnie zamykamy polaczenie z tym klientem
				close(client_data->client_descriptor);
				delete client_data;
				client_data = nullptr;
				return nullptr;
			}
			messageLenStr.append(messageLen, 0, currentLength);
			totalLength += currentLength;
		}

		// Odczytanie wiadomosci o danej dlugosci
		int charsToRead = atoi(messageLenStr.c_str());
		int totalCount = 0;
		int currentCount = 0;
		std::string requestStr = "";

		while (totalCount < charsToRead) {
			currentCount = read(client_data->client_descriptor, request, charsToRead-totalCount);
			if (currentCount <= 0) {
				printf("Error when receiving request from client.\n");

				// W przypadku bledu / rozlaczenia sie klienta usuwamy go z list subskrybcji 
				pthread_mutex_lock(client_data->queues_clients_mutex);

				typedef std::map<std::string, std::list<int>>::const_iterator MapIterator;
				for (MapIterator iter = (*client_data->queuesClientsList).begin(); iter != (*client_data->queuesClientsList).end(); iter++) {
					std::list<int> clients = iter->second;
					for (int list_iter : iter->second) {
						if (list_iter == client_data->client_descriptor) {
							clients.remove(client_data->client_descriptor);
							break;
						}
					}
					(*client_data->queuesClientsList).at(iter->first) = clients;
				}

				pthread_mutex_unlock(client_data->queues_clients_mutex);

				// A nastepnie zamykamy polaczenie z tym klientem
				close(client_data->client_descriptor);
				delete client_data;
				client_data = nullptr;
				return nullptr;
			}
			requestStr.append(request, 0, currentCount);
			totalCount += currentCount;
		}

		printf("\n--> REQUEST from %i: %s\n", client_data->client_descriptor, requestStr.c_str());

		// Poszczególne czesci requestu odzielone sa znakiem '|', dzielimy wiêc otrzymana wiadomosc na czesci.
		// Zapisujemy do vectora, zeby latwo bylo operowac na poszczególnych elementach.
		// requestVector[0] przechowuje typ wiadomosci: SUBSCRIBE, UNSUBSCRIBE, MESSAGE lub QUIT
		std::string delimiter = "|";
		std::vector<std::string> requestVector;

		auto start = 0U;
		auto end = requestStr.find(delimiter);
		while (end != std::string::npos) {
			requestVector.push_back(requestStr.substr(start, end - start));
			start = end + delimiter.length();
			end = requestStr.find(delimiter, start);
		}
		requestVector.push_back(requestStr.substr(start, end));

		if (requestVector[0] == "SUBSCRIBE") {
			// requestVector[1] - nazwa tematu do zasubskrybowania
			pthread_mutex_lock(client_data->queues_clients_mutex);
			(*client_data->queuesClientsList)[requestVector[1]].push_back(client_data->client_descriptor);
			pthread_mutex_unlock(client_data->queues_clients_mutex);

			// Wyslac klientowi zalegle wiadomosci z danej kolejki (tylko te, które nie sa starsze niz np. 20 sec)
			if ((*client_data->queuesMessagesList).find(requestVector[1]) != (*client_data->queuesMessagesList).end()) {
				for (Message msg : (*client_data->queuesMessagesList).at(requestVector[1])) {
					if (int(time(&currTime) - msg.time) <= client_data->messageTime) {
						std::string msgToSend = "MESSAGE|" + requestVector[1] + "|" + msg.text;
						int msgToSendLen = msgToSend.size();

						printf("\n<-- SENDING to %i: %s\n", client_data->client_descriptor, msgToSend.c_str());

						if (msgToSendLen > 100)
							msgToSend = std::to_string(msgToSendLen) + msgToSend;
						else if (msgToSendLen > 10)
							msgToSend = "0" + std::to_string(msgToSendLen) + msgToSend;
						else
							msgToSend = "00" + std::to_string(msgToSendLen) + msgToSend;

						pthread_mutex_lock(&((*client_data->clients_mutex).at(client_data->client_descriptor)));
						int currSend = 0;
						int totalSend = 0;

						while(totalSend < (int)msgToSend.size()) {
							currSend = write(client_data->client_descriptor, msgToSend.c_str(), msgToSend.size());
							if(currSend < 0) {
								printf("Error when sending message to client %d.\n", client_data->client_descriptor);
								close(client_data->client_descriptor);
								delete client_data;
								client_data = nullptr;
								return nullptr;
							}
							totalSend += currSend;
						}
						pthread_mutex_unlock(&((*client_data->clients_mutex).at(client_data->client_descriptor)));
					}
				}
			}
		}

		else if (requestVector[0] == "UNSUBSCRIBE") {
			// requestVector[1] - nazwa tematu do odsubskrybowania
			pthread_mutex_lock(client_data->queues_clients_mutex);
			(*client_data->queuesClientsList)[requestVector[1]].remove(client_data->client_descriptor);
			pthread_mutex_unlock(client_data->queues_clients_mutex);
		}

		else if (requestVector[0] == "MESSAGE") {
			// requestVector[1] - nazwa tematu
			// requestVector[2] - tresc wiadomosci
			
			// Przygotowanie wiadomosci do wyslania
			std::string message = messageLenStr + requestStr;

			// Wyslanie wiadomosci do klientów subskrybujacych dana kolejke
			pthread_mutex_lock(client_data->queues_clients_mutex);

			if ((*client_data->queuesClientsList).find(requestVector[1]) != (*client_data->queuesClientsList).end()) {
				
				for (int client : (*client_data->queuesClientsList).at(requestVector[1])) {

					printf("\n<-- SENDING to %i: %s\n", client, requestStr.c_str());
					
					pthread_mutex_lock(&((*client_data->clients_mutex).at(client)));
					int currSend = 0;
					int totalSend = 0;

					while(totalSend < (int)message.size()) {
						currSend = write(client, message.c_str(), message.size());
						if(currSend < 0) {
							printf("Error when sending message to client %d.\n", client);
							close(client);
							break;
						}
						totalSend += currSend;
					}
					pthread_mutex_unlock(&((*client_data->clients_mutex).at(client)));
				}
			}

			pthread_mutex_unlock(client_data->queues_clients_mutex);

			// Dodanie wiadomosci do queuesMessagesList
			pthread_mutex_lock(client_data->queues_messages_mutex);
			(*client_data->queuesMessagesList)[requestVector[1]].push_back({requestVector[2], time(&currTime)});
			pthread_mutex_unlock(client_data->queues_messages_mutex);
		}

		else if (requestVector[0] == "QUIT") {
			// Usuniecie klienta z wszystkich list subskrybcji
			pthread_mutex_lock(client_data->queues_clients_mutex);

			typedef std::map<std::string, std::list<int>>::const_iterator MapIterator;
			for (MapIterator iter = (*client_data->queuesClientsList).begin(); iter != (*client_data->queuesClientsList).end(); iter++) {
				std::list<int> clients = iter->second;
				for (int list_iter : iter->second) {
					if (list_iter == client_data->client_descriptor) {
						clients.remove(client_data->client_descriptor);
						break;
					}
				}
				(*client_data->queuesClientsList).at(iter->first) = clients;
			}

			pthread_mutex_unlock(client_data->queues_clients_mutex);
			break;
		}

		/*
		printf("\n------------------- QUEUES and MESSAGES -----------------\n");
        typedef std::map<std::string, std::vector<Message>>::const_iterator MsgIterator;
        for (MsgIterator iter = (*client_data->queuesMessagesList).begin(); iter != (*client_data->queuesMessagesList).end(); iter++)
        {
            printf("Queue: %s, Messages: ", (iter->first).c_str());
            for (Message msg : iter->second)
                printf("%s, ", msg.text.c_str());
            printf("\n");
        }
        printf("---------------------------------------------------------\n");
		*/

        printf("\n----------------- QUEUES and SUBSCRIBERS ----------------\n");
        typedef std::map<std::string, std::list<int>>::const_iterator SubIterator;
        for (SubIterator iter = (*client_data->queuesClientsList).begin(); iter != (*client_data->queuesClientsList).end(); iter++)
        {
            printf("Queue: %s, Subscribers: ", (iter->first).c_str());
            for (int list_iter : iter->second)
                printf("%d, ", list_iter);
            printf("\n");
        }
        printf("---------------------------------------------------------\n");
	}

	close(client_data->client_descriptor);
	delete client_data;
	client_data = nullptr;

	return nullptr;
}


int main(int argc, char *argv[]) 
{
	if (argc != 3) {
		printf("You have to pass 2 arguments: %s port_number message_lifetime\n", argv[0]);
		exit(1);
	}

	int port = atoi(argv[1]);
	int messageTime = atoi(argv[2]);

	std::map<std::string, std::list<int>> *queuesClientsList = new std::map<std::string, std::list<int>>;   // <-- Kluczami sa nazwy kolejek, wartosciami listy
																											//     subskrybujacych dany temat uzytkowników (deskryptory)

	std::map<std::string, std::vector<Message>> *queuesMessagesList = new std::map<std::string, std::vector<Message>>;	// <-- Kluczami sa nazwy kolejek, wartosciami wektory
																													//     z wyslanymi wiadomosciami

	std::map<int, pthread_mutex_t> *clients_mutex = new std::map<int, pthread_mutex_t>;                     // <-- Mapa przechowujaca mutexy dla klientów

	pthread_mutex_t	queues_clients_mutex = PTHREAD_MUTEX_INITIALIZER;;		// <-- mutex na queuesClientsList
	pthread_mutex_t	queues_messages_mutex = PTHREAD_MUTEX_INITIALIZER;;		// <-- mutex na queuesMessagesList
	pthread_mutex_t mutex_map = PTHREAD_MUTEX_INITIALIZER;					// <-- mutex na mape z mutexami klientów

	signal(SIGPIPE, SIG_IGN);

	struct sockaddr_in server_addr;

	int server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (server_socket < 0) {
		printf("Error when creating a scoket.\n");
		delete queuesClientsList;
		exit(-1);
	}

	memset(&server_addr, 0, sizeof(struct sockaddr_in));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	int opt = 1;
	setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	if (bind(server_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
		printf("Error when trying to bind IP address and port number to socket.\n");
		delete queuesClientsList;
		delete clients_mutex;
		exit(-1);
	}

	if (listen(server_socket, QUEUE_SIZE) < 0) {
		printf("Error when setting queue size.\n");
		delete queuesClientsList;
		delete clients_mutex;
		exit(-1);
	}

	// std::thread messageCleaner(cleanerThread, std::ref(*queuesMessagesList));
	// messageCleaner.detach();

	pthread_t thread_cleaner;
	CleanerData *c_data = new CleanerData;
	c_data->messageTime = messageTime;
	c_data->queuesMessagesList = queuesMessagesList;
	c_data->queues_messages_mutex = &queues_messages_mutex;

	if (pthread_create(&thread_cleaner, NULL, cleanerThread, (void *)c_data)) {
		printf("Error when trying to create message cleaner thread.\n");
		delete queuesClientsList;
		delete clients_mutex;
		exit(-1);
	}

	while (true) {
		int client = accept(server_socket, NULL, NULL);
		if (client < 0) {
			printf("Error when accepting connection with client.\n");
			delete queuesClientsList;
			delete clients_mutex;
			exit(-1);
		}

		pthread_mutex_lock(&mutex_map);
		clients_mutex->insert(std::pair<int, pthread_mutex_t>(client, PTHREAD_MUTEX_INITIALIZER));
		pthread_mutex_unlock(&mutex_map);

		// Uchwyt na watek
		pthread_t thread_client;

		// Dane, które zostana przekazane do w¹tku
		ThreadData *t_data = new ThreadData;
		t_data->client_descriptor = client;
		t_data->messageTime = messageTime;
		t_data->queuesClientsList = queuesClientsList;
		t_data->queuesMessagesList = queuesMessagesList;
		t_data->clients_mutex = clients_mutex;
		t_data->queues_clients_mutex = &queues_clients_mutex;
		t_data->queues_messages_mutex = &queues_messages_mutex;

		// Stworzenie oddzielnego watku dla kazdego klienta
		int create_result = pthread_create(&thread_client, NULL, clientThread, (void *)t_data);
		if (create_result) {
			printf("Error when trying to create client thread, error code: %d\n", create_result);
			delete queuesClientsList;
			delete clients_mutex;
			exit(-1);
		}
	}

	delete queuesClientsList;
	delete clients_mutex;

	close(server_socket);
}