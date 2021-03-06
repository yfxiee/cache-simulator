#include <math.h>
#include "Predictor.hh"
#include "ReplacementPolicy.hh"
#include "Cache.hh"

using namespace std;


deque<uint64_t> Predictor::m_global_PChistory;
deque<uint64_t> Predictor::m_callee_PChistory;


Predictor::Predictor(int id, int nbAssoc , int nbSet, int nbNVMways, DataArray& SRAMtable , DataArray& NVMtable, HybridCache* cache) :\
	m_tableSRAM(SRAMtable), m_tableNVM(NVMtable)
{
	
	m_nb_set = nbSet;
	m_assoc = nbAssoc;
	m_nbNVMways = nbNVMways;
	m_nbSRAMways = nbAssoc - nbNVMways;
	m_ID = id;
	m_policy = cache->getPolicy();
	
	m_cpt_bypassTag = 0;
	m_replacementPolicyNVM_ptr = new LRUPolicy(m_nbNVMways , m_nb_set , NVMtable);
	m_replacementPolicySRAM_ptr = new LRUPolicy(m_nbSRAMways , m_nb_set, SRAMtable);	
	
	m_cache = cache;

	stats_NVM_errors = vector<int>(1, 0);
	stats_SRAM_errors = vector<int>(1, 0);
	stats_BP_errors = vector<int>(1,0);
	stats_MigrationErrors = vector<int>(1,0);
	
	m_trackError = (m_ID == -1);
	stats_COREerrors = 0;
	stats_WBerrors = 0;
	
	stats_beginTimeFrame = 0;
	
	m_global_PChistory = deque<uint64_t>();
	m_callee_PChistory = deque<uint64_t>();
	
	if(simu_parameters.printDebug && simu_parameters.enableDatasetSpilling)
	{
		//stats_SRAMpressure = vector< vector<bool> >();
	}
	/********************************************/ 
	if(simu_parameters.simulate_conflicts)
	{
	
		/* Allocation of array of tracking conflict/capacity miss,
		 simulation of a FU cache of the same size */ 
	
		int size_SRAM_FU =  m_nbSRAMways*nbSet;
		int size_NVM_FU =  m_nbNVMways*nbSet;
	
		m_NVM_FU.resize(size_NVM_FU);
		for(int i = 0 ; i < size_NVM_FU; i++)
		{
			m_NVM_FU[i] = new MissingTagEntry();
		}
		m_SRAM_FU.resize(size_SRAM_FU);
		for(int i = 0 ; i < size_SRAM_FU; i++)
		{
			m_SRAM_FU[i] = new MissingTagEntry();
		}
	}	 
//	/********************************************/ 
//	m_trackError = false;
//	if(m_nbNVMways != 0 && simu_parameters.size_MT_SRAMtags != 0)
//		m_trackError = true;
	

	/* Allocation of the BP missing tags array*/
	BP_missing_tags.resize(m_nb_set);
	for(int i = 0 ; i < m_nb_set; i++)
	{
		BP_missing_tags[i].resize(m_assoc);
		for(unsigned j = 0 ; j < BP_missing_tags[i].size(); j++)
		{
			BP_missing_tags[i][j] = new MissingTagEntry();
		}
	}

	m_SRAMassoc_MT_size = simu_parameters.size_MT_SRAMtags;
	m_NVMassoc_MT_size = simu_parameters.size_MT_NVMtags;
	m_nb_MTcouters_sampling = simu_parameters.nb_MTcouters_sampling;

	if(m_ID == -1){
	
		/* Allocation of the SRAM missing tags array*/
		m_SRAM_missing_tags.resize(m_nb_set);
		//m_NVM_missing_tags.resize(m_nb_set);

		for(int i = 0 ; i < m_nb_set; i++)
		{
			m_SRAM_missing_tags[i].resize(m_SRAMassoc_MT_size);
			//m_NVM_missing_tags[i].resize(m_NVMassoc_MT_size);

			for(int j = 0 ; j < m_SRAMassoc_MT_size; j++)
			{
				m_SRAM_missing_tags[i][j] = new MissingTagEntry();
				if(m_policy == "Cerebron" || m_policy == "SimplePerceptron")
					m_SRAM_missing_tags[i][j]->initFeaturesHash(m_policy);
			}
			/*
			for(int j = 0 ; j < m_NVMassoc_MT_size; j++)
			{
				m_NVM_missing_tags[i][j] = new MissingTagEntry();
				if(m_policy == "Cerebron")
					m_NVM_missing_tags[i][j]->initFeaturesHash();
			}*/
		}
		if(simu_parameters.enableDatasetSpilling)
		{
		
			m_SRAM_MT_counters = vector<uint64_t>(m_nb_set , 0);		
			m_isSRAMbusy = vector<bool>(m_nb_set , false);		

			m_NVM_MT_counters = vector<uint64_t>(m_nb_set , 0);		
			m_isNVMbusy = vector<bool>(m_nb_set , false);		
		}
	}
	
}

