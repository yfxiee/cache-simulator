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

#include <map>	
#include <iomanip>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <assert.h>
#include <math.h>
#include <zlib.h>

#include "HybridCache.hh"
#include "Cache.hh"
#include "common.hh"

#include "ReplacementPolicy.hh"
#include "SaturationPredictor.hh"
#include "InstructionPredictor.hh"
#include "Cerebron.hh"
#include "SimplePerceptron.hh"
#include "PHC.hh"
#include "DBAMBPredictor.hh"
#include "Perceptron.hh"
//#include "monitorPredictor.hh"
//#include "SimplePerceptron.hh"

#define LLC_TRACE_BUFFER_SIZE 50

using namespace std;


gzFile LLC_trace;

HybridCache::HybridCache(int id, bool isInstructionCache, int size , int assoc , int blocksize , int nbNVMways, string policy, Level* system){

	m_assoc = assoc;
	m_cache_size = size;
	m_blocksize = blocksize;
	m_nb_set = size / (assoc * blocksize);
	m_nbNVMways = nbNVMways;	
	m_nbSRAMways = m_assoc - m_nbNVMways;
	m_system = system;
	m_ID = id;
	m_isInstructionCache = isInstructionCache;
	
	assert(isPow2(m_nb_set));
	assert(isPow2(m_blocksize));

	m_policy = policy;
	m_printStats = false;
	m_isWarmup = false;
		
	m_tableSRAM.resize(m_nb_set);
	m_tableNVM.resize(m_nb_set);
	for(int i = 0  ; i < m_nb_set ; i++){
		m_tableSRAM[i].resize(m_nbSRAMways);
		for(int j = 0 ; j < m_nbSRAMways ; j++){
			m_tableSRAM[i][j] = new CacheEntry();
			initializeLearningCl(m_tableSRAM[i][j]);
		}

		m_tableNVM[i].resize(m_nbNVMways);
		for(int j = 0 ; j < m_nbNVMways ; j++){
			m_tableNVM[i][j] = new CacheEntry();
			m_tableNVM[i][j]->isNVM = true;	
			initializeLearningCl(m_tableNVM[i][j]);

		}
	}

	if(m_policy == "preemptive")
		 m_predictor = new PreemptivePredictor(m_ID, m_assoc, m_nb_set, m_nbNVMways, m_tableSRAM, m_tableNVM,this);	
	else if(m_policy == "LRU")
		 m_predictor = new LRUPredictor(m_ID, m_assoc, m_nb_set, m_nbNVMways, m_tableSRAM, m_tableNVM, this);	
	else if(m_policy == "Saturation")
		 m_predictor = new SaturationCounter(m_ID, m_assoc, m_nb_set, m_nbNVMways, m_tableSRAM, m_tableNVM , this);	
	else if(m_policy == "Instruction")
		 m_predictor = new InstructionPredictor(m_ID, m_assoc, m_nb_set, m_nbNVMways, m_tableSRAM, m_tableNVM , this);	
	else if(m_policy == "PHC")
		 m_predictor = new PHCPredictor(m_ID, m_assoc, m_nb_set, m_nbNVMways, m_tableSRAM, m_tableNVM , this);	
	else if(m_policy == "Cerebron")
		 m_predictor = new CerebronPredictor(m_ID, m_assoc, m_nb_set, m_nbNVMways, m_tableSRAM, m_tableNVM , this);	
	/*else if(m_policy == "Monitor")
		 m_predictor = new monitorPredictor(m_ID, m_assoc, m_nb_set, m_nbNVMways, m_tableSRAM, m_tableNVM , this);*/
	else if(m_policy == "Perceptron")
		 m_predictor = new PerceptronPredictor(m_ID, m_assoc, m_nb_set, m_nbNVMways, m_tableSRAM, m_tableNVM , this);	
	else if(m_policy == "SimplePerceptron")
		 m_predictor = new SimplePerceptronPredictor(m_ID, m_assoc, m_nb_set, m_nbNVMways, m_tableSRAM, m_tableNVM , this);	
	//else if(m_policy == "SimplePerceptron")
	//	 m_predictor = new SimplePerceptronPredictor(m_ID, m_assoc, m_nb_set, m_nbNVMways, m_tableSRAM, m_tableNVM , this);	
	else if(m_policy == "DBAMB" || m_policy == "DBA" || m_policy == "DBAM" || m_policy == "DBAMBS")
		 m_predictor = new DBAMBPredictor(m_ID, m_assoc, m_nb_set, m_nbNVMways, m_tableSRAM, m_tableNVM , this);	
	else {
		assert(false && "Cannot initialize predictor for HybridCache");
	}
	
	if(m_ID == -1 && m_policy != "DBAMB")
	{
		
		if(m_policy == "Cerebron" )
		{		
			int learning_policy_sets = simu_parameters.Cerebron_separateLearning ? m_nb_set/2 : m_nb_set;
			int learning_weight_sets = simu_parameters.Cerebron_separateLearning ? m_nb_set/2 : 0;

			for(int i = 0 ; i < learning_policy_sets; i++)
			{
				for(int j = 0 ; j < m_nbSRAMways ; j++)
					m_tableSRAM[i][j]->isLearning_policy = true;
				for(int j = 0 ; j < m_nbNVMways ; j++)
					m_tableNVM[i][j]->isLearning_policy = true;
			}
			for(int i = learning_weight_sets ; i < m_nb_set; i++)
			{
				for(int j = 0 ; j < m_nbSRAMways ; j++)
					m_tableSRAM[i][j]->isLearning_weight = true;
				for(int j = 0 ; j < m_nbNVMways ; j++)
					m_tableNVM[i][j]->isLearning_weight = true;
			}
		}
		else
		{
			int constituency = m_nb_set / simu_parameters.nb_sampled_sets;
			int index = 0;
		
			for(int i = 0 ; i+index < m_nb_set; i += constituency)
			{
				for(int j = 0 ; j < m_nbSRAMways ; j++)
					m_tableSRAM[i+index][j]->isLearning = true;
				for(int j = 0 ; j < m_nbNVMways ; j++)
					m_tableNVM[i+index][j]->isLearning = true;
	
				index = (index+1)%constituency;
			}
		}		
	}
	
	m_start_index = log2(blocksize)-1;
	m_end_index = log2(m_blocksize) + log2(m_nb_set);

	stats_missSRAM = vector<uint64_t>(2 , 0);
	stats_hitsSRAM = vector<uint64_t>(2 , 0);
	stats_cleanWBSRAM = 0;
	stats_dirtyWBSRAM = 0;

	stats_missNVM = vector<uint64_t>(2 , 0);
	stats_hitsNVM = vector<uint64_t>(2 , 0);
	stats_cleanWBNVM = 0;
	stats_dirtyWBNVM = 0;
	stats_evict = 0;

	stats_migration = vector<uint64_t>(2,0);
	
	stats_nbFetchedLines = 0;
	stats_nbAlmostROlines = 0;
	stats_nbLostLine = 0;
	stats_nbROlines = 0;
	stats_nbROaccess = 0;
	stats_nbRWlines = 0;
	stats_nbRWaccess = 0;
	stats_nbAlmostROaccess = 0;
	stats_nbWOlines = 0 ;
	stats_nbWOaccess = 0;
	stats_bypass = 0;
	stats_nbDeadMigration = 0;
	stats_nbPingMigration = 0;
	
	stats_histo_ratioRW.clear();
	stats_allocateWB = 0, stats_allocateDemand = 0;
	stats_demand_BP = 0, stats_WB_BP = 0;
	
	// Record the number of operations issued by the cache 
	stats_operations = vector<uint64_t>(NUM_MEM_CMDS , 0); 

	if(simu_parameters.enablePCHistoryTracking && m_printStats){	
		ofstream file_history_cache("LLC_PC_History.out", std::ofstream::trunc); //Create the file
		file_history_cache.close(); 
	}
	
	if(simu_parameters.traceLLC && m_ID == -1)
		LLC_trace = gzopen("LLC_trace.out", "wb8");
}

