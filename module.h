/*
 * assembly.h
 *
 *  Created on: Apr 9, 2017
 *      Author: ondra
 */

#pragma once
#include "parts/common.h"

typedef IProc *(*EntryPoint)();

class Module: public json::RefCntObj {
public:
	Module(String path);
	~Module();

	IProc *getProc() const {return proc;}
	const String getPath() const {return path;}

protected:

	void *libHandle;
	IProc *proc;
	String path;
};


typedef RefCntPtr<Module> PModule;

void logOut(const StrViewA & msg);

class ModuleCompiler {
public:

	struct SourceInfo {
		///Complete source ready to compile
		String sourceCode;
		///Linker libraries
		String libraries;


	};

	ModuleCompiler(String cachePath, String gccPath, String gccOpts, String gccLibs, bool keepSource);
	~ModuleCompiler();

	PModule compile(StrViewA code) const;

	static SourceInfo createSource(StrViewA code) ;

	std::size_t calcHash(const StrViewA code) const;


	void setSharedCode(Value sharedCode);


	String prepareEnv() const;
	void dropEnv();

protected:
	String cachePath;
	String gccPath;
	String gccOpts;
	String gccLibs;
	mutable String envPath;

	Value sharedCode;

	bool keepSource;

};

class CompileError: public std::runtime_error {
public:
	CompileError(std::string error):std::runtime_error(error) {}
};
