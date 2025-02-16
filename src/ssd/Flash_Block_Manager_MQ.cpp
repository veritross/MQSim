#include "Flash_Block_Manager_MQ.h"
#include <queue>
#include "MQ_GC_Unit.h"
#include "FTL.h"
#include "Address_Mapping_Unit_MQ.h"


namespace SSD_Components{
    uint32_t Block_Type::page_vector_size = 0;
    Block_Type::Block_Type(NVM::FlashMemory::Physical_Page_Address &blockAddr)
    {
        this->blockAddr = new NVM::FlashMemory::Physical_Page_Address(blockAddr);
    }

    Block_Type::~Block_Type()
    {
        delete this->blockAddr;
    }

    void Block_Type::SetUp(level_type prevQueue)
    {
        if(Ongoing_user_program_count > 0 || Ongoing_user_read_count > 0){
            PRINT_ERROR("Setup using block")
        }
        status = MQ_Block_Status::IDLE;
        createTimestamp = 0;
        Stream_id = NO_STREAM;
        Holds_mapping_data = false;
        
        currentPageIdx = 0;
        invalid_page_count = 0;
        invalid_page_bitmap.resize(page_vector_size, 0);
        for(int i = 0; i < page_vector_size; i++){
            invalid_page_bitmap[i] = 0;
        }

        Ongoing_user_read_count = 0;
        Ongoing_user_program_count = 0;
        
        Erase_transaction = nullptr;

        this->prevQueue = prevQueue;
        nextQueue = UNDEFINED_LEVEL;
    }

    void Block_Type::StartUsing(lui_timestamp createTimestamp, stream_id_type streamID, bool Holds_mapping_data)
    {
        this->status = MQ_Block_Status::WORKING;
        this->createTimestamp = createTimestamp;
        this->Stream_id = streamID;
        this->Holds_mapping_data = Holds_mapping_data;
    }

    void Block_Type::SetEraseTags(level_type nextQueue)
    {
        if(this->nextQueue != UNDEFINED_LEVEL || this->status == MQ_Block_Status::ERASING || Erase_transaction != nullptr){
            PRINT_ERROR("Set erase tags")
        }
        this->status = MQ_Block_Status::ERASING;
        this->nextQueue = nextQueue;
    }

    Block_Queue::Block_Queue(level_type level)
    {
        this->level = level;
        currentBlockIdx = 0;
        blockList.clear();
        currentErasingBlocksCount = 0;
    }

    Block_Queue::~Block_Queue()
    {
    }

    Block_Type *Block_Queue::getCurrentBlock()
    {
        if(currentBlockIdx >= blockList.size()){
            PRINT_ERROR("Get Current Block")
        }
        return blockList.at(currentBlockIdx);
    }
    
    void Block_Queue::enqueueBlock(Block_Type *block)
    {
        block->SetUp(this->level);
        blockList.push_back(block);
    }
    
    level_type Block_Queue::getLevel()
    {
        return this->level;
    }

    bool Block_Queue::isFull()
    {
        return (blockList.size() == 0 || (currentBlockIdx >= blockList.size() - 1));
    }

    void Block_Queue::adjustBlockIdx(uint64_t pagesPerBlock)
    {
        currentBlockIdx = blockList.size();
        if(currentBlockIdx == 0) return;
        do{
            currentBlockIdx--;
            if(blockList.at(currentBlockIdx)->status != MQ_Block_Status::IDLE) break;
        } while(currentBlockIdx != 0);

        if(blockList.at(currentBlockIdx)->currentPageIdx == pagesPerBlock){
            currentBlockIdx++;
        }
    }

    Block_Type *Flash_Block_Manager_MQ::getBlock(const PPA_type &ppa)
    {
        uint32_t blockID = ppa / pagesPerBlock;
        return blocks[blockID];
    }

