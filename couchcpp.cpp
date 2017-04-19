
#include <grp.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <imtjson/json.h>
#include <imtjson/path.h>
#include <imtjson/validator.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>

#include "assembly.h"


using namespace json;



typedef std::size_t Hash;

std::map<String, var> storedDocs;
std::vector<PAssembly> views;
std::map<Hash, PAssembly> fncache;
time_t gcrun  = 0;



void logOut(const StrViewA & msg) {
	var x = {"log",String({"(couchcpp) ", msg})};
	x.toStream(std::cout);
	std::endl(std::cout);

}

void runGC() {
	time_t x;
	time(&x);
	if (x > gcrun) {
		fncache.clear();
	}
	gcrun = x+5;
}



 var doResetCommand(const var &cmd) {
 	views.clear();
 	runGC();
 	return true;
 }


class JSONStream {
public:
	JSONStream(std::istream &input, std::ostream &output):in(input),out(output) {}

	json::Value read() {
		return json::Value::fromStream(in);
	}

	void write(json::Value v) {
		v.toStream(out);
		out << std::endl;
	}

protected:
	std::istream &in;
	std::ostream &out;
};



class ErrorObserver: public Value {
public:

	class ObserverFn {
	public:
		Value &owner;
		ObserverFn(Value &owner):owner(owner) {}
		void operator()(Value x) const {owner = x;}
	};

	ObserverFn getFn()  {return ObserverFn(*this);}
	bool operator!() const {return defined() && (*this)[0].getString() == "error";}

};

PAssembly compileFunction(AssemblyCompiler& compiler, const StrViewA& cmd) {
	StrViewA code = cmd;
	std::size_t hash = compiler.calcHash(code);
	PAssembly &a = fncache[hash];
	if (a == nullptr) {
		a = compiler.compile(code);
		IProc* proc = a->getProc();
		proc->initLog(&logOut);
	}
	return a;
}

var doAddFun(AssemblyCompiler &compiler, const StrViewA &cmd) {
	views.push_back(compileFunction(compiler,cmd));
	return true;
}

var doMapDoc(const var &cmd) {


	ErrorObserver eobs;
	Array r;
	Array o;
	IProc::EmitFn emitFn = [&](const Value &key, const Value &value) {
		o.add({key.defined()?key:Value(nullptr), value.defined()?value:Value(nullptr)});
	};
	IProc::SetErrorFn errorFn = eobs.getFn();

	for (PAssembly x : views) {
		o.clear();
		IProc *p = x->getProc();
		p->initEmit(emitFn);
		p->initSetErrorFn(errorFn);
		p->mapdoc(cmd[1]);
		r.add(o);
	}
	if (!eobs) return eobs;
	return r;
}


var doReduce(AssemblyCompiler &compiler, const Value &cmd) {
	ErrorObserver eobs;
	IProc::SetErrorFn errorFn = eobs.getFn();

	Array result;
	Value fns = cmd[1];
	for (Value f : fns) {

		PAssembly a = compileFunction(compiler, f.getString());
		IProc *proc = a->getProc();
		proc->initSetErrorFn(errorFn);
		Value orgvalues = cmd[2];
		result.push_back(proc->reduce(IProc::RowSet(orgvalues)));
	}
	if (!eobs) return eobs;
	return Value({true,result});
}

var doReReduce(AssemblyCompiler &compiler, const Value &cmd) {
	ErrorObserver eobs;
	IProc::SetErrorFn errorFn = eobs.getFn();

	Array result;
	Value fns = cmd[1];
	for (Value f : fns) {
		PAssembly a = compileFunction(compiler,f.getString());
		IProc *proc = a->getProc();
		proc->initSetErrorFn(errorFn);
		result.push_back(proc->rereduce(cmd[2]));
	}
	if (!eobs) return eobs;
	return Value({true,result});
}

static String relpath(const StrViewA &abspath, const String &relpath) {

	if (relpath.empty()) return relpath;
	if (relpath[0] == '/') return relpath;
	return String({abspath,"/",relpath});
}


var doAddLib(String cachePath, Value lib) {

	//logOut(lib.toString());
	for (Value x : lib) {
		StrViewA key = x.getKey();
		if (!key.empty()) {
			String path = relpath(cachePath, key);
			if (x.type() == json::object) {
				mkdir(path.c_str(),0777); //will be modified by umask
				doAddLib(path,x);
			} else if (x.type() == json::string) {
				StrViewA content = x.getString();
				std::ofstream outf(path.c_str(), std::ios::out| std::ios::trunc);
				if (!outf) {
					throw std::runtime_error(String({"Unable to write to file:", path}).c_str());
				}
				outf.write(content.data, content.length);
				logOut(String({"Imported: ", path}));
			} else {
				logOut(String({"Warning: Can't import :",x.toString()}));
			}
		} else {
			logOut(String({"Skipping an empty key for: ", lib.toString()}));
		}
	}
	return true;


}

