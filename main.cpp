#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <exception>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <cmath>

#define BUFFER_SIZE 1024
#define CLIENT_KEY 45328
#define SERVER_KEY 54621
#define PORT 6666

class ConnectionException: public std::exception{
public:
    explicit ConnectionException(const char* msg): message(std::string(msg)){}
    const char* what() const noexcept {
        return message.c_str();
    }
private:
    std::string message;
};
struct LoginFailedException: public std::exception{
    const char* what() const noexcept {
        return "Failed to login.";
    }
};
struct SyntaxErrorException: public std::exception{
    const char* what() const noexcept {
        return "Invalid syntax.";
    }
};
struct TimeoutExcpetion: public std::exception{
    const char* what() const noexcept {
        return "Server timeout.";
    }
};
struct WrongLogicException: public std::exception{
    const char* what() const noexcept {
        return "Unexpected message.";
    }
};

int getConnection(int &s){
    struct sockaddr_in remoteAddress;
    socklen_t remoteAddressLength;

    int c = accept(s, (struct sockaddr *) &remoteAddress, &remoteAddressLength);
    if (c < 0) {
        throw ConnectionException("Can't accept new connection.");
    }
    return c;
}
int getSocket(){
    int s = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in address;
    address.sin_family = PF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = INADDR_ANY;

    if (bind(s,(struct sockaddr*) &address, sizeof(address)) != 0){
        throw ConnectionException("Problem with bind()");
    }

    if (listen(s,10) != 0) { //10 - how many clients could be served
        throw ConnectionException("Can't set socket to listen.");
    }
    return s;
}
void validateSockets(fd_set &sockets, struct timeval &timeout, int &c){
    FD_ZERO(&sockets);
    FD_SET(c, &sockets);
    if (select(c + 1, &sockets, NULL, NULL, &timeout) <
        0) { //is called before recv of each message, first argument: last FD +1
        throw ConnectionException("Error in select.");
    }
    if (!FD_ISSET(c, &sockets)) {
        throw TimeoutExcpetion();
    }
}
void sendData(int &c, std::string &msg){
    msg += "\a\b";
    int res = send(c, msg.c_str(), strlen(msg.c_str()), 0);
    if(res<=0){
        throw ConnectionException("Can't send");
    }
}
void sendData(int &c, const char *msg){
    std::string msg_str(msg);
    sendData(c, msg_str);
}
std::string getData(int &c){
    char buffer[BUFFER_SIZE];
    int bytesRecieved = recv(c, &buffer, BUFFER_SIZE - 1, 0);
    if (bytesRecieved <= 0) {
        throw ConnectionException("Can't read.");
    }
    buffer[bytesRecieved] = '\0';

    std::string res(buffer);
    if(res[res.length()-1] != '\b' || res[res.length()-2] != '\a')
        throw SyntaxErrorException();
    return res.substr(0, res.length()-2);
}


bool auth(int &c){
    std::string buffer = getData(c);
    int letterHash = 0;
    for (auto i: buffer) {
        letterHash+= (int)i;
    }
    letterHash *= 1000;
    int serverHash = (letterHash + SERVER_KEY) % 65536;
    int expectedClientHash = (letterHash + CLIENT_KEY)% 65536;
    std::string msg = std::to_string(serverHash);
    sendData(c, msg);

    std::string response = getData(c);
    if(expectedClientHash != std::stoi(response)) {
        sendData(c, "300 LOGIN FAILED");
        throw LoginFailedException();
    }
    sendData(c, "200 OK");
    return true;
}
std::pair<int, int> getCoordinates(std::string &data){
    std::vector<std::string> content;
    std::string token;
    std::stringstream ss(data);

    while(ss >> token) {
        content.push_back(token);
        std::cout << token << std::endl;
    }

    if(content[0] != "OK" || content.size() != 3) {
        throw SyntaxErrorException();
    }
    try {
        return std::make_pair(stoi(content[1]), stoi(content[2]));
    }
    catch (std::invalid_argument &e) {
        std::cout << e.what() << std::endl;
    }
}
std::string getDirection(std::pair<int, int> &pos1, std::pair<int,int> &pos2 ){
    std::string direction;
    if(pos1.first < pos2.first)
        direction = "Right";
    else if(pos1.first > pos2.first)
        direction = "Left";
    if(pos1.second < pos2.second)
        direction = "Up";
    else if(pos1.second > pos2.second)
        direction = "Down";
    return direction;
}
std::pair<std::pair<int, int>, std::string> getPositions(int &c){
    std::string raw_pos1 = "", raw_pos2="";
    do {
        sendData(c, "102 MOVE");
        raw_pos1 = getData(c);

        sendData(c, "102 MOVE");
        raw_pos2 = getData(c);
    } while(raw_pos1 == raw_pos2);
    std::pair<int, int> pos1, pos2;
    try {
        pos1 = getCoordinates(raw_pos1);
        pos2 = getCoordinates(raw_pos2);
    }
    catch(SyntaxErrorException &e){
        sendData(c, "301 SYNTAX ERROR");
        throw SyntaxErrorException();
    }
    return std::make_pair(pos2, getDirection(pos1, pos2));
}

