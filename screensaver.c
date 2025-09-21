#include <errno.h>
#include <wayland-client.h>
#include <stdio.h>
#include <string.h>



static struct wl_compositor *compositor = NULL;
static struct wl_shm *shm = NULL;

static void registry_handle_global(
  void *data,
  struct wl_registry *registry,
  uint32_t name,
  const char *interface,
  uint32_t version
) {
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    }
    else if (strcmp(interface, wl_shm_interface.name) == 0) {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    }
    // else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
    //     wm_base = wl_registry_bind(registry, name,static  &xdg_wm_base_interface, 1);
    // }
};

static void registry_handle_global_remove(
  void *data,
  struct wl_registry *registry,
  uint32_t name
) {
    (void)data; (void)registry; (void)name;
}


int main() {
  struct wl_display *display = wl_display_connect(NULL);
  if (!display) {
    fprintf(stderr, "Failed to create display.\n");
    fprintf(stderr, "%d\n", errno);
    return 1;
  }

  printf("Connected to Wayland Display.\n");
  struct wl_registry *registry = wl_display_get_registry(display);
  if (!registry) {
    fprintf(stderr, "Failed to create registry");
  }

  printf("Created registry\n");
  struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
  };


  wl_registry_add_listener(registry, &registry_listener, NULL);
  printf("Added listener\n");


  wl_display_disconnect(display);
  printf("Disconnected from wayland display\n");
  return 0;
}
