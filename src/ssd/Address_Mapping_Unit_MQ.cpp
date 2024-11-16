#include "Address_Mapping_Unit_Page_Level.h"
#include "Address_Mapping_Unit_MQ.h"
#include "../utils/Logical_Address_Partitioning_Unit.h"
#include <cmath>
#include "Log_Update_Interval.h"
#include "Flash_Block_Manager_MQ.h"



namespace SSD_Components{
    Address_Mapping_Unit_MQ* Address_Mapping_Unit_MQ::_my_instance = NULL;
    Address_Mapping_Unit_MQ::Address_Mapping_Unit_MQ(const sim_object_id_type &id, FTL *ftl, NVM_PHY_ONFI *flash_controller, Flash_Block_Manager_MQ *block_manager, bool ideal_mapping_table, unsigned int cmt_capacity_in_byte, unsigned int ConcurrentStreamNo, unsigned int ChannelCount, unsigned int chip_no_per_channel, unsigned int DieNoPerChip, unsigned int PlaneNoPerDie, std::vector<std::vector<flash_channel_ID_type>> stream_channel_ids, std::vector<std::vector<flash_chip_ID_type>> stream_chip_ids, std::vector<std::vector<flash_die_ID_type>> stream_die_ids, std::vector<std::vector<flash_plane_ID_type>> stream_plane_ids, unsigned int Block_no_per_plane, unsigned int Page_no_per_block, unsigned int SectorsPerPage, unsigned int PageSizeInBytes)
    : Sim_Object(id), ftl(ftl), flash_controller(flash_controller), block_manager(block_manager),
		ideal_mapping_table(ideal_mapping_table), no_of_input_streams(no_of_input_streams),
		channel_count(ChannelCount), chip_no_per_channel(chip_no_per_channel), die_no_per_chip(DieNoPerChip), plane_no_per_die(PlaneNoPerDie),
		block_no_per_plane(Block_no_per_plane), pages_no_per_block(Page_no_per_block), sector_no_per_page(SectorsPerPage)
	{
		page_no_per_plane = pages_no_per_block * block_no_per_plane;
		page_no_per_die = page_no_per_plane * plane_no_per_die;
		page_no_per_chip = page_no_per_die * die_no_per_chip;
		page_no_per_channel = page_no_per_chip * chip_no_per_channel;
		uint32_t total_physical_pages_no = page_no_per_channel * ChannelCount;

        _my_instance = this;
        domains = new AddressMappingDomain*[ConcurrentStreamNo];

		lui = new Log_Update_Interval(page_no_per_channel * channel_count / pages_no_per_block, pages_no_per_block);

        flash_channel_ID_type* channel_ids = NULL;
		flash_channel_ID_type* chip_ids = NULL;
		flash_channel_ID_type* die_ids = NULL;
		flash_channel_ID_type* plane_ids = NULL;
        for (unsigned int domainID = 0; domainID < ConcurrentStreamNo; domainID++) {
			/* Since we want to have the same mapping table entry size for all streams, the entry size
			*  is calculated at this level and then pass it to the constructors of mapping domains
			* entry size = sizeOf(lpa) + sizeOf(ppn) + sizeOf(bit vector that shows written sectors of a page)
			*/
			CMT_entry_size = (unsigned int)std::ceil(((2 * std::log2(total_physical_pages_no)) + sector_no_per_page) / 8);
			//In GTD we do not need to store lpa
			GTD_entry_size = (unsigned int)std::ceil((std::log2(total_physical_pages_no) + sector_no_per_page) / 8);
			no_of_translation_entries_per_page = (SectorsPerPage * SECTOR_SIZE_IN_BYTE) / GTD_entry_size;

			Cached_Mapping_Table* sharedCMT = NULL;
			unsigned int per_stream_cmt_capacity = 0;
			uint32_t cmt_capacity = cmt_capacity_in_byte / CMT_entry_size;
			per_stream_cmt_capacity = cmt_capacity;
			sharedCMT = new Cached_Mapping_Table(cmt_capacity);


			channel_ids = new flash_channel_ID_type[stream_channel_ids[domainID].size()];
			for (unsigned int i = 0; i < stream_channel_ids[domainID].size(); i++) {
				if (stream_channel_ids[domainID][i] < channel_count) {
					channel_ids[i] = stream_channel_ids[domainID][i];
				} else {
					PRINT_ERROR("Invalid channel ID specified for I/O flow " << domainID);
				}
			}

			chip_ids = new flash_channel_ID_type[stream_chip_ids[domainID].size()];
			for (unsigned int i = 0; i < stream_chip_ids[domainID].size(); i++) {
				if (stream_chip_ids[domainID][i] < chip_no_per_channel) {
					chip_ids[i] = stream_chip_ids[domainID][i];
				} else {
					PRINT_ERROR("Invalid chip ID specified for I/O flow " << domainID);
				}
			}

			die_ids = new flash_channel_ID_type[stream_die_ids[domainID].size()];
			for (unsigned int i = 0; i < stream_die_ids[domainID].size(); i++) {
				if (stream_die_ids[domainID][i] < die_no_per_chip) {
					die_ids[i] = stream_die_ids[domainID][i];
				} else {
					PRINT_ERROR("Invalid die ID specified for I/O flow " << domainID);
				}
			}

			plane_ids = new flash_channel_ID_type[stream_plane_ids[domainID].size()];
			for (unsigned int i = 0; i < stream_plane_ids[domainID].size(); i++) {
				if (stream_plane_ids[domainID][i] < plane_no_per_die) {
					plane_ids[i] = stream_plane_ids[domainID][i];
				} else {
					PRINT_ERROR("Invalid plane ID specified for I/O flow " << domainID);
				}
			}

			domains[domainID] = new AddressMappingDomain(per_stream_cmt_capacity, CMT_entry_size, no_of_translation_entries_per_page,
				sharedCMT,
				Flash_Plane_Allocation_Scheme_Type::CDPW,
				channel_ids, (unsigned int)(stream_channel_ids[domainID].size()), chip_ids, (unsigned int)(stream_chip_ids[domainID].size()), die_ids, 
				(unsigned int)(stream_die_ids[domainID].size()), plane_ids, (unsigned int)(stream_plane_ids[domainID].size()),
				Utils::Logical_Address_Partitioning_Unit::PDA_count_allocate_to_flow(domainID), Utils::Logical_Address_Partitioning_Unit::LHA_count_allocate_to_flow_from_device_view(domainID),
				sector_no_per_page);
			delete[] channel_ids;
			delete[] chip_ids;
			delete[] die_ids;
			delete[] plane_ids;
		}
    }