Predictor::~Predictor()

{
	if(m_trackError){
		for(unsigned i = 0 ; i < BP_missing_tags.size() ; i++)
		{
			for(unsigned j = 0 ; j < BP_missing_tags[i].size() ; j++)
			{
				delete BP_missing_tags[i][j];
			}
		}
	
		for(int i = 0 ; i < m_nb_set ; i++)
		{
			for(unsigned j = 0 ; j < m_SRAM_missing_tags[i].size() ; j++)
				delete m_SRAM_missing_tags[i][j];
			/*for(unsigned j = 0 ; j < m_NVM_missing_tags[i].size() ; j++)
				delete m_NVM_missing_tags[i][j];
			*/
		}
	}
	delete m_replacementPolicyNVM_ptr;
	delete m_replacementPolicySRAM_ptr;
}

void
Predictor::startWarmup()
{
	m_isWarmup = true;
}

void
Predictor::stopWarmup()
{
	m_isWarmup = false;
}


void  
Predictor::evictRecording(int set, int assoc_victim , bool inNVM)
{
	if(!m_trackError)
		return;

	CacheEntry* current=NULL;
		
	if(inNVM)
	{
		//Checking NVM MT tags
		//Choose the oldest victim for the missing tags to replace from the current one 	
		/*
		uint64_t cpt_longestime = m_NVM_missing_tags[set][0]->last_time_touched;
		uint64_t index_victim = 0;
		current = m_tableNVM[set][assoc_victim];
		for(unsigned i = 0 ; i < m_NVM_missing_tags[set].size(); i++)
		{
			if(!m_NVM_missing_tags[set][i]->isValid)
			{
				index_victim = i;
				break;
			}	
		
			if(cpt_longestime > m_NVM_missing_tags[set][i]->last_time_touched){
				cpt_longestime = m_NVM_missing_tags[set][i]->last_time_touched; 
				index_victim = i;
			}	
		}

		m_NVM_missing_tags[set][index_victim]->addr = current->address;
		m_NVM_missing_tags[set][index_victim]->last_time_touched = cpt_time;
		m_NVM_missing_tags[set][index_victim]->isValid = current->isValid;
		m_NVM_missing_tags[set][index_victim]->cost_value = current->cost_value;
		*/
	}
	else
	{
		//Checking SRAM MT tags
		//Choose the oldest victim for the missing tags to replace from the current one 	
		if(!simu_parameters.enableFullSRAMtraffic)
		{
			uint64_t cpt_longestime = m_SRAM_missing_tags[set][0]->last_time_touched;
			uint64_t index_victim = 0;
			current = m_tableSRAM[set][assoc_victim];
		
			for(unsigned i = 0 ; i < m_SRAM_missing_tags[set].size(); i++)
			{
				if(!m_SRAM_missing_tags[set][i]->isValid)
				{
					index_victim = i;
					break;
				}	
		
				if(cpt_longestime > m_SRAM_missing_tags[set][i]->last_time_touched){
					cpt_longestime = m_SRAM_missing_tags[set][i]->last_time_touched; 
					index_victim = i;
				}	
			}

			m_SRAM_missing_tags[set][index_victim]->addr = current->address;
			m_SRAM_missing_tags[set][index_victim]->last_time_touched = cpt_time;
			m_SRAM_missing_tags[set][index_victim]->isValid = current->isValid;
			m_SRAM_missing_tags[set][index_victim]->cost_value = current->cost_value;
		
			if(m_policy == "Cerebron")
			{
				for(unsigned i = 0 ; i < simu_parameters.PHC_features.size() ; i++)
					m_SRAM_missing_tags[set][index_victim]->features_hash[i] = current->PHC_allocation_pred[i].first;
		
				m_SRAM_missing_tags[set][index_victim]->missPC = current->missPC;
			}		
			else if(m_policy == "SimplePerceptron")
			{
				//for(unsigned i = 0 ; i < simu_parameters.simple_perceptron_features.size() ; i++)
				//	m_SRAM_missing_tags[set][index_victim]->features_hash[i] = current->simple_perceptron_hash[i];
				
				m_SRAM_missing_tags[set][index_victim]->missPC = current->missPC;
			}		
		}
	}
}


