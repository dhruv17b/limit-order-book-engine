#include <cstdio>
#include <cstring>
#include <chrono>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "ob/protocol.hpp"

constexpr int PORT = 9001;
constexpr int N = 100000;   // orders to send

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("connect"); return 1; }

    uint8_t role = 0;                 // submitter
    write(fd, &role, 1);

    auto start = std::chrono::steady_clock::now();

    for (int i = 1; i <= N; ++i) {
        // Alternate buy/sell around a mid price so orders interact.
        Side side = (i % 2) ? Side::Buy : Side::Sell;
        int64_t price = 100 + (i % 5);
        Command cmd{CommandType::New,
            Order{(uint64_t)i, side, OrderType::Limit, price, 10, (uint64_t)i}, 0};

        WireBytes buf = serialize(cmd);
        write(fd, buf.data(), WIRE_SIZE);     // send order

        uint8_t reply = 0;
        read(fd, &reply, 1);                  // wait for ack (this is the bottleneck)
    }

    auto end = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(end - start).count();

    printf("Sent %d orders over the network in %.3f s\n", N, seconds);
    printf("Round-trip throughput: %.0f orders/sec\n", N / seconds);

    close(fd);
    return 0;
}