    Address_Mapping_Unit_MQ::~Address_Mapping_Unit_MQ()
    {
        for (unsigned int i = 0; i < no_of_input_streams; i++) {
			delete domains[i];
		}
		delete[] domains;
    }

    void Address_Mapping_Unit_MQ::Setup_triggers()
    {
		Sim_Object::Setup_triggers();
		flash_controller->ConnectToTransactionServicedSignal(handle_transaction_serviced_signal_from_PHY);
    }

	void Address_Mapping_Unit_MQ::Start_simulation()
	{
	}

	void Address_Mapping_Unit_MQ::Validate_simulation_config()
	{
	}

	void Address_Mapping_Unit_MQ::Execute_simulator_event(MQSimEngine::Sim_Event *)
	{
	}

    void Address_Mapping_Unit_MQ::Translate_lpa_to_ppa_and_dispatch(const std::list<NVM_Transaction *> &transactionList)
    {
        for (std::list<NVM_Transaction*>::const_iterator it = transactionList.begin();
			it != transactionList.end(); ) {
			if (is_lpa_locked_for_gc((*it)->Stream_id, ((NVM_Transaction_Flash*)(*it))->LPA)) {
				//iterator should be post-incremented since the iterator may be deleted from list
				manage_user_transaction_facing_barrier((NVM_Transaction_Flash*)*(it++));
			} else {
				query_cmt((NVM_Transaction_Flash*)(*it++));
			}
		}

		if (transactionList.size() > 0) {
			ftl->TSU->Prepare_for_transaction_submit();
			for (std::list<NVM_Transaction*>::const_iterator it = transactionList.begin();
				it != transactionList.end(); it++) {
				if (((NVM_Transaction_Flash*)(*it))->Physical_address_determined) {
					ftl->TSU->Submit_transaction(static_cast<NVM_Transaction_Flash*>(*it));
					if (((NVM_Transaction_Flash*)(*it))->Type == Transaction_Type::WRITE) {
						if (((NVM_Transaction_Flash_WR*)(*it))->RelatedRead != NULL) {
							ftl->TSU->Submit_transaction(((NVM_Transaction_Flash_WR*)(*it))->RelatedRead);
						}
					}
				}
			}
			
			ftl->TSU->Schedule();
		}
    }
    void Address_Mapping_Unit_MQ::Get_data_mapping_info_for_gc(const stream_id_type stream_id, const LPA_type lpa, PPA_type &ppa, page_status_type &page_state)
    {
		if (domains[stream_id]->Mapping_entry_accessible(ideal_mapping_table, stream_id, lpa)) {
			ppa = domains[stream_id]->Get_ppa(ideal_mapping_table, stream_id, lpa);
			page_state = domains[stream_id]->Get_page_status(ideal_mapping_table, stream_id, lpa);
		} else {
			ppa = domains[stream_id]->GlobalMappingTable[lpa].PPA;
			page_state = domains[stream_id]->GlobalMappingTable[lpa].WrittenStateBitmap;
		}
    }
    void Address_Mapping_Unit_MQ::Get_translation_mapping_info_for_gc(const stream_id_type stream_id, const MVPN_type mvpn, MPPN_type &mppa, sim_time_type &timestamp)
    {
		mppa = domains[stream_id]->GlobalTranslationDirectory[mvpn].MPPN;
		timestamp = domains[stream_id]->GlobalTranslationDirectory[mvpn].TimeStamp;
    }