void
HybridCache::initializeLearningCl(CacheEntry* entry)
{

	if(m_policy == "Perceptron")
	{
		entry->perceptron_BPpred = vector<int>(simu_parameters.perceptron_BP_features.size() , 0);
		entry->perceptron_BPHash = vector<int>(simu_parameters.perceptron_BP_features.size() , 0);
		entry->predictedReused = vector<bool>(simu_parameters.perceptron_BP_features.size() , false);

		entry->perceptron_Allocpred = vector<int>(simu_parameters.perceptron_Allocation_features.size() , 0);
		entry->perceptron_AllocHash = vector<int>(simu_parameters.perceptron_Allocation_features.size() , 0);	
	}

	if(m_policy == "PHC" || m_policy == "Cerebron")
	{
		entry->PHC_allocation_pred = vector< pair<int,allocDecision> >(simu_parameters.PHC_features.size()\
			 , pair<int,allocDecision>(0, ALLOCATE_IN_SRAM) );
	}
	
	if( m_policy == "SimplePerceptron")
	{
		entry->simple_perceptron_pred =  vector<allocDecision>(simu_parameters.simple_perceptron_features.size() , ALLOCATE_IN_NVM);
		entry->simple_perceptron_hash = vector<int>(simu_parameters.simple_perceptron_features.size() , 0);
	}
}