    void Flash_Block_Manager_MQ::finishErase(Block_Type *block)
    {
        Block_Queue* queue = queues.at(block->prevQueue);
        block->SetUp(block->prevQueue);
        auto blockItr = queue->blockList.begin();
        for(uint32_t blockIdx = 0; blockItr != queue->blockList.end(); blockItr++, blockIdx++){
            // Common case.
            if((*blockItr) == block){
                queue->blockList.erase(blockItr);
                queue->blockList.push_back(block);
                if(queue->currentBlockIdx >= blockIdx){
                    queue->currentBlockIdx--;
                }
                return;
            }
        }

        // Special case cased by the startGroupConfiguration() function.
        queue->blockList.push_back(block);
    }

    // When the level has no free space for writes.
    bool Flash_Block_Manager_MQ::Stop_servicing_writes(const level_type level)
    {
        return queues.at(level)->isFull();
    }

    bool Flash_Block_Manager_MQ::overGCThreshold(level_type level)
    {
        Block_Queue* queue = queues.at(level);
        int totalBlockCount = queue->blockList.size();
        int freeBlockCount = totalBlockCount - (queue->currentBlockIdx + 1) + queue->currentErasingBlocksCount;
        if(totalBlockCount < 100){
            return (freeBlockCount < 2);
        } else{
            return (freeBlockCount < (totalBlockCount / 100) + 2);
        }
    }

    bool Flash_Block_Manager_MQ::isLastQueue(level_type level)
    {
        return level == (queueCount - 1);
    }

    bool Flash_Block_Manager_MQ::isErasing(level_type level)
    {
        return (queues.at(level)->currentBlockIdx > 0);
    }