void
Predictor::insertionPolicy(uint64_t set, uint64_t index, bool inNVM, Access element)
{
	CacheEntry* entry = inNVM ? m_tableNVM[set][index] : m_tableSRAM[set][index];
	uint64_t block_addr = bitRemove(element.m_address , 0 , log2(BLOCK_SIZE));
	updateFUcaches(block_addr , inNVM);
	updateCachePressure();


	//Update the missing tag as a new cache line is brough into the cache 
	//Has to remove the MT entry if it exists there 
	//DPRINTF("Predictor::insertRecord Begin ");	
	if(m_trackError)
	{
		if( simu_parameters.enableFullSRAMtraffic )
		{
			//insertIntoSRAMMT(set , inNVM, element );
		}
		else
		{
			if(inNVM)
			{
				for(unsigned i = 0 ; i < m_SRAM_missing_tags[set].size(); i++)
				{
					if(m_SRAM_missing_tags[set][i]->addr == entry->address)
						m_SRAM_missing_tags[set][i]->isValid = false;
				}		
			}		
		}	
		updateBypassTag( entry , set, inNVM);
	}	
	//DPRINTF("Predictor::insertRecord Begin ");

}
/*
void 
Predictor::insertIntoSRAMMT(uint64_t set , bool inNVM, Access element )
{
	uint64_t cpt_longestime = m_SRAM_missing_tags[set][0]->last_time_touched;
	uint64_t index_victim = 0;
	uint64_t block_addr = element.block_addr;
	
	//If block already present, update LRU position
	for(unsigned i = 0 ; i < m_SRAM_missing_tags[set].size(); i++)
	{
		if(m_SRAM_missing_tags[set][i].block_addr == block_addr )
		{
			m_SRAM_missing_tags[set][i].last_time_touched = cpt_time;
			return;
		}	
	}
	int index_victim = 0;
	uint64_t cpt_longestime = cpt_time;
	for(unsigned i = 0 ; i < m_SRAM_missing_tags[set].size(); i++)
	{
		if(!m_SRAM_missing_tags[set][i]->isValid)
		{
			index_victim = i;
			break;
		}	

		if(cpt_longestime > m_SRAM_missing_tags[set][i]->last_time_touched){
			cpt_longestime = m_SRAM_missing_tags[set][i]->last_time_touched; 
			index_victim = i;
		}	
	}
	
	m_SRAM_missing_tags[set][index_victim]->addr = current->address;
	m_SRAM_missing_tags[set][index_victim]->last_time_touched = cpt_time;
	m_SRAM_missing_tags[set][index_victim]->isValid = true;
	m_SRAM_missing_tags[set][index_victim]->cost_value = 0;// current->cost_value;


	if(m_policy == "Cerebron")
	{
		
		//for(unsigned i = 0 ; i < simu_parameters.PHC_features.size() ; i++)
		//	m_SRAM_missing_tags[set][index_victim]->features_hash[i] = current->PHC_allocation_pred[i].first;
		
		m_SRAM_missing_tags[set][index_victim]->missPC = element.m_pc;
	}
}
*/
int 
Predictor::missingTagCostValue(uint64_t block_addr, int set)
{
	for(unsigned i = 0 ; i < m_SRAM_missing_tags[set].size() ; i++)
	{
		if(m_SRAM_missing_tags[set][i]->addr == block_addr && m_SRAM_missing_tags[set][i]->isValid)
		{
			return m_SRAM_missing_tags[set][i]->cost_value;
		}
	}	
	return 0;
}