HybridCache::HybridCache(const HybridCache& a) : HybridCache(a.getID(), a.isInstCache(),\
		 a.getSize(), a.getAssoc() , a.getBlockSize(), a.getNVMways(), a.getPolicy(), a.getSystem()) 
{
}

HybridCache::~HybridCache(){
	for (int i = 0; i < m_nb_set; i++) {
		for (int j = 0; j < m_nbSRAMways; j++) {
		    delete m_tableSRAM[i][j];
		}
		for (int j = 0; j < m_nbNVMways; j++) {
		    delete m_tableNVM[i][j];
		}
	}
//	delete m_predictor;
}

void 
HybridCache::finishSimu()
{
	for (int i = 0; i < m_nb_set; i++) {
		for (int j = 0; j < m_nbSRAMways; j++) {
		    updateStatsDeallocate(m_tableSRAM[i][j]);
		}
		for (int j = 0; j < m_nbNVMways; j++) {
		    updateStatsDeallocate(m_tableNVM[i][j]);
		}
	}
	m_predictor->finishSimu();
	
	if(simu_parameters.traceLLC && m_ID == -1)
	{
		gzclose(LLC_trace);
	}

}

bool
HybridCache::lookup(Access element)
{	
	entete_debug();
	DPRINTF(DebugCache , "Lookup of addr %#lx\n" ,  element.m_address);
	return getEntry(element.block_addr) != NULL;
}

