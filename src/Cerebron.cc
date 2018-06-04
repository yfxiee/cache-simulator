#include <cmath>
#include <vector>
#include <map>
#include <ostream>

#include "Cerebron.hh"

using namespace std;


deque<uint64_t> CerebronPredictor::m_global_PChistory;
deque<uint64_t> CerebronPredictor::m_callee_PChistory;


CerebronPredictor::CerebronPredictor(int nbAssoc , int nbSet, int nbNVMways, DataArray& SRAMtable, DataArray& NVMtable, HybridCache* cache) :\
	Predictor(nbAssoc , nbSet , nbNVMways , SRAMtable , NVMtable , cache)
{	
	m_tableSize = simu_parameters.perceptron_table_size;

	m_features.clear();

	m_criterias_names = simu_parameters.PHC_features;

	m_need_callee_file = false;
	
	m_enableAllocation = true;
	if(simu_parameters.nvm_assoc == 0 || simu_parameters.sram_assoc == 0)
		m_enableAllocation = false;
	
	initFeatures();
	
	cout <<  "\tFeatures used : ";
	for(auto p : simu_parameters.PHC_features)
		cout << p << ",";
	cout << endl;


	
	if(m_need_callee_file)
	{
		m_callee_map = map<uint64_t,int>();
		load_callee_file();
	}
		
	m_costAccess = vector< vector<int> >(NUM_RW_TYPE , vector<int>(NUM_RD_TYPE, 0));
	m_costAccess[true][RD_MEDIUM] = simu_parameters.PHC_cost_mediumWrite;
	m_costAccess[false][RD_MEDIUM] = simu_parameters.PHC_cost_mediumRead;
	m_costAccess[true][RD_SHORT] = simu_parameters.PHC_cost_shortWrite;
	m_costAccess[false][RD_SHORT] = simu_parameters.PHC_cost_shortRead;
		
	
	miss_counter = 0;
	m_cpt = 0;

	m_global_PChistory = deque<uint64_t>();
	m_callee_PChistory = deque<uint64_t>();

	stats_nbBPrequests = vector<uint64_t>(1, 0);
	stats_nbDeadLine = vector<uint64_t>(1, 0);
	stats_missCounter = vector<int>();
	stats_access_class = vector<vector<int> >(NUM_RD_TYPE ,  vector<int>(2, 0));
	
	
	if(simu_parameters.perceptron_drawFeatureMaps)
	{
		stats_local_error = vector< vector<int> >(simu_parameters.PHC_features.size() , vector<int>(m_tableSize, 0));
		stats_global_error = vector< vector<int> >(m_tableSize , vector<int>(m_tableSize , 0 ));
	}
	
	if(simu_parameters.perceptron_compute_variance)
	{
		stats_variances_buffer = vector<double>();
		stats_variances = vector<double>();
	}	

	stats_allocate = 0;
	stats_nbMiss = 0;
	stats_nbHits = 0;

	stats_update_learning = 0, stats_update = 0;
	stats_allocate = 0, stats_allocate_learning = 0;
	stats_prediction_preemptive = 0 , stats_prediction_confident = 0;
	stats_cptLearningSRAMerror = 0, stats_cptSRAMerror = 0;

}


CerebronPredictor::~CerebronPredictor()
{
	for(auto feature : m_features)
		delete feature;
}

