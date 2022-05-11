#!/bin/bash
echo "------------- SERVER ------------"
gcc -o server `xml2-config --cflags` server.c `xml2-config --libs`
echo "------------- CLIENT ------------"
gcc -o client client.c