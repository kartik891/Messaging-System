#include<string>
#include<poll.h>
#include "../Protocol.h"

class Client{
    private:
    int sockfd;
    std::string username;

    public:
    Client(std::string username);
    int getSockfd();
    bool makeConnection(const char* host, const char* port);
    bool sendMessage(const std::string& message);
    ReceiveStatus receiveMessage(std::string &message);
    void shutdownConnection();
    void closeConnection();
    ~Client();

};