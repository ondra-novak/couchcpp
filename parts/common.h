#ifndef COUCHCPP_COMMON_HEADER
#define COUCHCPP_COMMON_HEADER

#pragma once

#include <functional>
#include <imtjson/json.h>

using namespace json;

#define INTERFACE_VERSION "1.0.4"

namespace {



class Document: public json::Value {
public:
	Document(const json::Value &x):json::Value(x) {}

	///Retrieve document id as string
	StrViewA getID() const {return (*this)["_id"].getString();}
	///Retrieve document type
	/**
	 * Document type is defined as prefix, which is separated by special character.
	 * For example: for "user.123484ewa15d8" document type is "user". Default separator is '.', however
	 * you can specify different separator as argument.
	 *
	 * @param typeSep separator between type and rest of id
	 * @return document type. if separator missing, returns empty string
	 */
	StrViewA getDocType(char typeSep = '.') const {
		StrViewA id = getID();
		std::size_t seppos = id.indexOf(StrViewA(&typeSep,1));
		if (seppos == id.npos) return StrViewA();
		else return id.substr(0,seppos);
	}

	///Replaces value in the document specified by the path. Returns updated document
	/**
	 * @param path path
	 * @param val new value
	 * @return updated document
	 *
	 * @note for multiple changes, it is better to use json::Object
	 */
	Document replace(const Path &path, const json::Value &val) const {
		return Document(json::Value::replace(path,val));
	}
	///Replaces value in the document specified by the a key (at first level). Returns updated document
	/**
	 * @param key to replace
	 * @param val new value
	 * @return updated document
	 *
	 * @note for multiple changes, it is better to use json::Object
	 */
	Document replace(const StrViewA &key, const json::Value &val) const {
		return Document(json::Value::replace(json::Path::root/key,val));
	}

	///Retrieve metadata about specified attachment
	Value getAttachment(StrViewA name) const {
		return getAttachments()[name];
	}
	///Retrieves container of attachments for enumerations
	Value getAttachments() const {
		return (*this)["_attachments"];
	}
	///calculates uru (relative to database root) for specified attachment
	String getAttachmentUri(StrViewA name, Value userContext) const {
		Value db = userContext["db"];
		if (!db.defined()) {
			db = userContext["userCtx"]["db"];
		}
		if (!db.defined()) {
			throw std::runtime_error("getAttachmentUri - invalid 2. argument");
		}
		Value idenc = urlEncoding->encodeBinaryValue(BinaryView(getID()));
		Value nameenc = urlEncoding->encodeBinaryValue(BinaryView(name));
		return String({db.getString(),"/",idenc,"/",nameenc});
	}
	///Sets attachment
	/**
	 * @param name name of attachment
	 * @param data metadata, which may include a data field with base64 content. This argument
	 * can be also undefined to delete attachment
	 * @return new document
	 *
	 * @note function replaces existing attachment.
	 */
	Document setAttachment(StrViewA name, Value data) {
		return replace(Path::root/"_attachments"/name, data);
	}

};



typedef json::Value Key;

///Contains context of the validation
struct ContextData {
	///Previous document. Can be null for first document
	Document prevDoc;
	///User which updates the document
	Value user;
	///security informations
	Value security;

	ContextData(Document prevDoc,Value user,Value security):prevDoc(prevDoc),user(user),security(security) {}
};


///Single row of RowSet for the reduce() function
/**
 * You can receive Row as result of iteration (through the RowIterator or range-based for
 *
 */
class Row {
public:
	///Key
	Key key;
	///Value
	Value value;
	///Document ID
	StrViewA docId;

	Row(Value row):key(row[0][0]),value(row[1]),docId(row[0][1].getString()) {}
};

///Single row of result for the list() function
/**
 * You can receive ListRow as result of getRow() function
 */
class ListRow: public Value {
public:
	ListRow():json::Value(nullptr) {}
	ListRow(const Value &v):json::Value(v) {}

