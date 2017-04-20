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
	String srcPath ({cachePath,"/",strhash,".cpp"});

	if (access(modulePath.c_str(), F_OK) != 0) {

		SourceInfo src = createSource(code);
		{
			std::ofstream t(srcPath.c_str(),std::ios::out);
			if (!t) {
				throw std::runtime_error(String({"Failed to create file: ",srcPath}).c_str());
			}
			t.write(src.sourceCode.c_str(), src.sourceCode.length());
		}


		String cmdLine({
			gccPath, " ",
			gccOpts, " ",
			" -o ", modulePath,
			" ", srcPath,
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
		if (res != 0) {
			if (!keepSource) unlink(srcPath.c_str());
			throw std::runtime_error(String({"Compile error: ",buffer.str()," - cmdline:", cmdLine}).c_str());
		}
		if (!keepSource) unlink(srcPath.c_str());
	}

	PModule a = new Module(modulePath);
	return a;
}

struct SeparatedSrc {
	String headers;
	String libs;
	String source;
};

SeparatedSrc separateSrc(StrViewA src) {


	std::size_t pos = src.indexOf("\n");

	std::ostringstream includes;
	std::ostringstream libs;

	while (pos != src.npos) {
		StrViewA ln = src.substr(0,pos);
		while (!ln.empty() && isspace(ln[0])) {
			ln = ln.substr(1);
		}
		if (!ln.empty()) {
			if (ln[0] == '#') {
				if (ln.substr(0,6) == "#libs ") {
					libs << ln.substr(5);
				}
				else {
					includes << ln << std::endl;
					while (ln.substr(ln.length-1) == "/") {
						src = src.substr(pos);
						pos = src.indexOf("\n");
						if (pos == src.npos) break;
						ln = ln.substr(pos);
						includes << ln << std::endl;
					}
				}
			} else if (ln.substr(0,2) == "//") {
				//skip this line
			} else if (ln.substr(0,6) == "using ") {
				includes << ln << std::endl;
			} else {
				break;
			}
		}

		src = src.substr(pos+1);
		pos = src.indexOf("\n");

	}

	SeparatedSrc s;
	s.headers = includes.str();
	s.libs = libs.str();
	s.source = src;
	return s;
}


ModuleCompiler::SourceInfo ModuleCompiler::createSource(StrViewA code){

	SeparatedSrc src = separateSrc(code);

	SourceInfo srcinfo;
	srcinfo.sourceCode = String({
		src.headers,
		"#include <couchcpp/parts/common.h>\n"
		"namespace {\n"
		"class Proc: public AbstractProc {\n"
		"public:\n", src.source, "};\n"
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