uint64_t 
Predictor::missingTagMissPC(uint64_t block_addr, int set)
{
	assert( m_policy == "Cerebron" || m_policy == "SimplePerceptron");
	
	for(unsigned i = 0 ; i < m_SRAM_missing_tags[set].size() ; i++)
	{
		if(m_SRAM_missing_tags[set][i]->addr == block_addr && m_SRAM_missing_tags[set][i]->isValid)
		{
			return m_SRAM_missing_tags[set][i]->missPC;
		}
	}
	
	//Sanity check , shouldn't go there 
	assert(false);

	return 0;
}

vector<int>
Predictor::missingTagFeaturesHash(uint64_t block_addr, int set)
{
	if( m_policy == "Cerebron")
	{
		for(unsigned i = 0 ; i < m_SRAM_missing_tags[set].size() ; i++)
		{
			if(m_SRAM_missing_tags[set][i]->addr == block_addr && m_SRAM_missing_tags[set][i]->isValid)
			{
				return m_SRAM_missing_tags[set][i]->features_hash;
			}
		}	
	}
	return vector<int>();
}

bool
Predictor::checkBypassTag(uint64_t block_addr , int set)
{	
	for(unsigned i = 0 ; i < BP_missing_tags[set].size() ; i++)
	{	
		if(BP_missing_tags[set][i]->addr == block_addr)
			return true;
	}
	return false;
}

void
Predictor::updateBypassTag(CacheEntry* entry, int set, bool inNVM)
{
	uint64_t block_addr = entry->address;
	//DPRINTF(DebugCache, "Predictor::recordAllocationDecision Set %d, block_addr = 0x%lx\n ", set, block_addr);

	//Look if the entry already exists, if yes => BP Error, if No insert it
	for(unsigned i = 0 ; i < BP_missing_tags[set].size() ; i++)
	{	
		if(BP_missing_tags[set][i]->addr == block_addr)
		{
			BP_missing_tags[set][i]->last_time_touched = m_cpt_bypassTag++;
			return;		
		}
	}
	//DPRINTF(DebugCache, "Predictor::recordAllocationDecision Didn't find the entry in the BP Missing Tags\n");
	int index_oldest =0;
	uint64_t oldest = BP_missing_tags[set][0]->last_time_touched;
	for(unsigned i = 0 ; i < BP_missing_tags[set].size() ; i++)
	{
		if(!BP_missing_tags[set][i]->isValid)		
		{
			index_oldest= i;
			break;
		}
	
		if(BP_missing_tags[set][i]->last_time_touched < oldest)
		{
			index_oldest = i;
			oldest = BP_missing_tags[set][i]->last_time_touched;
		}
	}
	//DPRINTF(DebugCache, "Predictor::Insertion of the new entry at assoc %d\n", index_oldest);
	
	BP_missing_tags[set][index_oldest]->last_time_touched = m_cpt_bypassTag++;
	BP_missing_tags[set][index_oldest]->addr = block_addr;
	BP_missing_tags[set][index_oldest]->isValid = true;		
	BP_missing_tags[set][index_oldest]->allocSite = inNVM;
	BP_missing_tags[set][index_oldest]->missPC = entry->missPC;
	
	
	/*
	if(m_policy == "Cerebron")
	{
		for(int i = 0 ; i < simu_parameters.PHC_features.size() ; i++)
		{		
			BP_missing_tags[set][index_oldest]->features_hash[i] = entry->PHC_allocation_pred[i].first;
			
		}
	}*/
		
}