allocDecision
HybridCache::handleAccess(Access element)
{
	uint64_t address = element.m_address;
	bool isWrite = element.isWrite();
 	int size = element.m_size;
	RD_TYPE rd_type = RD_NOT_ACCURATE;
	
	if(!m_isWarmup)
		stats_operations[element.m_type]++;
	
	assert(size > 0);
	
	uint64_t block_addr = bitRemove(address , 0 , m_start_index+1);
	element.block_addr = block_addr;
	
	int id_set = addressToCacheSet(address);

	int stats_index = isWrite ? 1 : 0;

	CacheEntry* current = getEntry(block_addr);
	allocDecision des = ALLOCATE_IN_SRAM;
	
	if(current == NULL){ // The cache line is not in the hybrid cache, Miss !
				
		CacheEntry* destination_entry = NULL;
		
		des = m_predictor->allocateInNVM(id_set, element);
		element.isSRAMerror = m_predictor->reportMiss(block_addr , id_set);

//		m_predictor->recordAllocationDecision(id_set, element, des);
		
		if(des == BYPASS_CACHE )
		{
			entete_debug();
			DPRINTF(DebugCache , "Bypassing the cache for this \n");
			if(!m_isWarmup)
				stats_bypass++;
			if( element.isDemandAccess())
				stats_demand_BP++;
			else
				stats_WB_BP++;
		}
		else
		{
		
			//Verify if the cache line is in missing tags 
			bool inNVM = (des == ALLOCATE_IN_NVM) ? true : false; 
			int id_assoc = -1;
			
			if( element.isDemandAccess())
				stats_allocateDemand++;
			else
				stats_allocateWB++;
					
			id_assoc = m_predictor->evictPolicy(id_set, inNVM);	
			
			if(inNVM){//New line allocated in NVM
				destination_entry = m_tableNVM[id_set][id_assoc];
			}
			else{//Allocated in SRAM 
				destination_entry = m_tableSRAM[id_set][id_assoc];
			}
				
			//Deallocate the cache line in the lower levels (inclusive system)
			if(destination_entry->isValid){


				if(simu_parameters.traceLLC && m_ID == -1)
				{
					string line =  "EVICT " + convert_hex(destination_entry->address);
					//cout << "Writing to LLC_trace.out: " <<  line << endl;
					gzwrite(LLC_trace, line.c_str() , LLC_TRACE_BUFFER_SIZE);	
				}


				entete_debug();
				DPRINTF(DebugCache , "Invalidation of the cache line : %#lx , id_assoc %d\n" , destination_entry->address,id_assoc);
				//Prevent the cache line from being migrated du to WBs 
				//Inform the higher level of the deallocation
				if(simu_parameters.strongInclusivity)	
					m_system->signalDeallocate(destination_entry->address); 
				
				//WB this cache line to the lower cache 			
				Access wb_request;
				wb_request.m_address = destination_entry->address;
				wb_request.m_size = 4;
				wb_request.m_pc = destination_entry->m_pc;
				if(destination_entry->isDirty)
					wb_request.m_type = MemCmd::DIRTY_WRITEBACK;
				else
					wb_request.m_type = MemCmd::CLEAN_WRITEBACK;

				signalWB(wb_request, false);	
				
				if(!m_isWarmup)
					stats_evict++;
		
			}


			
			deallocate(destination_entry);
			allocate(address , id_set , id_assoc, inNVM, element.m_pc, element.isPrefetch());			
			m_predictor->insertionPolicy(id_set , id_assoc , inNVM, element);
			


			if(inNVM){
				entete_debug();
				DPRINTF(DebugCache , "It is a Miss ! Block[%#lx] is allocated \
						 in the NVM cache : Set=%d, Way=%d\n", block_addr , id_set, id_assoc);
				if(!m_isWarmup)
				{
					stats_missNVM[stats_index]++;
					stats_hitsNVM[1]++; // The insertion write 
				}

				if(element.isWrite())
					m_tableNVM[id_set][id_assoc]->isDirty = true;			
							
				m_tableNVM[id_set][id_assoc]->coherence_state = COHERENCE_EXCLUSIVE;					
				m_tableNVM[id_set][id_assoc]->m_compilerHints = element.m_compilerHints;
			
				if(simu_parameters.enablePCHistoryTracking && m_printStats)
					m_tableNVM[id_set][id_assoc]->pc_history.push_back(element.m_pc);
			}
			else{
				entete_debug();
				DPRINTF(DebugCache , "It is a Miss ! Block[%#lx] is allocated \
						in the SRAM cache : Set=%d, Way=%d\n",block_addr, id_set, id_assoc);
				if(!m_isWarmup){				
					stats_missSRAM[stats_index]++;			
					stats_hitsSRAM[1]++; // The insertion write 
				}
			
				if(element.isWrite())
					m_tableSRAM[id_set][id_assoc]->isDirty = true;

				m_tableSRAM[id_set][id_assoc]->coherence_state = COHERENCE_EXCLUSIVE;
				m_tableSRAM[id_set][id_assoc]->m_compilerHints = element.m_compilerHints;

				if(simu_parameters.enablePCHistoryTracking&& m_printStats)
					m_tableSRAM[id_set][id_assoc]->pc_history.push_back(element.m_pc);
			}
		}
	}
	else{
		// It is a hit in the cache 		
		int id_assoc = -1;
		map<uint64_t,HybridLocation>::iterator p = m_tag_index.find(current->address);
		id_assoc = p->second.m_way;
		string a = current->isNVM ? "in NVM" : "in SRAM";

		entete_debug();
		DPRINTF(DebugCache , "It is a hit ! Block[%#lx] Found %s Set=%d, Way=%d, Req=%s\n" \
				 , block_addr, a.c_str(), id_set, id_assoc , memCmd_str[element.m_type]);
		
		m_predictor->updatePolicy(id_set , id_assoc, current->isNVM, element , false);
		
		current->justMigrate = false;
		if(element.isWrite()){
			current->isDirty = true;
			current->nbWrite++;
			current->coherence_state = COHERENCE_MODIFIED;
		}
		else{
			//current->coherence_state stays in the same state in any case 
			if(element.m_type != CLEAN_WRITEBACK)
				current->nbRead++;		
		}

		if(simu_parameters.enablePCHistoryTracking && m_printStats)
			current->pc_history.push_back(element.m_pc);
	
		if(!m_isWarmup)
		{
			if(current->isNVM)	
				stats_hitsNVM[stats_index]++;
			else
				stats_hitsSRAM[stats_index]++;
		
		}
		
		
		current->m_compilerHints = element.m_compilerHints;
	
		if(simu_parameters.traceLLC && m_ID == -1)
			rd_type = classifyRD(id_set , id_assoc);	
	}
	
	if(simu_parameters.traceLLC && m_ID == -1 && des != BYPASS_CACHE)
	{
		string access = element.isWrite() ? "WRITE" : "READ";
		string data_type = element.isInstFetch() ? "INST" : "DATA";
		string line = data_type + " " + access + " " + str_RD_status[rd_type];
		line += " 0x" + convert_hex(block_addr) + " 0x" + convert_hex(element.m_pc);  
		//cout << "Writing to LLC_trace.out: " <<  line << endl;
		gzwrite(LLC_trace, line.c_str() , LLC_TRACE_BUFFER_SIZE);	
	}
	return des;
}


