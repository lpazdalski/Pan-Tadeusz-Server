**Server was written in C**

Server sending with the requested interval selected books of Pan Tadeusz to clients using sockets.

**Compile readme**

- gcc -Wall server.c -o server -lrt
- gcc -Wall client.c -o client

**Using readme**

./server -k <pan_tadeusz_path> -p <bulletin_Board_Path> <br />
./client -r < > -f <way of transfer file> -x < > -o < > -s < > <br />
-r <realtime signal number> <br />
-f s - split into words <br />
-f l - split into letters <br />
-f - split into lines <br />
-x  <pan_tadeusz volume number> <br />
-o <interval in 1/64 second> <br />
-s <server PID> <br />
