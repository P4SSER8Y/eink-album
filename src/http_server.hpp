#pragma once
#include "config.hpp"
#include <WebServer.h>

extern WebServer server;
extern bool image_uploaded;

void http_server_begin(const Config &cfg);
