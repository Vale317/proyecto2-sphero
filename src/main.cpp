#include <SDL2/SDL.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>

#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

constexpr int WIDTH = 640;
constexpr int HEIGHT = 480;
constexpr int CHANNEL = 1;

struct Color {
    std::uint8_t red = 40;
    std::uint8_t green = 40;
    std::uint8_t blue = 40;
};

bool make_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    return flags >= 0 &&
           fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

int create_server()
{
    int server = socket(
        AF_BLUETOOTH,
        SOCK_STREAM,
        BTPROTO_RFCOMM
    );

    if (server < 0)
        return -1;

    sockaddr_rc address{};
    address.rc_family = AF_BLUETOOTH;
    address.rc_channel = CHANNEL;

    // Zero-initialized Bluetooth address means any local adapter.
    if (bind(server,
             reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) < 0 ||
        listen(server, 1) < 0 ||
        !make_nonblocking(server)) {
        int saved_errno = errno;
        close(server);
        errno = saved_errno;
        return -1;
    }

    return server;
}

void print_packet(const std::vector<std::uint8_t>& packet,
                  std::size_t length)
{
    std::cout << "RX: ";

    for (std::size_t i = 0; i < length; ++i)
        std::printf("%02X ", static_cast<unsigned>(packet[i]));

    std::cout << '\n';
}

void process_packets(std::vector<std::uint8_t>& buffer,
                     Color& color,
                     SDL_Window* window)
{
    while (buffer.size() >= 2) {
        // Locate the header emitted by the previous arm.s.
        if (buffer[0] != 0xFF || buffer[1] != 0xFE) {
            buffer.erase(buffer.begin());
            continue;
        }

        if (buffer.size() < 6)
            return;

        // DLEN includes payload and checksum.
        unsigned data_length = buffer[5];

        if (data_length == 0) {
            buffer.erase(buffer.begin());
            continue;
        }

        std::size_t packet_length = 6 + data_length;

        if (buffer.size() < packet_length)
            return;

        unsigned sum = 0;

        // DID through checksum must sum to FF modulo 256.
        for (std::size_t i = 2; i < packet_length; ++i)
            sum += buffer[i];

        if ((sum & 0xFF) != 0xFF) {
            std::cerr << "Invalid checksum; resynchronizing.\n";
            buffer.erase(buffer.begin());
            continue;
        }

        print_packet(buffer, packet_length);

        if (buffer[2] == 0x02 &&
            buffer[3] == 0x20 &&
            data_length == 5) {
            color.red = buffer[6];
            color.green = buffer[7];
            color.blue = buffer[8];

            char title[160];

            std::snprintf(
                title,
                sizeof(title),
                "Sphero emulator | RGB(%u, %u, %u) | sequence %u",
                static_cast<unsigned>(color.red),
                static_cast<unsigned>(color.green),
                static_cast<unsigned>(color.blue),
                static_cast<unsigned>(buffer[4])
            );

            SDL_SetWindowTitle(window, title);

            std::cout << "LED changed to RGB("
                      << static_cast<unsigned>(color.red) << ", "
                      << static_cast<unsigned>(color.green) << ", "
                      << static_cast<unsigned>(color.blue) << ")\n";
        } else {
            std::cout << "Valid packet, unsupported command.\n";
        }

        buffer.erase(
            buffer.begin(),
            buffer.begin() + packet_length
        );
    }
}