void
CerebronPredictor::initFeatures()
{
	vector<string>* criterias_name = &m_criterias_names;
	vector<hashing_function>* features_hash = &m_features_hash;
	vector<FeatureTable*>* features = &m_features;		

	for(auto feature : *criterias_name)
	{
		features->push_back( new FeatureTable(m_tableSize, feature, false));
		if(feature == "MissPC_LSB")
			features_hash->push_back(hashingMissPC_LSB);
		else if(feature == "MissPC_LSB1")
			features_hash->push_back(hashingMissPC_LSB1);
		else if(feature == "MissPC_MSB")
			features_hash->push_back(hashingMissPC_MSB);		

		else if(feature == "TagBlock")
			features_hash->push_back(hashingTag_block);
		else if(feature == "TagPage")
			features_hash->push_back(hashingTag_page);
	
		else if(feature == "MissCounter")
			features_hash->push_back(hashing_MissCounter);	
		else if(feature == "MissCounter1")
			features_hash->push_back(hashing_MissCounter1);
		
		else if( feature == "CallStack")
			features_hash->push_back(hashing_CallStack);
		else if( feature == "CallStack1")	
			features_hash->push_back(hashing_CallStack1);
			
		else if(feature == "currentPC")
			features_hash->push_back(hashing_currentPC);
		else if(feature == "currentPC1")
			features_hash->push_back(hashing_currentPC1);
		else if(feature == "currentPC_3")
			features_hash->push_back(hashingcurrentPC_3);
		else if(feature == "currentPC_2")
			features_hash->push_back(hashingcurrentPC_2);
		else if(feature == "currentPC_1")
			features_hash->push_back(hashingcurrentPC_1);
		else
			assert(false && "Error while initializing the features , wrong name\n");


		if(feature == "CallStack" || feature == "CallStack1")
			m_need_callee_file = true;
	}	
}


allocDecision
CerebronPredictor::allocateInNVM(uint64_t set, Access element)
{
//	if(element.isInstFetch())
//		return ALLOCATE_IN_NVM;
	
	update_globalPChistory(element.m_pc);
	stats_nbMiss++;
	miss_counter++;
	if(miss_counter > 255)
		miss_counter = 255;
	
	// All the set is a learning/sampled set independantly of its way or SRAM/NVM alloc
	//bool isLearning = m_tableSRAM[set][0]->isLearning; 

	int sum_pred = 0;
	for(unsigned i = 0 ; i < m_features.size() ; i++)
	{
		int hash = m_features_hash[i](element.block_addr , element.m_pc);
		int confidence = m_features[i]->getConfidence(hash, true);
		allocDecision des = convertToAllocDecision(m_features[i]->getAllocationPrediction(hash, true) , element.isWrite() );
		
		if( des == ALLOCATE_IN_SRAM )
			sum_pred += (-1) * confidence;
		else
			sum_pred += confidence;
	}
	return convertToAllocDecision(sum_pred, element.isWrite());
}

void
CerebronPredictor::updatePolicy(uint64_t set, uint64_t index, bool inNVM, Access element, bool isWBrequest)
{
	update_globalPChistory(element.m_pc);
	
	stats_update++;
	
	CacheEntry *entry = get_entry(set , index , inNVM);
	entry->policyInfo = m_cpt++;

	RD_TYPE rd_type = classifyRD( set , index , inNVM );
	stats_access_class[rd_type][element.isWrite()]++;


	if(entry->isLearning)
	{
		entry->cost_value += m_costAccess[element.isWrite()][rd_type];
//		cout << "UpdatePolicy Learning Cache line 0x" << std::hex << element.block_addr
//			<< std::dec << " Cost Value " << entry->cost_value << endl;

	}

	stats_nbHits++;
	miss_counter--;
	if(miss_counter < 0)
		miss_counter = 0;

	Predictor::updatePolicy(set , index , inNVM , element , isWBrequest);
}

void
CerebronPredictor::insertionPolicy(uint64_t set, uint64_t index, bool inNVM, Access element)
{
	CacheEntry* entry = get_entry(set , index , inNVM);
	entry->policyInfo = m_cpt++;

	stats_allocate++;
	RD_TYPE rd = RD_NOT_ACCURATE;
	if( element.isSRAMerror)
	{
		stats_cptSRAMerror++;
		rd = RD_MEDIUM;	
	}
	stats_access_class[rd][element.isWrite()]++;

	/** Training learning cache lines */ 
//	if(entry->isLearning)
//	{
	
//		cout << "Insertion Learning Cache line 0x" << std::hex << element.block_addr << endl;
//		if( element.isSRAMerror)
//		{
//			entry->cost_value = missingTagCostValue(element.block_addr , set) +  m_costAccess[element.isWrite()][RD_MEDIUM];
//			stats_cptLearningSRAMerror++;		
//		}
//		else 
//		{
	for(unsigned i = 0 ; i < m_features.size() ; i++)
	{
		int hash = m_features_hash[i](element.block_addr , element.m_pc);
		int pred = m_features[i]->getAllocationPrediction(hash, true);		
		entry->PHC_allocation_pred[i] = convertToAllocDecision(pred, element.isWrite() );
	}
	entry->cost_value = m_costAccess[element.isWrite()][RD_SHORT];
//		}
//	}
			
	Predictor::insertionPolicy(set , index , inNVM , element);
}


