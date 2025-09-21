#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <wayland-client.h>
#include <stdint.h>
#include "xdg-shell-client-protocol.h"

#define WIDTH 800
#define HEIGHT 600

uint32_t *pixel_data = NULL;
size_t shm_size = 0;
struct wl_display *display = NULL;
struct wl_compositor *compositor = NULL;
struct wl_shm *shm = NULL;
struct wl_surface *surface = NULL;
struct xdg_wm_base *xdg_wm_base = NULL;
struct xdg_surface *xdg_surface = NULL;
struct xdg_toplevel *xdg_toplevel = NULL;
struct wl_shm_pool *shm_pool = NULL;
struct wl_buffer *buffer = NULL;

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void registry_handler(
    void *data,
    struct wl_registry *registry,
    uint32_t id,
    const char *interface,
    uint32_t version) {

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        xdg_wm_base = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(xdg_wm_base, &wm_base_listener, NULL);
    }
}

static void registry_remover(void *data, struct wl_registry *registry, uint32_t id) {
    // Do nothing
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handler,
    .global_remove = registry_remover
};

int create_shm_file(size_t size) {
    char filename[] = "/tmp/wayland-shm-XXXXXX";
    int fd = mkstemp(filename);
    if (fd < 0) {
        perror("mkstemp failed");
        exit(1);
    }
    unlink(filename); // Unlink so it is removed after close
    if (ftruncate(fd, size) < 0) {
        perror("ftruncate failed");
        close(fd);
        exit(1);
    }
    return fd;
}

void create_buffer() {
    size_t stride = WIDTH * 4;
    shm_size = stride * HEIGHT;

    int fd = create_shm_file(shm_size);
    pixel_data = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixel_data == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    // Fill with white
    for (int i = 0; i < WIDTH * HEIGHT; ++i) {
        pixel_data[i] = 0xFFFFFFFF; // white
    }

    shm_pool = wl_shm_create_pool(shm, fd, shm_size);
    buffer = wl_shm_pool_create_buffer(shm_pool, 0,
                                       WIDTH, HEIGHT,
                                       stride,
                                       WL_SHM_FORMAT_ARGB8888);

    close(fd); // fd no longer needed after pool creation
}


int main() {
    display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to Wayland display.\n");
        return -1;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    if (!compositor || !shm || !xdg_wm_base) {
        fprintf(stderr, "Missing a required global interface.\n");
        return -1;
    }

    surface = wl_compositor_create_surface(compositor);
    xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, surface);
    xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_set_title(xdg_toplevel, "Screensaver");

    wl_surface_commit(surface);

    create_buffer();

    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, WIDTH, HEIGHT);
    wl_surface_commit(surface);

    long int i = 0;
    uint32_t color = 0;
    while (1) {
      int event = wl_display_dispatch(display);
      if (event == -1) {
        exit(0);
      }

      // Set pixel color
      pixel_data[i] = color++ | 0xFF000000;
      wl_surface_damage_buffer(surface, 0, 0, WIDTH, HEIGHT);
      wl_surface_attach(surface, buffer, 0, 0);
      wl_surface_commit(surface);
      i = (i + 1) % (WIDTH * HEIGHT);
    }

    return 0;
}