void draw_sphere(SDL_Renderer* renderer, const Color& color)
{
    SDL_SetRenderDrawColor(renderer, 20, 23, 30, 255);
    SDL_RenderClear(renderer);

    constexpr int center_x = WIDTH / 2;
    constexpr int center_y = HEIGHT / 2;
    constexpr int radius = 150;

    // Outline keeps an "off" sphere visible.
    SDL_SetRenderDrawColor(renderer, 110, 115, 125, 255);

    for (int y = -radius - 2; y <= radius + 2; ++y) {
        int half_width = static_cast<int>(
            std::sqrt((radius + 2) * (radius + 2) - y * y)
        );

        SDL_RenderDrawLine(
            renderer,
            center_x - half_width,
            center_y + y,
            center_x + half_width,
            center_y + y
        );
    }

    // Shade a circular surface to give it a spherical appearance.
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            float nx = static_cast<float>(x) / radius;
            float ny = static_cast<float>(y) / radius;
            float distance_squared = nx * nx + ny * ny;

            if (distance_squared > 1.0f)
                continue;

            float nz = std::sqrt(1.0f - distance_squared);

            float light = std::clamp(
                0.25f + 0.75f *
                (-0.35f * nx - 0.45f * ny + 0.82f * nz),
                0.15f,
                1.0f
            );

            SDL_SetRenderDrawColor(
                renderer,
                static_cast<Uint8>(color.red * light),
                static_cast<Uint8>(color.green * light),
                static_cast<Uint8>(color.blue * light),
                255
            );

            SDL_RenderDrawPoint(
                renderer,
                center_x + x,
                center_y + y
            );
        }
    }

    SDL_RenderPresent(renderer);
}

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL initialization: "
                  << SDL_GetError() << '\n';
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Sphero emulator | waiting for Bluetooth",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WIDTH,
        HEIGHT,
        SDL_WINDOW_SHOWN
    );

    SDL_Renderer* renderer = window
        ? SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE)
        : nullptr;

    if (!window || !renderer) {
        std::cerr << "SDL setup: " << SDL_GetError() << '\n';
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    int server = create_server();

    if (server < 0) {
        std::cerr << "Bluetooth server: "
                  << std::strerror(errno) << '\n';
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::cout << "Listening on RFCOMM channel "
              << CHANNEL << ".\n"
              << "Close the window or press Escape to quit.\n";

    int client = -1;
    bool running = true;
    bool redraw = true;

    Color color;
    std::vector<std::uint8_t> buffer;

    while (running) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN &&
                 event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }

            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_EXPOSED) {
                redraw = true;
            }
        }

        if (client < 0) {
            sockaddr_rc remote{};
            socklen_t size = sizeof(remote);

            int incoming = accept(
                server,
                reinterpret_cast<sockaddr*>(&remote),
                &size
            );

            if (incoming >= 0) {
                if (!make_nonblocking(incoming)) {
                    std::cerr << "Cannot configure client socket.\n";
                    close(incoming);
                } else {
                    client = incoming;
                    buffer.clear();

                    char address[19]{};
                    ba2str(&remote.rc_bdaddr, address);

                    std::cout << "Connected: " << address << '\n';
                    SDL_SetWindowTitle(
                        window,
                        "Sphero emulator | connected"
                    );
                }
            } else if (errno != EAGAIN &&
                       errno != EWOULDBLOCK &&
                       errno != EINTR) {
                std::cerr << "Accept: "
                          << std::strerror(errno) << '\n';
                running = false;
            }
        }

        if (client >= 0) {
            std::uint8_t bytes[1024];

            ssize_t count = recv(
                client,
                bytes,
                sizeof(bytes),
                0
            );

            if (count > 0) {
                buffer.insert(
                    buffer.end(),
                    bytes,
                    bytes + count
                );

                process_packets(buffer, color, window);
                redraw = true;
            } else if (count == 0 ||
                       (count < 0 &&
                        errno != EAGAIN &&
                        errno != EWOULDBLOCK &&
                        errno != EINTR)) {
                std::cout << "Disconnected.\n";

                close(client);
                client = -1;
                buffer.clear();

                SDL_SetWindowTitle(
                    window,
                    "Sphero emulator | waiting for Bluetooth"
                );
            }
        }

        if (redraw) {
            draw_sphere(renderer, color);
            redraw = false;
        }

        SDL_Delay(10);
    }

    if (client >= 0)
        close(client);

    close(server);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
