/*
Based on the requirements of this OS class project:
http://pages.cs.wisc.edu/~dusseau/Classes/CS537-F07/Projects/P2/p2.html

TODO: Clean up debug output.
*/
#include "string_util.h"
#include "threadpool/threadpool.h"
#include "utils.h"

#include <assert.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

const char *GET          = "GET";
const char *CGI_BIN_PATH = "/cgi-bin/";

const int MAX_CWD = 100;

void writeln_to_socket(int sockfd, const char *message) {
  write(sockfd, message, strlen(message));
  write(sockfd, "\r\n", 2);
}

void write_content_to_socket(int sockfd, const char *content) {
  char length_str[100];
  sprintf(length_str, "%d", (int)strlen(content));

  char *content_length_str = concat("Content-Length: ", length_str);
  writeln_to_socket(sockfd, "Server: PetkoWS/1.0 (MacOS)");
  writeln_to_socket(sockfd, "Content-Type: text/html");
  writeln_to_socket(sockfd, content_length_str);
  writeln_to_socket(sockfd, "");
  writeln_to_socket(sockfd, content);

  free(content_length_str);
}

void http_404_reply(int sockfd) {
  writeln_to_socket(sockfd, "HTTP/1.1 404 Not Found");

  static const char *content =
      "<html><body><h1>Not found</h1></body></html>\r\n";
  write_content_to_socket(sockfd, content);
}

void http_get_reply(int sockfd, const char *content) {
  writeln_to_socket(sockfd, "HTTP/1.1 200 OK");
  write_content_to_socket(sockfd, content);
}

void http_401_reply(int sockfd) {
  writeln_to_socket(sockfd, "HTTP/1.1 401 Unauthorized");
}

int is_get(char *text) { return starts_with(text, GET); }

char *get_path(char *text) {
  int beg_pos       = strlen(GET) + 1;
  char *end_of_path = strchr(text + beg_pos, ' ');
  int end_pos       = end_of_path - text;

  int pathlen = end_pos - beg_pos;
  char *path  = malloc(pathlen + 1);
  substr(text, beg_pos, pathlen, path);
  path[pathlen] = '\0';

  return path;
}

int is_cgi_bin_request(const char *path) {
  if (contains(path, "/cgi-bin/"))
    return 1;
  return 0;
}

char *read_file(FILE *fpipe) {
  int capacity = 10;
  char *buf    = malloc(capacity);
  int index    = 0;

  int c;
  while ((c = fgetc(fpipe)) != EOF) {
    assert(index < capacity);
    buf[index++] = c;

    if (index == capacity) {
      char *newbuf = malloc(capacity * 2);
      memcpy(newbuf, buf, capacity);
      free(buf);
      buf = newbuf;
      capacity *= 2;
    }
  }
  // TODO(petko): Test with feof, ferror?

  buf[index] = '\0';
  return buf;
}

struct request_pair {
  char *path;
  char *query;
};

struct request_pair extract_query(const char *cgipath_param) {
  struct request_pair ret;
  char *qq = strchr(cgipath_param, '?');

  if (qq == NULL) {
    ret.path  = strdup(cgipath_param);
    ret.query = NULL;
  } else {
    int path_len = qq - cgipath_param;
    ret.path     = malloc(path_len + 1);
    strncpy(ret.path, cgipath_param, path_len);
    ret.path[path_len] = 0;

    int query_len               = strlen(cgipath_param) - path_len - 1;
    ret.query                   = malloc(query_len + 1);
    const char *query_start_pos = cgipath_param + path_len + 1;
    strncpy(ret.query, query_start_pos, query_len);
    ret.query[query_len] = '\0';
  }

  return ret;
}

void run_cgi(int sockfd, const char *curdir, const char *cgipath_param) {
  char *fullpath;
  struct request_pair req = extract_query(cgipath_param);

  char *params;
  if (req.query) {
    params = malloc(strlen(req.query) + 100);
    sprintf(params, "QUERY_STRING='%s' ", req.query);
  } else {
    params = strdup("");
  }

  if (ends_with(req.path, ".py")) {
    // TODO: Overflow possible?
    fullpath = concat4(params, "python ", curdir, req.path);
  } else {
    fullpath = concat3(params, curdir, req.path);
  }

  free(params);
  free(req.path);
  free(req.query);

  printf("Executing: [%s]\n", fullpath);

  char *quoted_path = malloc(strlen(fullpath)+3);
  snprintf(quoted_path, strlen(fullpath)+3, "\"%s\"", fullpath);
  FILE *fpipe = popen(quoted_path, "r");
  free(fullpath);
  free(quoted_path);

  if (!fpipe) {
    perror("Problem with popen");
    http_404_reply(sockfd);
  } else {
    char *result = read_file(fpipe);
    http_get_reply(sockfd, result);
    free(result);
  }
}

