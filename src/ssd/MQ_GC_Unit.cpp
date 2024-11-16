#include "Address_Mapping_Unit_MQ.h"
#include "MQ_GC_Unit.h"
#include "FTL.h"
#include "Flash_Block_Manager_MQ.h"


namespace SSD_Components{
    MQ_GC_Unit* MQ_GC_Unit::_my_instance = NULL;

    MQ_GC_Unit::MQ_GC_Unit(const sim_object_id_type &id, FTL *ftl, uint32_t pagesPerBlock, uint32_t sectorsPerPage)
    : Sim_Object(id), ftl(ftl), pagesPerBlock(pagesPerBlock), sectorsPerPage(sectorsPerPage){
        _my_instance = this;
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
        Block_Type* block = _my_instance->ftl->BlockManager->getBlock(transaction->PPA);
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
                if (block->IsErasing()) {
                    if(block->Ongoing_user_program_count == 0 && block->Ongoing_user_read_count == 0){
                        _my_instance->submitTransactions(block);
                    }
                }
                return;
        }

        switch (transaction->Type) {
            case Transaction_Type::READ:
            {
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

                        if(_my_instance->ftl->BlockManager->Stop_servicing_writes(block->nextQueue)){
                            _my_instance->ftl->Address_Mapping_Unit->manage_unsuccessful_transaction(((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite, block->nextQueue);
                        } else{
                            _my_instance->ftl->Address_Mapping_Unit->allocate_page_for_write(((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite, block->nextQueue);
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
                _my_instance->ftl->BlockManager->finishErase(block);
                _my_instance->ftl->Address_Mapping_Unit->Start_servicing_writes_for_level(block->nextQueue);

                break;
            } //switch (transaction->Type)
    }

    void MQ_GC_Unit::gc_start(Block_Queue* prevQueue, Block_Queue* nextQueue)
    {
        if(prevQueue->isFull()){
            Block_Type* victimBlock = nullptr;

            //last Queue
            if(prevQueue == nextQueue){
                victimBlock = ftl->Address_Mapping_Unit->selectVictimBlockCB(prevQueue);
                ftl->Address_Mapping_Unit->lui->addBlockAge(victimBlock, Queue_Type::LAST_QUEUE);
            } else{
                if(prevQueue->getLevel() == 0){
                    ftl->Address_Mapping_Unit->lui->addBlockAge(victimBlock, Queue_Type::HOT_QUEUE);
                }
                victimBlock = prevQueue->dequeueBlock();
            }

            victimBlock->SetEraseTags(nextQueue->getLevel());
            prevQueue->Ongoing_erase_blocks.insert(victimBlock);
            ftl->Address_Mapping_Unit->Set_barrier_for_accessing_physical_block(victimBlock);

            if(victimBlock->Ongoing_user_program_count == 0 && victimBlock->Ongoing_user_read_count == 0){
                submitTransactions(victimBlock);
            }
        } else{
            PRINT_ERROR("GC - Q is not fully charged")
        }
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
        if(victimBlock->invalid_page_count != pagesPerBlock){
            NVM_Transaction_Flash_RD* gc_read = NULL;
            NVM_Transaction_Flash_WR* gc_write = NULL;
            NVM::FlashMemory::Physical_Page_Address gc_candidate_address = *victimBlock->blockAddr;

            for (flash_page_ID_type pageID = 0; pageID < pagesPerBlock; pageID++) {
                if (ftl->BlockManager->isPageValid(victimBlock, pageID)) {
                    Stats::Total_page_movements_for_gc++;
                    gc_candidate_address.PageID = pageID;
                    gc_read = new NVM_Transaction_Flash_RD(Transaction_Source_Type::GC_WL, victimBlock->Stream_id, sectorsPerPage * SECTOR_SIZE_IN_BYTE,
                        NO_LPA, ftl->Address_Mapping_Unit->Convert_address_to_ppa(gc_candidate_address), gc_candidate_address, NULL, 0, NULL, 0, INVALID_TIME_STAMP);
                    gc_write = new NVM_Transaction_Flash_WR(Transaction_Source_Type::GC_WL, victimBlock->Stream_id, sectorsPerPage * SECTOR_SIZE_IN_BYTE,
                        NO_LPA, NO_PPA, gc_candidate_address, NULL, 0, gc_read, 0, INVALID_TIME_STAMP);
                    gc_write->ExecutionMode = WriteExecutionModeType::SIMPLE;
                    gc_write->RelatedErase = gc_erase_tr;
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
