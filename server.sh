rm fs3_client_log_small.txt && touch fs3_client_log_small.txt; \
rm fs3_server_log_small.txt && touch fs3_server_log_small.txt; \
\
clear; \
\
\
./fs3_server -v -l fs3_server_log_small.txt