allocDecision
CerebronPredictor::convertToAllocDecision(int alloc_counter, bool isWrite)
{

	if( abs(alloc_counter) < simu_parameters.perceptron_allocation_threshold )
	{
		if( isWrite )
			return ALLOCATE_IN_SRAM;
		else 
			return ALLOCATE_IN_NVM;
	}
	else
	{
		if( alloc_counter < simu_parameters.perceptron_allocation_threshold )
			return ALLOCATE_IN_SRAM;
		else
			return ALLOCATE_IN_NVM;
	}
}

void
CerebronPredictor::update_globalPChistory(uint64_t pc)
{	
	m_global_PChistory.push_front(pc);
	if( m_global_PChistory.size() == 11)
		m_global_PChistory.pop_back();
	
	if(!m_need_callee_file)
		return;
	
	if(m_callee_map.count(pc) != 0)
	{
		m_callee_PChistory.push_front(pc);
		if( m_callee_PChistory.size() == 11)
			m_callee_PChistory.pop_back();
	}
}


int CerebronPredictor::evictPolicy(int set, bool inNVM)
{
	int assoc_victim = -1;
	assert(m_replacementPolicyNVM_ptr != NULL);
	assert(m_replacementPolicySRAM_ptr != NULL);

	CacheEntry* entry = NULL;
	if(inNVM)
	{	
		assoc_victim = m_replacementPolicyNVM_ptr->evictPolicy(set);
		entry = m_tableNVM[set][assoc_victim];
	}
	else
	{
		assoc_victim = m_replacementPolicySRAM_ptr->evictPolicy(set);
		entry = m_tableSRAM[set][assoc_victim];		
	}

	if(entry->isValid)
	{
	
		if(simu_parameters.perceptron_drawFeatureMaps)
		{
			bool global_error = false;
			allocDecision des = entry->cost_value > simu_parameters.PHC_cost_threshold ? ALLOCATE_IN_SRAM : ALLOCATE_IN_NVM;
			
			if(( des == ALLOCATE_IN_SRAM && inNVM ) || \
				( des == ALLOCATE_IN_NVM && !inNVM ))
				global_error = true;
			if(global_error)
			{
				int hash1 = m_features_hash[0](entry->address , entry->m_pc);
				int hash2 = (m_features.size() > 1) ? m_features_hash[1](entry->address , entry->m_pc) : 0;
				stats_global_error[hash1][hash2]++;
			}
		
			for(unsigned i = 0; i < m_features.size() ; i++)
			{
				int hash = m_features_hash[i](entry->address , entry->m_pc);

				bool local_error = false;
				if( des == ALLOCATE_IN_NVM && entry->PHC_allocation_pred[i] == ALLOCATE_IN_NVM)
					local_error = true;
				else if(  des == ALLOCATE_IN_SRAM && entry->PHC_allocation_pred[i] == ALLOCATE_IN_SRAM )
				 	local_error = true;
				 					
				if(local_error)	
					stats_local_error[i][hash]++;
			}

		}		

		if( entry->isLearning)
		{
			for(unsigned i = 0; i < m_features.size() ; i++)
			{
				int hash = m_features_hash[i](entry->address , entry->m_pc);

				allocDecision des =  entry->cost_value > simu_parameters.PHC_cost_threshold ? ALLOCATE_IN_SRAM : ALLOCATE_IN_NVM;
				if( (entry->PHC_allocation_pred[i] == ALLOCATE_IN_NVM && des == ALLOCATE_IN_NVM) || \
					(entry->PHC_allocation_pred[i] == ALLOCATE_IN_SRAM && des == ALLOCATE_IN_SRAM))
				{
					m_features[i]->increaseConfidence(hash);
					
				}
				else if( (entry->PHC_allocation_pred[i] == ALLOCATE_IN_NVM && des == ALLOCATE_IN_SRAM) || \
					(entry->PHC_allocation_pred[i] == ALLOCATE_IN_SRAM && des == ALLOCATE_IN_NVM))
				{
					m_features[i]->decreaseConfidence(hash);
				}
			}

		

			if(entry->cost_value > simu_parameters.PHC_cost_threshold)
			{
				for(unsigned i = 0; i < m_features.size() ; i++)
				{
					int hash = m_features_hash[i](entry->address , entry->m_pc);
					m_features[i]->decreaseAlloc(hash);
				}
			}			
			else
			{
				for(unsigned i = 0; i < m_features.size() ; i++)
				{
					int hash = m_features_hash[i](entry->address , entry->m_pc);
					m_features[i]->increaseAlloc(hash);
				}
			}
	

			if(entry->nbWrite == 0 && entry->nbRead == 0)
				stats_nbDeadLine[stats_nbDeadLine.size()-1]++;
		}
	}
	
	evictRecording(set , assoc_victim , inNVM);	
	return assoc_victim;
}