class TextBuffer {
public:
	void clear() {
		outbuffer.clear();
	}
	String str() const {
		return StrViewA(outbuffer.data(), outbuffer.size());
	}
	void push_back(char c) {
		outbuffer.push_back(c);
	}
	void push_back(StrViewA txt) {
		outbuffer.reserve(outbuffer.size()+txt.length);
		for (auto c: txt) outbuffer.push_back(c);
	}

protected:
	std::vector<char> outbuffer;
};


static TextBuffer buff;

var doCommandDDocShow(IProc &proc, Value args) {
	ErrorObserver eobs;
	IProc::SetErrorFn errorFn = eobs.getFn();
	buff.clear();
	Value respObj(json::object);
	Value doc = args[0];
	Value request = args[1];
	proc.initSetErrorFn(eobs.getFn());
	proc.initShowListFns([&]{Value r = doc; doc = nullptr; return r;},
			[](const StrViewA &v) {buff.push_back(v);},
	         [&](const Value &resp) {respObj = resp;});
	proc.show(doc,request);
	if (!eobs) return eobs;
	return {"resp",respObj.replace(Path::root/"body",buff.str())};
}

var doCommandDDocUpdates(IProc &proc, Value args) {
	ErrorObserver eobs;
	IProc::SetErrorFn errorFn = eobs.getFn();
	buff.clear();
	Value respObj(json::object);
	Value doc = args[0];
	Value newdoc = doc;
	Value request = args[1];
	proc.initSetErrorFn(eobs.getFn());
	proc.initShowListFns([&]{Value r = doc; doc = nullptr; return r;},
			[](const StrViewA &v) {buff.push_back(v);},
			[&](const Value &resp) {respObj = resp;});
	proc.update(newdoc,request);
	if (!eobs) return eobs;
	if (newdoc.isCopyOf(doc)) newdoc = nullptr;
	return {"up",newdoc,respObj.replace(Path::root/"body",buff.str())};
}

var doCommandDDocList(IProc &proc, Value args, JSONStream &stream) {
	ErrorObserver eobs;
	IProc::SetErrorFn errorFn = eobs.getFn();
	buff.clear();
	Value respObj(json::object);
	Value head = args[0];
	Value request = args[1];
	bool isend = false;
	bool needstart = true;
	proc.initSetErrorFn(eobs.getFn());
	proc.initShowListFns(
			[&]() -> Value {
				if (isend) return nullptr;
				Value s;
				if (needstart) {
					s = {"start",Value(json::array,{buff.str()}), respObj};
					needstart = false;
				} else {
					s = {"chunks",Value(json::array,{buff.str()})};
				}
				buff.clear();
				stream.write(s);
				Value r = stream.read();
				StrViewA cmd = r[0].getString();
				if (cmd == "list_row") {
					return r[1];
				} else {
					isend = true;
					return nullptr;
				}
			},
			[](const StrViewA &v) {buff.push_back(v);},
			[&](const Value &resp) {respObj = resp;});

	proc.list(head,request);
	if (!eobs) return eobs;
	return {"end",Value(json::array,{buff.str()})};
}

var doCommandDDocFilters(IProc &proc, Value args) {
	ErrorObserver eobs;
	IProc::SetErrorFn errorFn = eobs.getFn();
	proc.initSetErrorFn(eobs.getFn());
	Value docs = args[0];
	Value req = args[1];
	Array results;
	results.reserve(docs.size());
	for (Value doc : docs) {
		results.push_back(proc.filter(doc,req));
		if (!eobs) return eobs;
	}
	return {true,results};
}

var doCommandDDocViews(IProc &proc, Value args) {
	ErrorObserver eobs;
	bool docres;
	IProc::SetErrorFn errorFn = eobs.getFn();
	IProc::EmitFn emitFn =[&docres](Value,Value) {docres = true;};
	proc.initSetErrorFn(eobs.getFn());
	proc.initEmit(emitFn);
	Value docs = args[0];

	Array results;
	results.reserve(docs.size());
	for (Value doc : docs) {
		docres = false;
		proc.mapdoc(doc);
		if (!eobs) return eobs;
		results.push_back(docres);
	}
	return {true,results};
}

