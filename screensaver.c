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


#define BLACK 0xFF000000
#define WHITE 0xFFFFFFFF
#define NUM_POINTS 7

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;

typedef struct Point {
  i16 x;
  i16 y;
  i16 x_speed;
  i16 y_speed;
} Point;

void print_point(Point* p) {
  printf("Point: x: %d; y: %d; x_speed: %d; y_speed: %d\n", p->x, p->y, p->x_speed, p->y_speed);
}

u8 num_lines = (NUM_POINTS * (NUM_POINTS - 1)) / 2;
uint32_t global_height = 1048;
uint32_t global_width = 1920;
uint32_t *global_pixel_data = NULL;
size_t global_shm_size = 0;
u32 global_pending_serial = 0;
Point global_points[NUM_POINTS];

struct wl_display *global_display = NULL;
struct wl_compositor *global_compositor = NULL;
struct wl_shm *global_shm = NULL;
struct wl_surface *global_surface = NULL;
struct xdg_wm_base *global_xdg_wm_base = NULL;
struct xdg_surface *global_xdg_surface = NULL;
struct xdg_toplevel *global_xdg_toplevel = NULL;
struct wl_shm_pool *global_shm_pool = NULL;
struct wl_buffer *global_buffer = NULL;

Point nextPoint(Point* prevPoint) {
  i16 next_x = prevPoint->x + prevPoint->x_speed;
  i16 next_x_speed = prevPoint->x_speed;
  if (next_x < 0) {
    next_x *= -1;
    next_x_speed *= -1;
  }

  if (next_x >= global_width) {
    i16 overshoot = next_x - (global_width - 1);
    next_x = global_width - overshoot;
    next_x_speed *= -1;
  }

  i16 next_y = prevPoint->y + prevPoint->y_speed;
  i16 next_y_speed = prevPoint->y_speed;
  if (next_y < 0) {
    next_y *= -1;
    next_y_speed *= -1;
  }

  if (next_y >= global_height) {
    i16 overshoot = next_y - (global_height - 1);
    next_y = global_height - overshoot;
    next_y_speed  *= -1;
  }
  Point p = {
    .x = next_x,
    .y = next_y,
    .x_speed = next_x_speed,
    .y_speed = next_y_speed,
  };

  if (p.x >= global_width || p.y >= global_height) {
    fprintf(stderr, "GLOBAL WIDTH OR GLOBAL HEIGHT OVERFLOW\n");
    printf("x: %d, global_width: %d, y: %d, global_height: %d\n", p.x, global_width, p.y, global_height);
    fflush(stderr);
    fflush(stdout);
    exit(-1);
  }
  return p;
}


static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
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

    // Fill with black
    for (int i = 0; i < global_width * global_height; ++i) {
        global_pixel_data[i] = BLACK;
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

    for (u8 i = 0; i < NUM_POINTS; i++) {
      i8 new_x_speed = (rand() & 0x03) + 1;
      i8 new_y_speed = (rand() & 0x03) + 1;
      if (rand() & 0x01) {
        new_x_speed *= -1;
      }
      if (rand() & 0x01) {
        new_y_speed *= -1;
      }
      global_points[i].x = rand() % global_width;
      global_points[i].y = rand() % global_height;
      global_points[i].x_speed = new_x_speed;
      global_points[i].y_speed = new_y_speed;
    }

    // Each line has a max number of pixels of `global_width` because
    // I'm assigning one pixel per x index.
    u32 prev_frame_white_pixels_len = global_width * num_lines;
    size_t* prev_frame_white_pixels = calloc(sizeof(size_t), prev_frame_white_pixels_len);
    for (u32 i = 0; i < global_width * global_height; i++) {
      global_pixel_data[i] = BLACK;
    }

    long double slope;
    // wl_display_dispatch returns -1 on failure
    while (wl_display_dispatch(global_display) != -1) {
      // Set pixel color, masked with full alpha
      for (u32 i = 0; i < prev_frame_white_pixels_len; i++) {
        global_pixel_data[prev_frame_white_pixels[i]] = BLACK;
      }

      u32 prev_frame_white_pixels_index = 0;
      for (u8 j = 0; j < NUM_POINTS - 1; j++) {
        for (u8 k = j + 1; k < NUM_POINTS; k++) {
          global_points[j] = nextPoint(&global_points[j]);
          global_points[k] = nextPoint(&global_points[k]);
          i32 lower_x = global_points[j].x;
          i32 upper_x = global_points[k].x;
          if (global_points[k].x < global_points[j].x) {
            lower_x = global_points[k].x;
            upper_x = global_points[j].x;
          }

          i32 lower_y = global_points[j].y;
          i32 upper_y = global_points[k].y;
          if (global_points[k].y < global_points[j].y) {
            lower_y = global_points[k].y;
            upper_y = global_points[j].y;
          }

          slope = (long double)(global_points[k].y - global_points[j].y) / (long double)(global_points[k].x - global_points[j].x);
          long double b = global_points[j].y - (slope * global_points[j].x);
          for (i32 i = lower_x; i < upper_x; i++) {
            long double y = (slope * i) + b;
            i32 y_i32 = (i32)y;
            if (i < 0 || i >= global_width || y_i32 < 0 || y_i32 >= global_height) {
              printf("Invalid values\n");
              print_point(&global_points[j]);
              print_point(&global_points[k]);
              printf("i: %d, y: %.4Lf, slope: %.4Lf, b: %.4Lf, y_i32: %d, global_width: %d, global_height: %d\n", i, y, slope, b, y_i32, global_width, global_height);
              exit(-1);
            }
            size_t index = i + (y_i32 * global_width);
            global_pixel_data[index] = WHITE;
            prev_frame_white_pixels[prev_frame_white_pixels_index++] = index;
          }
        }
      }
      wl_surface_damage_buffer(global_surface, 0, 0, global_width, global_height);
      wl_surface_attach(global_surface, global_buffer, 0, 0);
      wl_surface_commit(global_surface);
      usleep(25000); // sleep to slow down the loop (~200 FPS)
    }
    fprintf(stderr, "ERROR: wl_display_dispatch failed\n");
    exit(-1);

    wl_display_disconnect(global_display);
    return -1;
}
