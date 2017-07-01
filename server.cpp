#include "server.h"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <iostream>

using namespace std;

Server::Server() {
    if ((epollFd = epoll_create(100)) < 0) {
        throw runtime_error("Epoll creating is failed.");
    }
}

void Server::epollChange(Socket *socket, socketMode mode) {
    auto modeCurrent = socket->mode;
    unsigned epollMode, epollSocketMode;

    if (modeCurrent == mode) {
        return;
    }

    if (modeCurrent == socketMode::none) {
        epollMode = EPOLL_CTL_ADD;
    } else if (mode == socketMode::none) {
        epollMode = EPOLL_CTL_DEL;
    } else {
        epollMode = EPOLL_CTL_MOD;
    }

    switch (mode) {
        case socketMode::toRead :
        case socketMode::toListen :
            epollSocketMode = EPOLLIN;
            break;
        case socketMode::toWrite :
            epollSocketMode = EPOLLOUT;
            break;
        case socketMode::toReadAndWrite :
            epollSocketMode = EPOLLIN | EPOLLOUT;
    }

    epoll_event event;
    event.events = epollSocketMode;
    event.data.ptr = socket;

    if (epoll_ctl(epollFd, epollMode, socket->fd, &event) < 0) {
        throw runtime_error("Unable to set socket mode");
    }
}

weak_ptr<Socket> Server::addSocket(socketMode mode, socketState state, int fd) {

    servedSockets.push_back(make_shared<Socket>(this, mode, state, fd));

    socketMode modeBuffer = servedSockets.back()->mode;
    servedSockets.back()->mode = socketMode::none;

    try {
        if (state == socketState::connecting) {
            epollChange(servedSockets.back().get(), socketMode::toWrite);
        } else {
            epollChange(servedSockets.back().get(), modeBuffer);
        }
        servedSockets.back()->mode = modeBuffer;
    } catch (...) {
        servedSockets.pop_back();
        throw;
    }
    return weak_ptr<Socket>(servedSockets.back());
}

SocketWrap Server::connect(const string &addres, const string &port, socketMode mode, void *dataPtr) {
    addrinfo *current, *addrArray, hint;
    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(addres.c_str(), port.c_str(), &hint, &addrArray) < 0) {
        throw runtime_error("Cannot find desirable socket");
    }

    int tmpFd;

    socketState tmpSocketState = socketState::close;
    for (current = addrArray; current != nullptr; current = current->ai_next) {
        if ((tmpFd = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol)) < 0) {
            continue;
        }

        if (fcntl(tmpFd, F_SETFL, O_NONBLOCK) < 0) {
            close(tmpFd);
            continue;
        }

        if (::connect(tmpFd, current->ai_addr, current->ai_addrlen) < 0) {
            if (errno == EINPROGRESS) {
                tmpSocketState = socketState::connecting;
            } else {
                close(tmpFd);
                continue;
            }
        } else {
            tmpSocketState = socketState::open;
        }
        break;
    }

    ::freeaddrinfo(addrArray);
    if (current == nullptr) {
        throw runtime_error("Cannot find desirable socket");
    }
    auto tmpSocket = addSocket(mode, tmpSocketState, tmpFd);
    tmpSocket.lock()->dataPtr = dataPtr;
    return SocketWrap(tmpSocket);
}

SocketWrap Server::listen(const string &port, void *dataPtr) {
    addrinfo *current, *addrArray, hint;

    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, port.c_str(), &hint, &addrArray) < 0) {
        throw runtime_error("Cannot find desirable socket");
    }

    int tmpFd;
    for (current = addrArray; current != nullptr; current = current->ai_next) {
        if ((tmpFd = socket(current->ai_family, current->ai_socktype, current->ai_protocol)) < 0) {
            continue;
        }

        if (fcntl(tmpFd, F_SETFL, O_NONBLOCK) < 0) {
            close(tmpFd);
            continue;
        }

        if (bind(tmpFd, current->ai_addr, current->ai_addrlen) < 0) {
            close(tmpFd);
            continue;
        }

        if (::listen(tmpFd, Server::listenBacklog) < 0) {
            close(tmpFd);
            continue;
        }

        break;
    }

    freeaddrinfo(addrArray);
    if (current == nullptr) {
        throw runtime_error("Cannot find desirable socket");
    }

    auto tmpSocket = addSocket(socketMode::toListen, socketState::open, tmpFd);
    tmpSocket.lock()->dataPtr = dataPtr;
    return SocketWrap(tmpSocket);
}

void Server::setSlot(const slotType &slot, socketMode mode) {
    signalsHolder[mode].connect(slot);
}

void Server::removeSlot(const slotType &slot, socketMode mode) {
    //signalsHolder[mode].disconnect(slot);
    signalsHolder[mode].disconnect_all_slots();
}

void Server::setErrorSlot(const slotType &slot) {
    errorSignalHolder.connect(slot);
}

void Server::removeErrorSlot(const slotType &slot) {
    //errorSignalHolder.disconnect(slot);
    errorSignalHolder.disconnect_all_slots();
}

void Server::run(int timeOut) {
    int eventCount;
    for (;;) {
        epoll_event events[servedSockets.size()];
        if ((eventCount = epoll_wait(epollFd, events, servedSockets.size(), timeOut)) <= 0) {
            if (errno == EINTR) {
                run(timeOut);
                return;
            }

            if (eventCount == 0) {
                return;
            }

            throw runtime_error("Epoll wait error occurred.");
        }

        for (int i = 0; i < eventCount; ++i) {
            try {
                auto currentEvent = events[i];

                Socket *dataPtr = (Socket *) currentEvent.data.ptr;
                if (dataPtr->state != socketState::close && currentEvent.events & EPOLLIN) {
                    if (dataPtr->mode == socketMode::toListen) {
                        signalsHolder[socketMode::toListen](*dataPtr);
                    } else {
                        signalsHolder[socketMode::toRead](*dataPtr);
                    }
                }
                if (dataPtr->state != socketState::close && currentEvent.events & EPOLLOUT) {
                    if (dataPtr->state == socketState::connecting) {
                        dataPtr->state = socketState::open;
                        socketMode modeBuffer = dataPtr->mode;
                        dataPtr->mode = socketMode::toWrite;
                        epollChange(dataPtr, modeBuffer);
                        dataPtr->mode = modeBuffer;
                    } else {
                        signalsHolder[socketMode::toWrite](*dataPtr);
                    }
                }
                if (dataPtr->state != socketState::close && currentEvent.events & EPOLLERR) {
                    dataPtr->state = socketState::error;
                    errorSignalHolder(*dataPtr);
                }
            } catch (...) {
                continue;
            }
        }
        try {
            if (signalsHolder.find(socketMode::none) != signalsHolder.end()) {
                for (auto iter = servedSockets.begin(); iter != servedSockets.end(); ++iter) {
                    if ((**iter).state != socketState::close && iter->get()->mode == socketMode::none) {
                        signalsHolder[socketMode::none](*iter->get());
                    }
                }
            }
        } catch (...) {
            //ignore exceptions from slots
        }
        for (auto currentPtr = toRemoveList.begin(); currentPtr != toRemoveList.end(); ++currentPtr) {
            auto it = std::find_if(servedSockets.begin(), servedSockets.end(),
                                   [&currentPtr](const std::shared_ptr<Socket> &ptr) {
                                       return *currentPtr == ptr.get();
                                   });
            servedSockets.erase(it);
        }
        toRemoveList.clear();
    }
}

Server::~Server() {
    servedSockets.clear();
    close(epollFd);
}

void Server::needToRemove(Socket *socket) {
    toRemoveList.push_back(socket);
}

