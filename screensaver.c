#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <wayland-client-core.h>
#include <wayland-client.h>
#include <stdint.h>
#include <wayland-util.h>
#include "xdg-shell-client-protocol.h"

typedef uint32_t u32;
typedef int32_t i32;

uint32_t global_height = 600;
uint32_t global_width = 800;
uint32_t *global_pixel_data = NULL;
size_t global_shm_size = 0;
u32 global_pending_serial = 0;

struct wl_display *global_display = NULL;
struct wl_compositor *global_compositor = NULL;
struct wl_shm *global_shm = NULL;
struct wl_surface *global_surface = NULL;
struct xdg_wm_base *global_xdg_wm_base = NULL;
struct xdg_surface *global_xdg_surface = NULL;
struct xdg_toplevel *global_xdg_toplevel = NULL;
struct wl_shm_pool *global_shm_pool = NULL;
struct wl_buffer *global_buffer = NULL;

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
  printf("xdg_wm_base_ping called\n");
  fflush(stdout);
  xdg_wm_base_pong(wm_base, serial);
}

static void handle_close(void* data, struct xdg_toplevel *toplevel) {
  printf("Window close requested\n");
  fflush(stdout);
  wl_display_disconnect(global_display);
  exit(0);
}

static void handle_toplevel_configure(void* data, struct xdg_toplevel* toplevel, i32 new_width, i32 new_height, struct wl_array* arr) {
  printf("Handle configure called with width: %d and height %d\n", new_width, new_height);
  fflush(stdout);
}

static void handle_surface_configure(void* data, struct xdg_surface* xdg_surface, u32 serial) {
  printf("Handle surface configuration called");
  fflush(stdout);
  /*
   * When a configure event is received, if a client commits the
   * surface in response to the configure event, then the client
   * must make an ack_configure request sometime before the commit
   * request, passing along the serial of the configure event.
   */
  xdg_surface_ack_configure(xdg_surface, serial);
}



static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void registry_handler(
  void *data,
  struct wl_registry *registry,
  uint32_t id,
  const char *interface,
  uint32_t version
) {
    printf("registry_handler called\n");
    fflush(stdout);
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        global_compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        global_shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        global_xdg_wm_base = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(global_xdg_wm_base, &wm_base_listener, NULL);
    }
}

static void registry_remover(void *data, struct wl_registry *registry, uint32_t id) {
  printf("registry_remover called\n");
  fflush(stdout);
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
    size_t stride = global_width * 4;
    global_shm_size = stride * global_height;

    int fd = create_shm_file(global_shm_size);
    global_pixel_data = mmap(NULL, global_shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (global_pixel_data == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    // Fill with white
    for (int i = 0; i < global_width * global_height; ++i) {
        global_pixel_data[i] = 0xFFFFFFFF; // white
    }

    global_shm_pool = wl_shm_create_pool(global_shm, fd, global_shm_size);
    global_buffer = wl_shm_pool_create_buffer(
      global_shm_pool,
      0,
      global_width,
      global_height,
      stride,
      WL_SHM_FORMAT_ARGB8888
    );

    close(fd); // fd no longer needed after pool creation
}


int main() {
    global_display = wl_display_connect(NULL);
    if (!global_display) {
        fprintf(stderr, "Failed to connect to Wayland display.\n");
        return -1;
    }

    struct wl_registry *registry = wl_display_get_registry(global_display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(global_display);

    if (!global_compositor || !global_shm || !global_xdg_wm_base) {
        fprintf(stderr, "Missing a required global interface.\n");
        return -1;
    }

    global_surface = wl_compositor_create_surface(global_compositor);
    global_xdg_surface = xdg_wm_base_get_xdg_surface(global_xdg_wm_base, global_surface);
    global_xdg_toplevel = xdg_surface_get_toplevel(global_xdg_surface);
    xdg_toplevel_set_title(global_xdg_toplevel, "Screensaver");
    static const struct xdg_toplevel_listener toplevel_listener = {
      .configure = handle_toplevel_configure,
      .close = handle_close,
    };

    xdg_toplevel_add_listener(global_xdg_toplevel, &toplevel_listener, NULL);

    wl_surface_commit(global_surface);

    create_buffer();

    wl_surface_attach(global_surface, global_buffer, 0, 0);
    wl_surface_damage_buffer(global_surface, 0, 0, global_width, global_height);
    wl_surface_commit(global_surface);

    uint32_t color = 0;
    // wl_display_dispatch returns -1 on failure
    while (wl_display_dispatch(global_display) != -1) {
      // Set pixel color, masked with full alpha
      for (u32 i = 0; i < global_width * global_height; i++) {
        global_pixel_data[i] = (color++ % 0x01000000) | 0xFF000000;
      }
      wl_surface_damage_buffer(global_surface, 0, 0, global_width, global_height);
      wl_surface_attach(global_surface, global_buffer, 0, 0);
      wl_surface_commit(global_surface);
      usleep(5000); // sleep to slow down the loop (~200 FPS)
      // printf("Main loop iteration");
    }
    fprintf(stderr, "ERROR: wl_display_dispatch failed\n");
    exit(-1);

    wl_display_disconnect(global_display);
    return -1;
}
