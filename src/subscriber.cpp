#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "ob/protocol.hpp"

constexpr int PORT = 9001;

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect"); return 1;
    }

    // Declare our role: 1 = subscriber (market-data feed).
    uint8_t role = 1;
    write(fd, &role, 1);
    printf("Subscribed to market data. Waiting for updates...\n");

    // Loop forever: read each 32-byte top-of-book update and display it.
    WireBytes buf;
    while (true) {
        size_t got = 0;
        bool disconnected = false;
        while (got < WIRE_SIZE) {
            ssize_t n = read(fd, buf.data() + got, WIRE_SIZE - got);
            if (n <= 0) { disconnected = true; break; }
            got += n;
        }
        if (disconnected) break;

        // Decode the 4 fields (same layout the server's broadcast wrote).
        int64_t  best_bid, best_ask;
        uint64_t bid_qty, ask_qty;
        std::memcpy(&best_bid, &buf[0],  8);
        std::memcpy(&bid_qty,  &buf[8],  8);
        std::memcpy(&best_ask, &buf[16], 8);
        std::memcpy(&ask_qty,  &buf[24], 8);

        printf("BOOK UPDATE  |  bid: %lld x %llu   |   ask: %lld x %llu\n",
               (long long)best_bid, (unsigned long long)bid_qty,
               (long long)best_ask, (unsigned long long)ask_qty);
    }

    printf("Feed closed.\n");
    close(fd);
    return 0;
}