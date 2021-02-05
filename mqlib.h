#ifndef MSG_Q
#define MSG_Q

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <cstdio>
#include <sys/types.h>
#include <vector>


int connect(std::string, int port);

bool disconnect(int sock);

int subscribe(int sock, std::string queueName);

int unsubscribe(int sock, std::string queueName);

int send(int sock, std::string queueName, std::string message);

std::string receive(int sock);


#endif