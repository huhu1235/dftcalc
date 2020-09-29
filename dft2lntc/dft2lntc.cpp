/*
 * dft2lntc.cpp
 * 
 * Part of dft2lnt library - a library containing read/write operations for DFT
 * files in Galileo format and translating DFT specifications into Lotos NT.
 * 
 * @author Freark van der Berg and extended by Dennis Guck and Enno
 * Ruijters.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <string>
#include <fstream>
#include <libgen.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef WIN32
#include <locale>
#include <io.h>
#include <shlobj.h>
#include <codecvt>
#include <winerror.h>
#include <combaseapi.h>
#include <Knownfolders.h>
#endif

#include "dft_parser.h"
#include "dft_ast.h"
#include "ASTPrinter.h"
#include "ASTValidator.h"
#include "ASTDFTBuilder.h"
#include "FileSystem.h"
#include "dft2lnt.h"
#include "DFTree.h"
#include "DFTreeValidator.h"
#include "DFTreePrinter.h"
#ifdef HAVE_CADP
#include "DFTreeBCGNodeBuilder.h"
#endif
#include "DFTreeAUTNodeBuilder.h"
#include "DFTreeEXPBuilder.h"
#include "compiletime.h"
#include "Settings.h"
#include "modularize.h"

FILE* pp_outputFile = stdout;

const int VERBOSITY_FLOW = 1;
const int VERBOSITY_DATA = 1;

void print_help(MessageFormatter* messageFormatter, string topic="") {
	if(topic.empty()) {
		messageFormatter->notify ("dft2lntc [INPUTFILE.dft] [options]");
		messageFormatter->message("  Compiles the inputfile to EXP and SVL script. If no inputfile was specified,");
		messageFormatter->message("  stdin is used. If no outputfile was specified, 'a.svl' and 'a.exp' are used.");
		messageFormatter->message("");
		messageFormatter->notify ("General Options:");
		messageFormatter->message("  -h, --help      Show this help.");
		messageFormatter->message("  --color         Use colored messages.");
		messageFormatter->message("  --no-color      Do not use colored messages.");
		messageFormatter->message("  --version       Print version info and quit.");
		messageFormatter->message("");
		messageFormatter->notify ("Debug Options:");
		messageFormatter->message("  -a FILE         Output AST to file. '-' for stdout.");
		messageFormatter->message("  -t FILE         Output DFT to file. '-' for stdout.");
		messageFormatter->message("  --verbose=x     Set verbosity to x, -1 <= x <= 5.");
		messageFormatter->message("  -v, --verbose   Increase verbosity. Up to 5 levels.");
		messageFormatter->message("  -q              Decrease verbosity.");
		messageFormatter->message("");
		messageFormatter->notify ("Output Options:");
		messageFormatter->message("  -o FILE         Output EXP to <FILE>.exp and SVL to <FILE>.svl.");
		messageFormatter->message("  -m FILE         Output module description to <FILE>, '-' for stdout.");
		messageFormatter->message("  -x FILE         Output EXP to file. '-' for stdout. Overrules -o.");
		messageFormatter->message("  -s FILE         Output SVL to file. '-' for stdout. Overrules -o.");
		messageFormatter->message("  -b FILE         Output of SVL to this BCG file. Overrules -o.");
		messageFormatter->message("  -e evidence     Comma separated list of BE names that fail at startup.");
		messageFormatter->message("  -r root         Root node of the subtree to analyse.");
		messageFormatter->message("  -n FILE         Name to use in error messages and to find");
		messageFormatter->message("                  embedded bcg files mentioned as aph attributes");
		messageFormatter->message("                  (used by dftcalc; not intented to be used directly by user).");
		messageFormatter->message("  --warn-code     Return non-zero if there are one or more warnings.");
		messageFormatter->flush();
	} else if(topic=="topics") {
		messageFormatter->notify ("Help topics:");
		messageFormatter->message("  output          Displays the specification of the output format");
		messageFormatter->message("  To view topics: dft2lntc --help=<topic>");
		messageFormatter->message("");
	} else {
		messageFormatter->reportAction("Unknown help topic: " + topic);
	}		
}

void print_version(MessageFormatter* messageFormatter) {
	
	messageFormatter->notify ("dft2lntc");
	messageFormatter->message(string("  built on ") + COMPILETIME_DATE);
	{
		FileWriter out;
		out << string("  git version: ") + string(COMPILETIME_GITVERSION) + " (nearest)" << out.applypostfix;
		out << string("  git revision `") + COMPILETIME_GITREV + "'";
		if(COMPILETIME_GITCHANGED)
			out << " + uncommited changes";
		messageFormatter->message(out.toString());
	}
	messageFormatter->message("  ** Copyright statement. **");
	messageFormatter->flush();
}

static std::string getCache(CompilerContext* compilerContext) {
	std::string root;
#ifdef WIN32
	PWSTR rootStr;
	HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &rootStr);
	if (!SUCCEEDED(hr)) {
		compilerContext->reportError("Unable to lookup Application Data directory");
		exit(EXIT_FAILURE);
	}
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
	root = converter.to_bytes(rootStr);
	CoTaskMemFree(rootStr);
#elif defined __APPLE__
	root = std::getenv("HOME");
	root += "/Library/Caches";
#else
	root = std::getenv("HOME");
	root += "/.cache";
#endif
	/* End of OS-specific part */
	std::string cache = root + "/dftcalc";
	if (!FileSystem::isDir(File(cache))) {
		if (FileSystem::mkdir(File(cache))) {
			compilerContext->reportError("Could not create cache directory (" + cache + ")");
		}
	}

	std::string autDir = cache + DFT2LNT::AUT_CACHE_DIR;
	if (!FileSystem::isDir(File(autDir))) {
		if (FileSystem::mkdir(File(autDir))) {
			compilerContext->reportError("Could not create .aut directory (" + autDir + ")");
		}
	}

	return cache;
}