    PPA_type Address_Mapping_Unit_MQ::Convert_address_to_ppa(const NVM::FlashMemory::Physical_Page_Address &pageAddress)
    {
        return (PPA_type)this->page_no_per_chip * (PPA_type)(pageAddress.ChannelID * this->chip_no_per_channel + pageAddress.ChipID)
			+ this->page_no_per_die * pageAddress.DieID + this->page_no_per_plane * pageAddress.PlaneID
			+ this->pages_no_per_block * pageAddress.BlockID + pageAddress.PageID;
    }
    void Address_Mapping_Unit_MQ::Set_barrier_for_accessing_physical_block(const Block_Type* block)
    {
		//The LPAs are actually not known until they are read one-by-one from flash storage. But, to reduce MQSim's complexity, we assume that LPAs are stored in DRAM and thus no read from flash storage is needed.
		NVM::FlashMemory::Physical_Page_Address pageAddr = *block->blockAddr;
		for (flash_page_ID_type pageID = 0; pageID < block->writeIdx; pageID++) {
			if (block_manager->isPageValid(block, pageID)) {
				pageAddr.PageID = pageID;
				if (block->Holds_mapping_data) {
					MVPN_type mpvn = (MVPN_type)flash_controller->Get_metadata(pageAddr.ChannelID, pageAddr.ChipID, pageAddr.DieID, pageAddr.PlaneID, pageAddr.BlockID, pageAddr.PageID);
					if (domains[block->Stream_id]->GlobalTranslationDirectory[mpvn].MPPN != Convert_address_to_ppa(pageAddr)) {
						PRINT_ERROR("Inconsistency in the global translation directory when locking an MPVN!")
					}
					Set_barrier_for_accessing_mvpn(block->Stream_id, mpvn);
				} else {
					LPA_type lpa = flash_controller->Get_metadata(pageAddr.ChannelID, pageAddr.ChipID, pageAddr.DieID, pageAddr.PlaneID, pageAddr.BlockID, pageAddr.PageID);
					LPA_type ppa = domains[block->Stream_id]->GlobalMappingTable[lpa].PPA;
					if (domains[block->Stream_id]->CMT->Exists(block->Stream_id, lpa)) {
						ppa = domains[block->Stream_id]->CMT->Retrieve_ppa(block->Stream_id, lpa);
					}
					if (ppa != Convert_address_to_ppa(pageAddr)) {
						PRINT_ERROR("Inconsistency in the global mapping table when locking an LPA!")
					}
					Set_barrier_for_accessing_lpa(block->Stream_id, lpa);
				}
			}
		}
    }
    Block_Type *Address_Mapping_Unit_MQ::selectVictimBlockCB(Block_Queue *queue)
    {
		lui_timestamp currentTimestamp = lui->getCurrentTimestamp();

		double lowestCost = pages_no_per_block;
		auto lowestCostItr = queue->blockList.begin();

		for(auto blockItr = queue->blockList.begin(); blockItr != queue->blockList.end(); blockItr++){
			lui_timestamp age = currentTimestamp - (*blockItr)->createTimestamp;
			double currentCost = (pages_no_per_block - (*blockItr)->invalid_page_count) / (age * (*blockItr)->invalid_page_count);
			if(currentCost < lowestCost){
				lowestCostItr = blockItr;
				lowestCost = currentCost;
			}
		}
		Block_Type* targetBlock = (*lowestCostItr);
		queue->blockList.erase(lowestCostItr);
		queue->currentBlockIdx--;
        return targetBlock;
    }
    void Address_Mapping_Unit_MQ::Set_barrier_for_accessing_lpa(const stream_id_type stream_id, const LPA_type lpa)
    {
		auto itr = domains[stream_id]->Locked_LPAs.find(lpa);
		if (itr != domains[stream_id]->Locked_LPAs.end()) {
			PRINT_ERROR("Illegal operation: Locking an LPA that has already been locked!");
		}
		domains[stream_id]->Locked_LPAs.insert(lpa);
    }
    void Address_Mapping_Unit_MQ::Set_barrier_for_accessing_mvpn(const stream_id_type stream_id, const MVPN_type mvpn)
    {
		auto itr = domains[stream_id]->Locked_MVPNs.find(mvpn);
		if (itr != domains[stream_id]->Locked_MVPNs.end()) {
			PRINT_ERROR("Illegal operation: Locking an MVPN that has already been locked!");
		}
		domains[stream_id]->Locked_MVPNs.insert(mvpn);
    }
    void Address_Mapping_Unit_MQ::Remove_barrier_for_accessing_lpa(const stream_id_type stream_id, const LPA_type lpa)
    {
		auto itr = domains[stream_id]->Locked_LPAs.find(lpa);
		if (itr == domains[stream_id]->Locked_LPAs.end()) {
			PRINT_ERROR("Illegal operation: Unlocking an LPA that has not been locked!");
		}
		domains[stream_id]->Locked_LPAs.erase(itr);

		//If there are read requests waiting behind the barrier, then MQSim assumes they can be serviced with the actual page data that is accessed during GC execution
		auto read_tr = domains[stream_id]->Read_transactions_behind_LPA_barrier.find(lpa);
		while (read_tr != domains[stream_id]->Read_transactions_behind_LPA_barrier.end()) {
			connected_transaction_serviced_signal_handler((*read_tr).second);
			delete (*read_tr).second;
			domains[stream_id]->Read_transactions_behind_LPA_barrier.erase(read_tr);
			read_tr = domains[stream_id]->Read_transactions_behind_LPA_barrier.find(lpa);
		}

		//If there are write requests waiting behind the barrier, then MQSim assumes they can be serviced with the actual page data that is accessed during GC execution. This may not be 100% true for all write requests, but, to avoid more complexity in the simulation, we accept this assumption.
		auto write_tr = domains[stream_id]->Write_transactions_behind_LPA_barrier.find(lpa);
		while (write_tr != domains[stream_id]->Write_transactions_behind_LPA_barrier.end()) {
			connected_transaction_serviced_signal_handler((*write_tr).second);
			delete (*write_tr).second;
			domains[stream_id]->Write_transactions_behind_LPA_barrier.erase(write_tr);
			write_tr = domains[stream_id]->Write_transactions_behind_LPA_barrier.find(lpa);
		}
    }
    void Address_Mapping_Unit_MQ::Remove_barrier_for_accessing_mvpn(const stream_id_type stream_id, const MVPN_type mvpn)
    {
		auto itr = domains[stream_id]->Locked_MVPNs.find(mvpn);
		if (itr == domains[stream_id]->Locked_MVPNs.end()) {
			PRINT_ERROR("Illegal operation: Unlocking an MVPN that has not been locked!");
		}
		domains[stream_id]->Locked_MVPNs.erase(itr);

		//If there are read requests waiting behind the barrier, then MQSim assumes they can be serviced with the actual page data that is accessed during GC execution
		if (domains[stream_id]->MVPN_read_transactions_waiting_behind_barrier.find(mvpn) != domains[stream_id]->MVPN_read_transactions_waiting_behind_barrier.end()) {
			domains[stream_id]->MVPN_read_transactions_waiting_behind_barrier.erase(mvpn);
			PPA_type ppn = domains[stream_id]->GlobalTranslationDirectory[mvpn].MPPN;
			if (ppn == NO_MPPN) {
				PRINT_ERROR("Reading an invalid physical flash page address in function generate_flash_read_request_for_mapping_data!")
			}

			NVM_Transaction_Flash_RD* readTR = new NVM_Transaction_Flash_RD(Transaction_Source_Type::MAPPING, stream_id,
					SECTOR_SIZE_IN_BYTE, NO_LPA, NO_PPA, NULL, mvpn, ((page_status_type)0x1) << sector_no_per_page, CurrentTimeStamp);
			Convert_ppa_to_address(ppn, readTR->Address);
			readTR->PPA = ppn;
			Stats::Total_flash_reads_for_mapping++;
			Stats::Total_flash_reads_for_mapping_per_stream[stream_id]++;

			handle_transaction_serviced_signal_from_PHY(readTR);
			
			delete readTR;
		}

		if (domains[stream_id]->MVPN_write_transaction_waiting_behind_barrier.find(mvpn) != domains[stream_id]->MVPN_write_transaction_waiting_behind_barrier.end()) {
			domains[stream_id]->MVPN_write_transaction_waiting_behind_barrier.erase(mvpn);
			//Writing back all dirty CMT entries that fall into the same translation virtual page (MVPN)
			unsigned int read_size = 0;
			page_status_type readSectorsBitmap = 0;
			LPA_type start_lpn = get_start_LPN_in_MVP(mvpn);
			LPA_type end_lpn = get_end_LPN_in_MVP(mvpn);
			for (LPA_type lpn_itr = start_lpn; lpn_itr <= end_lpn; lpn_itr++) {
				if (domains[stream_id]->CMT->Exists(stream_id, lpn_itr)) {
					if (domains[stream_id]->CMT->Is_dirty(stream_id, lpn_itr)) {
						domains[stream_id]->CMT->Make_clean(stream_id, lpn_itr);
					} else {
						page_status_type bitlocation = (((page_status_type)0x1) << (((lpn_itr - start_lpn) * GTD_entry_size) / SECTOR_SIZE_IN_BYTE));
						if ((readSectorsBitmap & bitlocation) == 0) {
							readSectorsBitmap |= bitlocation;
							read_size += SECTOR_SIZE_IN_BYTE;
						}
					}
				}
			}

			//Read the unchaged mapping entries from flash to merge them with updated parts of MVPN
			MPPN_type mppn = domains[stream_id]->GlobalTranslationDirectory[mvpn].MPPN;
			NVM_Transaction_Flash_WR* writeTR = new NVM_Transaction_Flash_WR(Transaction_Source_Type::MAPPING, stream_id, SECTOR_SIZE_IN_BYTE * sector_no_per_page,
				mvpn, mppn, NULL, mvpn, NULL, (((page_status_type)0x1) << sector_no_per_page) - 1, CurrentTimeStamp);

			Stats::Total_flash_reads_for_mapping++;
			Stats::Total_flash_writes_for_mapping++;
			Stats::Total_flash_reads_for_mapping_per_stream[stream_id]++;
			Stats::Total_flash_writes_for_mapping_per_stream[stream_id]++;

			handle_transaction_serviced_signal_from_PHY(writeTR);

			delete writeTR;
		}
    }
    void Address_Mapping_Unit_MQ::Start_servicing_writes_for_level(const uint32_t level)
    {
		if(write_transactions_for_level.size() < level){
			std::set<NVM_Transaction_Flash_WR*>& waiting_write_list = write_transactions_for_level.at(level);
			auto trItr = waiting_write_list.begin();
			while(trItr != waiting_write_list.end()){
				if(translate_lpa_to_ppa((*trItr)->Stream_id, *trItr, level)) {
					ftl->TSU->Submit_transaction(*trItr);
					if((*trItr)->RelatedRead != NULL){
						ftl->TSU->Submit_transaction((*trItr)->RelatedRead);
					}
					waiting_write_list.erase(trItr++);
				} else{
					break;
				}
			}
			ftl->TSU->Schedule();
		}
    }
    
