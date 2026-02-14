#include "fio_cli.h"
#include "fiobj_str.h"
#include "fiobject.h"
#include "main.h"
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

/* Global storage for our templates */
static mustache_s *TEMPLATE_INDEX;
static mustache_s *TEMPLATE_COUNT;

/* Global counter state */
atomic_int global_counter = 0;

/* Helper: Build the data context for Mustache (Model) */
FIOBJ build_view_model(void) {
    FIOBJ data = fiobj_hash_new();

    // Add integer value
    fiobj_hash_set(data, fiobj_str_new("value", 5), fiobj_num_new(global_counter));

    return data;
}

/* TODO: edit this function to handle HTTP data and answer Websocket requests.*/
static void on_http_request(http_s *h) {
  /* set a response and send it (finnish vs. destroy). */

  // Convert path to C-String for comparison
  fio_str_info_s path = fiobj_obj2cstr(h->path);

  // Route: ROOT "/" -> Render full page
  if (path.len == 1 && path.data[0] == '/') {
      http_set_header(h, HTTP_HEADER_CONTENT_TYPE, http_mimetype_find("html", 4));

      // Build data
      FIOBJ data = build_view_model();

      // Render template: index.mustache
      FIOBJ html = fiobj_mustache_build(TEMPLATE_INDEX, data);

      // Send response
      fio_str_info_s str = fiobj_obj2cstr(html);
      http_send_body(h, str.data, str.len);

      // Cleanup
      fiobj_free(data);
      fiobj_free(html);
      return;
  }

  // Route: "/count" -> Render only the fragment (HTMX)
  if (path.len == 6 && memcmp(path.data, "/count", 6) == 0) {
      // Increment global counter
      // ++global_counter;
      atomic_fetch_add(&global_counter, 1);

      http_set_header(h, HTTP_HEADER_CONTENT_TYPE, http_mimetype_find("html", 4));

      // Build data
      FIOBJ data = build_view_model();

      // Render template: count.mustache (Fragment only!)
      FIOBJ html = fiobj_mustache_build(TEMPLATE_COUNT, data);

      // Send response
      fio_str_info_s str = fiobj_obj2cstr(html);
      http_send_body(h, str.data, str.len);

      // Cleanup
      fiobj_free(data);
      fiobj_free(html);
      return;
  }

  // Route: "/static" -> Render only css file
  if (path.len == 18 && memcmp(path.data, "/static/output.css", 18) == 0) {
      // Open the file
      int fd = open("static/output.css", O_RDONLY);

      if (fd == -1) {
          // File not found locally
          http_send_error(h, 404);
          return;
    }

      // Get file size
      struct stat st;
      if (fstat(fd, &st) == -1) {
          close(fd);
          http_send_error(h, 500);
          return;
      }

      http_set_header(h, HTTP_HEADER_CONTENT_TYPE, http_mimetype_find("css", 3));

      http_sendfile(h, fd, st.st_size, 0);
      return;
  }

  // 4. 404 Not Found
  http_send_error(h, 404);
}

/* starts a listeninng socket for HTTP connections. */
void initialize_http_service(void) {
    // Load Mustache Templates

    FIOBJ index_path = fiobj_str_new("views/index.mustache", 20);
    FIOBJ count_path = fiobj_str_new("views/count.mustache", 20);

    TEMPLATE_INDEX = fiobj_mustache_load(fiobj_obj2cstr(index_path));
    TEMPLATE_COUNT = fiobj_mustache_load(fiobj_obj2cstr(count_path));

    if (!TEMPLATE_INDEX || !TEMPLATE_COUNT) {
        fprintf(stderr, "ERROR: Could not load templates. Make sure 'views/' folder exists.\n");
        exit(1);
    }

  /* listen for inncoming connections */
  if (http_listen(fio_cli_get("-p"), fio_cli_get("-b"),
                  .on_request = on_http_request,
                  .max_body_size = fio_cli_get_i("-maxbd") * 1024 * 1024,
                  .ws_max_msg_size = fio_cli_get_i("-max-msg") * 1024,
                  .public_folder = fio_cli_get("-public"),
                  .log = fio_cli_get_bool("-log"),
                  .timeout = fio_cli_get_i("-keep-alive"),
                  .ws_timeout = fio_cli_get_i("-ping")) == -1) {
    /* listen failed ?*/
    perror("ERROR: facil couldn't initialize HTTP service (already running?)");
    exit(1);
  }
}
