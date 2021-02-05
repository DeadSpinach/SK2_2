#include "mqlib.h"


/* Funkcja connect laczy klienta z serwerem o podanym adresie IP i porcie.
 *
 * Funkcja zwraca nr socketu gdy operacja się powiedzie lub:
 *  -1 gdy wystapi blad w funkcji gethostbyname
 *  -2 gdy wystapi blad w funkcji socket
 *  -3 gdy wystapi blad w funkcji connect
 *  -4 gdy polaczenie sie udalo, ale serwer nie odpowiedzial z wiadomoscia inicjujaca
 */
int connect(std::string address, int port)
{
	int sock;
	int connect_result;
	struct sockaddr_in server_address;
	struct hostent* server_host_entity;

	server_host_entity = gethostbyname(address.c_str());
	if (!server_host_entity)
	{
		std::cout << "Error when getting server address." << std::endl;
		return -1;
	}

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
	{
		std::cout << "Error when creating a socket." << std::endl;
		return -2;
	}

	memset(&server_address, 0, sizeof(struct sockaddr));
	server_address.sin_family = AF_INET;
	memcpy(&server_address.sin_addr.s_addr, server_host_entity->h_addr, server_host_entity->h_length);
	server_address.sin_port = htons(port);

	connect_result = connect(sock, (struct sockaddr*)&server_address, sizeof(struct sockaddr));
	if (connect_result < 0)
	{
		std::cout << "Problem with connecting to server (" << address << ":" << port << ")." << std::endl;
		return -3;
	}

	char buffer[9];
	int received = read(sock, buffer, 9);
	std::string response(buffer, 9);

	if (strcmp(response.c_str(), "CONNECTED") != 0 || received != 9) {
		std::cout << "Server didn't respond." << std::endl;
		close(sock);
		return -4;
	}
	else {
		std::cout << "Successfully connected to server." << std::endl;
	}

	return sock;
}


/* Funkcja disconnect wysyla do serwera wiadomosc o rozlaczeniu i zamyka socket.
 *
 * Funkcja zwraca wartosc true gdy operacja się powiedzie lub zwraca false w przeciwnym wypadku.
 */
bool disconnect(int sock)
{
	std::string request = "004QUIT";

	int currSend = 0;
	int totalSend = 0;

	while(totalSend < 7) {
		currSend = write(sock, request.c_str(), 7);
		if(currSend < 0) {
			std::cout << "Error when trying to disconnect." << std::endl;
			close(sock);
			return false;
		}
		totalSend += currSend;
	}

	close(sock);

	return true;
}

/* Funkcja send wysyla do serwera wiadomosc w danej kolejce.
 *
 * Funkcja zwraca wartosc 1 gdy operacja się powiedzie lub:
 *  -1 gdy nazwa kolejki lub tresc wiadomosci jest za dluga
 *  -2 gdy nazwa kolejki lub tresc wiadomosci zawiera znak '|'
 *  -3 gdy nazwa kolejki lub tresc wiadomosci jest pustym stringiem 
 *  -4 gdy wystapi blad w funkcji write
 */
int send(int sock, std::string queueName, std::string message) 
{
	if(queueName.size() > 50) {
		std::cout << "Queue name is too long (max 50 characters)." << std::endl;
		return -1;
	}

	if(message.size() > 500) {
		std::cout << "Message is too long (max 500 characters)." << std::endl;
		return -1;
	}

	if(queueName.find('|') != std::string::npos) {
		std::cout << "Queue name contains forbidden character '|'." << std::endl;
		return -2;
	}

	if(message.find('|') != std::string::npos) {
		std::cout << "Message contains forbidden character '|'." << std::endl;
		return -2;
	}

	if(queueName.empty() || message.empty()) {
		std::cout << "Queue name and message cannot be empty." << std::endl;
		return -3;
	}

	std::string messageToSend = "MESSAGE|" + queueName + "|" + message;
	int messageSize = messageToSend.size();

	if (messageSize > 100)
		messageToSend = std::to_string(messageSize) + messageToSend;
	else if (messageSize > 10)
		messageToSend = "0" + std::to_string(messageSize) + messageToSend;
	else
		messageToSend = "00" + std::to_string(messageSize) + messageToSend;

	int currSend = 0;
	int totalSend = 0;

	while(totalSend < (int)messageToSend.size()) {
		currSend = write(sock, messageToSend.c_str(), messageToSend.size());
		if(currSend < 0) {
			std::cout << "Error when sending message to server.";
			return -4;
		}
		totalSend += currSend;
	}

	return 1;
}


