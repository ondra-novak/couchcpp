
#include <grp.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <imtjson/json.h>
#include <imtjson/path.h>
#include <imtjson/validator.h>
#include <pwd.h>
#include <unistd.h>
#include <fstream>

#include "assembly.h"


using namespace json;



typedef std::size_t Hash;

//std::map<String, var> storedDocs;
std::vector<PAssembly> views;
std::map<Hash, PAssembly> fncache;
time_t gcrun  = 0;



void logOut(const StrViewA & msg) {
	var x = {"log",msg};
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


String formatErrorMsg(const var &rejs) {
	std::ostringstream out;

	for (var r : rejs) {

		out << "doc";
		for (var p : r[0]) {
			if (p.type() == json::number) {
				out << "[" << p.getUInt() << "]";
			} else {
				out << "." << p.getString();
			}
		}
		out << " : " << r[1].toString() << ". ";
		out << std::endl;

	}

	return String(out.str());


}

/*
var doCommandDDoc(const var &cmd) {
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
		String subcmd (cmd[2][0]);
		if (subcmd =="filters") {
			String name (cmd[2][1]);
			return doFilter(iter->second, name, cmd[3]);
		} else if (subcmd == "validate_doc_update") {
			return doValidation(iter->second, cmd[3]);
		} else {
			return {"error","Unsupported","Unsupported feature"};
		}

	}

}
*/

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


	Array r;
	Array o;
	IProc::EmitFn emitFn = [&](const Value &key, const Value &value) {
		o.add({key.defined()?key:Value(nullptr), value.defined()?value:Value(nullptr)});
	};
	for (PAssembly x : views) {
		o.clear();
		IProc *p = x->getProc();
		p->initEmit(emitFn);
		p->mapdoc(cmd[1]);
		r.add(o);
	}
	return r;
}


var doReduce(AssemblyCompiler &compiler, const Value &cmd) {
	Array result;
	Value fns = cmd[1];
	for (Value f : fns) {

		PAssembly a = compileFunction(compiler, f.getString());
		IProc *proc = a->getProc();
		Value orgvalues = cmd[2];
		RefCntPtr<ArrayValue> values = ArrayValue::create(orgvalues.size());
		RefCntPtr<ArrayValue> keys = ArrayValue::create(orgvalues.size());
		for (Value v : orgvalues) {
			values->push_back(v[1].getHandle());
			keys->push_back(v[0].getHandle());
		}
		result.push_back(proc->reduce(
							Value(PValue::staticCast(keys)),
							Value(PValue::staticCast(values)))
					);
	}
	return Value({true,result});
}

var doReReduce(AssemblyCompiler &compiler, const Value &cmd) {
	Array result;
	Value fns = cmd[1];
	for (Value f : fns) {
		PAssembly a = compileFunction(compiler,f.getString());
		IProc *proc = a->getProc();
		result.push_back(proc->rereduce(cmd[2]));
	}
	return Value({true,result});
}


static String getcwd() {
	char *p = get_current_dir_name();
	String ret(p);
	free(p);
	return ret;

}

static String relpath(const StrViewA &abspath, const String &relpath) {

	if (relpath.empty()) return relpath;
	if (relpath[0] == '/') return relpath;
	return String({abspath,"/",relpath});
}

static Value loadConfig(const String &path) {
	std::ifstream input(path.c_str(), std::ios::in);
	if (!input) throw std::runtime_error(String({"Unable to open config:", path}).c_str());
	return Value::fromStream(input);
}

static void changeCurrentUser(const String &user) {
	struct passwd *uinfo = getpwnam(user.c_str());
	if (uinfo == 0) throw std::runtime_error(String({"Cannot find user:",user}).c_str());
	if (setuid(uinfo->pw_uid)) throw std::runtime_error(String({"Cannot change to user:",user}).c_str());
}
static void changeCurrentGroup(const String &group) {
	struct group *ginfo = getgrnam(group.c_str());
	if (ginfo == 0) throw std::runtime_error(String({"Cannot find group:",group}).c_str());
	if (setgid(ginfo->gr_gid)) throw std::runtime_error(String({"Cannot change to group:",group}).c_str());

}

int main(int argc, char **argv) {

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



		Value user = cfg["user"];
		Value group = cfg["group"];
		if (!group.getString().empty()) changeCurrentGroup(group.getString());
		if (!user.getString().empty()) changeCurrentUser(user.getString());

		Value x = cfg["cache"];
		if (!x.defined()) throw std::runtime_error("Missing 'cache' in config");
		String strcache = relpath(cwd,String(x));
		x = cfg["compiler"]["program"];
		if (!x.defined()) throw std::runtime_error("Missing 'compiler/program' in config");
		String strcompiler = relpath(cwd,String(x));
		x = cfg["compiler"]["params"];
		if (!x.defined()) throw std::runtime_error("Missing 'compiler/params' in config");
		String strparams(x);
		bool keepSources = cfg["keepSource"].getBool();


		AssemblyCompiler compiler(strcache, strcompiler, strparams, keepSources);


		try {
		do {


			var v = Value::fromStream(std::cin);
// 		    logOut(v.toString());
			var res;
			try {

				String cmd ( v[0]);
				if (cmd == "reset") res = doResetCommand(v);
			//	else if (cmd == "ddoc") res=doCommandDDoc(v);
				else if (cmd == "add_fun") res=doAddFun(compiler,v[1].getString());
				else if (cmd == "reduce") res=doReduce(compiler,v);
				else if (cmd == "rereduce") res=doReReduce(compiler,v);
				else if (cmd == "map_doc") res = doMapDoc(v);
				else res = {"error","unsupported","Operation is not supported by this query server"};

			} catch (std::exception &e) {
				res = {"error", "exception",e.what() };
			}
			res.toStream(std::cout);
			std::cout << std::endl;

		} while (true);

		} catch (std::exception &e) {
			std::cout << var({"error", e.what() }).stringify() << std::endl;
		}

	} catch (std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
	}
}
