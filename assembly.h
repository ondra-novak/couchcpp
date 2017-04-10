/*
 * assembly.h
 *
 *  Created on: Apr 9, 2017
 *      Author: ondra
 */

#pragma once
#include "parts/common.h"

typedef IProc *(*EntryPoint)();

class Assembly: public json::RefCntObj {
public:
	Assembly(String path);
	~Assembly();

	IProc *getProc() const {return proc;}
	const String getPath() const {return path;}

protected:

	void *libHandle;
	IProc *proc;
	String path;
};


typedef RefCntPtr<Assembly> PAssembly;

void logOut(const StrViewA & msg);

class AssemblyCompiler {
public:

	struct SourceInfo {
		///Complete source ready to compile
		String sourceCode;
		///Linker libraries
		String libraries;


	};

	AssemblyCompiler(String cachePath, String gccPath, String gccOpts, String gccLibs, bool keepSource);

	PAssembly compile(StrViewA code) const;

	static SourceInfo createSource(StrViewA code) ;

	std::size_t calcHash(const StrViewA code) const;

protected:
	String cachePath;
	String gccPath;
	String gccOpts;
	String gccLibs;
	bool keepSource;

};