	///Retrieves key
	Value getKey() const {return Value::operator[]("key");}
	///Retrieves value
	Value getValue() const {return Value::operator[]("value");}
	///Retrieves ID
	Value getID() const {return Value::operator[]("id");}
	///Retrieves documenr (can be NULL, if document is not present)
	Document getDoc() const {return Value::operator[]("doc");}

	///Returns true, when ListRow contains a row.
	/**
	 * @retval true row exists
	 * @retval false end of list (no row)
	 */
	operator bool() const {return !isNull();}
	///Returns false, when ListRow contains a row.
	/**
	 * @retval false row exists
	 * @retval true end of list (no row)
	 */
	bool operator!() const {return isNull();}
};


///Iterates through the RowSet
/**
 * The RowSet is available in the reduce()
 *
 * @note iterator is very limited. It is better to use range-based for
 *
 * @see RowSet
 */
class RowIterator: public ValueIterator {
public:
	RowIterator(const ValueIterator &iter):ValueIterator(iter) {}

	Row operator *() const {return Row(ValueIterator::operator *());}

	typedef Row value_type;
	typedef Row *        pointer;
	typedef Row &        reference;
	typedef std::intptr_t  difference_type;
};

///Contains sets of rows for reduction
/**
 * RowSet is avaiable in the function reduce().
 *
 * You can only iterate RowSet. Because the RowSet inherits the Value, you can
 * access rows directly. However it is much easier to perform range-based iteration
 *
 * @code
 * Value reduce(RowSet rowSet) {
 *   for (Row r : rowSet) {
 *   	// puts each row to the variable "r"
 *
 *   }
 * }
 * @endcode
 *
 */
class RowSet: public Value {
public:

	RowSet(Value v):Value(v) {}
	Row operator[](int pos) const {return Row(Value::operator[](pos));}
	RowIterator begin() const {return RowIterator(Value::begin());}
	RowIterator end() const {return RowIterator(Value::end());}

};

///Exception object
/** throwing this object from any function causes that error is reported
 * to the CouchDB. You can specify type of error and description
 */
class Error: public std::exception {
public:
	Error(String type, String desc):type(type),desc(desc) {}

	String type;
	String desc;

	const char *what() const throw() {return type.c_str();}
	virtual ~Error() throw() {}

};

///Exception object
class NotFound: public Error {
public:
	NotFound(String what):Error("not_found", what) {}
};


enum ValidationDecree {
	accepted,
	rejected,
	forbidden,
	unauthorized
};

class ValidationResult {
public:
	ValidationResult(bool res):decree(res?accepted:rejected) {}
	ValidationResult(ValidationDecree decree):decree(decree) {}
	ValidationResult(ValidationDecree decree, String description):decree(decree),description(description) {}

	ValidationDecree decree;
	String description;
};


typedef const ContextData &Context;

}

class IProc {
public:

	typedef std::function<void(const Value &key, const Value &value)> EmitFn;
	typedef std::function<void(const StrViewA &string)> LogFn;
	typedef std::function<Value()> GetRowFn;
	typedef std::function<void(const StrViewA &)> SendFn;
	typedef std::function<void(const Value &)> StartFn;