    void Flash_Block_Manager_MQ::startGroupConfiguration()
    {
        if(Simulator->loadMileStone != 0){
            return;
        }
        UID* uid = lui->getUID();
        if(uid == nullptr) return;
        std::queue<Block_Type*> freeBlockPool;
        std::queue<Block_Type*> blockPool;

        while(queues.size() < uid->groupConf.size()){
            createQueue();
        }

        // // Pop the blocks from queues.
        // for(uint32_t groupNumber = 0; groupNumber < queueCount; groupNumber++){
        //     Block_Queue* currentQueue = queues.at(groupNumber);
        //     int popCount = 0;
        //     if(uid->groupConf.size() < groupNumber + 1){
        //         popCount = currentQueue->blockList.size();
        //     } else{
        //         popCount = (currentQueue->blockList.size() - uid->groupConf.at(groupNumber));
        //     }
        //     while(popCount > 0){
        //         Block_Type* blockToPop = currentQueue->blockList.back();

        //         if(blockToPop->status == MQ_Block_Status::IDLE){
        //             // If there is a block is not used, just pop.
        //             freeBlockPool.push(blockToPop);
        //             currentQueue->blockList.pop_back();
        //         } else{
        //             // Else, pop the head block.
        //             blockPool.push(currentQueue->blockList.front());
        //             currentQueue->blockList.erase(currentQueue->blockList.begin());
        //         }
        //         popCount--;
        //     }
        // }

        // Block_Queue* lastQueue = queues.at(uid->groupConf.size() - 1);
        // while(!lastQueue->blockList.empty()){
        //     Block_Type* blockToPop = lastQueue->blockList.back();
        //     if(blockToPop->status != MQ_Block_Status::IDLE){
        //         blockPool.push(blockToPop);
        //     } else{
        //         freeBlockPool.push(blockToPop);
        //     }
        //     lastQueue->blockList.pop_back();
        // }

        // while(lastQueue->blockList.size() < uid->groupConf.back() && !freeBlockPool.empty()){
        //     lastQueue->enqueueBlock(freeBlockPool.front());
        //     freeBlockPool.pop();
        // }

        for(auto queue : queues){
            auto blockItr = queue->blockList.begin();
            while(blockItr != queue->blockList.end()){
                if((*blockItr)->status == MQ_Block_Status::IDLE){
                    freeBlockPool.push((*blockItr));
                } else if(((*blockItr)->status == MQ_Block_Status::WORKING) && (*blockItr)->currentPageIdx == pagesPerBlock){
                    blockPool.push((*blockItr));
                } else{
                    blockItr++;
                    continue;
                }
                blockItr = queue->blockList.erase(blockItr);
            }
        }

        for(uint32_t popTargetQueueIdx = 0; popTargetQueueIdx < queues.size(); popTargetQueueIdx++){
            Block_Queue* popTargetQueue = queues.at(popTargetQueueIdx);
            int popCount = 0;
            if(uid->groupConf.size() < popTargetQueueIdx){
                popCount = popTargetQueue->blockList.size() - uid->groupConf.at(popTargetQueueIdx);
            } else{
                popCount = popTargetQueue->blockList.size();
            }
            while(popCount > 0){
                Block_Type* popBlock = popTargetQueue->blockList.back();
                popTargetQueue->blockList.pop_back();
                if(popBlock->status == MQ_Block_Status::IDLE){
                    freeBlockPool.push(popBlock);
                } else{
                    blockPool.push(popBlock);
                }
                popCount--;
            }
        }

        // Push the blocks to queues.
        for(uint32_t pushTargetQueueIdx = 0; pushTargetQueueIdx < uid->groupConf.size(); pushTargetQueueIdx++){
            Block_Queue* pushTargetQueue = queues.at(pushTargetQueueIdx);
            int pushCount = uid->groupConf.at(pushTargetQueueIdx) - pushTargetQueue->blockList.size();
            while(pushCount > 0){
                Block_Type* pushBlock;
                if(!blockPool.empty()){
                    pushBlock = blockPool.front(); blockPool.pop();
                } else{
                    pushBlock = freeBlockPool.front(); freeBlockPool.pop();
                }
                if(pushBlock->status == MQ_Block_Status::WORKING){
                    if(pushBlock->prevQueue > pushTargetQueueIdx){
                        auto itr = pushTargetQueue->blockList.begin();
                        for(; itr != pushTargetQueue->blockList.end(); itr++){
                            if((*itr)->status != MQ_Block_Status::ERASING){
                                itr++;
                                break;
                            }
                        }
                        pushTargetQueue->blockList.insert(itr, 1, pushBlock);
                    } else if(pushBlock->prevQueue <= pushTargetQueueIdx){
                        auto itr = pushTargetQueue->blockList.rbegin();
                        for(; itr != pushTargetQueue->blockList.rend(); itr++){
                            if((*itr)->status != MQ_Block_Status::IDLE){
                                break;
                            }
                        }
                        pushTargetQueue->blockList.insert(itr.base(), 1, pushBlock);
                    }
                    pushBlock->prevQueue = pushTargetQueueIdx;
                } else if(pushBlock->status == MQ_Block_Status::ERASING){
                    queues.at(pushBlock->prevQueue)->currentErasingBlocksCount--;
                    pushTargetQueue->currentErasingBlocksCount++;
                    pushBlock->prevQueue = pushTargetQueueIdx;
                } else if(pushBlock->status == MQ_Block_Status::IDLE){
                    pushTargetQueue->enqueueBlock(pushBlock);
                }
                pushCount--;
            }
        }

        queueCount = uid->groupConf.size();
        
        for(int groupNumber = 0; groupNumber < queueCount; groupNumber++){
            Block_Queue* curQueue = queues.at(groupNumber);
            curQueue->adjustBlockIdx(pagesPerBlock);
            if(overGCThreshold(groupNumber)){
                ftl->GC_and_WL_Unit->gc_start(curQueue, 0);
            } else{
                ftl->Address_Mapping_Unit->Start_servicing_writes_for_level(groupNumber);
            }
        }
    }

    void Flash_Block_Manager_MQ::handleHotFilter(const LPA_type& lpa, const PPA_type& old_ppa, const bool forGC)
    {
        Block_Type* block = getBlock(old_ppa);
        lui_timestamp blkAge = (lui->getCurrentTimestamp() - block->createTimestamp);

        lui->updateHotFilter(lpa, blkAge, block->prevQueue, forGC);
    }