void 
Predictor::updateFUcaches(uint64_t block_addr, bool inNVM)
{
	if(!simu_parameters.simulate_conflicts)
		return;
	DPRINTF(DebugFUcache, "Predictor::updateFUcaches update block %#lx\n", block_addr);
	
	if(m_accessedBlocks.count(block_addr) == 0)
	{
		m_accessedBlocks.insert(pair<uint64_t,uint64_t>(block_addr, cpt_time));
		DPRINTF(DebugFUcache, "First Touch of the block\n");
	}

	vector< vector<MissingTagEntry*> > FUcaches = { m_SRAM_FU , m_NVM_FU };
	
	bool find = false;
		
	for(unsigned i = 0 ; i < FUcaches[inNVM].size() && !find ; i++)
	{
		if(FUcaches[inNVM][i]->addr == block_addr)
		{
			DPRINTF(DebugFUcache, "Find at index %d , updatation !!! \n" , i);				
			FUcaches[inNVM][i]->last_time_touched = cpt_time;
			find = true;
		}
	}
	if(!find) // Data not found, insert the access in the FU cache 
	{
		for(unsigned i = 0; i < FUcaches[inNVM].size() && !find; i++)
		{
			if(!FUcaches[inNVM][i]->isValid)
			{
				FUcaches[inNVM][i]->addr = block_addr;
				FUcaches[inNVM][i]->isValid = true;
				FUcaches[inNVM][i]->last_time_touched = cpt_time;	
				DPRINTF(DebugFUcache, "Added in index %d \n" , i);				
				find = true;		
			}
		}
		if(!find){	
			DPRINTF(DebugFUcache, "HERE ? \n");				
			uint64_t min_time = FUcaches[inNVM][0]->last_time_touched, min_index = 0;
			for(unsigned i = 0; i < FUcaches[inNVM].size() ; i++)
			{
				if(FUcaches[inNVM][i]->last_time_touched < min_time)
				{
					min_time = FUcaches[inNVM][i]->last_time_touched;
					min_index = i;
				}
			}
			FUcaches[inNVM][min_index]->addr = block_addr;
			FUcaches[inNVM][min_index]->isValid = true;
			FUcaches[inNVM][min_index]->last_time_touched = cpt_time;					
		}
	}
}


bool
Predictor::reportMiss(uint64_t block_addr , int id_set)
{

	stats_total_miss++;
	
	/* Miss classification between cold/conflict/capacity */
	if(simu_parameters.simulate_conflicts)
	{
		//DPRINTF(DebugFUcache , "Predictor::reportMiss block %#lx \n" , block_addr);
		if(m_accessedBlocks.count(block_addr) == 0)
		{
			stats_cold_miss++;
			//DPRINTF(DebugFUcache , "Predictor::reportMiss block not been accessed \n" );
		}
		else
		{
			bool find = false;
			for(unsigned i = 0; i < m_NVM_FU.size() && !find; i++)
			{
				if(m_NVM_FU[i]->addr == block_addr)
				{
					stats_nvm_conflict_miss++;
					find = true;
					break;
				}
			}
			for(unsigned i = 0; i < m_SRAM_FU.size() && !find; i++)
			{
				if(m_SRAM_FU[i]->addr == block_addr)
				{
					stats_sram_conflict_miss++;
					find = true;
					break;
				}
			}
			if(!find)
				stats_capacity_miss++;
		}
	 }
	 
	/************************************************************/
	bool sram_error = hitInSRAMMissingTags(block_addr, id_set);
	if(sram_error)
	{
		stats_SRAM_errors[stats_SRAM_errors.size()-1]++;	
		
		if(simu_parameters.enableDatasetSpilling)
		{
			int starting_set = (id_set/m_nb_MTcouters_sampling) * m_nb_MTcouters_sampling; 
			for(int i = starting_set ; i < starting_set + m_nb_MTcouters_sampling ; i++)
				m_SRAM_MT_counters[id_set]++;
		
		}
	}
	/*
	if(simu_parameters.enableDatasetSpilling)
	{	
		bool nvm_error = hitInNVMMissingTags(block_addr , id_set);
		if(nvm_error)
			m_NVM_MT_counters[id_set]++;
	}*/
	
	bool bypass_error = checkBypassTag(block_addr , id_set);
	if( bypass_error )
		stats_BP_errors[stats_BP_errors.size()-1]++;
	
	
	return sram_error;
}

bool 
Predictor::hitInNVMMissingTags(uint64_t block_addr, int id_set)
{
	/* Check if the block is in the missing tag array */ 
	if(m_trackError && !m_isWarmup){
		//An error in the NVM part is a block that miss in the NVM array 
		//but not in the MT array
		for(unsigned i = 0 ; i < m_NVM_missing_tags[id_set].size() ; i++)
		{
			if(m_NVM_missing_tags[id_set][i]->addr == block_addr && m_NVM_missing_tags[id_set][i]->isValid)
			{
				return true;
			}
		}	
	}
	return false;
}		