	PPA_type Address_Mapping_Unit_MQ::online_create_entry_for_reads(LPA_type lpa, const stream_id_type stream_id, NVM::FlashMemory::Physical_Page_Address &read_address, uint64_t read_sectors_bitmap)
    {
        // in page level mapping table, allocate page and no process.
		block_manager->Allocate_page(stream_id, read_address, 1);
		PPA_type ppa = Convert_address_to_ppa(read_address);
		domains[stream_id]->Update_mapping_info(ideal_mapping_table, stream_id, lpa, ppa, read_sectors_bitmap);
        return ppa;
    }

    void Address_Mapping_Unit_MQ::manage_user_transaction_facing_barrier(NVM_Transaction_Flash *transaction)
    {
		std::pair<LPA_type, NVM_Transaction_Flash*> entry(transaction->LPA, transaction);
		if (transaction->Type == Transaction_Type::READ) {
			domains[transaction->Stream_id]->Read_transactions_behind_LPA_barrier.insert(entry);
		} else {
			domains[transaction->Stream_id]->Write_transactions_behind_LPA_barrier.insert(entry);
		}
    }

    void Address_Mapping_Unit_MQ::manage_mapping_transaction_facing_barrier(stream_id_type stream_id, MVPN_type mvpn, bool read)
    {
		if (read) {
			domains[stream_id]->MVPN_read_transactions_waiting_behind_barrier.insert(mvpn);
		} else {
			domains[stream_id]->MVPN_write_transaction_waiting_behind_barrier.insert(mvpn);
		}
    }
    
	bool Address_Mapping_Unit_MQ::is_lpa_locked_for_gc(stream_id_type stream_id, LPA_type lpa)
    {
		return domains[stream_id]->Locked_LPAs.find(lpa) != domains[stream_id]->Locked_LPAs.end();
    }
    bool Address_Mapping_Unit_MQ::is_mvpn_locked_for_gc(stream_id_type stream_id, MVPN_type mvpn)
    {
		return domains[stream_id]->Locked_MVPNs.find(mvpn) != domains[stream_id]->Locked_MVPNs.end();
    }

    bool Address_Mapping_Unit_MQ::query_cmt(NVM_Transaction_Flash *tr)
    {
        stream_id_type stream_id = tr->Stream_id;
		Stats::total_CMT_queries++;
		Stats::total_CMT_queries_per_stream[stream_id]++;

		level_type level = lui->isHot(tr->LPA) ? 0 : 1;
		lui->writeData(tr->LPA);
		
		UID* uid = lui->getUID();
		if(uid != nullptr){
			block_manager->startGroupConfiguration(uid);
		}

		if (domains[stream_id]->Mapping_entry_accessible(ideal_mapping_table, stream_id, tr->LPA))//Either limited or unlimited CMT
		{
			Stats::CMT_hits_per_stream[stream_id]++;
			Stats::CMT_hits++;
			if (tr->Type == Transaction_Type::READ) {
				Stats::total_readTR_CMT_queries_per_stream[stream_id]++;
				Stats::total_readTR_CMT_queries++;
				Stats::readTR_CMT_hits_per_stream[stream_id]++;
				Stats::readTR_CMT_hits++;
			} else {
				//This is a write transaction
				Stats::total_writeTR_CMT_queries++;
				Stats::total_writeTR_CMT_queries_per_stream[stream_id]++;
				Stats::writeTR_CMT_hits++;
				Stats::writeTR_CMT_hits_per_stream[stream_id]++;
			}

			if (translate_lpa_to_ppa(stream_id, tr, level)) {
				return true;
			} else {
				manage_unsuccessful_transaction((NVM_Transaction_Flash_WR*)tr, level);
				return false;
			}
		} else {//Limited CMT

			PRINT_ERROR("MAPPING HAS NOT BEEN IMPLEMENTED")
			//Maybe we can catch mapping data from an on-the-fly write back request
			if (request_mapping_entry(stream_id, tr->LPA)) {
				Stats::CMT_miss++;
				Stats::CMT_miss_per_stream[stream_id]++;
				if (tr->Type == Transaction_Type::READ) {
					Stats::total_readTR_CMT_queries++;
					Stats::total_readTR_CMT_queries_per_stream[stream_id]++;
					Stats::readTR_CMT_miss++;
					Stats::readTR_CMT_miss_per_stream[stream_id]++;
				} else { //This is a write transaction
					Stats::total_writeTR_CMT_queries++;
					Stats::total_writeTR_CMT_queries_per_stream[stream_id]++;
					Stats::writeTR_CMT_miss++;
					Stats::writeTR_CMT_miss_per_stream[stream_id]++;
				}
				if (translate_lpa_to_ppa(stream_id, tr, level)) {
					return true;
				} else {
					manage_unsuccessful_transaction((NVM_Transaction_Flash_WR*)tr, level);
					return false;
				}
			} else {
				if (tr->Type == Transaction_Type::READ) {
					Stats::total_readTR_CMT_queries++;
					Stats::total_readTR_CMT_queries_per_stream[stream_id]++;
					Stats::readTR_CMT_miss++;
					Stats::readTR_CMT_miss_per_stream[stream_id]++;
					domains[stream_id]->Waiting_unmapped_read_transactions.insert(std::pair<LPA_type, NVM_Transaction_Flash*>(tr->LPA, tr));
				} else {//This is a write transaction
					Stats::total_writeTR_CMT_queries++;
					Stats::total_writeTR_CMT_queries_per_stream[stream_id]++;
					Stats::writeTR_CMT_miss++;
					Stats::writeTR_CMT_miss_per_stream[stream_id]++;
					domains[stream_id]->Waiting_unmapped_program_transactions.insert(std::pair<LPA_type, NVM_Transaction_Flash*>(tr->LPA, tr));
				}
			}

			return false;
		}
	}
    bool Address_Mapping_Unit_MQ::translate_lpa_to_ppa(stream_id_type streamID, NVM_Transaction_Flash *transaction, level_type level)
    {
		PPA_type ppa = domains[streamID]->Get_ppa(ideal_mapping_table, streamID, transaction->LPA);

		if (transaction->Type == Transaction_Type::READ) {
			if (ppa == NO_PPA) {
				ppa = online_create_entry_for_reads(transaction->LPA, streamID, transaction->Address, ((NVM_Transaction_Flash_RD*)transaction)->read_sectors_bitmap);
				block_manager->Program_transaction_serviced(ppa);
				flash_controller->Change_flash_page_status_for_preconditioning(transaction->Address, transaction->LPA);
			}
			transaction->PPA = ppa;
			Convert_ppa_to_address(transaction->PPA, transaction->Address);
			block_manager->Read_transaction_issued(ppa);
			transaction->Physical_address_determined = true;
			
			// FIN Read.
			return true;
		} else {
			if(block_manager->Stop_servicing_writes(level)){
				return false;
			}
			allocate_page_for_write((NVM_Transaction_Flash_WR*)transaction, level);
			transaction->Physical_address_determined = true;
			
			return true;
		}
    }
    
