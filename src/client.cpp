#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "ob/protocol.hpp"

constexpr int PORT = 9001;

// Send one command and read the 1-byte reply (event count).
void send_command(int fd, const Command& cmd) {
    WireBytes buf = serialize(cmd);
    write(fd, buf.data(), WIRE_SIZE);

    uint8_t reply = 0;
    read(fd, &reply, 1);
    printf("  -> server produced %u events\n", reply);
}

int main() {
    // socket(): same as server — create a TCP socket.
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    // Describe who to connect to: 127.0.0.1 (this machine) on PORT.
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);  // 127.0.0.1 = localhost

    // connect(): reach out to the server (the client-side counterpart to accept()).
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect"); return 1;
    }
    printf("Connected to server.\n");

    // Send a small scenario: rest two sells, then a crossing buy.
    printf("Sending sell 10 @ 100:\n");
    send_command(fd, Command{CommandType::New,
        Order{1, Side::Sell, OrderType::Limit, 100, 10, 1}, 0});

    printf("Sending sell 5 @ 101:\n");
    send_command(fd, Command{CommandType::New,
        Order{2, Side::Sell, OrderType::Limit, 101, 5, 2}, 0});

    printf("Sending buy 12 @ 101 (should cross both):\n");
    send_command(fd, Command{CommandType::New,
        Order{3, Side::Buy, OrderType::Limit, 101, 12, 3}, 0});

    close(fd);
    return 0;
}