    void Flash_Block_Manager_MQ::handleLUIBlockAge(Block_Type *block)
    {
        // if(isLastQueue(block->prevQueue)){
        //     lui->addBlockAge(block, Queue_Type::LAST_QUEUE);
        // } else{
        //     if(block->prevQueue == 0){
        //         lui->addBlockAge(block, Queue_Type::HOT_QUEUE);
        //     }
        // }
    }

    bool Flash_Block_Manager_MQ::isFilled(const level_type level)
    {
        for(uint32_t curLevel = level; curLevel < queueCount; curLevel++){
            if(!Stop_servicing_writes(curLevel)){
                return false;
            }
        }
        if(level == 0){
            PRINT_ERROR("is filled")
        }
        return true;
    }

    void Flash_Block_Manager_MQ::handleTrLevel(NVM_Transaction_Flash* tr)
    {
        if(tr->level == UNDEFINED_LEVEL){
            if(lui->isHot(tr->LPA)){
                tr->level = 0;
            } else{
                tr->level = 1;
            }
        }
        if(queueCount + 1 < tr->level){
            tr->level = queueCount - 1;
        }
    }

    Flash_Block_Manager_MQ::Flash_Block_Manager_MQ(FTL *ftl, uint32_t channelCount, uint32_t chipsPerChannel, uint32_t diesPerChip, uint32_t planesPerDie, uint32_t blocksPerPlane, uint32_t pagesPerBlock)
    :ftl(ftl), pagesPerBlock(pagesPerBlock) {
        //In initialize, we assumed that MiDAS has only hot queue.
        Block_Type::page_vector_size = pagesPerBlock / (sizeof(uint64_t) * 8) + (pagesPerBlock % (sizeof(uint64_t) * 8) == 0 ? 0 : 1);

        uint64_t totalBlockCount = channelCount * chipsPerChannel * diesPerChip * planesPerDie * blocksPerPlane;
        
                        for(uint32_t channelID = 0; channelID < channelCount; channelID++){
                    for(uint32_t chipID = 0; chipID < chipsPerChannel; chipID++){
                for(uint32_t dieID = 0; dieID < diesPerChip; dieID++){
            for(uint32_t planeID = 0; planeID < planesPerDie; planeID++){
        for(uint32_t blockID = 0; blockID < blocksPerPlane; blockID++){
                            NVM::FlashMemory::Physical_Page_Address blockAddr = NVM::FlashMemory::Physical_Page_Address(channelID, chipID, dieID, planeID, blockID, 0);
                            Block_Type* newBlock = new Block_Type(blockAddr);
                            blocks.push_back(newBlock);
                        }
                    }
                }
            }
        }

        // In initialize, # of queues is 8.
        // And all queues have same amount of blocks.
        queueCount = 4;
        for(uint32_t i = 0; i < queueCount; i++){
            createQueue();
        }

        uint32_t blockIdx = 0;
        // uint32_t lastBlockCount = 10;
        // uint32_t unLastBlocksCount = blocks.size() - lastBlockCount;
        // for(int qIdx = 0; qIdx < queueCount - 1; qIdx++){
        //     for(int i = 0; i < unLastBlocksCount / (queueCount - 1); i++, blockIdx++){
        //         queues.at(qIdx)->enqueueBlock(blocks.at(blockIdx));
        //     }
        // }
        // while(blockIdx < blocks.size()){
        //     queues.back()->enqueueBlock(blocks.at(blockIdx++));
        // }

        for(auto& queue : queues){
            for(uint32_t i = 0; i < blocks.size() / queueCount; i++, blockIdx++){
                queue->enqueueBlock(blocks.at(blockIdx));
            }
        }


        std::vector<uint32_t> initialGroupConf;

        for(auto queue : queues){
            initialGroupConf.push_back(queue->blockList.size());
        }

        this->lui = new Log_Update_Interval(totalBlockCount, pagesPerBlock, initialGroupConf);
    }