bool 
Predictor::hitInSRAMMissingTags(uint64_t block_addr, int id_set)
{
	/* Check if the block is in the missing tag array */ 
	if(m_trackError && !m_isWarmup){
		//An error in the SRAM part is a block that miss in the SRAM array 
		//but not in the MT array
		for(unsigned i = 0 ; i < m_SRAM_missing_tags[id_set].size() ; i++)
		{
			if(m_SRAM_missing_tags[id_set][i]->addr == block_addr && m_SRAM_missing_tags[id_set][i]->isValid)
			{
				////DPRINTF("BasePredictor::checkMissingTags Found SRAM error as %#lx is present in MT tag %i \n", block_addr ,i);  
				return true;
			}
		}	
	}
	return false;
}

void
Predictor::updateCachePressure()
{
	if(!m_trackError || !simu_parameters.enableDatasetSpilling)
		return;
		
	if( (cpt_time - stats_beginTimeFrame) > simu_parameters.MT_timeframe)
	{
		for(int i = 0 ; i < m_nb_set ; i++)
		{
			m_isNVMbusy[i] = (m_NVM_MT_counters[i] > simu_parameters.MT_counter_th) ? true : false;		
			m_isSRAMbusy[i] = (m_SRAM_MT_counters[i] > simu_parameters.MT_counter_th) ? true : false;
		
			m_NVM_MT_counters[i] = 0;
			m_SRAM_MT_counters[i] = 0;
		}

		if(simu_parameters.printDebug)
		{
			//stats_SRAMpressure.push_back(m_isSRAMbusy);
		}

		stats_beginTimeFrame = cpt_time;	
	}
}

void
Predictor::openNewTimeFrame()
{
	if(m_isWarmup)
		return;
		
	//DPRINTF("BasePredictor::openNewTimeFrame New Time Frame Start\n");  
	stats_NVM_errors.push_back(0);
	stats_SRAM_errors.push_back(0);
	stats_BP_errors.push_back(0);
	stats_MigrationErrors.push_back(0);
	stats_nbLLCaccessPerFrame = 0;

}

void
Predictor::migrationRecording()
{

	if(!m_isWarmup)
	{
		//stats_NVM_errors[stats_NVM_errors.size()-1]++;
		stats_MigrationErrors[stats_MigrationErrors.size()-1]++;	
	}
}


void
Predictor::updatePolicy(uint64_t set, uint64_t index, bool inNVM, Access element , bool isWBrequest = false)
{	
	updateCachePressure();
	
	uint64_t block_addr = bitRemove(element.m_address , 0 , log2(BLOCK_SIZE));
	updateFUcaches(block_addr , inNVM);

	if(m_trackError)
	{
		CacheEntry* current = inNVM ? m_tableNVM[set][index] : m_tableSRAM[set][index];
		updateBypassTag( current , set , inNVM);
	}

	if(!m_isWarmup)
	{	
		stats_nbLLCaccessPerFrame++;
		//An error in the NVM side is when handling a write 
		if(inNVM && element.isWrite()){
			stats_NVM_errors[stats_NVM_errors.size()-1]++;

			if(isWBrequest)
				stats_WBerrors++;
			else
				stats_COREerrors++;
		}
	}
}

void
Predictor::printBParray(int set , ofstream& out)
{
	for(unsigned i = 0 ; i < BP_missing_tags[set].size() ; i++)
	{
		if(BP_missing_tags[set][i]->isValid)
		{
			string alloc_site = BP_missing_tags[set][i]->allocSite ? "NVM" : "SRAM";
			out << "Index " << i << " - Addr = " << std::hex << BP_missing_tags[set][i]->addr \
			<< std::dec << " - Time = " << BP_missing_tags[set][i]->last_time_touched << " - Allocation = " << alloc_site << endl;		
		}
	}
	
}


void
Predictor::update_globalPChistory(uint64_t pc)
{	
 	m_global_PChistory.push_front(pc);
	if( m_global_PChistory.size() == 11)
		m_global_PChistory.pop_back();
}


bool 
Predictor::isHitInBPtags(uint64_t block_addr , int set)
{
	for(unsigned i = 0 ; i < BP_missing_tags[set].size(); i++)
	{
		if( block_addr == BP_missing_tags[set][i]->addr && BP_missing_tags[set][i]->isValid)
			return true;
	}
	return false;
}

