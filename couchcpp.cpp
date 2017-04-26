
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

#include "module.h"


using namespace json;



typedef std::size_t Hash;

std::map<String, var> storedDocs;
std::vector<PModule> views;
std::map<Hash, PModule> fncache;
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



 var doResetCommand(ModuleCompiler &comp, const var &cmd) {
 	views.clear();
 	runGC();
 	comp.dropEnv();
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

	bool isEof() {
		int c = in.peek();
		while (c != EOF && isspace(c)) {
			in.get();
			c = in.peek();
		}
		return c == EOF;
	}

protected:
	std::istream &in;
	std::ostream &out;
};




PModule compileFunction(ModuleCompiler& compiler, const StrViewA& cmd) {
	StrViewA code = cmd;
	std::size_t hash = compiler.calcHash(code);
	PModule &a = fncache[hash];
	if (a == nullptr) {
		a = compiler.compile(code);
		IProc* proc = a->getProc();
		proc->initLog(&logOut);
	}
	return a;
}


var doAddFun(ModuleCompiler &compiler, const StrViewA &cmd) {
	views.push_back(compileFunction(compiler,cmd));
	return true;
}

var doMapDoc(const var &cmd) {


	Array r;
	Array o;
	IProc::EmitFn emitFn = [&](const Value &key, const Value &value) {
		o.add({key.defined()?key:Value(nullptr), value.defined()?value:Value(nullptr)});
	};

	for (PModule x : views) {
		o.clear();
		IProc *p = x->getProc();
		p->initEmit(emitFn);
		p->mapdoc(cmd[1]);
		r.add(o);
	}
	return r;
}


var doReduce(ModuleCompiler &compiler, const Value &cmd) {

	Array result;
	Value fns = cmd[1];
	for (Value f : fns) {

		PModule a = compileFunction(compiler, f.getString());
		IProc *proc = a->getProc();
		Value orgvalues = cmd[2];
		result.push_back(proc->reduce(RowSet(orgvalues)));
	}
	return Value({true,result});
}

var doReReduce(ModuleCompiler &compiler, const Value &cmd) {

	Array result;
	Value fns = cmd[1];
	for (Value f : fns) {
		PModule a = compileFunction(compiler,f.getString());
		IProc *proc = a->getProc();
		result.push_back(proc->rereduce(cmd[2]));
	}
	return Value({true,result});
}

static String relpath(const StrViewA &abspath, const String &relpath) {

	if (relpath.empty()) return relpath;
	if (relpath[0] == '/') return relpath;
	return String({abspath,"/",relpath});
}


var doAddLib(ModuleCompiler &compiler,  Value lib) {

	compiler.setSharedCode(lib);
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
	buff.clear();
	Value respObj(json::object);
	Value doc = args[0];
	Value request = args[1];
	proc.initShowListFns([&]() -> ListRow { return Value(nullptr);},
			[](const StrViewA &v) {buff.push_back(v);},
	         [&](const Value &resp) {respObj = resp;});
	proc.show(doc,request);
	return {"resp",respObj.replace(Path::root/"body",buff.str())};
}

var doCommandDDocUpdates(IProc &proc, Value args) {
	buff.clear();
	Value respObj(json::object);
	Document doc = args[0];
	Document newdoc = doc;
	Value request = args[1];
	proc.initShowListFns([&] () -> ListRow { return Value(nullptr);},
			[](const StrViewA &v) {buff.push_back(v);},
			[&](const Value &resp) {respObj = resp;});
	proc.update(newdoc,request);
	if (newdoc.isCopyOf(doc)) newdoc = Value( nullptr);
	return {"up",newdoc,respObj.replace(Path::root/"body",buff.str())};
}

var doCommandDDocList(IProc &proc, Value args, JSONStream &stream) {
	buff.clear();
	Value respObj(json::object);
	Value head = args[0];
	Value request = args[1];
	bool isend = false;
	bool needstart = true;
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
	return {"end",Value(json::array,{buff.str()})};
}

var doCommandDDocFilters(IProc &proc, Value args) {
	Value docs = args[0];
	Value req = args[1];
	Array results;
	results.reserve(docs.size());
	for (Value doc : docs) {
		results.push_back(proc.filter(doc,req));
	}
	return {true,results};
}

var doCommandDDocViews(IProc &proc, Value args) {
	bool docres;
	IProc::EmitFn emitFn =[&docres](Value,Value) {docres = true;};
	proc.initEmit(emitFn);
	Value docs = args[0];

	Array results;
	results.reserve(docs.size());
	for (Value doc : docs) {
		docres = false;
		proc.mapdoc(doc);
		results.push_back(docres);
	}
	return {true,results};
}