var doCommandDDocValidate(IProc &proc, Value args) {
	ErrorObserver eobs;
	IProc::SetErrorFn errorFn = eobs.getFn();
	Value doc = args[0];
	Value prevDoc = args[1];
	Value userContext = args[2];
	Value security = args[3];

	proc.initSetErrorFn(eobs.getFn());
	proc.validate(doc,IProc::ContextData(prevDoc, userContext, security));
	if (!eobs) return eobs;
	return 1;
}


var doCommandDDoc(AssemblyCompiler &compiler, const var &cmd, JSONStream &stream) {
	String id (cmd[1]);
	if (id == "new") {
		String id ( cmd[2]);
		var doc = cmd[3];
		if (id.defined() && doc.defined()) {
			storedDocs[id] = doc;
			return true;
		}
		else return {"error","Internal error","Failed to update design document"};
	} else {

		auto iter = storedDocs.find(id);
		if (iter == storedDocs.end()) {
			return {"error","Internal error","Unknown design document"};
		}
		Value fn = iter->second;
		for (Value v : cmd[2]) {
			fn = fn[v.getString()];
			if (!fn.defined()) {
				return {"error","not_found","Required function not exists"};
			}
		}

		StrViewA callType = cmd[2][0].getString();
		PAssembly a = compileFunction(compiler,fn.getString());
		IProc *proc = a->getProc();

		if (callType == "shows") return doCommandDDocShow(*proc, cmd[3]);
		else if (callType == "list") return doCommandDDocList(*proc, cmd[3], stream);
		else if (callType == "updates") return doCommandDDocUpdates(*proc, cmd[3]);
		else if (callType == "filters") return doCommandDDocFilters(*proc, cmd[3]);
		else if (callType == "views") return doCommandDDocViews(*proc, cmd[3]);
		else if (callType == "validate_doc_update") return doCommandDDocValidate(*proc, cmd[3]);
		else return {"error","Unsupported","Unsupported feature"};
	}

}




static String getcwd() {
	char *p = get_current_dir_name();
	String ret(p);
	free(p);
	return ret;

}


static Value loadConfig(const String &path) {
	std::ifstream input(path.c_str(), std::ios::in);
	if (!input) throw std::runtime_error(String({"Unable to open config:", path}).c_str());
	return Value::fromStream(input);
}

int main(int argc, char **argv) {

	JSONStream stream(std::cin, std::cout);

	try {
		String cwd = getcwd();
		String cfgpath = "/etc/couchdb/couchcpp.conf";
		String appwd = relpath(cwd, argv[0]);
		cwd = cwd.substr(0,appwd.lastIndexOf("/"));



		int argp = 1;
		while (argp < argc) {
			StrViewA a(argv[argp++]);
			if (a == "-f") {
				if (argp >= argc) throw std::runtime_error("Missing argument after -f");
				cfgpath = argv[argp++];
			}
		}

		cfgpath = relpath(cwd, cfgpath);
		Value cfg = loadConfig(relpath(cwd,cfgpath));
		cwd = cfgpath.substr(0, cfgpath.lastIndexOf("/"));


		Value x = cfg["cache"];
		if (!x.defined()) throw std::runtime_error("Missing 'cache' in config");
		String strcache = relpath(cwd,String(x));
		x = cfg["compiler"]["program"];
		if (!x.defined()) throw std::runtime_error("Missing 'compiler/program' in config");
		String strcompiler = relpath(cwd,String(x));
		x = cfg["compiler"]["params"];
		if (!x.defined()) throw std::runtime_error("Missing 'compiler/params' in config");
		String strparams(x);
		x = cfg["compiler"]["libs"];
		String strlibs(x);

		bool keepSources = cfg["keepSource"].getBool();


		AssemblyCompiler compiler(strcache, strcompiler, strparams, strlibs, keepSources);


		try {
		do {


			var v = stream.read();
// 		    logOut(v.toString());
			var res;
			try {

				String cmd ( v[0]);
				if (cmd == "reset") res = doResetCommand(v);
				else if (cmd == "add_lib") res=doAddLib(strcache,v[1]);
				else if (cmd == "add_fun") res=doAddFun(compiler,v[1].getString());
				else if (cmd == "reduce") res=doReduce(compiler,v);
				else if (cmd == "rereduce") res=doReReduce(compiler,v);
				else if (cmd == "map_doc") res = doMapDoc(v);
				else if (cmd == "ddoc") res = doCommandDDoc(compiler,v,stream);
				else res = {"error","unsupported","Operation is not supported by this query server"};

			} catch (std::exception &e) {
				res = {"error", "general_error","exception",e.what() };
			}
			stream.write(res);

		} while (true);

		} catch (std::exception &e) {
			stream.write({"error","general_error", e.what() });
		}

	} catch (std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
	}
}
