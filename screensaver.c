#include <errno.h>
#include <wayland-client.h>
#include <stdio.h>

int main() {
  const char* w_name = "russ_123";
  struct wl_display *display = wl_display_connect(w_name);
  if (!display) {
    fprintf(stderr, "Failed to create display.\n");
    fprintf(stderr, "%d\n", errno);
    return 1;
  }

  printf("Connected to Wayland Display.\n");
  while (1) {
  }
  wl_display_disconnect(display);
  return 0;
}