void 
HybridCache::deallocate(CacheEntry* destination_entry)
{
	uint64_t addr = destination_entry->address;
	updateStatsDeallocate(destination_entry);

	map<uint64_t,HybridLocation>::iterator it = m_tag_index.find(addr);	
	if(it != m_tag_index.end()){
		m_tag_index.erase(it);	
	}	

	destination_entry->initEntry();
	
	
}

void
HybridCache::updateStatsDeallocate(CacheEntry* current)
{

	if(!current->isValid || m_isWarmup)
		return;
	
	if(simu_parameters.enablePCHistoryTracking && m_printStats){	
		ofstream file_history_cache("LLC_PC_History.out", std::ostream::app);
		file_history_cache << std::hex << current->address << "\t";
		for(unsigned i = 0; i < current->pc_history.size()-1; i++)
			file_history_cache << current->pc_history[i] << ",";
		file_history_cache << current->pc_history[current->pc_history.size()-1] << endl;
		file_history_cache << std::dec;
		file_history_cache.close();
	}
	
	if(stats_histo_ratioRW.count(current->nbWrite) == 0)
		stats_histo_ratioRW.insert(pair<int,int>(current->nbWrite , 0));
		
	stats_histo_ratioRW[current->nbWrite]++;
	
	if(current->justMigrate)
		stats_nbDeadMigration++;
	
	if(current->nbWrite == 0 && current->nbRead > 0){
		stats_nbROlines++;
		stats_nbROaccess+= current->nbRead; 
	}
	else if(current->nbWrite > 0 && current->nbRead == 0){
		stats_nbWOlines++;
		stats_nbWOaccess+= current->nbWrite; 	
	}
	else if( current->nbWrite == 0 && current->nbRead == 0){	
		stats_nbLostLine++;
	}
	else if( current->nbWrite == 1 && current->nbRead > 0){
		stats_nbAlmostROlines++;
		stats_nbAlmostROaccess+= current->nbWrite + current->nbRead; 		
	}
	else
	{
		stats_nbRWlines++;
		stats_nbRWaccess+= current->nbWrite + current->nbRead; 	
	}
}

void
HybridCache::deallocate(uint64_t block_addr)
{
	entete_debug();
	DPRINTF(DebugCache , "DEALLOCATE %#lx\n", block_addr);
	map<uint64_t,HybridLocation>::iterator it = m_tag_index.find(block_addr);	
	
	if(it != m_tag_index.end()){

		int id_set = blockAddressToCacheSet(block_addr);

		HybridLocation loc = it->second;
		CacheEntry* current = NULL; 

		if(loc.m_inNVM)
			current = m_tableNVM[id_set][loc.m_way]; 		
		else
			current = m_tableSRAM[id_set][loc.m_way]; 		

		updateStatsDeallocate(current);
		current->initEntry();
				
		m_tag_index.erase(it);	

	}
}