	void Address_Mapping_Unit_MQ::allocate_page_for_write(NVM_Transaction_Flash_WR *tr, uint32_t level)
    {
		AddressMappingDomain* domain = domains[tr->Stream_id];
		PPA_type old_ppa = domain->Get_ppa(ideal_mapping_table, tr->Stream_id, tr->LPA);

		if(old_ppa == NO_PPA){
			if(level > 0){
				PRINT_ERROR("Unexpected mapping table status in allocate_page_in_plane_for_user_write function for a GC/WL write!")
			}
		} else {
			// Case of GC.
			if(level > 0){
				NVM::FlashMemory::Physical_Page_Address addr;
				Convert_ppa_to_address(old_ppa, addr);
				block_manager->Invalidate_page_in_block(tr->Stream_id, old_ppa);
				page_status_type page_status_in_cmt = domain->Get_page_status(ideal_mapping_table, tr->Stream_id, tr->LPA);
				if (page_status_in_cmt != tr->write_sectors_bitmap)
					PRINT_ERROR("Unexpected mapping table status in allocate_page_in_plane_for_user_write for a GC/WL write!")
			} 
			// Case of user-write.
			else{
				page_status_type prev_page_status = domain->Get_page_status(ideal_mapping_table, tr->Stream_id, tr->LPA);
				page_status_type status_intersection = tr->write_sectors_bitmap & prev_page_status;

				// Case of RAM is not needed.
				if (status_intersection == prev_page_status) {
					block_manager->Invalidate_page_in_block(tr->Stream_id, old_ppa);
				}
				// Case of RAM is needed.
				else {
					page_status_type read_pages_bitmap = status_intersection ^ prev_page_status;
					NVM_Transaction_Flash_RD *update_read_tr = new NVM_Transaction_Flash_RD(tr->Source, tr->Stream_id,
						count_sector_no_from_status_bitmap(read_pages_bitmap) * SECTOR_SIZE_IN_BYTE, tr->LPA, old_ppa, tr->UserIORequest,
						tr->Content, tr, read_pages_bitmap, domain->GlobalMappingTable[tr->LPA].TimeStamp);
					Convert_ppa_to_address(old_ppa, update_read_tr->Address);
					block_manager->Read_transaction_issued(old_ppa);//Inform block manager about a new transaction as soon as the transaction's target address is determined
					block_manager->Invalidate_page_in_block(tr->Stream_id, old_ppa);
					tr->RelatedRead = update_read_tr;
				}
			}
		}

		block_manager->Allocate_page(tr->Stream_id, tr->Address, level);
		tr->PPA = Convert_address_to_ppa(tr->Address);
		domain->Update_mapping_info(ideal_mapping_table, tr->Stream_id, tr->LPA, tr->PPA, 
			((NVM_Transaction_Flash_WR*)tr)->write_sectors_bitmap | domain->Get_page_status(ideal_mapping_table, tr->Stream_id, tr->LPA));
    }

    void Address_Mapping_Unit_MQ::allocate_page_for_translation_write(NVM_Transaction_Flash_WR *tr, MVPN_type mvpn)
    {
		PRINT_ERROR("allocate_page_for_translation_write - MAPPING IS NOT IMPLEMENTED")
		// AddressMappingDomain* domain = domains[tr->Stream_id];
		// MPPN_type old_MPPN = domain->GlobalTranslationDirectory[mvpn].MPPN;

		// if(old_MPPN != NO_MPPN){
		// 	NVM::FlashMemory::Physical_Page_Address prevAddr;
		// 	block_manager->Invalidate_page_in_block(tr->Stream_id, old_MPPN);
		// }

		// block_manager->Allocate_mapping_page(tr->Stream_id, tr->Address);
		// tr->PPA = Convert_address_to_ppa(tr->Address);
		// domain->GlobalTranslationDirectory[mvpn].MPPN = (MPPN_type)tr->PPA;
		// domain->GlobalTranslationDirectory[mvpn].TimeStamp = CurrentTimeStamp;
    }

