**Server was written in C**

Server sending with the requested interval selected books of Pan Tadeusz to clients using sockets.

**Compile readme**

- gcc -Wall server.c -o server -lrt
- gcc -Wall client.c -o client

**Using readme**

./server -k <pan_tadeusz_path> -p <bulletin_Board_Path>
./client -r < > -f <way of transfer file> -x < > -o < > -s < >
- r <realtime signal number>
- f s - split into words
- f l - split into letters
- f - split into lines
- x  <pan_tadeusz volume number>
- o <interval in 1/64 second> 
- s <server PID>