void 
HybridCache::allocate(uint64_t address , int id_set , int id_assoc, bool inNVM, uint64_t pc, bool isPrefetch)
{

	uint64_t block_addr = bitRemove(address , 0 , m_start_index+1);
	
	if(inNVM){
	 	assert(!m_tableNVM[id_set][id_assoc]->isValid);

		m_tableNVM[id_set][id_assoc]->isValid = true;	
		m_tableNVM[id_set][id_assoc]->address = block_addr;
		m_tableNVM[id_set][id_assoc]->policyInfo = 0;
		m_tableNVM[id_set][id_assoc]->m_pc = pc;
		m_tableNVM[id_set][id_assoc]->isPrefetch = isPrefetch;
	}
	else
	{
	 	assert(!m_tableSRAM[id_set][id_assoc]->isValid);

		m_tableSRAM[id_set][id_assoc]->isValid = true;	
		m_tableSRAM[id_set][id_assoc]->address = block_addr;
		m_tableSRAM[id_set][id_assoc]->policyInfo = 0;		
		m_tableSRAM[id_set][id_assoc]->m_pc = pc;		
		m_tableSRAM[id_set][id_assoc]->isPrefetch = isPrefetch;
	}
	
	if(!m_isWarmup)
		stats_nbFetchedLines++;	
		
	HybridLocation loc(id_assoc, inNVM);
	m_tag_index.insert(pair<uint64_t , HybridLocation>(block_addr , loc));
}


void HybridCache::triggerMigration(int set, int id_assocSRAM, int id_assocNVM , bool fromNVM)
{
	entete_debug();
	DPRINTF(DebugCache , "TriggerMigration set %d , id_assocSRAM %d , id_assocNVM %d\n" , set , id_assocSRAM , id_assocNVM);
	
	CacheEntry* from_entry = fromNVM ? m_tableNVM[set][id_assocNVM] : m_tableSRAM[set][id_assocSRAM];
	CacheEntry* destination_entry = fromNVM ? m_tableSRAM[set][id_assocSRAM] : m_tableNVM[set][id_assocNVM];
	
	if(!m_isWarmup)
		stats_migration[fromNVM]++;
	
	assert(from_entry->isValid);
	
	if(destination_entry->isValid)
	{
		//WB this cache line to the lower levels 			
		Access wb_request;
		wb_request.m_address = destination_entry->address;
		wb_request.m_pc = destination_entry->m_pc;
		wb_request.m_size = 4;
		if(destination_entry->isDirty)
			wb_request.m_type = MemCmd::DIRTY_WRITEBACK;
		else
			wb_request.m_type = MemCmd::CLEAN_WRITEBACK;
		signalWB(wb_request, false);
		stats_evict++;	
	}
	
	deallocate(destination_entry);
	destination_entry->copyCL(from_entry);
	destination_entry->justMigrate = true;
	destination_entry->policyInfo = cpt_time;
	destination_entry->isValid = true;
	
	map<uint64_t,HybridLocation>::iterator p1 = m_tag_index.find(destination_entry->address);
	if(p1 != m_tag_index.end())
	{
 		p1->second.m_inNVM = !fromNVM;
		p1->second.m_way = fromNVM ? id_assocSRAM : id_assocNVM;
	}
	
	from_entry->isValid = false;
	
	
	if(fromNVM)
	{
		//One read NVM , one write SRAM 
		stats_hitsNVM[0]++;
		stats_hitsSRAM[1]++;
	}
	else
	{
		//One read SRAM , one write NVM  
		stats_hitsNVM[1]++;
		stats_hitsSRAM[0]++;
	}
}


int 
HybridCache::addressToCacheSet(uint64_t address) 
{
	uint64_t a = address;
	a = bitRemove(a , 0, m_start_index);
	a = bitRemove(a , m_end_index,64);
	
	a = a >> (m_start_index+1);
	assert(a < (unsigned int)m_nb_set);
	
	return (int)a;
}

void
HybridCache::signalWB(Access wb_request, bool isKept)
{
	CacheEntry* entry = getEntry(wb_request.m_address);
	if(entry != NULL)
	{
		entete_debug();
		DPRINTF(DebugCache , "Invalidation of the block [%#lx]\n" , entry->address);
		m_system->signalWB(wb_request , isKept);		
	}
	
}