    Flash_Block_Manager_MQ::~Flash_Block_Manager_MQ()
    {
        for(auto block : blocks){
            delete block;
        }
        for(auto queue : queues){
            delete queue;
        }
        delete this->lui;
    }

    void Flash_Block_Manager_MQ::Allocate_page(const stream_id_type streamID, NVM::FlashMemory::Physical_Page_Address &address, LPA_type lpa, uint32_t& level, bool forGC, bool forRead)
    {
        Block_Queue* queue = queues.at(level);

        Block_Type* block = queue->getCurrentBlock();
        if(block->status == MQ_Block_Status::IDLE){
            block->StartUsing(lui->getCurrentTimestamp(), streamID, false);
        }
        address = *block->blockAddr;
        address.PageID = block->currentPageIdx++;
        if(!forRead){
            Program_transaction_issued(block);
        }
        
        if(block->currentPageIdx == pagesPerBlock){
            queue->currentBlockIdx++;
            while(!queue->isFull()){
                block = queue->getCurrentBlock();
                if(block->status == MQ_Block_Status::IDLE) break;
                else queue->currentBlockIdx++;
            }
            if(overGCThreshold(level)){
                ftl->GC_and_WL_Unit->gc_start(queue, lui->getCurrentTimestamp());
            } else{
                block->StartUsing(lui->getCurrentTimestamp(), streamID, false);
            }
        }

        if(!forRead){
            startGroupConfiguration();
            if(!forGC){
                lui->updateTable(lpa);
            }
        }
    }

    void Flash_Block_Manager_MQ::Allocate_mapping_page(const stream_id_type streamID, NVM::FlashMemory::Physical_Page_Address &address)
    {
        PRINT_ERROR("NOT IMPLEMENTED ALLOCATE MAPPING PAGE")
    }

    void Flash_Block_Manager_MQ::Program_transaction_issued(Block_Type *block)
    {
        block->Ongoing_user_program_count++;
    }

    void Flash_Block_Manager_MQ::createQueue()
    {
        Block_Queue* queue = new Block_Queue(this->queues.size());
        this->queues.push_back(queue);
    }

    void Flash_Block_Manager_MQ::removeLastQueue()
    {
        Block_Queue* lastQueue = queues.back();

        if(lastQueue->blockList.size() != 0){
            PRINT_ERROR("Remove Last Queue")
        }

        queues.pop_back();
    }

    void Flash_Block_Manager_MQ::Read_transaction_issued(const PPA_type& ppa)
    {
        getBlock(ppa)->Ongoing_user_read_count++;
    }

    void Flash_Block_Manager_MQ::Read_transaction_serviced(const PPA_type &ppa)
    {
        Block_Type* block = getBlock(ppa);
        if(block->Ongoing_user_read_count < 1){
            PRINT_ERROR("Read transaction serviced")
        }
        block->Ongoing_user_read_count--;
    }

    void Flash_Block_Manager_MQ::Program_transaction_serviced(const PPA_type &ppa)
    {
        Block_Type* block = getBlock(ppa);
        if(block->Ongoing_user_program_count > 0){
            block->Ongoing_user_program_count--;
        } else{
            PRINT_ERROR("Program transaction serviced")
        }
    }

    void Flash_Block_Manager_MQ::Invalidate_page_in_block(const stream_id_type streamID, const PPA_type &ppa)
    {
        uint32_t pageID = ppa % pagesPerBlock;
        Block_Type* block = getBlock(ppa);
        block->invalid_page_bitmap[pageID / 64] |= ((uint64_t)1 << ((uint64_t)pageID % (uint64_t)64));
        block->invalid_page_count++;
    }

    bool Flash_Block_Manager_MQ::isPageValid(const Block_Type *block, flash_page_ID_type page)
    {
        return !(block->invalid_page_bitmap[page / 64] & ((uint64_t)1 << ((uint64_t)page % (uint64_t)64)));
    }

}