std::string getRoot(CompilerContext* compilerContext) {
	char* root = getenv((const char*)"DFT2LNTROOT");
	std::string dft2lntRoot = root?string(root):"";

	if(dft2lntRoot=="")
		dft2lntRoot = DFT2LNTROOT;
	
	// \ to /
	{
		char buf[dft2lntRoot.length()+1];
		for(int i=dft2lntRoot.length();i--;) {
			if(dft2lntRoot[i]=='\\')
				buf[i] = '/';
			else
				buf[i] = dft2lntRoot[i];
		}
		buf[dft2lntRoot.length()] = '\0';
		if(buf[dft2lntRoot.length()-1]=='/') {
			buf[dft2lntRoot.length()-1] = '\0';
		}
		dft2lntRoot = string(buf);
	}
	
	struct stat rootStat;
	if(stat((dft2lntRoot).c_str(),&rootStat)) {
		// report error
		compilerContext->reportError("Could not stat DFT2LNTROOT (`" + dft2lntRoot + "')");
		dft2lntRoot = "";
		goto end;
	}

#ifdef HAVE_CADP
	if(stat((dft2lntRoot+DFT2LNT::LNTSUBROOT).c_str(),&rootStat)) {
		if(FileSystem::mkdir(dft2lntRoot+DFT2LNT::LNTSUBROOT,0755)) {
			compilerContext->reportError("Could not create LNT Nodes directory (`" + dft2lntRoot+DFT2LNT::LNTSUBROOT + "')");
			dft2lntRoot = "";
			goto end;
		}
	}

	if(stat((dft2lntRoot+DFT2LNT::BCGSUBROOT).c_str(),&rootStat)) {
		if(FileSystem::mkdir(dft2lntRoot+DFT2LNT::BCGSUBROOT,0755)) {
			compilerContext->reportError("Could not create BCG Nodes directory (`" + dft2lntRoot+DFT2LNT::BCGSUBROOT + "')");
			dft2lntRoot = "";
			goto end;
		}
	}
#endif
	compilerContext->reportAction("DFT2LNTROOT is: " + dft2lntRoot,VERBOSITY_DATA);
end:
	return dft2lntRoot;
}

