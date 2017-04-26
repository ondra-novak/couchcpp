# CouchCPP

Write your views in C++!

![Screenshot](screenshot.png?raw=true "Screenshot")

## Project status

Full query server. It supports: map, reduce, rereduce, show, list, update, validation and filter (and filter through the view)

Supported software: 
 * CouchDB 1.6.x, 
 * CouchDB 2.0+ (theoretically, not tested, shared code doesn't compile due a bug)
 * Linux (tested in Ubuntu)

Required software:
  
 * CouchDB, 
 * The library "imtjson"(https://github.com/ondra-novak/imtjson), 
 * C++ compiler (G++ compatible) which supports C++11 (you need the compiler on the production server as well)
 * CMake 2.8+  

## General rules 

Function is written as pure text. You can use preprocesor and also include custom headers.
The header inclusion must be specified at the top of the code 

The namespace "json" is always available as well as all functions from the "imtjson". You can also include other namespaces specifying them on the top of the code.
```
#include <utility>
#include <string>

using namespace std;

void mapdoc(Document document) {
   emit(document["_id"], document["_rev"]);
}
```


## API

The full description of the API is provided by couchcpp/parts/common.h There are a lot of types and functions. The script generally
defines implementation of methods from the class AbstractProc.

### handlers

```
void mapdoc(Document doc);
Value reduce(RowSet rows);
Value rereduce(Value values);
void show(Document doc, Value req);
void list(Value head, Value req);
void update(Document &doc, Value req);
bool filter(Document doc, Value req);
ValidationResult validate(Document doc, Context context);

```

### API functions

The script can use following API functions

 - **emit**: emit(key,value), emit(key), emit() - function is available in the script **mapdoc()** only
 - **log**: log(text), log(text, value) - sends message to the logfile
 - **getRow**: ListRow row = getRow() - receives next row from the view.  The function is available in the script **list()**
 - **mapRows**: Value rows = mapRows(fn, count) - maps rows to JSON-array through the function.  The function is available in the script **list()**
 - **start**: start(Value headers, int code=200) -  The function is available in the script **list()**,**show()**,**update()**
-  **send**: send(text) -  The function is available in the script **list()**,**show()**,**update()**
-  **sendJSON**: sendJSON(Value) -  The function is available in the script **list()**,**show()**,**update()**

### Types and Objects

 * all objects from **imtjson** library
 * **Document** - improved json::Value
 * **Key** - json::Value used as key
 * **Context** - validation context, contains document's previous revision, user context and security object
 * **Row** - A single row for reduce () contains key, value and docId
 * **RowIterator** - iterator through rows for the function reduce()
 * **RowSet** - set of rows to reduce
 * **ListRow** - A single row returned by getRow() exposes function to access at key, value, id a and document itself (when include_docs is active)
 * **ValidationResult** - result of validation
 * **Error** - error exception
 * **NotFound** -  not_found exception

 

## custom libraries

To use custom libraries, you need to include special line at the beginning of 
the begging of the code snippet: "//!link" - contains instructions for linker

```
//!link -lssl -lcrypto
#include <openssl/sha.h>

void mapdoc(Document document) {
...
}
```

## shared code

There can be shared code for every script in context of single design document without reduce and rereduce functions.
You can put shared code to the section "views/lib". Each key there is assumed as file, which can be included into the script

```
{
   "views": {
           "lib": {
                "consts.h":"const float pi = 3.14159265;\n"
	   },
           "whatever":{
                 "map":"#include \"consts.h\"\n\nvoid mapdoc(Document doc) { ..."
            }
}
```
(NOTE, shared code doesn't work in CouchDB 2.0 because issue "COUCHDB-3388")

## instalation

### step 1 - install imtjson

```
$ git clone https://github.com/ondra-novak/imtjson.git
$ cd imtjson
$ cmake .
$ make all
$ sudo make install
```

### step 2 - install CouchDB (you can skip this step if the CouchDB is already installed)

```
$ sudo apt install couchdb
```

### step 3 - install couchcpp

```
$ git clone https://github.com/ondra-novak/couchcpp.git
$ cd couchcpp
$ cmake .
$ make all
$ sudo make install
```

Now the language C++ should be available in the Futon

## Configuration

/etc/couchdb/couchcpp.conf

 * **keepSource** - for debugging purpose. The option 'false' (default) causes, that intermediate source
 files are removed. Set this option to 'true' to leave these files in the cache for inspection.
 * **cache** - path to the cache. The cache contains compiled functions into modules *.so. The files
 are never deleted by the application, so the cache can contain many old modules. These modules need to be 
 be deleted manually. It is possible to empty whole directory enforcing to recompile all of currenly used modules.
 * **compiler/program** - contains full path to the **g++**
 * **compiler/param** - options of the program placed before option -o (output) and name of the source file.
 * **compiler/libs** - libraries and other options placed after the source file. 
 
  
 
# Security

 ## Running couchcpp under different user - this prevent to the user code to access couchdb's files. 
  
  1. create new user and group
  2. give /usr/local/bin/couchcpp to the new user and group (chown)
  3. give /var/cache/couchcpp to the new user and group
  4. set suid-bit on couchcpp
  5. restart CouchDB
  
 ## Prevent compilation on production
  
  **couchcpp** can theoretically run without the compiler if you popupate its cache with precompiled objects. However, any
  modification into design documents requires to repopuplate the cache, otherwise the view regeneration fails 
  making modified views unavailable.
  
# Practical advices

 - use couchapp to manage your scripts
 - couchcpp supports option "-c" that allows to check syntax of your code snippets. Use it in your makefiles, or as an hook of couchapp. Also see couchcpp -h
 - the cache can grow faster during development, clean it sometimes. However, this should not be an issue in the production because scripts are not
modified often