void
CerebronPredictor::evictRecording( int id_set , int id_assoc , bool inNVM)
{ 
	Predictor::evictRecording(id_set, id_assoc, inNVM);
}


void 
CerebronPredictor::printStats(std::ostream& out, std::string entete) { 

	uint64_t total_BP = 0, total_DeadLines = 0;
	for(unsigned i = 0 ; i < stats_nbBPrequests.size() ; i++ )
	{
		total_BP+= stats_nbBPrequests[i];		
		total_DeadLines += stats_nbDeadLine[i];
	}
	/*
	ofstream file(FILE_OUTPUT_Cerebron);	
	for(unsigned i = 0 ; i < stats_histo_value.size() ; i++)
	{
		file << "Feature : " <<  m_criterias_names[i] << endl;
		for(unsigned j = 0 ; j < stats_histo_value[i].size() ; j++)
		{
			file << "Index " << j << "\t";
			for(auto cost : stats_histo_value[i][j])
				file << cost << ", ";
			file << endl;
		}
		
		
		file << "-------------" << endl;
		file << endl;
	}
	
	file.close();
	*/
	out << entete << ":CerebronPredictor:PredictionPreemptive\t" << stats_prediction_preemptive << endl;
	out << entete << ":CerebronPredictor:PredictionConfident\t" << stats_prediction_confident << endl;
	out << entete << ":CerebronPredictor:NBBypass\t" << total_BP << endl;
	out << entete << ":CerebronPredictor:TotalDeadLines\t" << total_DeadLines << endl;
	out << entete << ":CerebronPredictor:SRAMerror\t" << stats_cptSRAMerror << endl;
	out << entete << ":CerebronPredictor:LearningSRAMerror\t" << stats_cptLearningSRAMerror << endl;

	
	out << entete << ":CerebronPredictor:AccessClassification" << endl;
	for(unsigned i = 0 ; i < stats_access_class.size() ; i++)
	{
		if(stats_access_class[i][0] != 0 || stats_access_class[i][1] != 1)
		{
			out << entete << ":CerebronPredictor:Read" <<  str_RD_status[i]	<< "\t" << stats_access_class[i][false] << endl;	
			out << entete << ":CerebronPredictor:Write" << str_RD_status[i] << "\t" << stats_access_class[i][true] << endl; 		
		}
	}

	Predictor::printStats(out, entete);
}



void CerebronPredictor::printConfig(std::ostream& out, std::string entete) { 

	out << entete << ":Cerebron:TableSize\t" << m_tableSize << endl; 
	
	string a = m_enableAllocation ? "TRUE" : "FALSE";
	out << entete << ":Cerebron:EnableAllocation\t" << a << endl;
	
	out << entete << ":Cerebron:CostValueThreshold\t" << simu_parameters.PHC_cost_threshold  << endl;
	out << entete << ":Cerebron:AllocationThreshold\t" << simu_parameters.perceptron_allocation_threshold  << endl;
//	out << entete << ":Cerebron:CostLearning\t" << simu_parameters.perceptron_allocation_learning << endl; 
	out << entete << ":Cerebron:AllocFeatures\t"; 
	for(auto p : m_criterias_names)
		out << p << ",";
	out << endl;	

	out << entete << ":CerebronPredictor:CostModel" << endl;
	out << entete << ":CerebronPredictor:ShortRead\t" << m_costAccess[false][RD_SHORT] << endl; 		
	out << entete << ":CerebronPredictor:ShortWrite\t" << m_costAccess[true][RD_SHORT] << endl; 		
	out << entete << ":CerebronPredictor:MediumRead\t" << m_costAccess[false][RD_MEDIUM] << endl; 		
	out << entete << ":CerebronPredictor:MediumWrite\t" << m_costAccess[true][RD_MEDIUM] << endl; 		


	Predictor::printConfig(out, entete);
}

