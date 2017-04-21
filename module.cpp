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

Module::Module(String path):path(path) {

	libHandle = dlopen(path.c_str(),RTLD_NOW);
	if (libHandle == nullptr)
		throw std::runtime_error(String({"Cannot open assembly: ", path}).c_str());

	EntryPoint e = (EntryPoint)dlsym(libHandle, "initProc");
	if (e == nullptr) {
		dlclose(libHandle);
		throw std::runtime_error(String({"Assembly is corrupted: ", path}).c_str());
	}

	proc = e();
	logOut(String({"load: ", path}));
}

Module::~Module() {
	proc->onClose();
	dlclose(libHandle);
	logOut(String({"unload: ", path}));
}

ModuleCompiler::ModuleCompiler(String cachePath, String gccPath, String gccOpts, String gccLibs, bool keepSource)
	:cachePath(cachePath)
	,gccPath(gccPath)
	,gccOpts(gccOpts)
	,gccLibs(gccLibs)
	,keepSource(keepSource)
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

PModule ModuleCompiler::compile(StrViewA code) const {
	std::size_t hash = calcHash(code);
	String strhash = hashToModuleName(hash);


	String modulePath ({cachePath,"/",strhash,".so"});


	if (access(modulePath.c_str(), F_OK) != 0) {

		String srcPath ({cachePath,"/",strhash,".cpp"});
		String envPath = prepareEnv();
		String envSrcPath ({envPath,"/", strhash,".cpp"});
		String envModulePath ({envPath,"/", strhash,".so"});

		SourceInfo src = createSource(code);
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

		logOut(String({"compile: ", cmdLine}));


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

SeparatedSrc separateSrc(StrViewA src) {


	std::size_t pos = 0;
	auto getNext = [&] {
		if (pos == src.length) return -1;
		else return (int)(src[pos++]);
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

	int c;
	while ((c = getNext()) != -1) {
		if (isspace(c)) {
			includes.push_back((char)c);
		} else if (c == '#') {
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
	s.source = src.substr(pos);
	return s;
}


ModuleCompiler::SourceInfo ModuleCompiler::createSource(StrViewA code){

	SeparatedSrc src = separateSrc(code);

	SourceInfo srcinfo;
	srcinfo.sourceCode = String({
		src.headers,
		"#include <couchcpp/parts/common.h>\n"
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

static void doAddLib(String cachePath, Value lib) {

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
				logOut(String({"Imported: ", path}));
			} else {
				logOut(String({"Warning: Can't import :",x.toString()}));
			}
		} else {
			logOut(String({"Skipping an empty key for: ", lib.toString()}));
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
	envPath = String();
}

ModuleCompiler::~ModuleCompiler() {
	dropEnv();
}
