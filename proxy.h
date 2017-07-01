
#ifndef PROXY_PROXY_H
#define PROXY_PROXY_H

#include <queue>
#include <memory>
#include <iostream>
#include "socket.h"

using namespace std;

enum class Protocol {
    HTTP, HTTPS
};

static map<Protocol, const string> defaultPorts{{Protocol::HTTP,  "80"},
                                                {Protocol::HTTPS, "443"}};

class Proxy {
    class DataStorage {
        queue<unique_ptr<char[]>> data;
        const int dataSize;

    public:
        DataStorage(int pullSize, int dataSize) : dataSize(dataSize) {
            for (int i = 0; i < pullSize; ++i) {
                data.push(make_unique<char[]>(dataSize));
            }
        }

        char *pull() {
            unique_ptr<char[]> answer;

            if (data.size() > 0) {
                answer.reset(data.front().release());
                data.pop();
            } else {
                answer = make_unique<char[]>(dataSize);
            }

            return answer.release();
        }

        void release(char *ptr) {
            data.push(unique_ptr<char[]>(ptr));
        }

    } dataStorage;

    struct Node {
        unique_ptr<char> buffer;
        Node *peer;
        unsigned size = 0, shift = 0;
        string port, address;
        SocketWrap socket;
        bool isClient, untilEnd = false, crutch = false;
        DataStorage *ds;

        Node(DataStorage &storage, bool isClient) : buffer(storage.pull()), peer(nullptr), isClient(isClient),
                                                    ds(&storage) {}

        ~Node() {
            if (isClient) {
                cout << "Client drop";
                if (address != "") {
                    cout << address;
                }
                cout << "\n";
            } else {
                cout << "Server drop\n";
            }
            ds->release(buffer.release());
            if (!crutch) {
                socket.close();
            }
        }
    };

    list <unique_ptr<Node>> connectedClients;
    //pain
    map<string, vector<pair<unique_ptr<Node>, unique_ptr<Node>>>> servedNodes;
    Server server;

    void onReadSlot(Socket &socket);

    void onWriteSlot(Socket &socket);

    void onListenSlot(Socket &socket);

    void onErrorSlot(Socket &socket);

public:
    static const unsigned BUFFER_SIZE = 10 * 1024 * 1024, STARTED_POOL = 10;

    Proxy() : dataStorage(STARTED_POOL, BUFFER_SIZE) {
        server.setSlot(boost::bind(&Proxy::onListenSlot, this, _1), socketMode::toListen);
        server.setSlot(boost::bind(&Proxy::onReadSlot, this, _1), socketMode::toRead);
        server.setSlot(boost::bind(&Proxy::onWriteSlot, this, _1), socketMode::toWrite);
        server.setErrorSlot(boost::bind(&Proxy::onErrorSlot, this, _1));
    }

    void listen(const string &port, Protocol protocol) {
        server.listen(port, (void *) (&defaultPorts[protocol]));
    }

    void run(const string &httpPort, const string &httpsPort) {
        listen(httpPort, Protocol::HTTP);
        listen(httpsPort, Protocol::HTTPS);
        server.run();
    }

    void connect(Node &node) {
        cout << "Connect to " + node.address + "\n";
        unique_ptr<Node> tmpPtr;
        SocketWrap socketWrap;

        try {
            tmpPtr = make_unique<Node>(dataStorage, false);
            socketWrap = server.connect(node.address, node.port, socketMode::toReadAndWrite, tmpPtr.get());
        } catch (...) {
            onErrorSlot(node.socket.toSocket());
        }

        tmpPtr->socket = socketWrap;

        for (auto listIter = connectedClients.begin(); listIter != connectedClients.end(); ++listIter) {
            if (listIter->get() == &node) {
                listIter->release();
                connectedClients.erase(listIter);
                break;
            }
        }


        Node *serverPtr = tmpPtr.get();
        servedNodes[node.address].push_back(
                pair<unique_ptr<Node>, unique_ptr<Node>>(unique_ptr<Node>(&node), move(tmpPtr)));
        node.socket.setMode(socketMode::toReadAndWrite);

        if (node.port != defaultPorts[Protocol::HTTP]) {
            string resp = "HTTP/1.1 200 Connection established\r\n\r\n";
            copy(resp.c_str(), resp.c_str() + resp.length(), serverPtr->buffer.get());
            serverPtr->size = resp.length();
            node.size = 0;
        }
        serverPtr->peer = &node;
        node.peer = serverPtr;

    }

    ~Proxy() {
    }

};