/* Funkcja receive odbiera od serwera wiadomosc.
 * 
 * Funkcja zwraca string zawierający nazwe kolejki i tresc wiadomosci gdy operacja się powiedzie lub
 * w przeciwnym wypadku pusty string.
 */
std::string receive(int sock)
{
	char messageLen[3] = "";
	char message[580] = "";

	// Odczytanie dlugosci wiadomosci
	int totalLength = 0;
	int currentLength = 0;
	std::string messageLenStr = "";

	while (totalLength < 3) {
		currentLength = read(sock, messageLen, 3-totalLength);
		if (currentLength < 0) {
			std::cout << "Error when receiving message length from server." << std::endl;
			return std::string();
		}
		totalLength += currentLength;
		messageLenStr.append(messageLen, 0, currentLength);
	}

	// Odczytanie wiadomosci o danej dlugosci
	int charsToRead = atoi(messageLenStr.c_str());
	int totalCount = 0;
	int currentCount = 0;
	std::string messageStr = "";

	while (totalCount < charsToRead) {
		currentCount = read(sock, message, charsToRead-totalCount);
		if (currentCount < 0) {
			std::cout << "Error when receiving message from server." << std::endl;
			return std::string();
		}
		totalCount += currentCount;
		messageStr.append(message, 0, currentCount);
	}

	// Dodanie poszczegolnych czesci wiadomosci do wektora
	std::string delimiter = "|";
	std::vector<std::string> messageVector;

	auto start = 0U;
	auto end = messageStr.find(delimiter);
	while (end != std::string::npos) {
		messageVector.push_back(messageStr.substr(start, end - start));
		start = end + delimiter.length();
		end = messageStr.find(delimiter, start);
	}
	messageVector.push_back(messageStr.substr(start, end));

	return std::string("QUEUE: " + messageVector[1] + ", MESSAGE: " + messageVector[2]);
}

/* Funkcja subscribe wysyla do serwera request o zapisanie sie do odbioru wiadomosci z danej kolejki. 
 *
 * Funkcja zwraca wartosc 1 gdy operacja się powiedzie lub:
 *  -1 gdy nazwa kolejki jest za dluga (> 50 znakow)
 *  -2 gdy nazwa kolejki zawiera znak '|'
 *  -3 gdy nazwa kolejki jest pustym stringiem 
 *  -4 gdy wystapi blad w funkcji write
 */
int subscribe(int sock, std::string queueName) 
{
	if(queueName.size() > 50) {
		std::cout << "Queue name is too long (max 50 characters)." << std::endl;
		return -1;
	}

	if(queueName.find('|') != std::string::npos) {
		std::cout << "Queue name contains forbidden character '|'." << std::endl;
		return -2;
	}

	if(queueName.empty()) {
		std::cout << "Queue name cannot be empty." << std::endl;
		return -3;
	}

	std::string request = "SUBSCRIBE|" + queueName;

	int requestSize = request.size();
	request = "0" + std::to_string(requestSize) + request;

	int currSend = 0;
	int totalSend = 0;

	while(totalSend < (int)request.size()) {
		currSend = write(sock, request.c_str(), request.size());
		if(currSend < 0) {
			std::cout << "Error when sending request to server.";
			return -4;
		}
		totalSend += currSend;
	}

	return 1;
}


/* Funkcja unsubscribe wysyla do serwera request o wypisanie sie z odbioru wiadomosci z danej kolejki. 
 *
 * Funkcja zwraca wartosc 1 gdy operacja się powiedzie lub:
 *  -1 gdy nazwa kolejki jest za dluga (> 50 znakow)
 *  -2 gdy nazwa kolejki zawiera znak '|'
 *  -3 gdy nazwa kolejki jest pustym stringiem 
 *  -4 gdy wystapi blad w funkcji write
 */
int unsubscribe(int sock, std::string queueName)
{
	if(queueName.size() > 50) {
		std::cout << "Queue name is too long (max 50 characters)." << std::endl;
		return -1;
	}

	if(queueName.find('|') != std::string::npos) {
		std::cout << "Queue name contains forbidden character '|'." << std::endl;
		return -2;
	}

	if(queueName.empty()) {
		std::cout << "Queue name cannot be empty." << std::endl;
		return -3;
	}

	std::string request = "UNSUBSCRIBE|" + queueName;

	int requestSize = request.size();
	request = "0" + std::to_string(requestSize) + request;

	int currSend = 0;
	int totalSend = 0;

	while(totalSend < (int)request.size()) {
		currSend = write(sock, request.c_str(), request.size());
		if(currSend < 0) {
			std::cout << "Error when sending request to server.";
			return -4;
		}
		totalSend += currSend;
	}

	return 1;
}