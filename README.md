# Filesystem-Driver

---
## How to compile and test

- To cleanup your compiled objects, run:
  ```
  make clean
  ```

- To compile your code, you have to use:
  ```
  make
  ```

- To run the server:
  ```
  ./fs3_server -v -l fs3_server_log.txt 
  OR
  ./fs3_server
  ```

**Note:** you need to restart the server each time you run the client.
**Note:** when you use the `-l` argument, you will see `*` appear every so often. Each dot represents 100k workload operations. This allows you to see how things are moving along.

- To run the client:
  ```
  ./fs3_client -v -l fs3_client_log_small.txt assign4-small-workload.txt
  ./fs3_client -v -l fs3_client_log_medium.txt assign4-medium-workload.txt
  ./fs3_client -v -l fs3_client_log_jumbo.txt assign4-jumbo-workload.txt
  ```

- If the program completes successfully, the following should be displayed as the last log entry:
  ```
  FS3 simulation: all tests successful!!!
  ```
---