var doCommandDDocValidate(IProc &proc, Value args) {
	Value doc = args[0];
	Value prevDoc = args[1];
	Value userContext = args[2];
	Value security = args[3];

	ValidationResult res = proc.validate(doc,ContextData(prevDoc, userContext, security));
	switch (res.decree) {
	case accepted: return 1;
	case rejected: return {"error","validation_rejected",res.description};
	case unauthorized: return Object("unauthorized",res.description);
	case forbidden: return Object("forbidden",res.description);
	}
	return 1;
}


var doCommandDDoc(ModuleCompiler &compiler, const var &cmd, JSONStream &stream) {
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
		PModule a = compileFunction(compiler,fn.getString());
		IProc *proc = a->getProc();

		if (callType == "shows") return doCommandDDocShow(*proc, cmd[3]);
		else if (callType == "lists") return doCommandDDocList(*proc, cmd[3], stream);
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
		String tryCompile;
		String cacheOverride;
		std::vector<String> populate;
		bool clearcache = false;
		bool needPopulate = false;



		int argp = 1;
		while (argp < argc) {
			StrViewA a(argv[argp++]);
			if (a == "-f") {
				if (argp >= argc) throw std::runtime_error("Missing argument after -f");
				cfgpath = argv[argp++];
			}
			else if (a == "-c") {
				if (argp >= argc) throw std::runtime_error("Missing argument after -c");
				tryCompile = relpath(cwd,argv[argp++]);
			}
			else if (a == "-l") {
				if (argp >= argc) throw std::runtime_error("Missing argument after -l");
				String absdir = relpath(cwd,argv[argp++]);
				if (chdir(absdir.c_str())) {
					std::cerr << "Failed to change directory (ignored): " << absdir << std::endl;
				}
			}
			else if (a == "-h") {
				std::cerr << argv[0] << " -f <config> [ -c <file> [ -l <dir>] ] [ -p <files...>][-c][-o <dir>]" << std::endl;
				std::cerr << std::endl;
				std::cerr << "-f\tSpecifies path to configuration file (mandatory)" << std::endl;
				std::cerr << std::endl;
				std::cerr << "-c\tOpens specified file and tries to compile function in it." << std::endl;
				std::cerr << "\tIt doesn't generate module. In case that compiler fails, " << std::endl
						  << "\ta report is send to standard error (and return value indicates error)" << std::endl;
				std::cerr << "-l\tSpecify path to lib directory" << std::endl ;
				std::cerr << "-p\tPopuplate the cache by compiling specified files" << std::endl ;
				std::cerr << "-r\tClear cache"<< std::endl << std::endl;
				std::cerr << "-o\tPut results to the specified directory (overrides configuration file)"<< std::endl << std::endl;


				return 1;
			}
			else if (a == "-p") {
				while (argp < argc && argv[argp][0] != '-') {
					populate.push_back(relpath(cwd,argv[argp++]));
				}
				needPopulate = true;
			}
			else if (a == "-r") {
				clearcache = true;
			}
			else if (a == "-o") {
				if (argp >= argc) throw std::runtime_error("Missing argument after -o");
				cacheOverride = relpath(cwd,argv[argp++]);
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
		if (!cacheOverride.empty()) strcache = cacheOverride;


		ModuleCompiler compiler(strcache, strcompiler, strparams, strlibs, keepSources);

		if (clearcache) {
			compiler.clearCache();
		}
		if (!tryCompile.empty()) {
			return compiler.compileFromFile(tryCompile,false);
		}
		if (needPopulate) {
			if (populate.empty()) {
					std::cerr << "Nothing to populate" << std::endl;
			} else {
				for (auto s: populate) compiler.compileFromFile(s,true);
			}
			return 0;
		}

		logOut("MIT License - Copyright (c) 2017 Ondrej Novak");
		logOut("Project origin: https://github.com/ondra-novak/couchcpp");
		logOut("Use -h for help");

		try {
		while (!stream.isEof()) {


			var v = stream.read();
// 		    logOut(v.toString());
			var res;
			try {

				String cmd ( v[0]);
				if (cmd == "reset") res = doResetCommand(compiler,v);
				else if (cmd == "add_lib") res=doAddLib(compiler,v[1]);
				else if (cmd == "add_fun") res=doAddFun(compiler,v[1].getString());
				else if (cmd == "reduce") res=doReduce(compiler,v);
				else if (cmd == "rereduce") res=doReReduce(compiler,v);
				else if (cmd == "map_doc") res = doMapDoc(v);
				else if (cmd == "ddoc") res = doCommandDDoc(compiler,v,stream);
				else res = {"error","unsupported","Operation is not supported by this query server"};

			} catch (const CompileError &e) {
				res = {"error","compile_error",e.what()};
			} catch (const Error &e) {
				res = {"error", e.type,e.desc };
			} catch (std::exception &e) {
				res = {"error", "general_error",e.what() };
			}
			stream.write(res);

		}

		} catch (std::exception &e) {
			stream.write({"error","general_error", e.what() });
		}

	} catch (std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
	}
}