	///Map document to the view
	/**
	 * @param doc document to map. To perform mapping, call emit(key,value) inside of the function
	 */
	virtual void mapdoc(Document doc) = 0;
	///Reduce multiple documents
	/**
	 *
	 * @param rows Contains rows in object RowSet. You can iterate this object through ranged-loop, and
	 *   receive Row items. Each Row item has following member fields: key, value, docId
	 * @return reduced value
	 *
	 */
	virtual Value reduce(RowSet rows) = 0;
	///Rereduce multiple reduced results
	/**
	 * @param values Results of previously reduced documents
	 * @return Reduced value
	 */
	virtual Value rereduce(Value values) = 0;
	///Show function (send document as http response)
	/**
	 * @param doc document to show
	 * @param request request context
	 *
	 * The function need to use functions start() and send() to create and send content. Function
	 * start() receives response object which can define http headers (see CouchDB's user guide). The
	 * start() must be called as first, otherwise it is ignored. If start is not called, the default headers
	 * are used.
	 *
	 * The function send() sends the content to the client. If called before start() the function start with
	 * a default settings is executed the first
	 */
	virtual void show(Document doc, Value request) = 0;
	///List function (format content of view)
	/**
	 *
	 * @param head header of the view
	 * @param request request context
	 *
	 * The function need to use functions start(), send() and getRow(). The start() must be called
	 * as the first, otherwise default settings is applied. The function start() accepts response object (see
	 * CouchDB's user guide). The function getRow() receives next row from the view. If there are no
	 * more rows, the function getRow() returns null. Function send() accepts string and it is used
	 * to send text to the client.
	 */
	virtual void list(Value head, Value request) = 0;
	///Update document
	/**
	 * @param doc reference to document to update. Function must replace document with new document. In
	 * case that this variable is not changed, or set to null, function doesn't perform any update to the document
	 * @param request request context
	 *
	 * The function need to use functions start() and send() to generate resposne. Function
	 * start() receives response object which can define http headers (see CouchDB's user guide). The
	 * start() must be called as the first, otherwise it is ignored. If start is not called, the default headers
	 * are used.
	 *
	 */
	virtual void update(Document &doc, Value request) = 0;

	///Filter function
	/**
	 * Called to filter documents
	 * @param doc document
	 * @param request request context
	 * @retval true use this document
	 * @retval false skip this document
	 */
	virtual bool filter(Document doc, Value request) = 0;

	///Validation function
	/**
	 *
	 * @param doc document to validate
	 * @param context context of validation
	 *
	 * To emit failed validation, you need to use an error reporting function. You can call error() to emit
	 * general error. To request authorization, call unauthorized(). To emit forbidden operation, call forbidden().
	 * Once an error reporting function is called, the validation fails right after the function returns. If none
	 * of error reporting function is called, validation passes.
	 */
	virtual ValidationResult validate(Document doc, Context context) = 0;

	virtual void onClose() = 0;


	virtual void initEmit(EmitFn fn) = 0;
	virtual void initLog(LogFn fn) = 0;
	virtual void initShowListFns(GetRowFn getrow, SendFn send, StartFn start) = 0;

	virtual ~IProc() {}
};

class AbstractProc: public IProc {

	///Function emit
	/** The function is available only for mapdoc function
	 *
	 * @code
	 * void emit(Value key, Value value);
	 * @endcode
	 *
	 * @param key key
	 * @param value value
	 *
	 * The value undefined is converted to null
	 *
	 */
	EmitFn fn_emit;

	///Function log
	/**
	 * Function sends line to the couchdb's log. It is always available.
	 *
	 * @code
	 * void log(StrViewA text)
	 * @endcode
	 *
	 * Sends text to couchdb's log. Whole line must be send (it cannot be send per-partes)
	 */
	LogFn fn_log;

	///Function getRow
	/**
	 * The function retrieves next row from the result.
	 * The function is available for the functions list(), update() and show().
	 * For the functions update() and show() returns only one document (the same document from the argument).
	 * For the function list() it can iterate through the all results in the requested view. Note that
	 * there is no way to rewind the iteration. It is always one direction only (like a pipe)
	 *
	 * @code
	 * Value getRow();
	 * @endcode
	 *
	 * @return Function returns next row, or null, if there are no more rows
	 */
	GetRowFn fn_getRow;

	///Function initiates output and sets parameters for the output - for instance: http headers
	/**
	 * The function is available in list(), update() and show()
	 *
	 * @code
	 * void start(Value setting);
	 * @endcode
	 *
	 * @note function must be called before the function getRow is called otherwise the settings is ignored.
	 * It can be also called many time before the first getRow causing, that settings from the last
	 * call is applied.
	 */
	StartFn fn_start;

	///Function sends text to the output
	/**
	 * The function is available in list(), update() and show()
	 *
	 * @code
	 * void send(StrViewA text);
	 * @endcode
	 *
	 * @param text text send to the output. */