uint64_t 
Predictor::getMissPCfromBPtags(uint64_t block_addr , int set)
{
	for(unsigned i = 0 ; i < BP_missing_tags[set].size(); i++)
	{
		if( block_addr == BP_missing_tags[set][i]->addr && BP_missing_tags[set][i]->isValid)
			return BP_missing_tags[set][i]->missPC;
	}
	assert(false);
	return 0;
}

bool 
Predictor::getAllocSitefromBPtags( uint64_t addr , int set)
{
	for(unsigned i = 0 ; i < BP_missing_tags[set].size(); i++)
	{
		if( addr == BP_missing_tags[set][i]->addr && BP_missing_tags[set][i]->isValid)
			return BP_missing_tags[set][i]->allocSite;
	}
	assert(false);
	return false;
}



bool
Predictor::getHitPerDataArray(uint64_t block_addr, int set , bool inNVM)
{
	uint64_t ref_time = 0;
	bool find = false;
	for(unsigned i = 0 ; i < BP_missing_tags[set].size() && !find; i++)
	{
		if( block_addr == BP_missing_tags[set][i]->addr && BP_missing_tags[set][i]->isValid)
		{
			ref_time = BP_missing_tags[set][i]->last_time_touched;
			find = true;		
		}
	}
	
	if(!find)
		return false;
	
	/*string str_inNVM = inNVM ? "TRUE" : "FALSE";
	cout << "*** Call getHitPerDataArray with inNVM =" << str_inNVM << endl;
	cout << "Block is " << std::hex << block_addr << ", ref_time = " << ref_time << endl;
	for(unsigned i = 0 ; i < BP_missing_tags[set].size() ; i++)
	{
		if(BP_missing_tags[set][i]->isValid)
		{
			string alloc_site = BP_missing_tags[set][i]->allocSite ? "NVM" : "SRAM";
			cout << "Index " << i << " - Addr = " << std::hex << BP_missing_tags[set][i]->addr \
			<< std::dec << " - Time = " << BP_missing_tags[set][i]->last_time_touched << " - Allocation = " << alloc_site << endl;		
		}
	}
	*/
	int pos = 0;
	
	for(unsigned i = 0 ; i < BP_missing_tags[set].size() ; i++)
	{
		if( BP_missing_tags[set][i]->isValid && \
			BP_missing_tags[set][i]->allocSite == inNVM && \
			ref_time < BP_missing_tags[set][i]->last_time_touched)
		{
			pos++;
		}
	}

	//cout << "pos = " << pos << endl;
	if(inNVM)
		return pos < simu_parameters.nvm_assoc ? true : false;
	else
		return pos < simu_parameters.sram_assoc ? true : false;
}

