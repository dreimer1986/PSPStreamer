/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native nxdk connection preview. No Sony code, no proprietary Microsoft SDK.
 * Not a video player yet: validate the target, controller and authenticated LAN
 * API before selecting/porting a decoder. Passwords never appear on screen. */
#include <hal/debug.h>
#include <hal/video.h>
#include <hal/xbox.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <nxdk/net.h>
#include <lwip/sockets.h>
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static char host[64], password[129];
static unsigned port = 8091;
static char reply[16385];

static int config(void)
{
    FILE *f = fopen("D:\\server.cfg", "r");
    char line[256];
    if (!f) return 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "host=", 5)) snprintf(host, sizeof(host), "%s", line+5);
        else if (!strncmp(line, "password=", 9)) snprintf(password, sizeof(password), "%s", line+9);
        else if (!strncmp(line, "port=", 5)) port = (unsigned)strtoul(line+5, NULL, 10);
    }
    fclose(f);
    struct in_addr address;
    return port > 0 && port <= 65535 && inet_aton(host, &address);
}

static void base64(const unsigned char *input, size_t n, char *out)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    while (n) {
        size_t take = n < 3 ? n : 3;
        unsigned value = (unsigned)input[0] << 16;
        if (take > 1) value |= (unsigned)input[1] << 8;
        if (take > 2) value |= input[2];
        *out++ = alphabet[value >> 18]; *out++ = alphabet[(value >> 12) & 63];
        *out++ = take > 1 ? alphabet[(value >> 6) & 63] : '=';
        *out++ = take > 2 ? alphabet[value & 63] : '=';
        input += take; n -= take;
    }
    *out = 0;
}

static int ready(int fd, int writing, DWORD started)
{
    DWORD elapsed = GetTickCount() - started;
    if (elapsed >= 10000) return 0;
    fd_set set; FD_ZERO(&set); FD_SET(fd, &set);
    struct timeval timeout = { (10000-elapsed)/1000, ((10000-elapsed)%1000)*1000 };
    return select(fd+1, writing ? NULL : &set, writing ? &set : NULL, NULL, &timeout) > 0;
}

static void request(const char *path)
{
    char credentials[140], encoded[192], header[800];
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET; address.sin_port = htons(port);
    inet_aton(host, &address.sin_addr);
    snprintf(credentials, sizeof(credentials), "psp:%s", password);
    base64((unsigned char *)credentials, strlen(credentials), encoded);
    int size = snprintf(header, sizeof(header),
        "GET %s HTTP/1.0\r\nHost: %s:%u\r\nAuthorization: Basic %s\r\nConnection: close\r\n\r\n",
        path, host, port, encoded);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { debugPrint("Socket allocation failed\n"); return; }
    unsigned long nonblocking = 1;
    ioctl(fd, FIONBIO, &nonblocking);
    DWORD started = GetTickCount();
    debugPrint("Request %s (10 second total timeout)\n", path);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0 && errno != EINPROGRESS) goto failed;
    if (!ready(fd, 1, started)) goto failed;
    int error = 0; socklen_t length = sizeof(error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) || error) goto failed;
    int offset = 0;
    while (offset < size) {
        if (!ready(fd, 1, started)) goto failed;
        int n = send(fd, header+offset, size-offset, 0);
        if (n <= 0) goto failed;
        offset += n;
    }
    offset = 0;
    while (offset < (int)sizeof(reply)-1) {
        if (!ready(fd, 0, started)) goto failed;
        int n = recv(fd, reply+offset, sizeof(reply)-1-offset, 0);
        if (n < 0) { if (errno == EWOULDBLOCK) continue; goto failed; }
        if (!n) break;
        offset += n;
    }
    reply[offset] = 0;
    closesocket(fd);
    char *body = strstr(reply, "\r\n\r\n");
    debugPrint("%.40s\n%d bytes in %lu ms%s\n", reply, offset,
               (unsigned long)(GetTickCount()-started), offset == sizeof(reply)-1 ? " (preview truncated)" : "");
    if (body) debugPrint("%.650s\n", body+4);
    return;
failed:
    closesocket(fd);
    debugPrint("Request failed or timed out. Check LAN/config/server.\n");
}

int main(void)
{
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    debugPrint("PSPStreamer Xbox - native CONNECTION PREVIEW\nNot a media player yet. HTTP LAN only.\n\n");
    MM_STATISTICS memory = {0}; memory.Length = sizeof(memory);
    if (!MmQueryStatistics(&memory))
        debugPrint("Reported RAM: %lu MiB\n", (unsigned long)(memory.TotalPhysicalPages/256));
    int configured = config();
    if (!configured) debugPrint("Missing/invalid D:\\server.cfg (IPv4 and port required).\n");
    else debugPrint("Server: %s:%u\n", host, port);
    if (SDL_Init(SDL_INIT_GAMECONTROLLER)) { debugPrint("Controller initialization failed\n"); for (;;) Sleep(1000); }
    SDL_GameController *pads[4] = {0};
    debugPrint("Initializing Ethernet...\n");
    int network = nxNetInit(NULL);
    debugPrint("Network init: %d\nA: health  X: library preview\nBack: dashboard. Config changes require relaunch.\n", network);
    SDL_Event event;
    for (;;) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_CONTROLLERDEVICEADDED) {
                for (int i=0; i<4; ++i) if (!pads[i]) { pads[i] = SDL_GameControllerOpen(event.cdevice.which); break; }
            } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
                for (int i=0; i<4; ++i) if (pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == event.cdevice.which) {
                    SDL_GameControllerClose(pads[i]); pads[i] = NULL;
                }
            } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                    nxNetShutdown(); SDL_Quit(); XReboot();
                }
                if (configured && !network && (event.cbutton.button == SDL_CONTROLLER_BUTTON_A || event.cbutton.button == SDL_CONTROLLER_BUTTON_X)) {
                    debugClearScreen(); debugResetCursor();
                    request(event.cbutton.button == SDL_CONTROLLER_BUTTON_A ? "/api/health" : "/api/library?root=0&path=");
                    debugPrint("\nA: health  X: library  Back: dashboard\n");
                }
            }
        }
        Sleep(16);
    }
}