bool
HybridCache::receiveInvalidation(uint64_t block_addr)
{
	entete_debug();
	DPRINTF(DebugCache , "HERE Receive Invalidation for the block [%#lx] by the LLC\n" , block_addr);

	map<uint64_t,HybridLocation>::iterator p = m_tag_index.find(block_addr);
	
	if (p != m_tag_index.end()){

	
		DPRINTF(DebugCache , "Find block [%#lx]\n" , block_addr);

		int id_set = blockAddressToCacheSet(block_addr);
		HybridLocation loc = p->second;
		CacheEntry* entry = NULL;

		if(loc.m_inNVM)
			entry = m_tableNVM[id_set][loc.m_way];
		else 
			entry = m_tableSRAM[id_set][loc.m_way];	
	
		entete_debug();
		DPRINTF(DebugCache , "Receive Invalidation for the block [%#lx] by the LLC\n" , entry->address);

		entry->isValid = false;
		return entry->isDirty;
	}
	else{
		DPRINTF(DebugCache , "Didn't find block [%#lx]\n" , block_addr);
		return false;
	
	}
}


void HybridCache::startWarmup()
{
	m_isWarmup = true;
	if(m_predictor)
		m_predictor->startWarmup();
}

void HybridCache::stopWarmup()
{
	m_isWarmup = false;
	if(m_predictor)
		m_predictor->stopWarmup();
}


int 
HybridCache::blockAddressToCacheSet(uint64_t block_addr) 
{
	uint64_t a =block_addr;
	a = bitRemove(a , m_end_index,64);
	
	a = a >> (m_start_index+1);
	assert(a < (unsigned int)m_nb_set);
	
	return (int)a;
}

CacheEntry*
HybridCache::getEntry(uint64_t block_addr)
{
//	uint64_t block_addr =  bitRemove(addr , 0 , m_start_index+1);

	map<uint64_t,HybridLocation>::iterator p = m_tag_index.find(block_addr);
	
	if (p != m_tag_index.end()){
	
		int id_set = addressToCacheSet(block_addr);
		HybridLocation loc = p->second;
		if(loc.m_inNVM)
			return m_tableNVM[id_set][loc.m_way];
		else 
			return m_tableSRAM[id_set][loc.m_way];	
	

	}
	else{
		return NULL;
	}
}

void 
HybridCache::openNewTimeFrame()
{
	if(!m_isWarmup)
		m_predictor->openNewTimeFrame();
}

RD_TYPE
HybridCache::classifyRD(int set , int index)
{
	vector<CacheEntry*> line;
	int64_t ref_rd = m_tableSRAM[set][index]->policyInfo;
	
	line = m_tableSRAM[set];
	
	int position = 0;
	
	/* Determine the position of the cache line in the LRU stack */
	for(unsigned i = 0 ; i < line.size() ; i ++)
	{
		if(line[i]->policyInfo < ref_rd && line[i]->isValid)
			position++;
	}	

	if(position < 4)
		return RD_SHORT;
	else
		return RD_MEDIUM;
}


bool
HybridCache::isPrefetchBlock(uint64_t addr)
{
	CacheEntry* entry = getEntry(addr);
	if(entry != NULL)
		return entry->isPrefetch;
	return false;
}

void
HybridCache::resetPrefetchFlag(uint64_t addr)
{
	CacheEntry* entry = getEntry(addr);
	if(entry != NULL)
		entry->isPrefetch = false;
}

void 
HybridCache::print(ostream& out) 
{
	printResults(out);	
}


void
HybridCache::printConfig(std::ostream& out) 
{		
	string entete = "Cache";
	if(m_ID != -1){
		entete+="L1";
		if(m_isInstructionCache)
		 	entete += "I";
		else
			entete += "D";
	}
	else
		entete+= "LLC";

	out << entete << ":Size\t" << m_cache_size << endl;
	out << entete << ":SRAMways\t" << m_nbSRAMways << endl;
	out << entete << ":NVMways\t" << m_nbNVMways << endl;
	out << entete << ":Blocksize\t" << m_blocksize << endl;
	out << entete << ":Sets    \t" << m_nb_set << endl;
	out << entete << ":Predictor\t" << m_policy << endl;
	m_predictor->printConfig(out,entete);
	out << "************************" << endl;
}


