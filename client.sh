#!/bin/bash


clear; \
if [ $1 -eq 1 ]
then
	make && ./fs3_client -v assign4-small-workload.txt
else
	make && ./fs3_client -v -l fs3_client_log_small.txt assign4-small-workload.txt
fi
