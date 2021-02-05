# SK2_2
Publisch-subscribe type message broker

Klient:

Kompilacja:

g++ client.cpp mqlib.cpp -pthread -Wall -o client

chmod +x client

Argumenty:

adres_serwera numer_portu typ_użytkownika(publisher/subscriber)

Np:

./client localhost 1234 subscriber

Serwer:

Kompilacja:

g++ server.cpp -pthread -Wall -o server

chmod +x server

Argumenty:

numer_portu czas_życia_wiadomości(w sekundach)

Np:

./server 1234 20
