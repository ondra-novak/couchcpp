#ifndef COUCHCPP_COMMON_HEADER
#define COUCHCPP_COMMON_HEADER

#pragma once

#include "../api.h"


class IProc {
public:



	class IRenderFns {
	public:
		virtual ListRow getRow() = 0;
		virtual void send(StrViewA text) = 0;
		virtual void sendJSON(Value json) = 0;
		virtual void start(Value resp) = 0;
		virtual Array lookup(const Array &docs) = 0;
		virtual Array queryView( StrViewA viewName, const Array &keys, QueryViewOutput outMode = allRows) = 0;
		virtual ~IRenderFns() {}
	};

	typedef std::function<void(const Value &key, const Value &value)> EmitFn;
	typedef std::function<void(const StrViewA &string)> LogFn;


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
	virtual void initRenderFns(IRenderFns *renderFns) = 0;

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

	Array rowBuffer;

public:
	class AbstractRenderFns: public IRenderFns {
	public:
		virtual ListRow getRow() override {return ListRow();}
		virtual void send(StrViewA ) override {}
		virtual void sendJSON(Value ) override {}
		virtual void start(Value ) override {}
		virtual Array lookup(const Array &docs) override {return Array();}
		virtual Array queryView(StrViewA viewName, const Array &keys, QueryViewOutput outMode = allRows) override {
			Array res;
			res.reserve(keys.size());
			for (std::size_t cnt = keys.size(), i = 0; i < cnt; i++) res.push_back(nullptr);
			return res;
		}
	};

	IRenderFns *curRenderFns;
	AbstractRenderFns fallbackRenderFns;


public:

	AbstractProc():curRenderFns(&fallbackRenderFns) {}


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
	inline ListRow getRow() {return curRenderFns->getRow();}



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
		curRenderFns->start(Object("code",code)("headers",headers));
	}
	///Send text to the output
	/**
	 * @param str text to send
	 */
	inline void send(StrViewA str) {curRenderFns->send(str);}
	///Send json to the outpur
	/**
	 *
	 * @param json json to send
	 */
	inline void sendJSON(const json::Value json) {curRenderFns->sendJSON(json);}

	///Lookups for given documents
	/**
	 * This function allows to query other view. Function can be used during render functions, such
	 *   a list(), show(), update() and also during validation().
	 *
	 * @param docIds array of document IDs. Each item must be json::Value of string type.
	 * This allows to put json-Value directly from the source json without extracting and re-creating
	 * a string value
	 * @return returns object, where key carries the document id and value contains the corresponding document
	 *
	 * @note The function is resource intensive and can degrade performance a lot if it is used
	 * more then few times during rendering the result. It is much faster to ask for multiple documents (it can
	 * handle a lot of documents at once!) then calling this function for each document separatedly.
	 */
	inline Array lookup(const Array &docIds) {return curRenderFns->lookup(docIds);}

	///Query other view.
	/**
	 * This function allows to query other view. Function can be used during render functions, such
	 *   a list(), show(), update() and also during validation(). It is very limited, because it
	 *   can query for keys only. It doesn't support search for range.
	 *
	 * if you want to use "current design document"
	 * @param viewName name of the view in the design document
	 * @param keys list of keys to query.
	 * @param flags Combination of QueryViewFlags: includeDocs and groupRows
	 * @return List of found rows for each key. Unless groupRows is in effect, there can be multiple rows
	 * for single key. Each row is represented by a object with following members: "id","key","value","doc". You
	 * always need to pick "key" from the row to determine which key were used for this row
	 *
	 * @note The function is always search in current version of the view (stalled version). It is possible
	 * to receive an old data in case that view has not been recently updated. If you want to use this
	 * feature, you need to keep the views updated.
	 */
	inline Array queryView(StrViewA viewName, const Array &keys, QueryViewOutput outMode = allRows) {
		return curRenderFns->queryView(viewName, keys, outMode);
	}


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
	virtual void initRenderFns(IRenderFns *rfns) {
		if (rfns)curRenderFns = rfns; else curRenderFns = &fallbackRenderFns;
	}
};




#endif