int main(int argc, char** argv) {
	
	/* Set defaults */
	Settings default_settings;
	default_settings["warn-code"] = "0";
	
	/* Initialize default settings */
	Settings settings = default_settings;
	
	/* Command line arguments and their default settings */
	string inputFileName     = "";
	int    inputFileSet      = 0;
	string origFileName      = "";
	int    origFileSet       = 0;
	string outputFileName    = "a";
	int    outputFileSet     = 0;
	string outputASTFileName = "";
	int    outputASTFileSet  = 0;
	string outputDFTFileName = "";
	int    outputDFTFileSet  = 0;
	string outputSVLFileName = "";
	int    outputSVLFileSet  = 0;
	string outputEXPFileName = "";
	int    outputEXPFileSet  = 0;
	string outputBCGFileName = "";
	int    outputBCGFileSet  = 0;
	string outputMODFileName = "";
	int    outputMODFileSet  = 0;
	string rootNode          = "";

	int stopAfterPreproc     = 0;
	int useColoredMessages   = 1;
	int verbosity            = 0;
	int printHelp            = 0;
	int printVersion         = 0;
	
	std::vector<std::string> failedBEs;
	
	/* Parse command line arguments */
	char c;
	while( (c = getopt(argc,argv,"o:m:n:a:b:e:r:t:s:x:hvq-:")) >= 0 ) {
		switch(c) {

			// -o FILE
			case 'o':
				if(strlen(optarg)==1 && optarg[0]=='-') {
					outputFileName = "";
					outputFileSet = 1;
				} else {
					outputFileName = string(optarg);
					outputFileSet = 1;
				}
				break;

			// -m FILE
			case 'm':
				if(strlen(optarg)==1 && optarg[0]=='-') {
					outputMODFileName = "";
					outputMODFileSet = 1;
				} else {
					outputMODFileName = string(optarg);
					outputMODFileSet = 1;
				}
				break;

			// -n FILENAME
			case 'n':
				origFileName = string(optarg);
				origFileSet = 1;
				break;

			// -a FILE
			case 'a':
				if(strlen(optarg)==1 && optarg[0]=='-') {
					outputASTFileName = "";
					outputASTFileSet = 1;
				} else {
					outputASTFileName = string(optarg);
					outputASTFileSet = 1;
				}
				break;

			// -t FILE
			case 't':
				if(strlen(optarg)==1 && optarg[0]=='-') {
					outputDFTFileName = "";
					outputDFTFileSet = 1;
				} else {
					outputDFTFileName = string(optarg);
					outputDFTFileSet = 1;
				}
				break;

			// -s FILE
			case 's':
				if(strlen(optarg)==1 && optarg[0]=='-') {
					outputSVLFileName = "";
					outputSVLFileSet = 1;
				} else {
					outputSVLFileName = string(optarg);
					outputSVLFileSet = 1;
				}
				break;

			// -x FILE
			case 'x':
				if(strlen(optarg)==1 && optarg[0]=='-') {
					outputEXPFileName = "";
					outputEXPFileSet = 1;
				} else {
					outputEXPFileName = string(optarg);
					outputEXPFileSet = 1;
				}
				break;

			// -b FILE
			case 'b':
				if(strlen(optarg)==1 && optarg[0]=='-') {
					outputBCGFileName = "";
					outputBCGFileSet = 1;
				} else {
					outputBCGFileName = string(optarg);
					outputBCGFileSet = 1;
				}
				break;
			
			// -h
			case 'h':
				printHelp = true;
				break;
				
			// -v
			case 'v':
				++verbosity;
				break;
			
			// -q
			case 'q':
				--verbosity;
				break;
			
			// -e
			case 'e': {
				const char* begin = optarg;
				const char* end = begin;
				while(*begin) {
					end = begin;
					while(*end && *end!=',') ++end;
					if(begin<end) {
						failedBEs.push_back(std::string(begin,end));
					}
					if(!*end) break;
					begin = end + 1;
				}
				break;
				
			}
			case 'r':
				  rootNode = string(optarg);
				  break;
			// --
			case '-':
				if(!strncmp("help",optarg,4)) {
					printHelp = true;
				} else if(!strcmp("version",optarg)) {
					printVersion = true;
				} else if(!strcmp("color",optarg)) {
					useColoredMessages = true;
				} else if(!strncmp("verbose",optarg,7)) {
					if(strlen(optarg)>8 && optarg[7]=='=') {
						verbosity = atoi(optarg+8);
					} else if (strlen(optarg)==7) {
						++verbosity;
					}
				} else if(!strcmp("no-color",optarg)) {
					useColoredMessages = false;
				} else if(!strcmp("warn-code",optarg)) {
					settings["warn-code"] = "1";
				}
		}
	}

//	printf("args:\n");
//	for(unsigned int i=0; i<(unsigned int)argc; i++) {
//		printf("  %s\n",argv[i]);
//	}

	/* Create a new compiler context */
	CompilerContext compilerContext(std::cerr);
	compilerContext.useColoredMessages(useColoredMessages);
	compilerContext.setVerbosity(verbosity);

	/* Print help / version if requested and quit */
	if(printHelp) {
		print_help(&compilerContext);
		exit(0);
	}
	if(printVersion) {
		print_version(&compilerContext);
		exit(0);
	}

	/* Parse command line arguments without a -X.
	 * These specify the input files. Currently it will only allow
	 * one input file.
	 */
	if(optind<argc) {
		int isSet = 0;
		for(unsigned int i=optind; i<(unsigned int)argc; ++i) {
			if(isSet) {
				compilerContext.reportError("too many input files: "+string(argv[i]));
				continue;
			}
			isSet = 1;
			if(strlen(argv[i])==1 && argv[i][0]=='-') {
				inputFileName = "";
			} else {
				inputFileName = string(argv[i]);
				inputFileSet = 1;
				FILE* f = fopen(argv[i],"r");
				if(f) {
					fclose(f);
				} else {
					compilerContext.reportError("unable to open inputfile " + string(argv[i]));
					return 1;
				}
			}
		}
	}
	
	/*
	 * Handle arguments
	 */
	{
		// Set SVL and EXP output file names
		outputSVLFileName = outputSVLFileSet ? outputSVLFileName : outputFileName + "." + DFT::FileExtensions::SVL;
		outputEXPFileName = outputEXPFileSet ? outputEXPFileName : outputFileName + "." + DFT::FileExtensions::EXP;
		outputBCGFileName = outputBCGFileSet ? outputBCGFileName : outputFileName + "." + DFT::FileExtensions::BCG;
		outputFileSet = outputFileSet || outputSVLFileSet || outputEXPFileSet;
		
		// Test all the files that need to be written if they are writable
		bool ok = true;
		if(!outputSVLFileName.empty() && !compilerContext.testWritable(outputSVLFileName)) {
			compilerContext.reportError("SVL output file is not writable: `" + outputSVLFileName + "'");
			ok = false;
		}
		if(!outputEXPFileName.empty() && !compilerContext.testWritable(outputEXPFileName)) {
			compilerContext.reportError("EXP output file is not writable: `" + outputEXPFileName + "'");
			ok = false;
		}
		if(!outputDFTFileName.empty() && !compilerContext.testWritable(outputDFTFileName)) {
			compilerContext.reportError("DFT output file is not writable: `" + outputDFTFileName + "'");
			ok = false;
		}
		if(!outputASTFileName.empty() && !compilerContext.testWritable(outputASTFileName)) {
			compilerContext.reportError("AST output file is not writable: `" + outputASTFileName + "'");
			ok = false;
		}
		if(!outputMODFileName.empty() && !compilerContext.testWritable(outputMODFileName)) {
			compilerContext.reportError("MOD output file is not writable: `" + outputMODFileName + "'");
			ok = false;
		}
		compilerContext.flush();
		if(!ok) {
			return 1;
		}
	}

	FILE* inputFile = inputFileSet ? fopen(inputFileName.c_str(),"rb") : stdin ;

	if(!origFileSet) {
		origFileName = inputFileName;
	}
	
	compilerContext.notify("Running dft2lntc...");

	std::string parserInputFilePath(origFileName);

	std::string parserInputFileName(path_basename(inputFileName.c_str()));

	std::string dft2lntRoot = getRoot(&compilerContext);
	bool rootValid = dft2lntRoot!="";

	std::string cacheDir = getCache(&compilerContext);

	/* Parse input file */
	compilerContext.notify("Checking syntax...",VERBOSITY_FLOW);
	Parser* parser = new Parser(inputFile,parserInputFilePath,&compilerContext);
	DFT::AST::ASTNodes* ast = parser->parse();
	compilerContext.flush();
	if(!ast || compilerContext.getErrors()>0) {
		compilerContext.reportError("Syntax is incorrect");
		ast = 0;
	} else {
		compilerContext.reportAction("Syntax is correct",VERBOSITY_FLOW);
	}
	compilerContext.flush();

	/* Print AST */
	if(ast && outputASTFileSet) {
		compilerContext.notify("Printing AST...",VERBOSITY_FLOW);
		compilerContext.flush();
		DFT::ASTPrinter printer(ast,&compilerContext);
		if(outputASTFileName!="") {
			std::ofstream astFile (outputASTFileName);
			astFile << printer.print();
		} else {
			FileWriter ast;
			ast << printer.print();
			compilerContext.reportFile("AST",ast.toString());
		}
	}
	compilerContext.flush();
	
	/* Validate input */
	int astValid = false;
	if(ast) {
		compilerContext.notify("Validating AST...",VERBOSITY_FLOW);
		compilerContext.flush();
		DFT::ASTValidator validator(ast, &compilerContext);
		astValid = validator.validate();
		if(!astValid) {
			compilerContext.reportError("AST invalid");
		} else {
			compilerContext.reportAction("AST is valid",VERBOSITY_FLOW);
		}
	}
	compilerContext.flush();
	
	/* Create DFT */
	DFT::DFTree* dft = NULL;
	if(astValid) {
		compilerContext.notify("Building DFT...",VERBOSITY_FLOW);
		compilerContext.flush();
		DFT::ASTDFTBuilder builder(ast, &compilerContext);
		dft = builder.build();
		if(!dft) {
			compilerContext.reportError("Could not build DFT");
		} else {
			compilerContext.reportAction("DFT built successfully",VERBOSITY_FLOW);
		}
	}
	compilerContext.flush();

	/* Validate DFT */
	int dftValid = false;
	if(dft) {
		compilerContext.notify("Validating DFT...",VERBOSITY_FLOW);
		compilerContext.flush();
		DFT::DFTreeValidator validator(dft, &compilerContext);
		dftValid = validator.validate();
		if(!dftValid) {
			compilerContext.reportError("DFT invalid");
		} else {
			compilerContext.reportAction("DFT is valid",VERBOSITY_FLOW);
		}
	}
	compilerContext.flush();

	if (!rootNode.empty()) {
		DFT::Nodes::Node *newRoot = dft->getNode(rootNode);
		if (newRoot == nullptr) {
			compilerContext.reportError("Root node " + rootNode + " does not exist.");
			compilerContext.flush();
			return 1;
		}
		dft->setTopNode(newRoot);
		dft->removeUnreachable();
	}

	if (dftValid && outputMODFileSet) {
		compilerContext.reportAction("Writing static modules...",VERBOSITY_FLOW);
		compilerContext.flush();
		try {
			writeModules(outputMODFileName, dft);
		} catch(std::vector<std::string>& errors) {
			for(std::string e: errors) {
				compilerContext.reportError(e);
			}
			compilerContext.flush();
		}
		compilerContext.reportAction("Done writing static modules.",VERBOSITY_FLOW);
		return 0;
	}
	
	/* Apply evidence to DFT */
	if(dftValid && !failedBEs.empty()) {
		compilerContext.reportAction("Applying evidence to DFT...",VERBOSITY_FLOW);
		compilerContext.flush();
		try {
			dft->applyEvidence(failedBEs);
		} catch(std::vector<std::string>& errors) {
			for(std::string e: errors) {
				compilerContext.reportError(e);
			}
			compilerContext.flush();
		}
	}
    
	/* Add repair knowledge to gates */
	if(dft) {
		compilerContext.reportAction("Applying repair knowledge to DFT gates...",VERBOSITY_FLOW);
		compilerContext.flush();
		dft->addRepairInfo();
		compilerContext.reportAction("Done applying repair knowledge to DFT gates...",VERBOSITY_FLOW);
	}

	if(dft) {
		/* Add always-active knowledge to gates */
		compilerContext.reportAction("Applying always-active knowledge to DFT gates...",VERBOSITY_FLOW);
		compilerContext.flush();
		dft->addAlwaysActiveInfo();
		compilerContext.reportAction("Done applying always-active knowledge to DFT gates...",VERBOSITY_FLOW);
    
		/* Remove superflous FDEP edges */
        compilerContext.reportAction("Applying FDEP cleanup to DFT gates...",VERBOSITY_FLOW);
        compilerContext.flush();
        dft->checkFDEPInfo();
        compilerContext.reportAction("Done applying FDEP cleanup to DFT gates...",VERBOSITY_FLOW);

		/* Replace sequence enforcers by SAND gates when possible. */
        compilerContext.reportAction("Applying SEQ cleanup to DFT gates...",VERBOSITY_FLOW);
        compilerContext.flush();
        dft->replaceSEQs();
        compilerContext.reportAction("Done applying SEQ cleanup to DFT gates...",VERBOSITY_FLOW);
    }

	/* Printing DFT */
	if(dftValid && outputDFTFileSet) {
		compilerContext.notify("Printing DFT...",VERBOSITY_FLOW);
		compilerContext.flush();
		DFT::DFTreePrinter printer(dft, &compilerContext);
		if(outputDFTFileName!="") {
			std::ofstream dftFile (outputDFTFileName);
			printer.print(dftFile);
		} else {
			std::stringstream out;
			printer.print(out);
			compilerContext.reportFile("DFT",out.str());
		}
		
	}
	compilerContext.flush();

	/* Building needed BCG files for DFT */
	if(rootValid && dftValid) {
		compilerContext.notify("Building needed AUT files...",VERBOSITY_FLOW);
		compilerContext.flush();
		DFT::DFTreeAUTNodeBuilder autBuilder(cacheDir, dft, &compilerContext);
		DFT::DFTreeNodeBuilder *nodeBuilder = &autBuilder;
#ifdef HAVE_CADP
		DFT::DFTreeBCGNodeBuilder bcgBuilder(dft2lntRoot,dft, &compilerContext);
		if (autBuilder.generate()) {
			compilerContext.notify("Unable to make AUT files, building needed BCG files...",VERBOSITY_FLOW);
			bcgBuilder.generate();
			nodeBuilder = &bcgBuilder;
		}
#else
		if (autBuilder.generate()) {
			compilerContext.reportError("Unable to create AUT files.");
		}
#endif
		if (outputFileSet) {
			/* Building EXP out of DFT */
			compilerContext.notify("Building EXP...",VERBOSITY_FLOW);
			compilerContext.flush();
			DFT::DFTreeEXPBuilder builder(dft2lntRoot,".",outputBCGFileName,outputEXPFileName,dft, nodeBuilder, &compilerContext);
			builder.build();

			if(outputSVLFileName!="") {
				std::ofstream svlFile (outputSVLFileName);
				builder.printSVL(svlFile);
			} else {
				std::stringstream out;
				builder.printSVL(out);
				compilerContext.reportFile("SVL",out.str());
			}
			if(outputEXPFileName!="") {
				std::ofstream expFile (outputEXPFileName);
				builder.printEXP(expFile);
			} else {
				std::stringstream out;
				builder.printEXP(out);
				compilerContext.reportFile("EXP",out.str());
			}

		}
	}
	compilerContext.flush();
	
	compilerContext.reportErrors();
	compilerContext.flush();
	
	if(compilerContext.getVerbosity()>=3) {
		compilerContext.notify("SUCCESS! Time for brandy!");
		compilerContext.flush();
	}
	
	if(ast) delete ast;
	if(dft) delete dft;
	delete parser;
	
	if(settings["warn-code"] && compilerContext.getWarnings()>0) {
		return EXIT_FAILURE;
	}
	return compilerContext.getErrors()>0;
}