	SendFn fn_send;


	Array rowBuffer;

public:


	///Write key-value pair to the current view
	/**
	 * @param key  key
	 * @param value value
	 *
	 * @note function is available only in mapdoc() function
	 */
	inline void emit(const Key &key, const Value &value) {fn_emit(key,value);}
	///Write key without value to the current view
	/**
	 * @param key  key
	 *
	 * @note function is available only in mapdoc() function
	 */
	inline void emit(const Key &key) {fn_emit(key,nullptr);}
	///Write document to the current view
	/**
	 * @note function is available only in mapdoc() function
	 */
	inline void emit() {fn_emit(nullptr,nullptr);}

	///Send text to the log
	/**
	 * @param msg message which appears in log
	 */
	inline void log(StrViewA msg) {fn_log(msg);}
	///Send message and object to the log
	/**
	 * @param msg message which appears in log
	 * @param data object which appears in log
	 */
	inline void log(StrViewA msg, json::Value data) {fn_log(String({msg,data.toString()}));}

	///Receive next row from the current rowset
	/**
	 * @return Next row in the list. You can use operator ! to test end of list
	 *
	 * @code
	 * ListRow rw;
	 * while (rw = getRow()) {
	 *
	 * }
	 * @endcode
	 *
	 * @note the function is available only in list() function
	 *
	 *
	 */
	inline ListRow getRow() {return fn_getRow();}



	///Fetchs and maps multiple rows into an array
	/**
	 *
	 * @param fn function which defines mapping
	 * @param maxRows maximum count of rows to fetch and map
	 * @return array contains mapped rows. If array is empty, no more rows are available
	 */
	template<typename Fn>
 	inline Value mapRows(Fn fn, std::size_t maxRows =(std::size_t)-1) {
		rowBuffer.clear();
		ListRow rw;
		while (maxRows && (rw = getRow())) {
			rowBuffer.push_back(fn(rw));
		}
		return rowBuffer;
	}

	///Initializes output
	/**
	 * @param headers headers as object key-value
	 * @param code status code;
	 */
	inline void start(json::Value headers, int code = 200) {
		fn_start(Object("code",code)("headers",headers));
	}
	///Send text to the output
	/**
	 * @param str text to send
	 */
	inline void send(StrViewA str) {fn_send(str);}
	///Send json to the outpur
	/**
	 *
	 * @param json json to send
	 */
	inline void sendJSON(const json::Value json) {fn_send(json.stringify());}



	virtual void mapdoc(Document ) override{
		throw std::runtime_error("Function 'void mapdoc(Document)' is not defined");
	}
	virtual Value reduce(RowSet rows) override {
		throw std::runtime_error("Function 'Value reduce(RowSet rows)' is not defined");
	}
	virtual Value rereduce(Value ) override {
		throw std::runtime_error("Function 'Value rereduce(Value)' is not defined");
	}

	virtual void show(Document , Value ) override{
		throw std::runtime_error("Function 'void show(Document doc, Value request)' is not defined");
	}
	virtual void list(Value , Value ) override{
		throw std::runtime_error("Function 'void list(Value head, Value request)' is not defined");
	}
	virtual void update(Document &, Value ) override{
		throw std::runtime_error("Function 'void update(Document &doc, Value request)' is not defined");
	}
	virtual bool filter(Document , Value ) override{
		throw std::runtime_error("Function 'bool filter(Document doc, Value request)' is not defined");
	}
	virtual ValidationResult validate(Document , Context )override {
		throw std::runtime_error("Function 'ValidationResult validate(Document doc, Context context)' is not defined");
	};


	virtual void onClose()override {delete this;}


	virtual void initEmit(EmitFn fn) {fn_emit = fn;}
	virtual void initLog(LogFn fn) {fn_log = fn;}
	virtual void initShowListFns(GetRowFn getrow, SendFn send, StartFn start) {
		this->fn_getRow = getrow;
		this->fn_send = send;
		this->fn_start = start;
	}
};




#endif
