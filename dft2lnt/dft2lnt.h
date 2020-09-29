/*
 * dft2lnt.h
 * 
 * Part of dft2lnt library - a library containing read/write operations for DFT
 * files in Galileo format and translating DFT specifications into Lotos NT.
 * 
 * @author Freark van der Berg
 */

#ifndef DFT2LNT_H
#define DFT2LNT_H

#include "DFTree.h"
#include "ConsoleWriter.h"
#include "compiler.h"

class DFT2LNT {
public:
	static const std::string LNTSUBROOT;
	static const std::string BCGSUBROOT;
	static const std::string TESTSUBROOT;
	static const std::string AUT_CACHE_DIR;
};

#endif // DFT2LNT_H
