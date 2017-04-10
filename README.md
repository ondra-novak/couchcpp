# CouchCPP

Write your views in C++!

## Project status

Query Server allows to write map and reduce functions in C++. 
It uses the library "imtjson" to work with documents and other json values 

## General rules 

Function is written as pure text. You can use preprocesor and also include custom headers.
The header inclusion must be specified at the top of the code 

The namespace "json" is always available as well as all functions from the "imtjson". You can also include other namespaces specifying them on the top of the code.
```
#include <utility>
#include <string>

using namespace std;

void mapdoc(Value document) {
   emit(documet["_id"], document);
}
```


## map function

The map function must be declared as **void mapdoc(Value document)**. To emit the
key-value pair, you can use function **void emit(Value key, Value value);**


```
void mapdoc(Value document) {
   emit(documet["_id"], document);
}
```

## reduce function

Reduce function can be used in two forms

### javascript style

```
Value reduce(Value keys, Value values, bool rereduce);
```

### split style

```
Value reduce(Value keys, Value values);
Value rereduce( values);
```

## debugging

Functions are hard to debug. However you can use simple logging feature through the
CouchDB's logfile

```
void log(StrViewA text)
```

## custom libraries

To use custom libraries, you need to include special line at the beginning of 
the begging of the code snippet: "libs"

```
#libs -lssl -lcrypto
#include <openssl/sha.h>

void mapdoc(Value document) {
...
}
```

## instalation

### step 1 - install imtjson

```
$ git clone https://github.com/ondra-novak/imtjson
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
$ git clone https://github.com/ondra-novak/couchcpp
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
 
  
# Limitations

 * Currently, only map and reduce functions are supported.
 
# Security

 - Running couchcpp under different user - this prevent to the user code to access couchdb's files. 
  
  1. create new user and group
  2. give /usr/local/bin/couchcpp to the new user and group (chown)
  3. give /var/cache/couchcpp to the new user and group
  4. set suid-bit on couchcpp
  5. restart CouchDB
  
  