void rotate(int &c, const std::string &from, const std::string &dest){
    std::map<std::string, int> directions;
    directions.insert(std::make_pair("Left", 3));
    directions.insert(std::make_pair("Up", 0));
    directions.insert(std::make_pair("Right", 1));
    directions.insert(std::make_pair("Down", 2));

    auto current = directions[from];
    auto final = directions[dest];

    if(current == final)
        return;
    auto diff = final - current;

    std::cout << "to turn from " << from << " to " << dest << " " << abs(diff) << " steps are needed" << std::endl;

    for(int i = 0; i< abs(diff); ++i){
        if(current < final) {
            sendData(c, "104 TURN RIGHT");

        }
        else {
            sendData(c, "103 TURN LEFT");

        }
        std::string msg = getData(c);
    }

}

std::string goStraight(int &c, std::pair<int, int> &pos, bool isY, int maxCoord){
    int curCoord = isY? pos.second: pos.first;
    if(curCoord == maxCoord)
        return "";
    while(curCoord != maxCoord){
        sendData(c, "102 MOVE");
        std::string msg = getData(c);
        pos = getCoordinates(msg);
        if(abs(pos.first) <= 2 && abs(pos.second) <=2 ) {
            sendData(c, "105 GET MESSAGE");
            msg = getData(c);
            if(!msg.empty()) {
                std::cout << msg << std::endl;
                return msg;
            }
        }
        curCoord = isY? pos.second: pos.first;
    }
    return "";
}
void turn (int &c, std::pair<int, int> &pos, bool isRight){
    if(isRight)
        sendData(c, "104 TURN RIGHT");
    else
        sendData(c, "103 TURN LEFT");
    std::string msg = getData(c);
    pos = getCoordinates(msg);
}

double distance (const std::pair<int, int> &point1, const std::pair<int, int> &point2){
    return sqrt(
            pow(point1.first - point2.first, 2) +
            pow(point1.second - point2.second, 2)
            );
}

std::pair<int, int> findClosestCorner(std::pair<int, int> &pos){
    if(pos.first >=0){
        if(pos.second>=0)
            return std::make_pair(2, 2);
        else
            return std::make_pair(2, -2);
    }
    else{
        if(pos.second >= 0)
            return std::make_pair(-2, 2);
        else
            return std::make_pair(-2, -2);
    }
}

std::string moveToClosestCorner(int &c, std::pair<int, int> &pos, std::string &dir){
    auto targetCoords = std::make_pair(-2,-2);//findClosestCorner(pos);
    std::cout << "closest corner = ( " << targetCoords.first << ", " << targetCoords.second << ")" <<std::endl;
    std::string newDir;
    if(pos.first < targetCoords.first){
        newDir = "Right";
    }
    else if (pos.first > targetCoords.first){
        newDir = "Left";
    }

    rotate(c, dir, newDir);
    dir = newDir;
    auto res = goStraight(c, pos, false, targetCoords.first);
    if(res != "")
        return res;

    if(pos.second < targetCoords.second){
        rotate(c, newDir,  "Up");
        dir = "Up";
    }
    else if (pos.second > targetCoords.second){
        rotate(c, newDir,  "Down");
        dir = "Down";
    }
    res = goStraight(c, pos, true, targetCoords.second);
    if(res != "")
        return res;
    else return "";
}

std::string moveByOne(int &c, std::pair<int, int> &pos){
    sendData(c, "102 MOVE");
    std::string msg = getData(c);
    pos = getCoordinates(msg);
    sendData(c, "105 GET MESSAGE");
    msg = getData(c);
    if(!msg.empty()) {
        std::cout << msg << std::endl;
        return msg;
    }
    else return "";
}

std::string move(int &c, std::pair<int, int> &pos, std::string &dir){
    std::string res;
    res = moveToClosestCorner(c, pos, dir);
    if(!res.empty()) {
        return res;
    }
    rotate(c, dir, "Up");
    int i = 0;
    while(true) {
        auto msgFound = goStraight(c, pos, true, 2);
        if (!msgFound.empty())
            return msgFound;
        rotate(c, "Up", "Right");
        msgFound = moveByOne(c, pos);

        if (!msgFound.empty())
            return msgFound;
        rotate(c, "Right", "Down");
        msgFound = goStraight(c, pos, true, -2);
        if (!msgFound.empty())
            return msgFound;
        rotate(c, "Down", "Right");
        msgFound = moveByOne(c, pos);
        if (!msgFound.empty())
            return msgFound;
        rotate(c, "Right", "Up");
    }
}


int main(int argc, char** argv) {
    int s = 0;
    try {
        s  = getSocket();
    }
    catch (std::exception &e){
        std::cout << e.what() << std::endl;
        return 1;
    }

    while (1) {
        int c = 0;
        try {
            c = getConnection(s);
        }
        catch (std::exception &e){
            std::cout << e.what() << std::endl;
            close(c);
            return 1;
        }


        pid_t  pid = fork(); //copy of actual process
        if (pid == 0) { //copy of process, does not have child
            close(s); //do not need listening socket in the copy
            struct timeval timeout;
            timeout.tv_sec = 10;
            timeout.tv_usec = 0;
            fd_set sockets;

            try {
                validateSockets(sockets, timeout, c);
                auth(c);
                auto direction = getPositions(c);
                auto msg = move(c, direction.first, direction.second);
                sendData(c, "106 LOGOUT");
            }
            catch (std::exception &e){
                std::cout << e.what() << std::endl;
                close(c);
                return 1;
            }

            close(c);
        }

        int status = 0;
        waitpid(0, &status, WNOHANG);
        close(c);
    }

    close(s);
    return 0;
}