    void Address_Mapping_Unit_MQ::Convert_ppa_to_address(const PPA_type ppa, NVM::FlashMemory::Physical_Page_Address &addr)
    {
		addr.ChannelID = (flash_channel_ID_type)(ppa / page_no_per_channel);
		addr.ChipID = (flash_chip_ID_type)((ppa % page_no_per_channel) / page_no_per_chip);
		addr.DieID = (flash_die_ID_type)(((ppa % page_no_per_channel) % page_no_per_chip) / page_no_per_die);
		addr.PlaneID = (flash_plane_ID_type)((((ppa % page_no_per_channel) % page_no_per_chip) % page_no_per_die) / page_no_per_plane);
		addr.BlockID = (flash_block_ID_type)(((((ppa % page_no_per_channel) % page_no_per_chip) % page_no_per_die) % page_no_per_plane) / pages_no_per_block);
		addr.PageID = (flash_page_ID_type)((((((ppa % page_no_per_channel) % page_no_per_chip) % page_no_per_die) % page_no_per_plane) % pages_no_per_block) % pages_no_per_block);
    }
    void Address_Mapping_Unit_MQ::manage_unsuccessful_transaction(NVM_Transaction_Flash_WR *tr, level_type level)
    {
		if(write_transactions_for_level.size() < level){
			write_transactions_for_level.resize(level + 1);
		}
		write_transactions_for_level.at(level).insert(tr);
    }
    
	bool Address_Mapping_Unit_MQ::request_mapping_entry(const stream_id_type stream_id, const LPA_type lpa)
    {
		PRINT_ERROR("request mapping entry - MAPPING IS NOT IMPLEMENTED")
		// AddressMappingDomain* domain = domains[stream_id];
		// MVPN_type mvpn = (MVPN_type)(lpa / no_of_translation_entries_per_page);

		// if(domain->GlobalTranslationDirectory[mvpn].MPPN == NO_MPPN){
		// 	if(!domain->CMT->Check_free_slot_availability()){
		// 		LPA_type evicted_lpa;
		// 		CMTSlotType evictedItem = domain->CMT->Evict_one_slot(evicted_lpa);
		// 		if(evictedItem.Dirty){
		// 			domain->GlobalMappingTable[evicted_lpa].PPA = evictedItem.PPA;
		// 			domain->GlobalMappingTable[evicted_lpa].WrittenStateBitmap = evictedItem.WrittenStateBitmap;
		// 			if(domain->GlobalMappingTable[evicted_lpa].TimeStamp > CurrentTimeStamp){
		// 				throw std::logic_error("Unexpected situation occurred in handling GMT!");
		// 			}
		// 			domain->GlobalMappingTable[evicted_lpa].TimeStamp = CurrentTimeStamp;
		// 			generate_flash_writeback_request_for_mapping_data(stream_id, evicted_lpa);
		// 		}
		// 	}
		// 	domain->CMT->Reserve_slot_for_lpn(stream_id, lpa);
		// 	domain->CMT->Insert_new_mapping_info(stream_id, lpa, NO_PPA, UNWRITTEN_LOGICAL_PAGE);

		// 	return true;
		// }

		// if (domain->ArrivingMappingEntries.find(mvpn) != domain->ArrivingMappingEntries.end())
		// {
		// 	if (domain->CMT->Is_slot_reserved_for_lpn_and_waiting(stream_id, lpa)) {
		// 		return false;
		// 	} else { //An entry should be created in the cache
		// 		if (!domain->CMT->Check_free_slot_availability()) {
		// 			LPA_type evicted_lpa;
		// 			CMTSlotType evictedItem = domain->CMT->Evict_one_slot(evicted_lpa);
		// 			if (evictedItem.Dirty) {
		// 				/* In order to eliminate possible race conditions for the requests that
		// 				* will access the evicted lpa in the near future (before the translation
		// 				* write finishes), MQSim updates GMT (the on flash mapping table) right
		// 				* after eviction happens.*/
		// 				domain->GlobalMappingTable[evicted_lpa].PPA = evictedItem.PPA;
		// 				domain->GlobalMappingTable[evicted_lpa].WrittenStateBitmap = evictedItem.WrittenStateBitmap;
		// 				if (domain->GlobalMappingTable[evicted_lpa].TimeStamp > CurrentTimeStamp)
		// 					throw std::logic_error("Unexpected situation occured in handling GMT!");
		// 				domain->GlobalMappingTable[evicted_lpa].TimeStamp = CurrentTimeStamp;
		// 				generate_flash_writeback_request_for_mapping_data(stream_id, evicted_lpa);
		// 			}
		// 		}
		// 		domain->CMT->Reserve_slot_for_lpn(stream_id, lpa);
		// 		domain->ArrivingMappingEntries.insert(std::pair<MVPN_type, LPA_type>(mvpn, lpa));

		// 		return false;
		// 	}
		// }

		// /*MQSim assumes that the data of all departing (evicted from CMT) translation pages are in memory, until
		// the flash program operation finishes and the entry it is cleared from DepartingMappingEntries.*/
		// if (domain->DepartingMappingEntries.find(mvpn) != domain->DepartingMappingEntries.end()) {
		// 	if (!domain->CMT->Check_free_slot_availability()) {
		// 		LPA_type evicted_lpa;
		// 		CMTSlotType evictedItem = domain->CMT->Evict_one_slot(evicted_lpa);
		// 		if (evictedItem.Dirty) {
		// 			/* In order to eliminate possible race conditions for the requests that
		// 			* will access the evicted lpa in the near future (before the translation
		// 			* write finishes), MQSim updates GMT (the on flash mapping table) right
		// 			* after eviction happens.*/
		// 			domain->GlobalMappingTable[evicted_lpa].PPA = evictedItem.PPA;
		// 			domain->GlobalMappingTable[evicted_lpa].WrittenStateBitmap = evictedItem.WrittenStateBitmap;
		// 			if (domain->GlobalMappingTable[evicted_lpa].TimeStamp > CurrentTimeStamp)
		// 				throw std::logic_error("Unexpected situation occured in handling GMT!");
		// 			domain->GlobalMappingTable[lpa].TimeStamp = CurrentTimeStamp;
		// 			generate_flash_writeback_request_for_mapping_data(stream_id, evicted_lpa);
		// 		}
		// 	}
		// 	domain->CMT->Reserve_slot_for_lpn(stream_id, lpa);
		// 	/*Hack: since we do not actually save the values of translation requests, we copy the mapping
		// 	data from GlobalMappingTable (which actually must be stored on flash)*/
		// 	domain->CMT->Insert_new_mapping_info(stream_id, lpa,
		// 		domain->GlobalMappingTable[lpa].PPA, domain->GlobalMappingTable[lpa].WrittenStateBitmap);
			
		// 	return true;
		// }

		// if (!domain->CMT->Check_free_slot_availability()) {
		// 	LPA_type evicted_lpa;
		// 	CMTSlotType evictedItem = domain->CMT->Evict_one_slot(evicted_lpa);
		// 	if (evictedItem.Dirty) {
		// 		/* In order to eliminate possible race conditions for the requests that
		// 		* will access the evicted lpa in the near future (before the translation
		// 		* write finishes), MQSim updates GMT (the on flash mapping table) right
		// 		* after eviction happens.*/
		// 		domain->GlobalMappingTable[evicted_lpa].PPA = evictedItem.PPA;
		// 		domain->GlobalMappingTable[evicted_lpa].WrittenStateBitmap = evictedItem.WrittenStateBitmap;
		// 		if (domain->GlobalMappingTable[evicted_lpa].TimeStamp > CurrentTimeStamp) {
		// 			throw std::logic_error("Unexpected situation occured in handling GMT!");
		// 		}
		// 		domain->GlobalMappingTable[evicted_lpa].TimeStamp = CurrentTimeStamp;
		// 		generate_flash_writeback_request_for_mapping_data(stream_id, evicted_lpa);
		// 	}
		// }
		// domain->CMT->Reserve_slot_for_lpn(stream_id, lpa);
		// generate_flash_read_request_for_mapping_data(stream_id, lpa);//consult GTD and create read transaction
		
		// return false;

    }
    
