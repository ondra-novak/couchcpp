/*
 * assembly.cpp
 *
 *  Created on: Apr 9, 2017
 *      Author: ondra
 */

#include <unistd.h>
#include "assembly.h"
#include <dlfcn.h>
#include <imtjson/fnv.h>
#include <cstring>
#include <fstream>

Assembly::Assembly(String path) {

	libHandle = dlopen(path.c_str(),RTLD_LAZY);
	if (libHandle == nullptr)
		throw std::runtime_error(String({"Cannot open assembly: ", path}).c_str());

	EntryPoint e = (EntryPoint)dlsym(libHandle, "initProc");
	if (e == nullptr) {
		dlclose(libHandle);
		throw std::runtime_error(String({"Assembly is corrupted: ", path}).c_str());
	}

	proc = e();
}

Assembly::~Assembly() {
	proc->onClose();
	dlclose(libHandle);
}

AssemblyCompiler::AssemblyCompiler(String cachePath, String gccPath, String gccOpts, bool keepSource)
	:cachePath(cachePath)
	,gccPath(gccPath)
	,gccOpts(gccOpts)
	,keepSource(keepSource)
{

}

PAssembly AssemblyCompiler::compile(StrViewA code) const {
	std::size_t hash = calcHash(code);
	String strhash = Value(hash).toString();


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
			src.libraries,
			" -o ", modulePath,
			" ", srcPath,
			" 2>&1"});

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

	PAssembly a = new Assembly(modulePath);
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


AssemblyCompiler::SourceInfo AssemblyCompiler::createSource(StrViewA code){

	SeparatedSrc src = separateSrc(code);

	SourceInfo srcinfo;
	srcinfo.sourceCode = String({
		src.headers,
		"#include <parts/common.h>\n"
		"namespace {\n"
		"class Proc: public AbstractProc {\n"
		"public:\n", src.source, "};\n"
		"}\n"
		"#include <parts/entryPoint.h>\n"});
	srcinfo.libraries = src.libs;
	return srcinfo;
}

std::size_t AssemblyCompiler::calcHash(const StrViewA code) const {

	StrViewA version(INTERFACE_VERSION);
	std::size_t h;
	FNV1a<sizeof(std::size_t)> hash(h);
	for (char c : StrViewA(code)) hash(c);
	for (char c : StrViewA(gccOpts)) hash(c);
	for (char c : version) hash(c);
	return h;
}