void 
Predictor::printConfig(std::ostream& out, std::string entete)
{
//	out << entete << ":Predictor" << endl;
	out << entete << ":Predictor:SizeSRAM_MTarray:\t" << m_SRAMassoc_MT_size << endl;	
	out << entete << ":Predictor:SizeNVM_MTarray:\t" << m_NVMassoc_MT_size << endl;	
	out << entete << ":Predictor:MT_counter_th:\t" << simu_parameters.MT_counter_th << endl;	
	out << entete << ":Predictor:MT_timeframe:\t" << simu_parameters.MT_timeframe << endl;	
	out << entete << ":Predictor:MT_sampling_sets:\t" << simu_parameters.nb_MTcouters_sampling << endl;	

//	else
//		out << entete << ":Predictor:TrackingEnabled\tFALSE" << endl;

	out << entete << ":Predictor:SampledSets\t" << simu_parameters.nb_sampled_sets << endl;
	out << entete << ":Predictor:MediumRDdef\t" << simu_parameters.mediumrd_def << endl;
}


					 
void 
Predictor::printStats(std::ostream& out, string entete)
{

	uint64_t totalNVMerrors = 0, totalSRAMerrors= 0, totalBPerrors = 0, totalMigration = 0;	
	
	if(simu_parameters.printDebug && simu_parameters.enableDatasetSpilling)
	{	/*
		ofstream output_file;
		output_file.open(PREDICTOR_OUTPUT_FILE);	
		for(int i = 0 ; i < m_nb_set ; i++)
		{
			output_file << "Set " << i << "\t";
			for(unsigned j = 0 ; j < stats_SRAMpressure.size() ; j++)
			{
				string a  = stats_SRAMpressure[j][i] ? "High" : "Low";
				output_file << a << ",";
			}
			output_file << endl;
		}
		output_file.close();*/
	}

	for(unsigned i = 0 ; i <  stats_NVM_errors.size(); i++)
	{	
		totalNVMerrors += stats_NVM_errors[i];
		totalSRAMerrors += stats_SRAM_errors[i];
		totalBPerrors += stats_BP_errors[i];
		totalMigration += stats_MigrationErrors[i];
	}
	
	out << entete << ":Predictor:TotalMiss:\t" << stats_total_miss << endl;
	
	if(simu_parameters.simulate_conflicts)
	{
		out << entete << ":Predictor:NVMConflictMiss:\t" << stats_nvm_conflict_miss << endl;
		out << entete << ":Predictor:SRAMConflictMiss:\t" << stats_sram_conflict_miss << endl;
		out << entete << ":Predictor:ColdMiss:\t" << stats_cold_miss << endl;
		out << entete << ":Predictor:CapacityMiss:\t" << stats_capacity_miss << endl;	
	}


	out << entete << ":Predictor:PredictorErrors:" <<endl;
	out << entete << ":Predictor:TotalNVMError\t" << totalNVMerrors << endl;
	out << entete << ":Predictor:NVMError:FromWB\t"  << stats_WBerrors << endl;
	out << entete << ":Predictor:NVMError:FromCore\t" <<  stats_COREerrors << endl;
	out << entete << ":Predictor:TotalMigration\t" <<  totalMigration << endl;
	out << entete << ":Predictor:SRAMError\t" << totalSRAMerrors << endl;
	out << entete << ":Predictor:BPError\t" << totalBPerrors << endl;
}


LRUPredictor::LRUPredictor(int id, int nbAssoc , int nbSet, int nbNVMways, DataArray& SRAMtable, DataArray& NVMtable, HybridCache* cache)\
 : Predictor(id, nbAssoc, nbSet, nbNVMways, SRAMtable, NVMtable, cache)
{
	/* With LRU policy, the cache is not hybrid, only NVM or only SRAM */ 
//	assert(m_nbNVMways == 0 || m_nbSRAMways == 0);
	
	m_cpt = 1;
}


allocDecision
LRUPredictor::allocateInNVM(uint64_t set, Access element)
{
	// Always allocate on the same data array 
	
	if(m_nbNVMways == 0)
		return ALLOCATE_IN_SRAM;
	else 
		return ALLOCATE_IN_NVM;
}

void
LRUPredictor::updatePolicy(uint64_t set, uint64_t index, bool inNVM, Access element , bool isWBrequest = false)
{
	if(inNVM)
		m_replacementPolicyNVM_ptr->updatePolicy(set, index , element);
	else
		m_replacementPolicySRAM_ptr->updatePolicy(set, index , element);

	Predictor::updatePolicy(set , index , inNVM , element);
	m_cpt++;
}

void
LRUPredictor::insertionPolicy(uint64_t set, uint64_t index, bool inNVM, Access element)
{	
	if(inNVM)
		m_replacementPolicyNVM_ptr->insertionPolicy(set, index , element);
	else
		m_replacementPolicySRAM_ptr->insertionPolicy(set, index , element);
	
	
	Predictor::insertionPolicy(set, index , inNVM , element);
	m_cpt++;
}

int
LRUPredictor::evictPolicy(int set, bool inNVM)
{
	int assoc_victim;
	
	if(inNVM)
		assoc_victim = m_replacementPolicyNVM_ptr->evictPolicy(set);
	else
		assoc_victim =  m_replacementPolicySRAM_ptr->evictPolicy(set);
	
	evictRecording(set , assoc_victim , inNVM);
	return assoc_victim;
}


allocDecision
PreemptivePredictor::allocateInNVM(uint64_t set, Access element)
{

	if( m_nbSRAMways == 0 )
		return ALLOCATE_IN_NVM;
	else if(m_nbNVMways == 0)
		return ALLOCATE_IN_SRAM;


	if(element.isWrite())
		return ALLOCATE_IN_SRAM;
	else
		return ALLOCATE_IN_NVM;
}


/********************************************/ 