	void Address_Mapping_Unit_MQ::generate_flash_writeback_request_for_mapping_data(const stream_id_type stream_id, const LPA_type lpn)
    {
		PRINT_ERROR("generate_flash_writeback_request_for_mapping_data - MAPPING IS NOT IMPLEMENTED")
		// MVPN_type mvpn = get_MVPN(lpn, stream_id);
		// if(is_mvpn_locked_for_gc(stream_id, mvpn)){
		// 	manage_mapping_transaction_facing_barrier(stream_id, mvpn, false);
		// 	domains[stream_id]->DepartingMappingEntries.insert(get_MVPN(lpn, stream_id));
		// } else {
		// 	ftl->TSU->Prepare_for_transaction_submit();

		// 	//Writing back all dirty CMT entries that fall into the same translation virtual page (MVPN)
		// 	unsigned int read_size = 0;
		// 	page_status_type readSectorsBitmap = 0;
		// 	LPA_type startLPN = get_start_LPN_in_MVP(mvpn);
		// 	LPA_type endLPN = get_end_LPN_in_MVP(mvpn);
		// 	for (LPA_type lpn_itr = startLPN; lpn_itr <= endLPN; lpn_itr++) {
		// 		if (domains[stream_id]->CMT->Exists(stream_id, lpn_itr)) {
		// 			if (domains[stream_id]->CMT->Is_dirty(stream_id, lpn_itr)) {
		// 				domains[stream_id]->CMT->Make_clean(stream_id, lpn_itr);
		// 				domains[stream_id]->GlobalMappingTable[lpn_itr].PPA = domains[stream_id]->CMT->Retrieve_ppa(stream_id, lpn_itr);
		// 			} else {
		// 				page_status_type bitlocation = (((page_status_type)0x1) << (((lpn_itr - startLPN) * GTD_entry_size) / SECTOR_SIZE_IN_BYTE));
		// 				if ((readSectorsBitmap & bitlocation) == 0) {
		// 					readSectorsBitmap |= bitlocation;
		// 					read_size += SECTOR_SIZE_IN_BYTE;
		// 				}
		// 			}
		// 		}
		// 	}

		// 	//Read the unchaged mapping entries from flash to merge them with updated parts of MVPN
		// 	NVM_Transaction_Flash_RD* readTR = NULL;
		// 	MPPN_type mppn = domains[stream_id]->GlobalTranslationDirectory[mvpn].MPPN;
		// 	if (mppn != NO_MPPN) {
		// 		readTR = new NVM_Transaction_Flash_RD(Transaction_Source_Type::MAPPING, stream_id, read_size,
		// 			mvpn, mppn, NULL, mvpn, NULL, readSectorsBitmap, CurrentTimeStamp);
		// 		Convert_ppa_to_address(mppn, readTR->Address);
		// 		block_manager->Read_transaction_issued(mppn);//Inform block_manager as soon as the transaction's target address is determined
		// 		domains[stream_id]->ArrivingMappingEntries.insert(std::pair<MVPN_type, LPA_type>(mvpn, lpn));
		// 		ftl->TSU->Submit_transaction(readTR);
		// 	}

		// 	NVM_Transaction_Flash_WR* writeTR = new NVM_Transaction_Flash_WR(Transaction_Source_Type::MAPPING, stream_id, SECTOR_SIZE_IN_BYTE * sector_no_per_page,
		// 		mvpn, mppn, NULL, mvpn, readTR, (((page_status_type)0x1) << sector_no_per_page) - 1, CurrentTimeStamp);
		// 	allocate_page_for_translation_write(writeTR, mvpn);
		// 	domains[stream_id]->DepartingMappingEntries.insert(get_MVPN(lpn, stream_id));

		// 	if(readTR != NULL){
		// 		readTR->RelatedWrite = writeTR;
		// 	}

		// 	ftl->TSU->Submit_transaction(writeTR);

		// 	Stats::Total_flash_reads_for_mapping++;
		// 	Stats::Total_flash_writes_for_mapping++;
		// 	Stats::Total_flash_reads_for_mapping_per_stream[stream_id]++;
		// 	Stats::Total_flash_writes_for_mapping_per_stream[stream_id]++;

		// 	ftl->TSU->Schedule();
		// }
    }
    
	void Address_Mapping_Unit_MQ::generate_flash_read_request_for_mapping_data(const stream_id_type stream_id, const LPA_type lpn)
    {
		PRINT_ERROR("generate_flash_read_request_for_mapping_data - MAPPING IS NOT IMPLEMENTED")
		// MVPN_type mvpn = get_MVPN(lpn, stream_id);

		// if (mvpn >= domains[stream_id]->Total_translation_pages_no) {
		// 	PRINT_ERROR("Out of range virtual translation page number!")
		// }

		// domains[stream_id]->ArrivingMappingEntries.insert(std::pair<MVPN_type, LPA_type>(mvpn, lpn));

		// if (is_mvpn_locked_for_gc(stream_id, mvpn)) {
		// 	manage_mapping_transaction_facing_barrier(stream_id, mvpn, true);
		// } else {
		// 	ftl->TSU->Prepare_for_transaction_submit();

		// 	PPA_type ppn = domains[stream_id]->GlobalTranslationDirectory[mvpn].MPPN;

		// 	if (ppn == NO_MPPN){
		// 		PRINT_ERROR("Reading an invalid physical flash page address in function generate_flash_read_request_for_mapping_data!")
		// 	}

		// 	NVM_Transaction_Flash_RD* readTR = new NVM_Transaction_Flash_RD(Transaction_Source_Type::MAPPING, stream_id,
		// 			SECTOR_SIZE_IN_BYTE, NO_LPA, NO_PPA, NULL, mvpn, ((page_status_type)0x1) << sector_no_per_page, CurrentTimeStamp);
		// 	Convert_ppa_to_address(ppn, readTR->Address);
		// 	block_manager->Read_transaction_issued(ppn);//Inform block_manager as soon as the transaction's target address is determined
		// 	readTR->PPA = ppn;
		// 	ftl->TSU->Submit_transaction(readTR);

		// 	Stats::Total_flash_reads_for_mapping++;
		// 	Stats::Total_flash_reads_for_mapping_per_stream[stream_id]++;

		// 	ftl->TSU->Schedule();
		// }
    }
    
