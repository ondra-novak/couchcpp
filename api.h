/*
 * api.h
 *
 * This is a fake header, which can be included into code fragnents to help various source assistants to
 * find symbols. It is not used during compilation.
 *
 *  Created on: 26. 4. 2017
 *      Author: ondra
 */

#ifndef COUCHCPP_API_HEADER_
#define COUCHCPP_API_HEADER_

#pragma once

#include <functional>
#include <imtjson/json.h>

#define INTERFACE_VERSION "1.1.0"

using namespace json;


namespace {


inline String encodeURIComponent(StrViewA component) {
	return String(urlEncoding->encodeBinaryValue(BinaryView(component)));
}


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

enum QueryViewOutput {
	///For each key, return all matching rows
	/** It put rows into array, even if there is only one row */
	allRows,
	///For each key, return all matching rows and associated documents
	/** It put rows into array, even if there is only one row */
	allRows_includeDocs,
	///For each key, group rows into single row
	/** It put one row for each key */
	groupRows

};



#ifndef __COUCHCPP_COMPILER


///Write key-value pair to the current view
/**
 * @param key  key
 * @param value value
 *
 * @note function is available only in mapdoc() function
 */
void emit(const Key &key, const Value &value);
///Write key without value to the current view
/**
 * @param key  key
 *
 * @note function is available only in mapdoc() function
 */
void emit(const Key &key);
///Write document to the current view
/**
 * @note function is available only in mapdoc() function
 */
void emit();

///Send text to the log
/**
 * @param msg message which appears in log
 */
void log(StrViewA msg);
///Send message and object to the log
/**
 * @param msg message which appears in log
 * @param data object which appears in log
 */
void log(StrViewA msg, json::Value data);

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
ListRow getRow();



///Fetchs and maps multiple rows into an array
/**
 *
 * @param fn function which defines mapping
 * @param maxRows maximum count of rows to fetch and map
 * @return array contains mapped rows. If array is empty, no more rows are available
 */
template<typename Fn>
Value mapRows(Fn fn, std::size_t maxRows =(std::size_t)-1);

///Initializes output
/**
 * @param headers headers as object key-value
 * @param code status code;
 */
void start(json::Value headers, int code = 200);
///Send text to the output
/**
 * @param str text to send
 */
void send(StrViewA str);
///Send json to the outpur
/**
 *
 * @param json json to send
 */
void sendJSON(const json::Value json);

///Lookups for given documents
/**
 * This function allows to query other view. Function can be used during render functions, such
 *   a list(), show(), update()
 *
 * @param docIds array of document IDs. Each item must be json::Value of string type.
 * This allows to put json-Value directly from the source json without extracting and re-creating
 * a string value
 * @return returns array, which is 1:1 map to the arguments. For each item in arguments, there
 * is item containing the document matching to corresponding document ID. If the document is
 * not exists (deleted or not found), the null appear instead.
 *
 * @note The function is resource intensive and can degrade performance a lot if it is used
 * more then few times during rendering the result. It is much faster to ask for multiple documents (it can
 * handle a lot of documents at once!) then calling this function for each document separatedly.
 */
inline Array lookup(const Array &docIds);

///Query other view.
/**
 * This function allows to query other view. Function can be used during render functions, such
 *   a list(), show(), update() It is very limited, because it can query for keys only.
 *   It doesn't support search for a range and groupLevel for reduce
 *
 * @param viewName name of the view in the current design document
 * @param keys list of keys to query.
 * @param outMode defines which result will be returned. For allRows or allRows_includeDocs,
 *  there will be array for every matching key from the argument keys. Non-matching keys
 *  have there null. The array can contain one or multiple rows depending on, how many rows matches.
 *  For the groupRows, the reduce is used to group rows into one. For every key there is
 *  directly the grouped row (no array)
 * * @return List of found rows for each key.
 *
 * @note The function is always search in current version of the view (stalled version). It is possible
 * to receive an old data in case that view has not been recently updated. If you want to use this
 * feature, you need to keep the views updated.
 */
inline Array queryView(StrViewA viewName, const Array &keys, QueryViewOutput outMode = allRows);


#endif

}









#endif /* API_H_ */
