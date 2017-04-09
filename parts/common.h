#include <functional>
#include <imtjson/json.h>

using namespace json;


#define INTERFACE_VERSION "1.0.0"


class IProc {
public:

	typedef std::function<void(const Value &key, const Value &value)> EmitFn;
	typedef std::function<void(const StrViewA &string)> LogFn;


	virtual void mapdoc(Value doc) = 0;
	virtual Value reduce(Value keys, Value values) = 0;
	virtual Value rereduce(Value values) = 0;

	virtual void onClose() = 0;


	virtual void initEmit(EmitFn fn) = 0;
	virtual void initLog(LogFn fn) = 0;

	virtual ~IProc() {}
};

class AbstractProc: public IProc {
public:

	EmitFn emit;
	LogFn log;


	virtual void mapdoc(Value doc) override{
		throw std::runtime_error("Function 'mapdoc(Value)' is not defined");
	}
	virtual Value reduce(Value keys, Value values) override {
		return reduce(keys,values,false);
	}
	virtual Value rereduce(Value values) override {
		return reduce(Value(),values,true);
	}
	virtual Value reduce(Value keys, Value values, bool rereduce)  {
		throw std::runtime_error("Function 'reduce(Value,Value,bool)' is not defined");
	}

	virtual void onClose()override {delete this;}


	virtual void initEmit(EmitFn fn) {emit = fn;}
	virtual void initLog(LogFn fn) {log = fn;}
};




