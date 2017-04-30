/*
 * assembly.cpp
 *
 *  Created on: Apr 9, 2017
 *      Author: ondra
 */

#include <unistd.h>
#include "module.h"
#include <dlfcn.h>
#include <imtjson/fnv.h>
#include <cstring>
#include <fstream>
#include <ftw.h>
#include <signal.h>

Module::Module(String path):path(path) {

	libHandle = dlopen(path.c_str(),RTLD_NOW);
	if (libHandle == nullptr)
		throw std::runtime_error(String({"Cannot open module: ", path, " - ", strerror(errno)}).c_str());

	EntryPoint e = (EntryPoint)dlsym(libHandle, "initProc");
	if (e == nullptr) {
		dlclose(libHandle);
		throw std::runtime_error(String({"Module is corrupted: ", path, " - ", strerror(errno)}).c_str());
	}

	proc = e();
}

Module::~Module() {
	proc->onClose();
	dlclose(libHandle);
}

ModuleCompiler::ModuleCompiler(String cachePath, String gccPath, String gccOpts, String gccLibs, bool keepSource, LogOutFn fn)
	:cachePath(cachePath)
	,gccPath(gccPath)
	,gccOpts(gccOpts)
	,gccLibs(gccLibs)
	,keepSource(keepSource)
	,logOutFn(fn)
{

}


String hashToModuleName(std::size_t sz) {
	char buff[256];
	char *c = buff;
	base64url->encodeBinaryValue(BinaryView(reinterpret_cast<const unsigned char *>(&sz), sizeof(sz)),[&](StrViewA str){
		std::memcpy(c,str.data,str.length);
		c+=str.length;
	});
	return String({"mod_",StrViewA(buff, c- buff)});
}


bool ModuleCompiler::isCompiled(StrViewA code) const {
	std::size_t hash = calcHash(code);
	String strhash = hashToModuleName(hash);
	String modulePath ({cachePath,"/",strhash,".so"});
	return access(modulePath.c_str(), F_OK) == 0;
}

PModule ModuleCompiler::compile(StrViewA code) const {
	std::size_t hash = calcHash(code);
	String strhash = hashToModuleName(hash);


	String modulePath ({cachePath,"/",strhash,".so"});


	if (access(modulePath.c_str(), F_OK) != 0) {

		String srcPath ({cachePath,"/",strhash,".cpp"});
		String envPath = prepareEnv();
		String envSrcPath ({envPath,"/", strhash,".cpp"});
		String envModulePath ({envPath,"/", strhash,".so"});

		SourceInfo src = createSource(code,"code_fragment");
		{
			std::ofstream t(envSrcPath.c_str(),std::ios::out);
			if (!t) {
				throw std::runtime_error(String({"Failed to create file: ",srcPath}).c_str());
			}
			t.write(src.sourceCode.c_str(), src.sourceCode.length());
		}


		String cmdLine({
			gccPath, " ",
			gccOpts, " ",
			" -o ", envModulePath,
			" ", envSrcPath,
			" ",src.libraries,
			" ",gccLibs,
			" 2>&1"});

		logOutFn(String({"compile: ", cmdLine}));


		FILE *f = popen(cmdLine.c_str(), "r");
		if (f == NULL) {
			if (!keepSource) unlink(srcPath.c_str());
			throw std::runtime_error(String({"Failed to invoke compiler: ",cmdLine}).c_str());
		}
		std::ostringstream buffer;
		char buff[128];
		while (fgets(buff,128,f) != NULL) buffer << buff;
		int res = pclose(f);
		if (keepSource) {
			rename(envSrcPath.c_str(), srcPath.c_str());
		}
		if (res != 0) {
			throw CompileError(buffer.str());
		}
		rename(envModulePath.c_str(), modulePath.c_str());
	}

	PModule a = new Module(modulePath);
	return a;
}

struct SeparatedSrc {
	String headers;
	String libs;
	String source;
	String namespaces;
};

static StrViewA hashline("#line ");