CacheEntry*
CerebronPredictor::get_entry(uint64_t set , uint64_t index , bool inNVM)
{
	if(inNVM)
		return m_tableNVM[set][index];
	else
		return m_tableSRAM[set][index];
}

void
CerebronPredictor::openNewTimeFrame()
{
	stats_nbBPrequests.push_back(0);
	stats_nbDeadLine.push_back(0);
	
	stats_missCounter.push_back( miss_counter );
	stats_nbHits = 0, stats_nbMiss = 0;
	
	if(simu_parameters.perceptron_compute_variance)
	{
		if(stats_variances_buffer.size() != 0)
		{
			double avg = 0;
			for(unsigned i = 0 ; i < stats_variances_buffer.size() ; i++)
				avg += stats_variances_buffer[i];
			avg /= (double)stats_variances_buffer.size();

			stats_variances.push_back(avg);	
			stats_variances_buffer.clear();
		}
	}

	for(auto feature : m_features)
		feature->openNewTimeFrame();
	
	
	Predictor::openNewTimeFrame();
}

void 
CerebronPredictor::finishSimu()
{
	if(simu_parameters.perceptron_drawFeatureMaps)
	{
		drawFeatureMaps();
	}
	
	for(auto feature : m_features)
		feature->finishSimu();

	Predictor::finishSimu();
}


void
CerebronPredictor::drawFeatureMaps()
{

	ofstream file("output_PHC.out");
	for(unsigned i = 0 ; i < stats_global_error.size() ; i++)
	{
		for(unsigned j =  0 ; j < stats_global_error[i].size() ; j++)
		{
			file << stats_global_error[i][j] << ",";
		}
		file << endl;
	}
	file.close();

	ofstream file1("output_PHC1.out");
	for(unsigned i = 0 ; i < stats_local_error.size() ; i++)
	{
		for(unsigned j =  0 ; j < stats_local_error[i].size() ; j++)
		{
			file1 << stats_local_error[i][j] << ",";
		}
		file1 << endl;
	}
	file1.close();

}

void 
CerebronPredictor::load_callee_file()
{

	string mem_trace = simu_parameters.memory_traces[0];
	string dir_name = splitFilename(mem_trace);
	string callee_filename = dir_name + "/callee_recap.txt"; 
	
	ifstream file(callee_filename);
	
	assert(file && "Callee file not found\n");
	string line;
	
	while( getline(file, line))
	{		
		m_callee_map.insert(pair<uint64_t,int>( hexToInt(line) , 0));		
	};
		
	file.close();	
}



RD_TYPE
CerebronPredictor::classifyRD(int set , int index, bool inNVM)
{
	vector<CacheEntry*> line;
	int64_t ref_rd = 0;
	if(inNVM)
	{
		ref_rd = m_tableNVM[set][index]->policyInfo;
		line = m_tableNVM[set];
	}
	else
	{
		line = m_tableSRAM[set];
		ref_rd = m_tableSRAM[set][index]->policyInfo;
	}
	
	int position = 0;
	/* Determine the position of the cache line in the LRU stack */
	for(unsigned i = 0 ; i < line.size() ; i ++)
	{
		if(line[i]->policyInfo < ref_rd && line[i]->isValid)
			position++;
	}	

	if(simu_parameters.nvm_assoc > simu_parameters.sram_assoc)
	{
		if(position < simu_parameters.sram_assoc)
			return RD_SHORT;
		else
			return RD_MEDIUM;	
	}
	else
	{
		if(position < simu_parameters.nvm_assoc)
			return RD_SHORT;
		else
			return RD_MEDIUM;
	}
}