	MVPN_type Address_Mapping_Unit_MQ::get_MVPN(const LPA_type lpa, const stream_id_type stream_id)
    {
        return (MVPN_type)(lpa / no_of_translation_entries_per_page);
    }
    LPA_type Address_Mapping_Unit_MQ::get_start_LPN_in_MVP(const MVPN_type mvpn)
    {
		return (MVPN_type)(mvpn * no_of_translation_entries_per_page);
    }
    LPA_type Address_Mapping_Unit_MQ::get_end_LPN_in_MVP(const MVPN_type mvpn)
    {
		return (MVPN_type)(mvpn * no_of_translation_entries_per_page + no_of_translation_entries_per_page - 1);
    }
    void Address_Mapping_Unit_MQ::handle_transaction_serviced_signal_from_PHY(NVM_Transaction_Flash *transaction)
    {
		PRINT_ERROR("generate_flash_read_request_for_mapping_data - MAPPING IS NOT IMPLEMENTED")
	// 	//First check if the transaction source is Mapping Module
	// 	if (transaction->Source != Transaction_Source_Type::MAPPING) {
	// 		return;
	// 	}

	// 	if (_my_instance->ideal_mapping_table){
	// 		throw std::logic_error("There should not be any flash read/write when ideal mapping is enabled!");
	// 	}

	// 	if (transaction->Type == Transaction_Type::WRITE) {
	// 		_my_instance->domains[transaction->Stream_id]->DepartingMappingEntries.erase((MVPN_type)((NVM_Transaction_Flash_WR*)transaction)->Content);
	// 	} else {
	// 		/*If this is a read for an MVP that is required for merging unchanged mapping enries
	// 		* (stored on flash) with those updated entries that are evicted from CMT*/
	// 		if (((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite != NULL) {
	// 			((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->RelatedRead = NULL;
	// 		}

	// 		level_type level = 0;

	// 		_my_instance->ftl->TSU->Prepare_for_transaction_submit();
	// 		MVPN_type mvpn = (MVPN_type)((NVM_Transaction_Flash_RD*)transaction)->Content;
	// 		std::multimap<MVPN_type, LPA_type>::iterator it = _my_instance->domains[transaction->Stream_id]->ArrivingMappingEntries.find(mvpn);
	// 		while (it != _my_instance->domains[transaction->Stream_id]->ArrivingMappingEntries.end()) {
	// 			if ((*it).first == mvpn) {
	// 				LPA_type lpa = (*it).second;

	// 				//This mapping entry may arrived due to an update read request that is required for merging new and old mapping entries.
	// 				//If that is the case, we should not insert it into CMT
	// 				if (_my_instance->domains[transaction->Stream_id]->CMT->Is_slot_reserved_for_lpn_and_waiting(transaction->Stream_id, lpa)) {
	// 					_my_instance->domains[transaction->Stream_id]->CMT->Insert_new_mapping_info(transaction->Stream_id, lpa,
	// 						_my_instance->domains[transaction->Stream_id]->GlobalMappingTable[lpa].PPA,
	// 						_my_instance->domains[transaction->Stream_id]->GlobalMappingTable[lpa].WrittenStateBitmap);
	// 					auto it2 = _my_instance->domains[transaction->Stream_id]->Waiting_unmapped_read_transactions.find(lpa);
	// 					while (it2 != _my_instance->domains[transaction->Stream_id]->Waiting_unmapped_read_transactions.end() &&
	// 						(*it2).first == lpa) {
	// 						if (_my_instance->is_lpa_locked_for_gc(transaction->Stream_id, lpa)) {
	// 							_my_instance->manage_user_transaction_facing_barrier(it2->second);
	// 						} else {
	// 							if (_my_instance->translate_lpa_to_ppa(transaction->Stream_id, it2->second, level)) {
	// 								_my_instance->ftl->TSU->Submit_transaction(it2->second);
	// 							}
	// 							else {
	// 								_my_instance->manage_unsuccessful_transaction(((NVM_Transaction_Flash_WR*)it2->second), level);
	// 							}
	// 						}
	// 						_my_instance->domains[transaction->Stream_id]->Waiting_unmapped_read_transactions.erase(it2++);
	// 					}
	// 					it2 = _my_instance->domains[transaction->Stream_id]->Waiting_unmapped_program_transactions.find(lpa);
	// 					while (it2 != _my_instance->domains[transaction->Stream_id]->Waiting_unmapped_program_transactions.end() &&
	// 						(*it2).first == lpa) {
	// 						if (_my_instance->is_lpa_locked_for_gc(transaction->Stream_id, lpa)) {
	// 							_my_instance->manage_user_transaction_facing_barrier(it2->second);
	// 						} else {
	// 							if (_my_instance->translate_lpa_to_ppa(transaction->Stream_id, it2->second, level)) {
	// 								_my_instance->ftl->TSU->Submit_transaction(it2->second);
	// 								if (((NVM_Transaction_Flash_WR*)it2->second)->RelatedRead != NULL) {
	// 									_my_instance->ftl->TSU->Submit_transaction(((NVM_Transaction_Flash_WR*)it2->second)->RelatedRead);
	// 								}
	// 							} else {
	// 								_my_instance->manage_unsuccessful_transaction(((NVM_Transaction_Flash_WR*)it2->second), level);
	// 							}
	// 						}
	// 						_my_instance->domains[transaction->Stream_id]->Waiting_unmapped_program_transactions.erase(it2++);
	// 					}
	// 				}
	// 			} else {
	// 				break;
	// 			}
	// 			_my_instance->domains[transaction->Stream_id]->ArrivingMappingEntries.erase(it++);
	// 		}
	// 		_my_instance->ftl->TSU->Schedule();
	// 	}
    // }
	}
}
