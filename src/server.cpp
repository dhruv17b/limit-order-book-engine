#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <vector>
#include <algorithm>
#include "ob/order_book.hpp"
#include "ob/protocol.hpp"

constexpr int PORT = 9001;

// A book-update message: 4 fixed fields (best bid, bid qty, best ask, ask qty).
// 32 bytes, same size as a command for simplicity.
void broadcast_top_of_book(const std::vector<int>& subscribers,
                           const OrderBook::TopOfBook& t) {
    WireBytes buf{};
    std::memcpy(&buf[0],  &t.best_bid, 8);
    std::memcpy(&buf[8],  &t.bid_qty,  8);
    std::memcpy(&buf[16], &t.best_ask, 8);
    std::memcpy(&buf[24], &t.ask_qty,  8);
    for (int fd : subscribers)
        write(fd, buf.data(), WIRE_SIZE);
}

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(listen_fd, 8) < 0) { perror("listen"); return 1; }
    printf("Server listening on port %d (select-based)...\n", PORT);

    OrderBook book;
    std::vector<int> clients;        // connected order-submitter sockets
    std::vector<int> subscribers;    // clients subscribed to top-of-book updates

    while (true) {
        // 1. Build the read set fresh each iteration.
        fd_set readset;
        FD_ZERO(&readset);
        FD_SET(listen_fd, &readset);
        int maxfd = listen_fd;
        for (int fd : clients) {
            FD_SET(fd, &readset);
            if (fd > maxfd) maxfd = fd;
        }
        for (int fd : subscribers) {       // watch subscribers too (for disconnect)
            FD_SET(fd, &readset);
            if (fd > maxfd) maxfd = fd;
        }

        // 2. Block until any socket is ready.
        int ready = select(maxfd + 1, &readset, nullptr, nullptr, nullptr);
        if (ready < 0) { perror("select"); break; }

        if (FD_ISSET(listen_fd, &readset)) {
            int client_fd = accept(listen_fd, nullptr, nullptr);
            if (client_fd >= 0) {
                // Read the 1-byte role declaration: 0 = submitter, 1 = subscriber.
                uint8_t role = 0;
                ssize_t n = read(client_fd, &role, 1);
                if (n == 1 && role == 1) {
                    subscribers.push_back(client_fd);
                    printf("Subscriber connected (fd %d). Subscribers: %zu\n",
                           client_fd, subscribers.size());
                } else {
                    clients.push_back(client_fd);
                    printf("Submitter connected (fd %d). Submitters: %zu\n",
                           client_fd, clients.size());
                }
            }
        }

        // 3b. Check each connected client for incoming data.
        //     Iterate over a copy of indices so we can remove safely.
        std::vector<int> to_remove;
        for (int fd : clients) {
            if (!FD_ISSET(fd, &readset)) continue;

            // Read exactly one 32-byte command (framing loop).
            WireBytes buf;
            size_t got = 0;
            bool disconnected = false;
            while (got < WIRE_SIZE) {
                ssize_t n = read(fd, buf.data() + got, WIRE_SIZE - got);
                if (n <= 0) { disconnected = true; break; }
                got += n;
            }
            if (disconnected) {
                printf("Client (fd %d) disconnected.\n", fd);
                close(fd);
                to_remove.push_back(fd);
                continue;
            }

            Command cmd = deserialize(buf);
            if (!is_valid(cmd)) {
                uint8_t reply = 0;          // reject: zero events
                write(fd, &reply, 1);
                continue;                    // skip applying garbage
            }
            std::vector<Event> events = book.apply(cmd);
            uint8_t reply = (uint8_t)events.size();
            write(fd, &reply, 1);
            broadcast_top_of_book(subscribers, book.top_of_book());
        }

        // 4. Remove any disconnected clients from the list.
        for (int fd : to_remove)
            clients.erase(std::remove(clients.begin(), clients.end(), fd), clients.end());
        std::vector<int> subs_remove;
        for (int fd : subscribers) {
            if (!FD_ISSET(fd, &readset)) continue;
            uint8_t tmp;
            ssize_t n = read(fd, &tmp, 1);
            if (n <= 0) {
                printf("Subscriber (fd %d) disconnected.\n", fd);
                close(fd);
                subs_remove.push_back(fd);
            }
        }
        for (int fd : subs_remove)
            subscribers.erase(std::remove(subscribers.begin(), subscribers.end(), fd),
                              subscribers.end());
    }

    close(listen_fd);
    return 0;
}