void 
HybridCache::printResults(std::ostream& out) 
{
	uint64_t total_missSRAM =  stats_missSRAM[0] + stats_missSRAM[1];
	uint64_t total_missNVM =  stats_missNVM[0] + stats_missNVM[1];
	uint64_t total_miss = total_missNVM + total_missSRAM;
	
	uint64_t total_accessSRAM =  stats_hitsSRAM[0] + stats_hitsSRAM[1];
	uint64_t total_accessNVM =  stats_hitsNVM[0] + stats_hitsNVM[1];
	uint64_t total_access = total_accessSRAM + total_accessNVM;
	
	string entete = "Cache";
	if(m_ID != -1){
		entete+="L1";
		if(m_isInstructionCache)
		 	entete += "I";
		else
			entete += "D";
	}
	else
		entete+= "LLC";

	if(total_access != 0 || stats_bypass > 0){
	
		out << entete << ":Results" << endl;
		out << entete << ":TotalAccess\t"<< total_access << endl;
		out << entete << ":TotalHits\t" << total_access - total_miss << endl;
		out << entete << ":TotalMiss\t" << total_miss << endl;		
		out << entete << ":MissRate\t" << (double)(total_miss)*100 / (double)(total_access) << "%"<< endl;
//		out << entete << ":CleanWriteBack\t" << stats_cleanWBNVM + stats_cleanWBSRAM << endl;
//		out << entete << ":DirtyWriteBack\t" << stats_dirtyWBNVM + stats_dirtyWBSRAM << endl;
		out << entete << ":AllocateDemand\t" << stats_allocateDemand << endl;
		out << entete << ":AllocateWB\t" << stats_allocateWB << endl;
		
		out << entete << ":Eviction\t" << stats_evict << endl;	
		out << entete << ":Bypass\t" << stats_bypass << endl;
		out << entete << ":DemandBypass\t" << stats_demand_BP << endl;
		out << entete << ":WBBypass\t" << stats_WB_BP << endl;
		
		out << entete << ":NVMways:reads\t"<< stats_hitsNVM[0] + stats_migration[true]<< endl;
		out << entete << ":NVMways:writes\t"<< stats_hitsNVM[1] + stats_dirtyWBNVM + stats_migration[false] << endl;		
		out << entete << ":SRAMways:reads\t"<< stats_hitsSRAM[0] + stats_migration[false] << endl;
		out << entete << ":SRAMways:writes\t"<< stats_hitsSRAM[1] + stats_dirtyWBSRAM + stats_migration[true]<< endl;	
		
		//cout << "************************" << endl;
		
		if(m_printStats)
		{
			out << entete << ":MigrationFromNVM\t" << stats_migration[true] << endl;
			out << entete << ":MigrationFromSRAM\t" << stats_migration[false] << endl;
			out << entete << ":BlockClassification" << endl;
			out << entete << ":FetchedBlocks\t" << stats_nbFetchedLines << endl;
			out << entete << ":DeadBlocks\t" << stats_nbLostLine << "\t" << \
				(double)stats_nbLostLine*100/(double)stats_nbFetchedLines << "%" << endl;
			out << entete << ":ROBlocks\t" << stats_nbROlines << "\t" << \
				(double)stats_nbROlines*100/(double)stats_nbFetchedLines << "%" << endl;
			out << entete << ":WOBlocks\t" << stats_nbWOlines << "\t" << \
				(double)stats_nbWOlines*100/(double)stats_nbFetchedLines << "%" << endl;
			out << entete << ":RWBlocks\t" << stats_nbRWlines << "\t" << \
				(double)stats_nbRWlines*100/(double)stats_nbFetchedLines << "%" << endl;
			/*
			out << "Histogram NB Write" << endl;
			for(auto p : stats_histo_ratioRW){
				out << p.first << "\t" << p.second << endl;
			}*/
		}

		out << entete << ":instructionsDistribution" << endl;

		for(unsigned i = 0 ; i < stats_operations.size() ; i++){
			if(stats_operations[i] != 0)
				out << entete << ":" << memCmd_str[i]  << "\t" << stats_operations[i] << endl;
		}
		m_predictor->printStats(out, entete);
	
	}
}


uint64_t
HybridCache::getActualPC()
{
	return m_system->getActualPC();
}

void
HybridCache::entete_debug()
{
	DPRINTF(DebugCache , "CACHE[%d]-", m_ID);
	if(m_isInstructionCache){
		DPRINTF(DebugCache , "I:");	
	}
	else{	
		DPRINTF(DebugCache , "D:");
	}
}

//ostream&
//operator<<(ostream& out, const HybridCache& obj)
//{
//    obj.print(out);
//    out << flush;
//    return out;
//}