void Proxy::onReadSlot(Socket &socket) {
    Node *ptr = socket.getData<Node>();

    //In this case, we don't know on which address we should forward the request
    if (ptr->peer == nullptr) {

        try {
            ptr->size += socket.read(ptr->buffer.get() + ptr->size, BUFFER_SIZE - ptr->size);
        } catch (...) {
            onErrorSlot(socket);
            return;
        }


        if (socket.getState() != socketState::open) {
            onErrorSlot(socket);
            return;
        }

        string tmpString(ptr->buffer.get(), ptr->size);

        unsigned long start, end;
        //Check if we got full destination address
        if (tmpString.length() > 4 && tmpString.substr(tmpString.length() - 4) == "\r\n\r\n" &&
            (start = tmpString.find("Host: ")) != string::npos &&
            (end = tmpString.find("\r\n", start)) != string::npos) {
            start += 6;
            string destinationAddress = tmpString.substr(start, end - start);
            if ((start = destinationAddress.find(":")) != string::npos) {
                ptr->address = destinationAddress.substr(start + 1);
                destinationAddress = destinationAddress.substr(0, start);
            }
            ptr->address = destinationAddress;

            connect(*ptr);
        }

    } else {
        cout << "Read from " + ptr->address + " | " + ptr->peer->address + "\n";
        char *start;
        unsigned size, initial_size = ptr->size;
        if (ptr->shift + ptr->size >= BUFFER_SIZE) {
            start = ptr->buffer.get() + (ptr->shift + ptr->size - BUFFER_SIZE);
            size = BUFFER_SIZE - ptr->size;
        } else {
            start = ptr->buffer.get() + ptr->shift;
            size = BUFFER_SIZE - ptr->shift - ptr->size;
        }

        try {
            ptr->size += socket.read(start, size);
        } catch (...) {
            onErrorSlot(socket);
        }

        if (socket.getState() != socketState::open) {
            onErrorSlot(socket);
        } else {
            if (initial_size == 0 && ptr->size != 0) {
                ptr = ptr->peer;
                if (ptr->socket.getMode() == socketMode::none || ptr->socket.getMode() == socketMode::toRead) {
                    socketMode current_mode = (ptr->socket.getMode() == socketMode::none) ? socketMode::toWrite
                                                                                          : socketMode::toReadAndWrite;
                    ptr->socket.setMode(current_mode);
                }
            } else if (ptr->size == BUFFER_SIZE) {
                socketMode current_mode = (socket.getMode() == socketMode::toRead) ? socketMode::none
                                                                                   : socketMode::toWrite;
                socket.setMode(current_mode);
            }
        }
    }
}

void Proxy::onWriteSlot(Socket &socket) {
    Node *ptr = socket.getData<Node>();
    ptr = ptr->peer;


    cout << "Write from " + ptr->address + " | " + ptr->peer->address + "\n";
    char *start = ptr->buffer.get() + ptr->shift;
    unsigned size, initial_size = ptr->size;
    if (ptr->shift + ptr->size >= BUFFER_SIZE) {
        size = BUFFER_SIZE - ptr->shift;
    } else {
        size = ptr->size;
    }

    unsigned tmp = 0;
    try {
        tmp = socket.write(start, size);
    } catch (...) {
        onErrorSlot(socket);
    }

    ptr->shift += tmp;
    ptr->size -= tmp;
    if (ptr->shift >= BUFFER_SIZE) {
        ptr->shift = 0;
    }

    if (socket.getState() != socketState::open) {
        onErrorSlot(socket);
    } else {
        if (ptr->size == 0) {
            if (ptr->peer->untilEnd) {
                onErrorSlot(socket);
                return;
            }
            socketMode current_mode = (socket.getMode() == socketMode::toWrite) ? socketMode::none : socketMode::toRead;
            socket.setMode(current_mode);
        } else if (initial_size == BUFFER_SIZE && ptr->size != BUFFER_SIZE) {
            socketMode current_mode = (ptr->socket.getMode() == socketMode::none ||
                                       ptr->socket.getMode() == socketMode::toRead) ? socketMode::toRead
                                                                                    : socketMode::toReadAndWrite;
            ptr->socket.setMode(current_mode);
        }
    }
}

void Proxy::onListenSlot(Socket &socket) {
    std::vector<SocketWrap> accepted;
    try {
        accepted = socket.accept(0);
    } catch (...) {
        return;
    }
    for_each(accepted.begin(), accepted.end(), [this, &socket](SocketWrap socketWrap) {

        connectedClients.push_back(make_unique<Node>(dataStorage, true));
        socketWrap.setMode(socketMode::toRead);
        socketWrap.setData(connectedClients.back().get());
        connectedClients.back()->socket = socketWrap;
        connectedClients.back()->port = *socket.getData<string>();
    });
}


void Proxy::onErrorSlot(Socket &socket) {
    Node *ptr = socket.getData<Node>();

    if (ptr->isClient) {
        if (ptr->peer == nullptr) {
            for (auto listIter = connectedClients.begin(); listIter != connectedClients.end(); ++listIter) {
                if (listIter->get() == ptr) {
                    connectedClients.erase(listIter);
                    break;
                }
            }
        } else {

            auto iter = servedNodes.find(ptr->address);
            for (auto listIter = iter->second.begin(); listIter != iter->second.end(); ++listIter) {
                if (listIter->first.get() == ptr) {
                    if (ptr->untilEnd) {
                        iter->second.erase(listIter);
                    } else {
                        ptr->crutch = true;
                        ptr->socket.close();
                        listIter->second->untilEnd = true;
                        listIter->second->socket.setMode(socketMode::toWrite);
                    }
                    break;
                }
            }
        }
    } else {
        auto iter = servedNodes.find(ptr->peer->address);
        for (auto listIter = iter->second.begin(); listIter != iter->second.end(); ++listIter) {
            if (listIter->second.get() == ptr) {
                if (ptr->untilEnd) {
                    iter->second.erase(listIter);
                } else {
                    ptr->crutch = true;
                    ptr->socket.close();
                    listIter->first->untilEnd = true;
                    listIter->first->socket.setMode(socketMode::toWrite);
                }
                break;
            }
        }
    }
}

#endif //PROXY_PROXY_H