SeparatedSrc separateSrc(StrViewA src, StrViewA lineMarkerFile) {


	std::size_t pos = 0;
	std::size_t line = 1;
	bool wasCR = false;
	auto getNext = [&] {
		if (pos == src.length) return -1;
		else {
			int i = (int)(src[pos++]);
			if (i == '\n' && !wasCR) line++;
			else if (i == '\r') {line++; wasCR = true;}
			else wasCR = false;
			return i;
		}
	};
	auto goBack = [&](int howMany) {pos-=howMany;};

	std::vector<char> includes;
	std::vector<char> libs;
	std::vector<char> namespaces;


	includes.reserve(src.length);
	namespaces.reserve(src.length);
	libs.reserve(src.length);

	auto copyLineEx = [&](std::vector<char> &where) {
		int c = getNext();
		while (c != -1 && c != '\n' && c != '\r') {
			if (c == '\\') {
				where.push_back((char)c);
				c = getNext();
				if (c == '\r') {
					where.push_back((char)c);
					c = getNext();
				}
				if (c == '\n') {
					where.push_back((char)c);
					c = getNext();
				}
				if (c == -1) break;
			}
			where.push_back((char)c);
			c = getNext();
		}
		where.push_back((char)c);
	};

	auto checkKw = [&](int c, StrViewA kw, bool rewind) {
		int z = 0;
		while (z < kw.length && ((kw[z] == ' ' && isspace(c)) || (kw[z] == (char)c))) {
			z++;
			c = getNext();
		}
		bool ok = z == kw.length;
		if (!ok) goBack(z);
		else if (rewind) goBack(z);
		else goBack(1);

		return ok;
	};

	auto appendLineMarker = [&](std::vector<char> &where) {
		for (char c : hashline) where.push_back(c);
		where.push_back(' ');
		std::string sline = std::to_string(line);
		for (char c : sline) where.push_back(c);
		where.push_back(' ');
		where.push_back('"');
		for (char c: lineMarkerFile) where.push_back(c);;
		where.push_back('"');
		where.push_back('\r');
		where.push_back('\n');
	};

	int c;
	while ((c = getNext()) != -1) {
		if (isspace(c)) {
			includes.push_back((char)c);
		} else if (c == '#') {
			appendLineMarker(includes);
			includes.push_back((char)c);
			copyLineEx(includes);
		}
		else if (checkKw(c,"//!link ",false)) {
			c = getNext();
			while (c != '\n' && c != '\r' && c != -1) {
				libs.push_back((char)c);
				c = getNext();
			}
		} else if (checkKw(c,"//",true)) {
			includes.push_back((char)c);
			copyLineEx(libs);
		} else if (checkKw(c,"using namespace ",true)) {
			appendLineMarker(namespaces);
			namespaces.push_back((char)c);
			copyLineEx(namespaces);
		} else {
			goBack(1);
			break;
		}
	}


	SeparatedSrc s;
	s.headers = StrViewA(includes.data(), includes.size());
	s.libs = StrViewA(libs.data(),libs.size());
	s.namespaces = StrViewA(namespaces.data(),namespaces.size());
	includes.clear();
	appendLineMarker(includes);
	s.source = {StrViewA(includes.data(),includes.size()),src.substr(pos) };
	return s;
}



ModuleCompiler::SourceInfo ModuleCompiler::createSource(StrViewA code, String lineMarkerFile){

	SeparatedSrc src = separateSrc(code, lineMarkerFile);

	SourceInfo srcinfo;
	srcinfo.sourceCode = String({
		"#define __COUCHCPP_COMPILER \"" INTERFACE_VERSION "\"\n",
		"#include <couchcpp/parts/common.h>\n",
		src.headers,
		"namespace {\n",
		src.namespaces,
		"class Proc: public AbstractProc {\n"
		"public:\n", src.source, "\nprivate: //are we still in class?\n};\n"
		"}\n"
		"#include <couchcpp/parts/entryPoint.h>\n"});
	srcinfo.libraries = src.libs;
	return srcinfo;
}

std::size_t ModuleCompiler::calcHash(const StrViewA code) const {

	StrViewA version(INTERFACE_VERSION);
	std::size_t h;
	FNV1a<sizeof(std::size_t)> hash(h);
	for (char c : StrViewA(code)) hash(c);
	for (char c : StrViewA(gccOpts)) hash(c);
	for (char c : version) hash(c);
	return h;
}

void ModuleCompiler::setSharedCode(Value sharedCode) {
	if (this->sharedCode != sharedCode) {
		this->sharedCode = sharedCode;
		dropEnv();
	}
}

