#include "Address_Mapping_Unit_MQ.h"
#include "MQ_GC_Unit.h"
#include "FTL.h"
#include "Flash_Block_Manager_MQ.h"
#include <algorithm>


namespace SSD_Components{
    MQ_GC_Unit* MQ_GC_Unit::_my_instance = NULL;

    MQ_GC_Unit::MQ_GC_Unit(const sim_object_id_type &id, FTL *ftl, uint32_t pagesPerBlock, uint32_t sectorsPerPage)
    : Sim_Object(id), ftl(ftl), pagesPerBlock(pagesPerBlock), sectorsPerPage(sectorsPerPage){
        _my_instance = this;
    }

    MQ_GC_Unit::~MQ_GC_Unit(){
    }

    void MQ_GC_Unit::Setup_triggers()
    {
        Sim_Object::Setup_triggers();
        ftl->PHY->ConnectToTransactionServicedSignal(handle_transaction_serviced_signal_from_PHY);
    }

    void MQ_GC_Unit::Start_simulation()
    {
    }

    void MQ_GC_Unit::Validate_simulation_config()
    {
    }

    void MQ_GC_Unit::Execute_simulator_event(MQSimEngine::Sim_Event *)
    {
    }

    void MQ_GC_Unit::handle_transaction_serviced_signal_from_PHY(NVM_Transaction_Flash *transaction)
    {
        Block_Type* block = nullptr;
        switch (transaction->Source) {
            case Transaction_Source_Type::USERIO:
            case Transaction_Source_Type::MAPPING:
            case Transaction_Source_Type::CACHE:
                switch (transaction->Type)
                {
                    case Transaction_Type::READ:
                        _my_instance->ftl->BlockManager->Read_transaction_serviced(transaction->PPA);
                        break;
                    case Transaction_Type::WRITE:
                        _my_instance->ftl->BlockManager->Program_transaction_serviced(transaction->PPA);
                        break;
                    default:
                        PRINT_ERROR("Unexpected situation in the GC function!")
                }
                block = _my_instance->ftl->BlockManager->getBlock(transaction->PPA);
                if (block->status == MQ_Block_Status::ERASING) {
                    if(block->Ongoing_user_program_count == 0 && block->Ongoing_user_read_count == 0){
                        _my_instance->submitTransactions(block);
                    }
                }
                return;
        }

        switch (transaction->Type) {
            case Transaction_Type::READ:
            {
                block = _my_instance->ftl->BlockManager->getBlock(transaction->PPA);
                PPA_type ppa;
                MPPN_type mppa;
                page_status_type page_status_bitmap;
                if (block->Holds_mapping_data) {
                    // _my_instance->ftl->Address_Mapping_Unit->Get_translation_mapping_info_for_gc(transaction->Stream_id, (MVPN_type)transaction->LPA, mppa, page_status_bitmap);
                    // //There has been no write on the page since GC start, and it is still valid
                    // if (mppa == transaction->PPA) {
                    //     ((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->write_sectors_bitmap = FULL_PROGRAMMED_PAGE;
                    //     ((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->LPA = transaction->LPA;
                    //     ((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->RelatedRead = NULL;
                    //     _my_instance->ftl->Address_Mapping_Unit->allocate_page_for_translation_write(((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite, transaction->LPA);
                    //     _my_instance->ftl->TSU->Prepare_for_transaction_submit();
                    //     _my_instance->ftl->TSU->Submit_transaction(((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite);
                    //     _my_instance->ftl->TSU->Schedule();
                    // } else {
                    //     PRINT_ERROR("Inconsistency found when moving a page for GC/WL!")
                    // }
                } else {
                    _my_instance->ftl->Address_Mapping_Unit->Get_data_mapping_info_for_gc(transaction->Stream_id, transaction->LPA, ppa, page_status_bitmap);
                    
                    //There has been no write on the page since GC start, and it is still valid
                    if (ppa == transaction->PPA) {
                        ((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->write_sectors_bitmap = page_status_bitmap;
                        ((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->LPA = transaction->LPA;
                        ((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->RelatedRead = NULL;

                        if(_my_instance->ftl->BlockManager->Stop_servicing_writes((((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite)->level)){
                            _my_instance->ftl->Address_Mapping_Unit->manage_unsuccessful_transaction(((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite, (((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite)->level);
                        } else{
                            _my_instance->ftl->Address_Mapping_Unit->allocate_page_for_write(((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite);
                            _my_instance->ftl->TSU->Prepare_for_transaction_submit();
                            _my_instance->ftl->TSU->Submit_transaction(((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite);
                            _my_instance->ftl->TSU->Schedule();
                        }
                    } else {
                        PRINT_ERROR("Inconsistency found when moving a page for GC/WL!")
                    }
                }
                break;
            }
            case Transaction_Type::WRITE:
                block = _my_instance->ftl->BlockManager->getBlock(((NVM_Transaction_Flash_WR*)transaction)->RelatedErase->PPA);
                if (block->Holds_mapping_data) {
                    _my_instance->ftl->Address_Mapping_Unit->Remove_barrier_for_accessing_mvpn(transaction->Stream_id, (MVPN_type)transaction->LPA);
                    DEBUG(Simulator->Time() << ": MVPN=" << (MVPN_type)transaction->LPA << " unlocked!!");
                } else {
                    _my_instance->ftl->Address_Mapping_Unit->Remove_barrier_for_accessing_lpa(transaction->Stream_id, transaction->LPA);
                    DEBUG(Simulator->Time() << ": LPA=" << (MVPN_type)transaction->LPA << " unlocked!!");
                }
                block->Erase_transaction->Page_movement_activities.remove((NVM_Transaction_Flash_WR*)transaction);
                if(block->Erase_transaction->Page_movement_activities.size() == 0){
                    _my_instance->ftl->TSU->Prepare_for_transaction_submit();
                    _my_instance->ftl->TSU->Submit_transaction(block->Erase_transaction);
                    _my_instance->ftl->TSU->Schedule();
                }
                break;
            case Transaction_Type::ERASE:
                block = _my_instance->ftl->BlockManager->getBlock(transaction->PPA);
                _my_instance->ftl->BlockManager->queues.at(block->prevQueue)->currentErasingBlocksCount--;
                _my_instance->ftl->BlockManager->finishErase(block);
                if(_my_instance->ftl->BlockManager->overGCThreshold(block->prevQueue)){
                    _my_instance->gc_start(_my_instance->ftl->BlockManager->queues.at(block->prevQueue), _my_instance->ftl->BlockManager->lui->getCurrentTimestamp());
                } else{
                    _my_instance->ftl->Address_Mapping_Unit->Start_servicing_writes_for_level(block->prevQueue);
                }
                break;
            } //switch (transaction->Type)
    }

    Block_Type *MQ_GC_Unit::selectVictimBlockFront(Block_Queue *queue)
    {
        auto victimBlockItr = queue->blockList.begin();

        while((*victimBlockItr)->status == MQ_Block_Status::ERASING){
            victimBlockItr++;
        }
        return (*victimBlockItr);
    }
    void MQ_GC_Unit::gc_start(Block_Queue* prevQueue, lui_timestamp currentTimeStamp)
    {
        Block_Type* victimBlock = nullptr;
        level_type nextLevel = prevQueue->getLevel();

        if(ftl->BlockManager->isLastQueue(prevQueue->getLevel())){
            //last Queue
            victimBlock = selectVictimBlockCB(prevQueue, currentTimeStamp);
        } else{
            victimBlock = selectVictimBlockFront(prevQueue);
            if(victimBlock->status == MQ_Block_Status::ERASING){
                return;
            }
            nextLevel++;
        }


        if(currentTimeStamp != 0) ftl->BlockManager->handleLUIBlockAge(victimBlock);

        victimBlock->SetEraseTags(nextLevel);
        prevQueue->currentErasingBlocksCount++;
        ftl->Address_Mapping_Unit->Set_barrier_for_accessing_physical_block(victimBlock);

        if(victimBlock->Ongoing_user_program_count == 0 && victimBlock->Ongoing_user_read_count == 0){
            submitTransactions(victimBlock);
        }
    }

    Block_Type *MQ_GC_Unit::selectVictimBlockCB(Block_Queue *queue, lui_timestamp currentTimeStamp)
    {

		double lowestCost = pagesPerBlock;
		auto lowestCostItr = queue->blockList.begin();

		for(auto blockItr = queue->blockList.begin(); blockItr != queue->blockList.end(); blockItr++){
			lui_timestamp age = currentTimeStamp - (*blockItr)->createTimestamp;
			double currentCost = (double)(pagesPerBlock - (*blockItr)->invalid_page_count) / (double)(age * (*blockItr)->invalid_page_count);
			if(currentCost < lowestCost && ((*blockItr)->Ongoing_user_read_count == 0) && ((*blockItr)->Ongoing_user_program_count == 0)){
                if((*blockItr)->currentPageIdx == pagesPerBlock && (*blockItr)->status == MQ_Block_Status::WORKING){
                    lowestCostItr = blockItr;
                    lowestCost = currentCost;
                }
			}
		}
        return (*lowestCostItr);
    }

    bool MQ_GC_Unit::GC_is_in_urgent_mode(NVM::FlashMemory::Flash_Chip *chip)
    {
        return false;
    }

    
    void MQ_GC_Unit::submitTransactions(Block_Type* victimBlock)
    {
        Stats::Total_gc_executions++;
        ftl->TSU->Prepare_for_transaction_submit();
        
        NVM_Transaction_Flash_ER* gc_erase_tr = new NVM_Transaction_Flash_ER(Transaction_Source_Type::GC_WL, victimBlock->Stream_id, *victimBlock->blockAddr);
        gc_erase_tr->PPA = ftl->Address_Mapping_Unit->Convert_address_to_ppa(gc_erase_tr->Address);
        Block_Type* tmpBlock = ftl->BlockManager->getBlock(gc_erase_tr->PPA);
        if(victimBlock->invalid_page_count != pagesPerBlock){
            NVM_Transaction_Flash_RD* gc_read = NULL;
            NVM_Transaction_Flash_WR* gc_write = NULL;
            NVM::FlashMemory::Physical_Page_Address gc_candidate_address = *victimBlock->blockAddr;
            for (flash_page_ID_type pageID = 0; pageID < victimBlock->currentPageIdx; pageID++) {
                if (ftl->BlockManager->isPageValid(victimBlock, pageID)) {
                    Stats::Total_page_movements_for_gc++;
                    gc_candidate_address.PageID = pageID;
                    gc_read = new NVM_Transaction_Flash_RD(Transaction_Source_Type::GC_WL, victimBlock->Stream_id, sectorsPerPage * SECTOR_SIZE_IN_BYTE,
                        NO_LPA, ftl->Address_Mapping_Unit->Convert_address_to_ppa(gc_candidate_address), gc_candidate_address, NULL, 0, NULL, 0, INVALID_TIME_STAMP);
                    gc_write = new NVM_Transaction_Flash_WR(Transaction_Source_Type::GC_WL, victimBlock->Stream_id, sectorsPerPage * SECTOR_SIZE_IN_BYTE,
                        NO_LPA, NO_PPA, gc_candidate_address, NULL, 0, gc_read, 0, INVALID_TIME_STAMP);
                    gc_write->ExecutionMode = WriteExecutionModeType::SIMPLE;
                    gc_write->RelatedErase = gc_erase_tr;
                    gc_write->level = victimBlock->nextQueue;
                    gc_read->RelatedWrite = gc_write;
                    ftl->TSU->Submit_transaction(gc_read);//Only the read transaction would be submitted. The Write transaction is submitted when the read transaction is finished and the LPA of the target page is determined
                    gc_erase_tr->Page_movement_activities.push_back(gc_write);
                }
            }
        }
        victimBlock->Erase_transaction = gc_erase_tr;
        if(gc_erase_tr->Page_movement_activities.size() == 0){
            ftl->TSU->Submit_transaction(gc_erase_tr);
        }
        ftl->TSU->Schedule();    
    }

}