void output_static_file(int sockfd, const char *curdir, const char *path) {
  char *fullpath = malloc(strlen(curdir) + strlen(path) + 1);
  strcpy(fullpath, curdir);
  strcat(fullpath, path);

  printf("Opening static file: [%s]\n", fullpath);

  FILE *f = fopen(fullpath, "r");
  if (!f) {
    perror("Problem with fopen");
    http_404_reply(sockfd);
  } else {
    char *result = read_file(f);
    http_get_reply(sockfd, result);
    free(result);
  }
}

int is_authorized(char *request) {
  char *auth_header = strcasestr(request, "X-fake-auth");
  char *secret      = "THISISATEST";

  if (auth_header != NULL && strlen(auth_header) >= 13) {
    char *nl    = strstr(auth_header + 13, "\r\n");
    char *token = malloc(sizeof(char) * (nl - (auth_header + 13) + 1));
    bzero(token, sizeof(char) * (nl - (auth_header + 13) + 1));
    memcpy(token, (auth_header + 13), (nl - (auth_header + 13)));

    if (strlen(token) == strlen(secret)) {
      for (int i = 0; i < strlen(secret); i++) {
        usleep(10000); // fake operation time
        if (token[i] != secret[i]) {
          free(token);
          return 0;
        }
      }

      printf("%s\n", token);
      free(token);
      return 1;
    }
    free(token);
  }

  return 0;
}

void *handle_socket_thread(void *sockfd_arg) {
  int sockfd = *((int *)sockfd_arg);

  printf("Handling socket: %d\n", sockfd);

  char *text = read_text_from_socket(sockfd);
  printf("From socket: %s", text);

  if (!is_authorized(text)) {
    http_401_reply(sockfd);
  } else if (is_get(text)) {
    char curdir[MAX_CWD];

    if (!getcwd(curdir, MAX_CWD)) {
      error("Couldn't read curdir");
    }

    char *path = get_path(text);

    if (is_cgi_bin_request(path)) {
      run_cgi(sockfd, curdir, path);
    } else {
      printf("cwd[%s]\n", curdir);
      printf("path[%s]\n", path);
      if (strcmp(path, "/") == 0) {
        printf("assuming index.html\n");
        output_static_file(sockfd, curdir, "/index.html");
      } else {
        output_static_file(sockfd, curdir, path);
      }
    }

    free(path);
  } else {
    // The server only supports GET.
    http_404_reply(sockfd);
  }

  free(text);
  close(sockfd);
  free(sockfd_arg);

  return NULL;
}

int create_listening_socket() {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    error("ERROR opening socket");
  }
  int setopt = 1;

  // Reuse the port. Otherwise, on restart, port 8000 is usually still occupied
  // for a bit and we need to start at another port.
  if (-1 == setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&setopt,
                       sizeof(setopt))) {
    error("ERROR setting socket options");
  }

  struct sockaddr_in serv_addr;

  uint16_t port = 8000;

  while (1) {
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_port        = htons(port);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
      port++;
    } else {
      break;
    }
  }

  if (listen(sockfd, SOMAXCONN) < 0)
    error("Couldn't listen");
  printf("Running on port: %d\n", port);

  return sockfd;
}

int main() {
  int sockfd = create_listening_socket();

  struct sockaddr_in client_addr;
  int cli_len = sizeof(client_addr);

  // this needs to be at least as big as our token char set
  struct thread_pool *pool = pool_init(30);

  while (1) {
    int newsockfd =
        accept(sockfd, (struct sockaddr *)&client_addr, (socklen_t *)&cli_len);
    if (newsockfd < 0)
      error("Error on accept");
    printf("New socket: %d\n", newsockfd);

    int *arg = malloc(sizeof(int));
    *arg     = newsockfd;
    pool_add_task(pool, handle_socket_thread, arg);
  }
  close(sockfd);

  return 0;
}
