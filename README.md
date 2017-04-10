# CouchCPP

Write your views in C++!

## Project status

Query Server allows to write map and reduce functions in C++. 
It uses the library "imtjson" to work with documents and other json values 

## General rules 

Function is written as pure text. You can use preprocesor and also include custom headers.
The header inclusion must be specified at the top of the function. 

The namespace "json" is always available as well as all functions from the "imtjson". 

```
#include <utility>
#include <string>

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

