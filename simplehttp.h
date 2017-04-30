#pragma once


#include <imtjson/json.h>

struct HttpResponse {
	int status;
	json::String message;
	json::String proto;
	json::Value headers;
	json::Value body;
};

HttpResponse  httpRequest(int dbPort, json::StrViewA requestUri, json::Value postData, json::Value headers);
