#include <functional>
#include <imtjson/json.h>

using namespace json;


#define INTERFACE_VERSION "1.0.2"


class IProc {
public:

	typedef std::function<void(const Value &key, const Value &value)> EmitFn;
	typedef std::function<void(const StrViewA &string)> LogFn;
	typedef std::function<Value()> GetRowFn;
	typedef std::function<void(const StrViewA &)> SendFn;
	typedef std::function<void(const Value &)> StartFn;

	struct ContextData {
		Value prevDoc;
		Value user;
		Value security;

		ContextData(Value prevDoc,Value user,Value security):prevDoc(prevDoc),user(user),security(security) {}
	};



	class Row {
	public:
		Value key;
		Value value;
		Value docId;

		Row(Value row):key(row[0][0]),value(row[1]),docId(row[0][1]) {}

	};

	class RowIterator: public ValueIterator {
	public:
		RowIterator(const ValueIterator &iter):ValueIterator(iter) {}

		Row operator *() const {return Row(ValueIterator::operator *());}

	    typedef Row value_type;
	    typedef Row *        pointer;
	    typedef Row &        reference;
	    typedef std::intptr_t  difference_type;
	};

	class RowSet: public Value {
	public:

		RowSet(Value v):Value(v) {}
		Row operator[](int pos) const {return Row(Value::operator[](pos));}
		RowIterator begin() const {return RowIterator(Value::begin());}
		RowIterator end() const {return RowIterator(Value::end());}

	};

	class Error: public std::exception {
	public:
		Error(String type, String desc):type(type),desc(desc) {}

		String type;
		String desc;

		const char *what() const throw() {return type.c_str();}
		virtual ~Error() throw() {}

	};

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

	///Map document to the view
	/**
	 * @param doc document to map. To perform mapping, call emit(key,value) inside of the function
	 */
	virtual void mapdoc(Value doc) = 0;
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
	virtual void show(Value doc, Value request) = 0;
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
	virtual void update(Value &doc, Value request) = 0;

	///Filter function
	/**
	 * Called to filter documents
	 * @param doc document
	 * @param request request context
	 * @retval true use this document
	 * @retval false skip this document
	 */
	virtual bool filter(Value doc, Value request) = 0;

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
	virtual ValidationResult validate(Value doc, Context context) = 0;

	virtual void onClose() = 0;


	virtual void initEmit(EmitFn fn) = 0;
	virtual void initLog(LogFn fn) = 0;
	virtual void initShowListFns(GetRowFn getrow, SendFn send, StartFn start) = 0;

	virtual ~IProc() {}
};

class AbstractProc: public IProc {
public:

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
	EmitFn emit;

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
	LogFn log;

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
	GetRowFn getRow;

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
	StartFn start;

	///Function sends text to the output
	/**
	 * The function is available in list(), update() and show()
	 *
	 * @code
	 * void send(StrViewA text);
	 * @endcode
	 *
	 * @param text text send to the output. */

	SendFn send;


	virtual void mapdoc(Value ) override{
		throw std::runtime_error("Function 'void mapdoc(Value)' is not defined");
	}
	virtual Value reduce(RowSet rows) override {
		throw std::runtime_error("Function 'Value reduce(RowSet rows)' is not defined");
	}
	virtual Value rereduce(Value ) override {
		throw std::runtime_error("Function 'Value rereduce(Value)' is not defined");
	}
	virtual Value reduce(Value , Value , bool )  {
	}

	virtual void show(Value , Value ) {
		throw std::runtime_error("Function 'void show(Value doc, Value request)' is not defined");
	}
	virtual void list(Value , Value ) {
		throw std::runtime_error("Function 'void list(Value head, Value request)' is not defined");
	}
	virtual void update(Value &, Value ) {
		throw std::runtime_error("Function 'void update(Value &doc, Value request)' is not defined");
	}
	virtual bool filter(Value , Value ) {
		throw std::runtime_error("Function 'bool filter(Value doc, Value request)' is not defined");
	}
	virtual ValidationResult validate(Value , Context ) {
		throw std::runtime_error("Function 'ValidationResult validate(Value doc, Context context)' is not defined");
	};


	virtual void onClose()override {delete this;}


	virtual void initEmit(EmitFn fn) {emit = fn;}
	virtual void initLog(LogFn fn) {log = fn;}
	virtual void initShowListFns(GetRowFn getrow, SendFn send, StartFn start) {
		this->getRow = getrow;
		this->send = send;
		this->start = start;
	}
};




