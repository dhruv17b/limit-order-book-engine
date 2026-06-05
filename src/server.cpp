#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>          // read, write, close
#include <arpa/inet.h>       // sockaddr_in, htons
#include <sys/socket.h>      // socket, bind, listen, accept
#include "ob/order_book.hpp"
#include "ob/protocol.hpp"

constexpr int PORT = 9001;

int main() {
    // 1. socket(): create a TCP socket.
    //    AF_INET = IPv4, SOCK_STREAM = TCP (a reliable byte stream).
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }

    // Allow quick reuse of the port if we restart the server (avoids "address in use").
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 2. bind(): attach the socket to PORT on all local interfaces.
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;      // accept connections to any local IP
    addr.sin_port = htons(PORT);            // htons: host-to-network byte order for the port
    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }

    // 3. listen(): start accepting connections (backlog of 1 pending).
    if (listen(listen_fd, 1) < 0) { perror("listen"); return 1; }
    printf("Server listening on port %d...\n", PORT);

    OrderBook book;   // created ONCE, persists across all clients

    // Outer loop: serve clients one after another, forever.
    while (true) {
        // accept(): block until a client connects.
        int client_fd = accept(listen_fd, nullptr, nullptr);
        if (client_fd < 0) { perror("accept"); continue; }
        printf("Client connected.\n");

        uint64_t commands_handled = 0;

        // Inner loop: read commands from THIS client until it disconnects.
        WireBytes buf;
        while (true) {
            size_t got = 0;
            bool disconnected = false;
            while (got < WIRE_SIZE) {
                ssize_t n = read(client_fd, buf.data() + got, WIRE_SIZE - got);
                if (n <= 0) { disconnected = true; break; }
                got += n;
            }
            if (disconnected) break;

            Command cmd = deserialize(buf);
            std::vector<Event> events = book.apply(cmd);
            commands_handled++;

            uint8_t reply = (uint8_t)events.size();
            write(client_fd, &reply, 1);
        }

        printf("Client disconnected. Handled %llu commands this session.\n",
               (unsigned long long)commands_handled);
        close(client_fd);
        // Loop back to accept() the next client. The book persists.
    }

    // (unreachable in practice — server runs until Ctrl+C)
    close(listen_fd);
    return 0;
}