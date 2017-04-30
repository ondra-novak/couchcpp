#include <cstring>

#include <cstdio>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include "simplehttp.h"

#include <arpa/inet.h>
#include <netinet/in.h>


using namespace json;


void sendAll(int socket, StrViewA text) {
	while (text.length) {
		int i = send(socket, text.data, text.length, 0);
		if (i == -1) {
			int e = errno;
			throw std::runtime_error(String({"Failed to send request - error: ", strerror(e)}).c_str());
		}
		text = text.substr(i);
	}
}

std::string recvAll(int socket) {
	char buff[1500];

	std::string res;
	res.reserve(1500);

	int i = recv(socket, buff, 1500, 0);
	while (i) {
		if (i == -1) {
			int e = errno;
			throw std::runtime_error(String({"Failed to recieve response - error: ", strerror(e)}).c_str());
		}
		res.append(buff,i);
		i = recv(socket, buff, 1500, 0);
	}
	return res;
}

StrViewA ltrim(const StrViewA &what) {
	if (what.length && isspace(what[0])) return ltrim(what.substr(1));
	else return what;
}
StrViewA rtrim(const StrViewA &what) {
	if (what.length && isspace(what[what.length-1])) return rtrim(what.substr(0,what.length-1));
	else return what;
}

StrViewA trim(const StrViewA &what) {
	return rtrim(ltrim(what));
}

int connectSocket(int port) {

	int s = socket(AF_INET,SOCK_STREAM, 0);
	if (s == -1) return -1;
	struct sockaddr_in sin;
	memset(&sin,0,sizeof(sin));
	inet_aton("127.0.0.1", &sin.sin_addr);
	sin.sin_port = htons(port);
	sin.sin_family = AF_INET;
	if (connect(s,reinterpret_cast<struct sockaddr *>(&sin),sizeof(sin))) {
		close(s);
		return -1;
	}
	return s;
}

HttpResponse httpRequest(int dbport, json::StrViewA requestUri, json::Value postData, json::Value headers) {
	String post;
	bool postRequest = postData.defined();
	std::ostringstream request;
	if (postRequest) {
		post =postData.stringify();
		request << "POST ";
	} else {
		request << "GET ";
	}


	request << requestUri << " HTTP/1.0\r\n"
		  << "Host: localhost:" << dbport << "\r\n"
		  << "Accept: application/json\r\n"
		  << "Connection: close\r\n";

	for (Value x: headers) {
		request << x.getKey() << ": " << x.getString() << "\r\n";
	}

	if (postRequest) {
		request << "Content-Type: application/json\r\n"
				<< "Content-Length: " << post.length() << "\r\n";
	}

	request << "\r\n";
	if (postRequest) {
		request << post;
	}



	int socket = connectSocket(dbport);
	if (socket == -1) {
		int e = errno;
		throw std::runtime_error(String({"Failed to connect database port: ", Value(dbport).toString(), " - error: ", strerror(e)}).c_str());
	}


	std::string strrequest = request.str();
	std::string strresponse;

	try {
		sendAll(socket, strrequest);
		strresponse = recvAll(socket);
		close(socket);



	} catch (...) {
		close(socket);
		throw;
	}

	StrViewA strresp2(strresponse);
	HttpResponse response;
	auto lines = strresp2.split("\r\n");
	{
		StrViewA firstLine = lines();
		auto parts = firstLine.split(" ");
		response.proto = parts();
		StrViewA sstatus = parts();
		response.message = StrViewA(parts);
		response.status = (int)strtol(sstatus.data,0,10);
	}

	{
		Object hdrs;
		StrViewA ln = lines();
		while (ln.length) {
			auto parts = ln.split(":");
			StrViewA key = trim(parts());
			StrViewA val = trim(parts);
			hdrs(key,val);
			ln = lines();
		}
		response.headers = hdrs;
	}

	if (response.headers["Content-Type"].getString() == "application/json") {
		response.body = Value::fromString(lines);
	} else {
		response.body = Value(StrViewA(lines));
	}
	return response;
}