void ModuleCompiler::doAddLib(String cachePath, Value lib) const {

	//logOut(lib.toString());
	for (Value x : lib) {
		StrViewA key = x.getKey();
		if (!key.empty()) {
			String path = {cachePath,"/", key};
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
				logOutFn(String({"Imported: ", path}));
			} else {
				logOutFn(String({"Warning: Can't import :",x.toString()}));
			}
		} else {
			logOutFn(String({"Skipping an empty key for: ", lib.toString()}));
		}
	}
}


String ModuleCompiler::prepareEnv() const {
	if (!envPath.empty()) return envPath;
	pid_t curPid = getpid();
	String pidStr = Value(curPid).toString();
	envPath = String({cachePath,"/",pidStr});
	mkdir(envPath.c_str(),0777);

	doAddLib(envPath, sharedCode);

	logOutFn(String({"Environment prepared at: ", envPath}));

	return envPath;
}

static int walkClear(const char *fname, const struct stat *, int type, struct FTW *) {
	switch (type) {
	case FTW_DP: rmdir(fname);break;
	case FTW_SL:
	case FTW_SLN:
	case FTW_F: unlink(fname);break;
	}
	return 0;
}


void ModuleCompiler::dropEnv() {
	if (envPath.empty()) return;

	nftw(envPath.c_str(),&walkClear,20,FTW_DEPTH|FTW_PHYS|FTW_MOUNT);
	logOutFn(String({"Environment dropped at: ", envPath}));
	envPath = String();
}

ModuleCompiler::~ModuleCompiler() {
	dropEnv();
}

int ModuleCompiler::compileFromFile(String file, bool moveToCache) {
	std::ifstream in(file.c_str(), std::ios::in);
	if (!in) {
		std::cerr << "Failed to open:" << file << std::endl;
		return 1;
	}

	auto s = [&in]{
	  std::ostringstream ss{};
	  ss << in.rdbuf();
	  return ss.str();
	}();

	std::size_t hash = calcHash(s);


	SourceInfo src = createSource(s,file);
	Value baseName(getpid());
	String tmpSrc ({baseName.toString(),"-tmp.cpp"});
	String tmpObj ({"./",baseName.toString(),"-tmp.so"});
	String strhash = hashToModuleName(hash);
	String modulePath ({cachePath,"/",strhash,".so"});

	if (moveToCache) {
		if (access(modulePath.c_str(),F_OK) == 0) return 0;
		else tmpObj = modulePath;
	}


	{
		std::ofstream t(tmpSrc.c_str(),std::ios::out);
		if (!t) {
			std::cout << "Failed to create temporary file: " << tmpSrc << std::endl;
			return 1;
		}
		t.write(src.sourceCode.c_str(), src.sourceCode.length());
	}


	String cmdLine({
		gccPath, " ",
		gccOpts, " ",
		" -o ", tmpObj,
		" ", tmpSrc,
		" ",src.libraries,
		" ",gccLibs});

	logOutFn(cmdLine);
	int res = system(cmdLine.c_str());
	if (res == 0) {
		try {
			Module testOpen(tmpObj);
		} catch (...) {
			unlink(tmpSrc.c_str());
			throw std::runtime_error(String({"Failed to link module: ", file}).c_str());
		}
	}


	if (!moveToCache) {
		remove(tmpObj.c_str());
	}
	if (unlink(tmpSrc.c_str())) {
		throw std::runtime_error(String({"Failed to remove file: ", tmpSrc, " - ", strerror(errno)}).c_str());

	}
	return res;
}

static int clearCacheWalk(const char *fname, const struct stat *, int type, struct FTW *ftw) {
	if (ftw->level == 0) {
		return FTW_CONTINUE;
	} else if (type == FTW_D) {
		long pid = strtol(fname,0,10);
		if (pid) {
			bool ok = kill(pid,0) == 0;
			if (!ok && errno != ESRCH) ok = true;
			if (!ok) {
				nftw(fname,&walkClear,20,0);
			}
		}
		return FTW_SKIP_SUBTREE;
	} else {
		StrViewA baseName(fname +ftw->base);
		if (baseName.substr(0,4) == "mod_") {
			remove(fname);
		}
		return  FTW_CONTINUE;
	}
}

void ModuleCompiler::clearCache() {
	dropEnv();

	nftw(cachePath.c_str(),&clearCacheWalk,20,FTW_ACTIONRETVAL);

}

ModuleCompiler::LogOutFn ModuleCompiler::setLogOutFn(LogOutFn fn) {
	LogOutFn x = this->logOutFn;
	this->logOutFn = fn;
	return x;
}
