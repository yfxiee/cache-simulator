/** 
Copyright (C) 2016 Gregory Vaumourin

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**/

#include <stdlib.h>     
#include <ctime>
#include <vector>
#include <iostream>
#include <fstream>
#include <assert.h>
#include <string>
#include <zlib.h>
#include <signal.h>
#include <unistd.h>

#include "main.hh"
#include "Hierarchy.hh"
#include "MemoryTrace.hh"
#include "common.hh"

#define MERGING_RESULTS true

using namespace std;

int start_debug;
Hierarchy* my_system;


int main(int argc , char* argv[]){
	
	vector<string> args; 

	bool mergingResults = MERGING_RESULTS;
	

	
	init_default_parameters();
	
	for(int i = 1; i < argc ; i++)
	{
		if(string(argv[i]) == "-p")
		{
			i++;
			simu_parameters.policy = string(argv[i]);		
		}
		else if(string(argv[i]) == "-n")
		{
			i++;
			simu_parameters.nbCores = atoi(argv[i]);
		}
		else if(string(argv[i]) == "--rap-assoc")
		{
			i++;
			simu_parameters.rap_assoc = atoi(argv[i]);
		}
		else if(string(argv[i]) == "--rap-sets")
		{
			i++;
			simu_parameters.rap_sets = atoi(argv[i]);
		}
		else if(string(argv[i]) == "--window_size")
		{
			i++;
			simu_parameters.window_size = atoi(argv[i]);
		}
		else if(string(argv[i]) == "--SRAM-assoc")
		{
			i++;
			simu_parameters.sram_assoc = atoi(argv[i]);
		}
		else if(string(argv[i]) == "--NVM-assoc")
		{
			i++;
			simu_parameters.nvm_assoc = atoi(argv[i]);
		}
		else if(string(argv[i]) == "--deadCounter")
		{
			i++;
			simu_parameters.deadSaturationCouter = atoi(argv[i]);
		}
		else if(string(argv[i]) == "--enableBP")
			simu_parameters.enableBP = true;
		else if(string(argv[i]) == "--flagTest")
			simu_parameters.flagTest = true ;
		else if(string(argv[i]) == "--enableDebug")
		{
			cout << "Debug enable "<< endl;		
			simu_parameters.printDebug = true;
		}
		else if(string(argv[i]) == "--enable-DYN")
			simu_parameters.enableMigration = true;
		else
			args.push_back(string(argv[i]));
	}
	
	vector<string> memory_traces;
	for(unsigned i = 0 ; i < args.size() ; i++)
		memory_traces.push_back(args[i]);		
	
	if(argc == 1)
		memory_traces.push_back(DEFAULT_TRACE);
	
	
	/* The Control C signal handler setup */
	/*struct sigaction sigIntHandler;
	sigIntHandler.sa_handler = my_handler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;
	sigaction(SIGINT, &sigIntHandler, NULL);*/
	/***********************************/	

	my_system = new Hierarchy(simu_parameters.policy , simu_parameters.nbCores);
 
	cout << "Launching simulation with " << memory_traces.size() << " file(s), the " << simu_parameters.policy \
	 << " policy and with " << simu_parameters.nbCores << " core(s)" << endl;
	cout << "Traces considered:" << endl;
	for(auto memory_trace : memory_traces)
		cout << "\t - " << memory_trace << endl;
	
	int id_trace = 0;
	for(auto memory_trace : memory_traces)
	{
		cout << "\tRunning Trace: " << memory_trace << " ... " <<  endl;
	
		MemoryTrace* traceWrapper = initTrace(memory_trace);
		assert(traceWrapper != NULL);
		
		Access element;
		int cpt_access = 0;
		my_system->startWarmup();
		my_system->stopWarmup();
		
		while(traceWrapper->readNext(element)){

			my_system->handleAccess(element);
			DPRINTF("TIME::%ld\n",cpt_access);
			
			if(cpt_access == WARMUP_WINDOW)
			{
				cout << "\t\tFinished the warmup phase" << endl;			
				my_system->stopWarmup();
			}
			
			cpt_time++;
			cpt_access++;
		}
		traceWrapper->close();
		free(traceWrapper);
		
		if(!mergingResults)
		{
			my_system->finishSimu();	
			printResults(mergingResults, id_trace);	
		
		}
		id_trace++;
	}	
	if(mergingResults)
	{
		my_system->finishSimu();	
		printResults(mergingResults , id_trace);	
	}
	
	delete(my_system);
	return 0;
}


void my_handler(int s){
	cout << endl;
	cout << "Ctrl+C event caught, printing results and exiting" << endl;
	
	printResults(false , 0);
		
	exit(1); 

}


/** Writing all the output+config files */ 
void printResults(bool mergingResults, int id_trace)
{
	if(!my_system)
		return;
		
		
	string suffixe = mergingResults ? "_"+to_string(id_trace) : ""; 
	
	ofstream configFile;
	configFile.open("config" + suffixe + ".ini");
	my_system->printConfig(configFile);
	configFile.close();
	
	ofstream output_file;
	output_file.open("results"+ suffixe + ".out");
	my_system->printResults(output_file);
	output_file.close();
}


