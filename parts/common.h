#include <functional>
#include <imtjson/json.h>

using namespace json;


#define INTERFACE_VERSION "1.0.0"


class IProc {
public:

	typedef std::function<void(const Value &key, const Value &value)> EmitFn;
	typedef std::function<void(const StrViewA &string)> LogFn;
	typedef std::function<Value()> GetRowFn;
	typedef std::function<void(const StrViewA &)> SendFn;
	typedef std::function<void(const Value &)> StartFn;
	typedef std::function<void(Value)> SetErrorFn;


	virtual void mapdoc(Value doc) = 0;
	virtual Value reduce(Value keys, Value values) = 0;
	virtual Value rereduce(Value values) = 0;
	virtual void show(Value doc, Value request) = 0;
	virtual void list(Value head, Value request) = 0;
	virtual void update(Value &doc, Value request) = 0;
	virtual bool filter(Value doc) = 0;
	virtual void validate(Value doc, Value prevDoc, Value userContext, Value securityContext) = 0;

	virtual void onClose() = 0;


	virtual void initEmit(EmitFn fn) = 0;
	virtual void initLog(LogFn fn) = 0;
	virtual void initShowListFns(GetRowFn getrow, SendFn send, StartFn start) = 0;
	virtual void initSetErrorFn(SetErrorFn fn) = 0;

	virtual ~IProc() {}
};

class AbstractProc: public IProc {
public:

	EmitFn emit;
	LogFn log;
	GetRowFn getRow;
	StartFn start;
	SendFn send;
	SetErrorFn setError;


	virtual void mapdoc(Value doc) override{
		throw std::runtime_error("Function 'void mapdoc(Value)' is not defined");
	}
	virtual Value reduce(Value keys, Value values) override {
		return reduce(keys,values,false);
	}
	virtual Value rereduce(Value values) override {
		return reduce(Value(),values,true);
	}
	virtual Value reduce(Value keys, Value values, bool rereduce)  {
		throw std::runtime_error("Function 'Value reduce(Value,Value,bool)' is not defined");
	}

	virtual void show(Value doc, Value request) {
		throw std::runtime_error("Function 'void show(Value doc, Value request)' is not defined");
	}
	virtual void list(Value head, Value request) {
		throw std::runtime_error("Function 'void list(Value head, Value request)' is not defined");
	}
	virtual void update(Value &doc, Value request) {
		throw std::runtime_error("Function 'void update(Value &doc, Value request)' is not defined");
	}
	virtual bool filter(Value doc) {
		throw std::runtime_error("Function 'bool filter(Value request)' is not defined");
	}
	virtual void validate(Value doc, Value prevDoc, Value userContext, Value securityContext) {
		throw std::runtime_error("Function 'void validate(Value doc, Value prevDoc, Value userContext, Value securityContext)' is not defined");
	};


	virtual void onClose()override {delete this;}


	virtual void initEmit(EmitFn fn) {emit = fn;}
	virtual void initLog(LogFn fn) {log = fn;}
	virtual void initShowListFns(GetRowFn getrow, SendFn send, StartFn start) {
		this->getRow = getrow;
		this->send = send;
		this->start = start;
	}
	virtual void initSetErrorFn(SetErrorFn fn) {
		this->setError = fn;
	}

	void unauthorized() {unauthorized("unauthorized");}
	void unauthorized(String a) {setError(json::Object("unauthorized",a));}
	void forbidden() {unauthorized("forbidden");}
	void forbidden(String a) {setError(json::Object("forbidden",a));}
	void error(String errName, String reason) {setError(Value({"error",errName,reason}));}
	void notFound(String what) {error("not_found",what);}
};




